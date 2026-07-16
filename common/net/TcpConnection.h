#pragma once

#include "common/net/Buffer.h"
#include "common/net/InetAddress.h"
#include "common/net/Socket.h"

#include <functional>
#include <memory>
#include <string>

namespace tinyimx {

class Channel;
class EventLoop;
class TcpConnection;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

using ConnectionCallback =
    std::function<void(const TcpConnectionPtr&)>;

using MessageCallback =
    std::function<void(const TcpConnectionPtr&, Buffer*)>;

using CloseCallback =
    std::function<void(const TcpConnectionPtr&)>;

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(EventLoop* loop,
                  std::string name,
                  int socket_fd,
                  const InetAddress& local_address,
                  const InetAddress& peer_address);

    ~TcpConnection();

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    void SetConnectionCallback(ConnectionCallback callback);
    void SetMessageCallback(MessageCallback callback);
    void SetCloseCallback(CloseCallback callback);

    void ConnectEstablished();
    void ConnectDestroyed();

    void Send(const std::string& message);
    void Send(const char* data, std::size_t length);

    void Shutdown();
    void ForceClose();

    bool IsConnected() const;

    const std::string& Name() const;
    const InetAddress& LocalAddress() const;
    const InetAddress& PeerAddress() const;

    EventLoop* GetLoop() const;

    Buffer* InputBuffer();
    Buffer* OutputBuffer();

private:
    enum class State {
        kConnecting = 0,
        kConnected,
        kDisconnecting,
        kDisconnected
    };

    void SetState(State state);

    void HandleRead();
    void HandleWrite();
    void HandleClose();
    void HandleError();

    void SendInLoop(const char* data, std::size_t length);
    void ShutdownInLoop();

private:
    EventLoop* loop_{nullptr};
    const std::string name_;

    State state_{State::kConnecting};

    Socket socket_;
    std::unique_ptr<Channel> channel_;

    const InetAddress local_address_;
    const InetAddress peer_address_;

    Buffer input_buffer_;
    Buffer output_buffer_;

    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
    CloseCallback close_callback_;
};

}  // namespace tinyimx