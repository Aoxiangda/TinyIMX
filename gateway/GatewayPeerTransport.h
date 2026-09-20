#pragma once

#include "common/net/TimerId.h"
#include "common/protocol/GatewayPeerProtocol.h"
#include "common/protocol/GatewayGroupPeerProtocol.h"
#include "common/protocol/ProtocolCodec.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <deque>

namespace tinyimx {

class Buffer;
class EventLoop;
class TcpClient;
class TcpConnection;

using TcpConnectionPtr =
    std::shared_ptr<TcpConnection>;


enum class GatewayPeerTransportStatus {
    kOk = 0,

    kInvalidArgument,
    kNotRunning,
    kNotConnected,

    kEncodeError,
    kRequestTimeout,
    kDisconnected,
    kProtocolError,

    kStopped
};


struct GatewayPeerTransportResult {
    GatewayPeerTransportStatus status{
        GatewayPeerTransportStatus::
            kProtocolError
    };

    GatewayForwardChatResponse response;

    std::string error_message;

    bool Succeeded() const noexcept {
        return status ==
            GatewayPeerTransportStatus::kOk;
    }
};

struct GatewayGroupPeerTransportResult {
    GatewayPeerTransportStatus status{GatewayPeerTransportStatus::kProtocolError};
    GatewayForwardGroupMessageResponse response;
    std::string error_message;
    bool Succeeded() const noexcept { return status == GatewayPeerTransportStatus::kOk; }
};


struct GatewayPeerTransportOptions {
    std::string local_gateway_id;

    std::string local_lease_token;

    std::string remote_gateway_id;

    std::string remote_host;

    std::uint16_t remote_port{0};

    std::chrono::milliseconds
        connect_timeout{
            std::chrono::milliseconds(3000)
        };

    std::chrono::milliseconds
        request_timeout{
            std::chrono::milliseconds(3000)
        };

    std::size_t max_body_size{
        kDefaultMaxBodySize
    };
};


class GatewayPeerTransport
    : public std::enable_shared_from_this<
          GatewayPeerTransport
      > {
public:
    using ForwardChatCallback =
        std::function<void(
            GatewayPeerTransportResult
        )>;

    using ForwardGroupMessageCallback =
        std::function<void(GatewayGroupPeerTransportResult)>;

    using ConnectionStateCallback =
        std::function<void(bool connected)>;

    GatewayPeerTransport(
        EventLoop* loop,
        GatewayPeerTransportOptions options
    );

    ~GatewayPeerTransport();

    GatewayPeerTransport(
        const GatewayPeerTransport&
    ) = delete;

    GatewayPeerTransport& operator=(
        const GatewayPeerTransport&
    ) = delete;


    /*
     * Start / Stop 可以跨线程调用。
     *
     * 实际 transport 状态仍然全部
     * 在 loop_ 所属线程修改。
     */
    bool Start();

    void Stop();


    /*
     * 调用者可以来自 Sub-Reactor。
     *
     * 函数本身不会等待远端响应，
     * 请求会投递到 transport EventLoop。
     */
    bool ForwardChat(
        std::uint64_t message_id,
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        std::string message_body,
        ForwardChatCallback callback
    );

    bool ForwardGroupMessage(
        std::uint64_t message_id,
        std::uint64_t recipient_user_id,
        ForwardGroupMessageCallback callback
    );


    bool IsRunning() const noexcept;

    bool IsConnected() const noexcept;

    std::size_t PendingCount()
        const noexcept;


    void SetConnectionStateCallback(
        ConnectionStateCallback callback
    );


    const std::string&
    RemoteGatewayId() const noexcept;


private:
    struct PendingRequest {
        ForwardChatCallback callback;

        TimerId timeout_timer;
    };

    struct PendingGroupRequest {
        ForwardGroupMessageCallback callback;
        TimerId timeout_timer;
    };

    struct QueuedForwardGroupMessage {
        std::uint64_t message_id{0};
        std::uint64_t recipient_user_id{0};
        ForwardGroupMessageCallback callback;
    };

    struct QueuedForwardChat {
        std::uint64_t message_id{0};
        std::uint64_t from_user_id{0};
        std::uint64_t to_user_id{0};

        std::string message_body;

        ForwardChatCallback callback;
    };

    void StartInLoop();

    void StopInLoop();


    void HandleConnection(
        const TcpConnectionPtr& connection
    );

    void HandleMessage(
        const TcpConnectionPtr& connection,
        Buffer* buffer
    );

    void HandleConnectError(
        int error_number
    );


    void ForwardGroupMessageInLoop(
        std::uint64_t message_id,
        std::uint64_t recipient_user_id,
        ForwardGroupMessageCallback callback
    );

    void HandleForwardGroupMessageResponse(
        const Packet& packet
    );

    void ForwardChatInLoop(
        std::uint64_t message_id,
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        std::string message_body,
        ForwardChatCallback callback
    );


    void HandleForwardChatResponse(
        const Packet& packet
    );


    void HandleRequestTimeout(
        std::uint32_t sequence
    );

    void HandleGroupRequestTimeout(
        std::uint32_t sequence
    );


    void FailAllPending(
        GatewayPeerTransportStatus status,
        const std::string& error_message
    );

    void FailAllPendingGroup(
        GatewayPeerTransportStatus status,
        const std::string& error_message
    );


    std::uint32_t NextSequence();


    bool ValidateOptions(
        std::string* error_message
    ) const;


    void FlushQueuedForwards();
    void FlushQueuedGroupForwards();

    void FailQueuedForwards(
        GatewayPeerTransportStatus status,
        const std::string& error_message
    );

    void FailQueuedGroupForwards(
        GatewayPeerTransportStatus status,
        const std::string& error_message
    );

private:
    EventLoop* loop_{nullptr};

    GatewayPeerTransportOptions
        options_;

    ProtocolCodec codec_;

    std::unique_ptr<TcpClient>
        tcp_client_;

    std::atomic<bool>
        running_{false};

    std::atomic<bool>
        connected_{false};

    std::atomic<std::size_t>
        pending_count_{0};

    std::uint32_t next_sequence_{1};

    std::unordered_map<
        std::uint32_t,
        PendingRequest
    > pending_requests_;

    std::unordered_map<std::uint32_t, PendingGroupRequest> pending_group_requests_;

    ConnectionStateCallback
        connection_state_callback_;

    std::deque<QueuedForwardChat>
        queued_forwards_;

    std::deque<QueuedForwardGroupMessage> queued_group_forwards_;
};


std::string
GatewayPeerTransportStatusToString(
    GatewayPeerTransportStatus status
);

}  // namespace tinyimx