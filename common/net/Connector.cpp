#include "common/net/Connector.h"

#include "common/logging/LogMacros.h"
#include "common/net/Channel.h"
#include "common/net/EventLoop.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace tinyimx {
namespace {

std::string ErrorString(
    int error_number
) {
    return std::strerror(
        error_number
    );
}

}  // namespace


Connector::Connector(
    EventLoop* loop,
    const InetAddress& server_address,
    std::chrono::milliseconds
        connect_timeout
)
    : loop_(loop),
      server_address_(
          server_address
      ),
      connect_timeout_(
          connect_timeout
      ) {
}


Connector::~Connector() {
    if (channel_) {
        LOG_WARN(
            "connector destroyed with "
            "active channel"
            << ", server="
            << server_address_.
                ToString()
        );
    }
}


void Connector::
SetNewConnectionCallback(
    NewConnectionCallback callback
) {
    new_connection_callback_ =
        std::move(callback);
}


void Connector::
SetConnectErrorCallback(
    ConnectErrorCallback callback
) {
    connect_error_callback_ =
        std::move(callback);
}


void Connector::Start() {
    connect_requested_.store(
        true,
        std::memory_order_release
    );

    if (loop_ == nullptr) {
        return;
    }

    const auto self =
        shared_from_this();

    loop_->RunInLoop(
        [self]() {
            self->StartInLoop();
        }
    );
}


void Connector::Stop() {
    connect_requested_.store(
        false,
        std::memory_order_release
    );

    if (loop_ == nullptr) {
        return;
    }

    const auto self =
        shared_from_this();

    loop_->RunInLoop(
        [self]() {
            self->StopInLoop();
        }
    );
}


bool Connector::IsConnecting()
    const noexcept {
    return state_.load(
        std::memory_order_acquire
    ) == State::kConnecting;
}


const InetAddress&
Connector::ServerAddress()
    const noexcept {
    return server_address_;
}


void Connector::StartInLoop() {
    if (
        loop_ == nullptr ||
        !loop_->IsInLoopThread()
    ) {
        LOG_ERROR(
            "connector start called "
            "outside loop"
            << ", server="
            << server_address_.
                ToString()
        );

        return;
    }

    if (
        !connect_requested_.load(
            std::memory_order_acquire
        )
    ) {
        return;
    }

    if (
        state_.load(
            std::memory_order_acquire
        ) == State::kConnecting
    ) {
        return;
    }

    if (!server_address_.IsValid()) {
        FailConnection(EINVAL);
        return;
    }

    Connect();
}


void Connector::StopInLoop() {
    if (
        loop_ == nullptr ||
        !loop_->IsInLoopThread()
    ) {
        LOG_ERROR(
            "connector stop called "
            "outside loop"
            << ", server="
            << server_address_.
                ToString()
        );

        return;
    }

    if (
        state_.load(
            std::memory_order_acquire
        ) != State::kConnecting
    ) {
        return;
    }

    CancelConnectTimeout();

    RemoveAndResetChannel();

    socket_.Close();

    state_.store(
        State::kDisconnected,
        std::memory_order_release
    );

    LOG_INFO(
        "connector connect cancelled"
        << ", server="
        << server_address_.ToString()
    );
}


void Connector::Connect() {
    Socket socket;

    if (!socket.CreateTcp()) {
        const int error_number =
            errno != 0
                ? errno
                : EIO;

        FailConnection(
            error_number
        );

        return;
    }

    const int result =
        ::connect(
            socket.Fd(),
            server_address_.SockAddr(),
            server_address_.Length()
        );

    if (result == 0) {
        socket_ =
            std::move(socket);

        CompleteConnection();

        return;
    }

    const int saved_errno =
        errno;

    switch (saved_errno) {
        case EINPROGRESS:
        case EINTR:
        case EALREADY:
            BeginWatchingConnect(
                std::move(socket)
            );
            return;

        case EISCONN:
            socket_ =
                std::move(socket);

            CompleteConnection();

            return;

        default:
            socket.Close();

            FailConnection(
                saved_errno
            );

            return;
    }
}


void Connector::BeginWatchingConnect(
    Socket socket
) {
    socket_ =
        std::move(socket);

    state_.store(
        State::kConnecting,
        std::memory_order_release
    );

    channel_ =
        std::make_unique<Channel>(
            loop_,
            socket_.Fd()
        );

    /*
     * epoll 正在回调 Channel 时，
     * 保证 Connector 自身还活着。
     */
    channel_->Tie(
        shared_from_this()
    );

    channel_->SetWriteCallback(
        [this]() {
            HandleWrite();
        }
    );

    channel_->SetErrorCallback(
        [this]() {
            HandleError();
        }
    );
    channel_->SetCloseCallback(
        [this]() {
            HandleError();
        }
    );
    /*
     * non-blocking connect 的核心：
     *
     * EINPROGRESS 后不等待，
     * 而是让 epoll 监听 EPOLLOUT。
     */
    channel_->EnableWriting();

    StartConnectTimeout();

    LOG_INFO(
        "connector connecting"
        << ", server="
        << server_address_.ToString()
        << ", fd="
        << socket_.Fd()
    );
}


