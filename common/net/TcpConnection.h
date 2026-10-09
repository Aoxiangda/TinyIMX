#pragma once

#include "common/net/Buffer.h"
#include "common/net/InetAddress.h"
#include "common/net/Socket.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace tinyimx {

class Channel;
class EventLoop;
class TcpConnection;

using TcpConnectionPtr =
    std::shared_ptr<TcpConnection>;

using ConnectionCallback =
    std::function<void(const TcpConnectionPtr&)>;

using MessageCallback =
    std::function<void(
        const TcpConnectionPtr&,
        Buffer*
    )>;

using CloseCallback =
    std::function<void(const TcpConnectionPtr&)>;

using WriteCompleteCallback =
    std::function<void(const TcpConnectionPtr&)>;

using HighWaterMarkCallback =
    std::function<void(
        const TcpConnectionPtr&,
        std::size_t
    )>;

class TcpConnection
    : public std::enable_shared_from_this<TcpConnection> {
public:
    using Clock =
        std::chrono::steady_clock;

    using TimePoint =
        Clock::time_point;

    TcpConnection(
        EventLoop* loop,
        std::string name,
        int socket_fd,
        const InetAddress& local_address,
        const InetAddress& peer_address
    );

    ~TcpConnection();

    TcpConnection(
        const TcpConnection&
    ) = delete;

    TcpConnection& operator=(
        const TcpConnection&
    ) = delete;

    void SetConnectionCallback(
        ConnectionCallback callback
    );

    void SetMessageCallback(
        MessageCallback callback
    );

    void SetCloseCallback(
        CloseCallback callback
    );

    void SetWriteCompleteCallback(
        WriteCompleteCallback callback
    );

    void SetHighWaterMarkCallback(
        HighWaterMarkCallback callback,
        std::size_t high_water_mark
    );

    void ConnectEstablished();
    void ConnectDestroyed();

    void Send(
        const std::string& message
    );

    void Send(
        const char* data,
        std::size_t length
    );

    void Shutdown();
    void ForceClose();

    bool IsConnected() const;

    const std::string& Name() const;

    const InetAddress&
    LocalAddress() const;

    const InetAddress&
    PeerAddress() const;

    EventLoop* GetLoop() const;

    TimePoint
    LastReadTime() const noexcept;

    Buffer* InputBuffer();
    Buffer* OutputBuffer();

private:
    enum class State {
        kConnecting = 0,
        kConnected,
        kDisconnecting,
        kDisconnected
    };

    using ActivityTick =
        Clock::duration::rep;

    static constexpr std::size_t
        kDefaultHighWaterMark =
            64U * 1024U * 1024U;

    static ActivityTick
    ToActivityTick(
        TimePoint time
    ) noexcept;

    State GetState() const noexcept;

    void SetState(
        State state
    ) noexcept;

    void RefreshLastReadTime() noexcept;

    void HandleRead();
    void HandleWrite();
    void HandleClose();
    void HandleError();

    void SendInLoop(
        const char* data,
        std::size_t length
    );

    void ShutdownInLoop();
    void ForceCloseInLoop();

    void QueueWriteCompleteCallback();

    void QueueHighWaterMarkCallback(
        std::size_t buffered_bytes
    );

private:
    EventLoop* loop_{nullptr};

    const std::string name_;

    std::atomic<State> state_{
        State::kConnecting
    };

    std::atomic<ActivityTick>
        last_read_tick_{0};

    Socket socket_;

    std::unique_ptr<Channel>
        channel_;

    const InetAddress local_address_;
    const InetAddress peer_address_;

    Buffer input_buffer_;
    Buffer output_buffer_;

    ConnectionCallback
        connection_callback_;

    MessageCallback
        message_callback_;

    CloseCallback
        close_callback_;

    WriteCompleteCallback
        write_complete_callback_;

    HighWaterMarkCallback
        high_water_mark_callback_;

    std::size_t high_water_mark_{
        kDefaultHighWaterMark
    };
};

}  // namespace tinyimx