#include "common/net/TcpClient.h"

#include "common/logging/LogMacros.h"
#include "common/net/EventLoop.h"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace tinyimx {


TcpClient::TcpClient(
    EventLoop* loop,
    const InetAddress&
        server_address,
    std::string name,
    std::chrono::milliseconds
        connect_timeout
)
    : loop_(loop),
      server_address_(
          server_address
      ),
      name_(
          std::move(name)
      ),
      connector_(
          std::make_shared<
              Connector
          >(
              loop,
              server_address,
              connect_timeout
          )
      ) {
    connector_->
        SetNewConnectionCallback(
            [this](
                int socket_fd
            ) {
                NewConnection(
                    socket_fd
                );
            }
        );

    connector_->
        SetConnectErrorCallback(
            [this](
                int error_number
            ) {
                HandleConnectError(
                    error_number
                );
            }
        );
}


TcpClient::~TcpClient() {
    Stop();
}


void TcpClient::
SetConnectionCallback(
    ConnectionCallback callback
) {
    connection_callback_ =
        std::move(callback);
}


void TcpClient::
SetMessageCallback(
    MessageCallback callback
) {
    message_callback_ =
        std::move(callback);
}


void TcpClient::
SetConnectErrorCallback(
    ConnectErrorCallback callback
) {
    connect_error_callback_ =
        std::move(callback);
}


bool TcpClient::Connect() {
    if (
        loop_ == nullptr ||
        !loop_->IsValid()
    ) {
        LOG_ERROR(
            "tcp client connect failed: "
            "invalid event loop"
            << ", name="
            << name_
        );

        return false;
    }

    if (!server_address_.IsValid()) {
        LOG_ERROR(
            "tcp client connect failed: "
            "invalid server address"
            << ", name="
            << name_
        );

        return false;
    }

    if (
        IsConnected() ||
        connector_->IsConnecting()
    ) {
        return true;
    }

    connect_requested_.store(
        true,
        std::memory_order_release
    );

    connector_->Start();

    return true;
}


void TcpClient::Disconnect() {
    connect_requested_.store(
        false,
        std::memory_order_release
    );

    connector_->Stop();

    const TcpConnectionPtr
        connection =
            Connection();

    if (connection) {
        connection->Shutdown();
    }
}


void TcpClient::Stop() {
    connect_requested_.store(
        false,
        std::memory_order_release
    );

    if (connector_) {
        connector_->Stop();
    }

    const TcpConnectionPtr
        connection =
            Connection();

    if (connection) {
        connection->ForceClose();
    }
}


bool TcpClient::IsConnected()
    const {
    const TcpConnectionPtr
        connection =
            Connection();

    return
        connection &&
        connection->IsConnected();
}


TcpConnectionPtr
TcpClient::Connection() const {
    std::lock_guard<std::mutex>
        lock(
            connection_mutex_
        );

    return connection_;
}


const std::string&
TcpClient::Name()
    const noexcept {
    return name_;
}


const InetAddress&
TcpClient::ServerAddress()
    const noexcept {
    return server_address_;
}


void TcpClient::NewConnection(
    int socket_fd
) {
    if (
        loop_ == nullptr ||
        !loop_->IsInLoopThread()
    ) {
        LOG_ERROR(
            "tcp client new connection "
            "called outside loop"
            << ", name="
            << name_
        );

        if (socket_fd >= 0) {
            ::close(socket_fd);
        }

        return;
    }

    if (
        !connect_requested_.load(
            std::memory_order_acquire
        )
    ) {
        ::close(socket_fd);
        return;
    }

    /*
     * Connector 只知道远端地址。
     *
     * connect 成功后通过 getsockname()
     * 获取客户端本地 IP/临时端口，
     * 再构造完整 TcpConnection。
     */
    sockaddr_in local_native {};

    socklen_t local_length =
        static_cast<socklen_t>(
            sizeof(local_native)
        );

    if (
        ::getsockname(
            socket_fd,
            reinterpret_cast<
                sockaddr*
            >(
                &local_native
            ),
            &local_length
        ) != 0
    ) {
        const int error_number =
            errno;

        ::close(socket_fd);

        HandleConnectError(
            error_number
        );

        return;
    }

    const InetAddress
        local_address(
            local_native
        );

    const std::string
        connection_name =
            MakeConnectionName();

    auto connection =
        std::make_shared<
            TcpConnection
        >(
            loop_,
            connection_name,
            socket_fd,
            local_address,
            server_address_
        );

    /*
     * 继续复用现有 TcpConnection。
     */
    connection->
        SetConnectionCallback(
            connection_callback_
        );

    connection->
        SetMessageCallback(
            message_callback_
        );

    connection->
        SetCloseCallback(
            [this](
                const TcpConnectionPtr&
                    closed_connection
            ) {
                RemoveConnection(
                    closed_connection
                );
            }
        );

    {
        std::lock_guard<std::mutex>
            lock(
                connection_mutex_
            );

        connection_ =
            connection;
    }

    LOG_INFO(
        "tcp client connected"
        << ", name="
        << name_
        << ", connection="
        << connection_name
        << ", server="
        << server_address_.ToString()
    );

    /*
     * TcpConnection 要求必须在
     * 所属 EventLoop 线程调用。
     *
     * 当前 NewConnection 本身就在
     * EventLoop 线程。
     */
    connection->
        ConnectEstablished();
}


void TcpClient::RemoveConnection(
    const TcpConnectionPtr&
        connection
) {
    if (!connection) {
        return;
    }

    if (
        loop_ == nullptr ||
        !loop_->IsInLoopThread()
    ) {
        LOG_ERROR(
            "tcp client remove connection "
            "called outside loop"
            << ", name="
            << name_
        );

        return;
    }

    {
        std::lock_guard<std::mutex>
            lock(
                connection_mutex_
            );

        if (
            connection_ ==
            connection
        ) {
            connection_.reset();
        }
    }

    connect_requested_.store(
        false,
        std::memory_order_release
    );

    /*
     * 和当前 TcpServer 的连接销毁方式一致：
     * 延迟执行 ConnectDestroyed()。
     */
    loop_->QueueInLoop(
        [connection]() {
            connection->
                ConnectDestroyed();
        }
    );

    LOG_INFO(
        "tcp client connection removed"
        << ", name="
        << name_
        << ", connection="
        << connection->Name()
    );
}


void TcpClient::HandleConnectError(
    int error_number
) {
    connect_requested_.store(
        false,
        std::memory_order_release
    );

    LOG_WARN(
        "tcp client connect failed"
        << ", name="
        << name_
        << ", server="
        << server_address_.ToString()
        << ", error="
        << std::strerror(
            error_number
        )
    );

    if (connect_error_callback_) {
        connect_error_callback_(
            error_number
        );
    }
}


std::string
TcpClient::MakeConnectionName() {
    ++next_connection_id_;

    return
        name_ +
        "-conn-" +
        std::to_string(
            next_connection_id_
        ) +
        "-" +
        server_address_.ToString();
}

}  // namespace tinyimx