void Connector::HandleWrite() {
    if (
        state_.load(
            std::memory_order_acquire
        ) != State::kConnecting
    ) {
        return;
    }

    /*
     * EPOLLOUT 并不等于一定连接成功。
     *
     * 必须查询 SO_ERROR。
     */
    const int error_number =
        GetSocketError(
            socket_.Fd()
        );

    CancelConnectTimeout();

    RemoveAndResetChannel();

    if (error_number != 0) {
        FailConnection(
            error_number
        );

        return;
    }

    CompleteConnection();
}


void Connector::HandleError() {
    if (
        state_.load(
            std::memory_order_acquire
        ) != State::kConnecting
    ) {
        return;
    }

    int error_number =
        GetSocketError(
            socket_.Fd()
        );

    if (error_number == 0) {
        error_number =
            ECONNABORTED;
    }

    CancelConnectTimeout();

    RemoveAndResetChannel();

    FailConnection(
        error_number
    );
}


void Connector::HandleTimeout() {
    connect_timeout_timer_ = {};

    if (
        state_.load(
            std::memory_order_acquire
        ) != State::kConnecting
    ) {
        return;
    }

    RemoveAndResetChannel();

    FailConnection(
        ETIMEDOUT
    );
}


void Connector::CompleteConnection() {
    state_.store(
        State::kDisconnected,
        std::memory_order_release
    );

    const bool should_connect =
        connect_requested_.exchange(
            false,
            std::memory_order_acq_rel
        );

    if (!should_connect) {
        socket_.Close();
        return;
    }

    /*
     * 所有权从 Connector::Socket
     * 转交给 TcpClient/TcpConnection。
     */
    const int socket_fd =
        socket_.Release();

    if (socket_fd < 0) {
        FailConnection(EBADF);
        return;
    }

    LOG_INFO(
        "connector connected"
        << ", server="
        << server_address_.ToString()
        << ", fd="
        << socket_fd
    );

    if (new_connection_callback_) {
        new_connection_callback_(
            socket_fd
        );

        return;
    }

    ::close(socket_fd);
}


void Connector::FailConnection(
    int error_number
) {
    socket_.Close();

    state_.store(
        State::kDisconnected,
        std::memory_order_release
    );

    const bool should_notify =
        connect_requested_.exchange(
            false,
            std::memory_order_acq_rel
        );

    LOG_WARN(
        "connector connect failed"
        << ", server="
        << server_address_.ToString()
        << ", error="
        << ErrorString(
            error_number
        )
    );

    if (
        connect_error_callback_ &&
        should_notify
    ) {
        connect_error_callback_(
            error_number
        );
    }
}


void Connector::
RemoveAndResetChannel() {
    if (!channel_) {
        return;
    }

    channel_->DisableAll();

    loop_->RemoveChannel(
        channel_.get()
    );

    /*
     * 这里不能直接：
     *
     * channel_.reset();
     *
     * 因为当前可能就在
     * Channel::HandleEvent() 内部。
     *
     * 如果此时析构 Channel，
     * 就会形成 callback 中销毁自己。
     */
    Channel* old_channel =
        channel_.release();

    loop_->QueueInLoop(
        [old_channel]() {
            delete old_channel;
        }
    );
}


void Connector::
StartConnectTimeout() {
    if (
        connect_timeout_ <=
        std::chrono::milliseconds::
            zero()
    ) {
        return;
    }

    const std::weak_ptr<Connector>
        weak_self =
            shared_from_this();

    connect_timeout_timer_ =
        loop_->RunAfter(
            connect_timeout_,
            [weak_self]() {
                const auto self =
                    weak_self.lock();

                if (self) {
                    self->
                        HandleTimeout();
                }
            }
        );
}


void Connector::
CancelConnectTimeout() {
    if (
        !connect_timeout_timer_.
            IsValid()
    ) {
        return;
    }

    loop_->Cancel(
        connect_timeout_timer_
    );

    connect_timeout_timer_ = {};
}


int Connector::GetSocketError(
    int socket_fd
) {
    if (socket_fd < 0) {
        return EBADF;
    }

    int socket_error = 0;

    socklen_t length =
        static_cast<socklen_t>(
            sizeof(socket_error)
        );

    if (
        ::getsockopt(
            socket_fd,
            SOL_SOCKET,
            SO_ERROR,
            &socket_error,
            &length
        ) != 0
    ) {
        return errno;
    }

    return socket_error;
}

}  // namespace tinyimx