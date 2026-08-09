#pragma once

#include "common/net/InetAddress.h"
#include "common/net/Socket.h"
#include "common/net/TcpConnection.h"
#include "common/net/TimerId.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace tinyimx {

class Channel;
class EventLoop;
class EventLoopThreadPool;

class TcpServer {
public:
    TcpServer(
        EventLoop* loop,
        const InetAddress& listen_address,
        std::string name,
        std::size_t io_thread_count = 0
    );

    ~TcpServer();

    TcpServer(
        const TcpServer&
    ) = delete;

    TcpServer& operator=(
        const TcpServer&
    ) = delete;

    bool Start();
    void Stop();

    void SetConnectionCallback(
        ConnectionCallback callback
    );

    void SetMessageCallback(
        MessageCallback callback
    );

    bool SetIdleTimeout(
        std::chrono::milliseconds idle_timeout,
        std::chrono::milliseconds check_interval
    );

    const std::string& Name() const;

    const InetAddress&
    ListenAddress() const;

    std::size_t ConnectionCount() const;

    bool IsStarted() const;

private:
    void HandleAccept();

    void RemoveConnection(
        const TcpConnectionPtr& connection
    );

    void RemoveConnectionInLoop(
        const TcpConnectionPtr& connection
    );

    void CheckIdleConnections();

    std::string MakeConnectionName(
        const InetAddress& peer_address
    );

private:
    EventLoop* loop_{nullptr};

    const std::string name_;
    const InetAddress listen_address_;

    const std::size_t io_thread_count_{0};

    std::unique_ptr<EventLoopThreadPool>
        thread_pool_;

    Socket listen_socket_;

    std::unique_ptr<Channel>
        accept_channel_;

    std::atomic<bool>
        started_{false};

    std::atomic<std::size_t>
        connection_count_{0};

    std::uint64_t
        next_connection_id_{0};

    std::unordered_map<
        std::string,
        TcpConnectionPtr
    > connections_;

    ConnectionCallback
        connection_callback_;

    MessageCallback
        message_callback_;

    std::chrono::milliseconds
        idle_timeout_{0};

    std::chrono::milliseconds
        idle_check_interval_{0};

    TimerId idle_check_timer_id_;
};

}  // namespace tinyimx