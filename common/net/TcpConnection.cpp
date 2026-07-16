#include "common/net/TcpConnection.h"

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

std::string ErrnoString() {
    return std::strerror(errno);
}

}  // namespace

TcpConnection::TcpConnection(EventLoop* loop,
                             std::string name,
                             int socket_fd,
                             const InetAddress& local_address,
                             const InetAddress& peer_address)
    : loop_(loop),
      name_(std::move(name)),
      socket_(socket_fd),
      channel_(std::make_unique<Channel>(loop, socket_fd)),
      local_address_(local_address),
      peer_address_(peer_address) {
    socket_.SetTcpNoDelay(true);
    socket_.SetKeepAlive(true);

    channel_->SetReadCallback([this]() {
        HandleRead();
    });

    channel_->SetWriteCallback([this]() {
        HandleWrite();
    });

    channel_->SetCloseCallback([this]() {
        HandleClose();
    });

    channel_->SetErrorCallback([this]() {
        HandleError();
    });

    LOG_INFO("tcp connection created"
             << ", name=" << name_
             << ", fd=" << socket_.Fd()
             << ", local=" << local_address_.ToString()
             << ", peer=" << peer_address_.ToString());
}

TcpConnection::~TcpConnection() {
    LOG_INFO("tcp connection destroyed"
             << ", name=" << name_
             << ", fd=" << socket_.Fd());
}

void TcpConnection::SetConnectionCallback(ConnectionCallback callback) {
    connection_callback_ = std::move(callback);
}

void TcpConnection::SetMessageCallback(MessageCallback callback) {
    message_callback_ = std::move(callback);
}

void TcpConnection::SetCloseCallback(CloseCallback callback) {
    close_callback_ = std::move(callback);
}

/*
void TcpConnection::ConnectEstablished() {
    SetState(State::kConnected);

    channel_->EnableReading();

    if (connection_callback_) {
        connection_callback_(shared_from_this());
    }

    LOG_INFO("tcp connection established"
             << ", name=" << name_
             << ", peer=" << peer_address_.ToString());
}

*/
void TcpConnection::ConnectEstablished() {
    SetState(State::kConnected);

    channel_->Tie(shared_from_this());
    channel_->EnableReading();

    if (connection_callback_) {
        connection_callback_(shared_from_this());
    }

    LOG_INFO("tcp connection established"
             << ", name=" << name_
             << ", peer=" << peer_address_.ToString());
}

/*
    void TcpConnection::ConnectDestroyed() {
    if (state_ == State::kConnected ||
        state_ == State::kDisconnecting) {
        SetState(State::kDisconnected);
        channel_->DisableAll();
    }

    if (loop_ != nullptr) {
        loop_->RemoveChannel(channel_.get());
    }

    LOG_INFO("tcp connection destroyed from loop"
             << ", name=" << name_);
    }

*/
void TcpConnection::ConnectDestroyed() {
    if (state_ == State::kConnected ||
        state_ == State::kDisconnecting) {
        SetState(State::kDisconnected);
        channel_->DisableAll();
    }

    if (connection_callback_) {
        connection_callback_(shared_from_this());
    }

    if (loop_ != nullptr) {
        loop_->RemoveChannel(channel_.get());
    }

    LOG_INFO("tcp connection destroyed from loop"
             << ", name=" << name_);
}

void TcpConnection::Send(const std::string& message) {
    Send(message.data(), message.size());
}

void TcpConnection::Send(const char* data, std::size_t length) {
    if (state_ != State::kConnected) {
        LOG_WARN("send ignored: connection is not connected"
                 << ", name=" << name_);
        return;
    }

    if (data == nullptr || length == 0) {
        return;
    }

    SendInLoop(data, length);
}

void TcpConnection::Shutdown() {
    if (state_ == State::kConnected) {
        SetState(State::kDisconnecting);
        ShutdownInLoop();
    }
}

void TcpConnection::ForceClose() {
    if (state_ == State::kConnected ||
        state_ == State::kDisconnecting) {
        HandleClose();
    }
}

bool TcpConnection::IsConnected() const {
    return state_ == State::kConnected;
}

const std::string& TcpConnection::Name() const {
    return name_;
}

