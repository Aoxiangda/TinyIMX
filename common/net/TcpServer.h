#pragma once

#include "common/net/InetAddress.h"
#include "common/net/Socket.h"
#include "common/net/TcpConnection.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace tinyimx {

class Channel;
class EventLoop;

class TcpServer {
public:
    TcpServer(EventLoop* loop,
              const InetAddress& listen_address,
              std::string name);

    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool Start();
    void Stop();

    void SetConnectionCallback(ConnectionCallback callback);
    void SetMessageCallback(MessageCallback callback);

    const std::string& Name() const;
    const InetAddress& ListenAddress() const;

    std::size_t ConnectionCount() const;
    bool IsStarted() const;

private:
    void HandleAccept();
    void RemoveConnection(const TcpConnectionPtr& connection);

    std::string MakeConnectionName(const InetAddress& peer_address);

private:
    EventLoop* loop_{nullptr};

    const std::string name_;
    const InetAddress listen_address_;

    Socket listen_socket_;
    std::unique_ptr<Channel> accept_channel_;

    std::atomic<bool> started_{false};

    std::uint64_t next_connection_id_{0};

    std::unordered_map<std::string, TcpConnectionPtr> connections_;

    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
};

}  // namespace tinyimx