#pragma once

#include "common/net/Connector.h"
#include "common/net/TcpConnection.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace tinyimx {

class EventLoop;

class TcpClient {
public:
    using ConnectErrorCallback =
        Connector::ConnectErrorCallback;

    TcpClient(
        EventLoop* loop,
        const InetAddress&
            server_address,
        std::string name,
        std::chrono::milliseconds
            connect_timeout =
                std::chrono::milliseconds(
                    3000
                )
    );

    ~TcpClient();

    TcpClient(
        const TcpClient&
    ) = delete;

    TcpClient& operator=(
        const TcpClient&
    ) = delete;

    void SetConnectionCallback(
        ConnectionCallback callback
    );

    void SetMessageCallback(
        MessageCallback callback
    );

    void SetConnectErrorCallback(
        ConnectErrorCallback callback
    );

    /*
     * true：
     * 连接请求已经成功提交给 EventLoop。
     *
     * 不代表 TCP 已经连接完成。
     */
    bool Connect();

    /*
     * 优雅关闭。
     */
    void Disconnect();

    /*
     * 强制停止。
     */
    void Stop();

    bool IsConnected() const;

    TcpConnectionPtr
    Connection() const;

    const std::string&
    Name() const noexcept;

    const InetAddress&
    ServerAddress() const noexcept;

private:
    void NewConnection(
        int socket_fd
    );

    void RemoveConnection(
        const TcpConnectionPtr&
            connection
    );

    void HandleConnectError(
        int error_number
    );

    std::string
    MakeConnectionName();

private:
    EventLoop*
        loop_{nullptr};

    InetAddress
        server_address_;

    std::string
        name_;

    std::shared_ptr<Connector>
        connector_;

    std::atomic<bool>
        connect_requested_{false};

    mutable std::mutex
        connection_mutex_;

    TcpConnectionPtr
        connection_;

    ConnectionCallback
        connection_callback_;

    MessageCallback
        message_callback_;

    ConnectErrorCallback
        connect_error_callback_;

    std::uint64_t
        next_connection_id_{0};
};

}  // namespace tinyimx