const InetAddress& TcpConnection::LocalAddress() const {
    return local_address_;
}

const InetAddress& TcpConnection::PeerAddress() const {
    return peer_address_;
}

EventLoop* TcpConnection::GetLoop() const {
    return loop_;
}

Buffer* TcpConnection::InputBuffer() {
    return &input_buffer_;
}

Buffer* TcpConnection::OutputBuffer() {
    return &output_buffer_;
}

void TcpConnection::SetState(State state) {
    state_ = state;
}

void TcpConnection::HandleRead() {
    int saved_errno = 0;

    const ssize_t n = input_buffer_.ReadFd(socket_.Fd(), &saved_errno);

    if (n > 0) {
        if (message_callback_) {
            message_callback_(shared_from_this(), &input_buffer_);
        }
        return;
    }

    if (n == 0) {
        HandleClose();
        return;
    }

    if (saved_errno == EAGAIN ||
        saved_errno == EWOULDBLOCK ||
        saved_errno == EINTR) {
        return;
    }

    if (saved_errno == ECONNRESET) {
        HandleClose();
        return;
    }

    LOG_ERROR("tcp connection read failed"
              << ", name=" << name_
              << ", fd=" << socket_.Fd()
              << ", error=" << std::strerror(saved_errno));

    HandleError();
}

void TcpConnection::HandleWrite() {
    if (!channel_->IsWriting()) {
        return;
    }

    const std::size_t readable = output_buffer_.ReadableBytes();

    if (readable == 0) {
        channel_->DisableWriting();

        if (state_ == State::kDisconnecting) {
            ShutdownInLoop();
        }

        return;
    }

    const ssize_t n = ::send(
        socket_.Fd(),
        output_buffer_.Peek(),
        readable,
        MSG_NOSIGNAL
    );

    if (n > 0) {
        output_buffer_.Retrieve(static_cast<std::size_t>(n));

        if (output_buffer_.ReadableBytes() == 0) {
            channel_->DisableWriting();

            if (state_ == State::kDisconnecting) {
                ShutdownInLoop();
            }
        }

        return;
    }

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return;
        }

        LOG_ERROR("tcp connection write failed"
                  << ", name=" << name_
                  << ", fd=" << socket_.Fd()
                  << ", error=" << ErrnoString());

        HandleError();
    }
}

void TcpConnection::HandleClose() {
    if (state_ == State::kDisconnected) {
        return;
    }

    SetState(State::kDisconnected);

    channel_->DisableAll();

    LOG_INFO("tcp connection closed"
             << ", name=" << name_
             << ", peer=" << peer_address_.ToString());

    if (close_callback_) {
        close_callback_(shared_from_this());
    }
}

void TcpConnection::HandleError() {
    LOG_ERROR("tcp connection error"
              << ", name=" << name_
              << ", fd=" << socket_.Fd()
              << ", peer=" << peer_address_.ToString());

    HandleClose();
}

void TcpConnection::SendInLoop(const char* data, std::size_t length) {
    std::size_t remaining = length;
    std::size_t written = 0;

    if (!channel_->IsWriting() &&
        output_buffer_.ReadableBytes() == 0) {
        const ssize_t n = ::send(
            socket_.Fd(),
            data,
            length,
            MSG_NOSIGNAL
        );

        if (n >= 0) {
            written = static_cast<std::size_t>(n);
            remaining = length - written;

            if (remaining == 0) {
                return;
            }
        } else {
            if (errno != EAGAIN &&
                errno != EWOULDBLOCK &&
                errno != EINTR) {
                LOG_ERROR("tcp connection send failed"
                          << ", name=" << name_
                          << ", fd=" << socket_.Fd()
                          << ", error=" << ErrnoString());
                HandleError();
                return;
            }

            written = 0;
            remaining = length;
        }
    }

    if (remaining > 0) {
        output_buffer_.Append(data + written, remaining);

        if (!channel_->IsWriting()) {
            channel_->EnableWriting();
        }
    }
}

void TcpConnection::ShutdownInLoop() {
    if (!channel_->IsWriting()) {
        socket_.ShutdownWrite();
    }
}

}  // namespace tinyimx