#pragma once

#include "common/net/InetAddress.h"
#include "common/net/Socket.h"
#include "common/net/TimerId.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

namespace tinyimx {

class Channel;
class EventLoop;

class Connector
    : public std::enable_shared_from_this<
          Connector
      > {
public:
    using NewConnectionCallback =
        std::function<void(int socket_fd)>;

    using ConnectErrorCallback =
        std::function<void(
            int error_number
        )>;

    Connector(
        EventLoop* loop,
        const InetAddress& server_address,
        std::chrono::milliseconds
            connect_timeout =
                std::chrono::milliseconds(
                    3000
                )
    );

    ~Connector();

    Connector(
        const Connector&
    ) = delete;

    Connector& operator=(
        const Connector&
    ) = delete;

    void SetNewConnectionCallback(
        NewConnectionCallback callback
    );

    void SetConnectErrorCallback(
        ConnectErrorCallback callback
    );

    void Start();

    void Stop();

    bool IsConnecting() const noexcept;

    const InetAddress&
    ServerAddress() const noexcept;

private:
    enum class State {
        kDisconnected = 0,
        kConnecting
    };

    void StartInLoop();

    void StopInLoop();

    void Connect();

    void BeginWatchingConnect(
        Socket socket
    );

    void HandleWrite();

    void HandleError();

    void HandleTimeout();

    void CompleteConnection();

    void FailConnection(
        int error_number
    );

    void RemoveAndResetChannel();

    void StartConnectTimeout();

    void CancelConnectTimeout();

    static int GetSocketError(
        int socket_fd
    );

private:
    EventLoop*
        loop_{nullptr};

    InetAddress
        server_address_;

    const std::chrono::milliseconds
        connect_timeout_;

    std::atomic<bool>
        connect_requested_{false};

    std::atomic<State>
        state_{
            State::kDisconnected
        };

    Socket socket_;

    std::unique_ptr<Channel>
        channel_;

    TimerId
        connect_timeout_timer_;

    NewConnectionCallback
        new_connection_callback_;

    ConnectErrorCallback
        connect_error_callback_;
};

}  // namespace tinyimx