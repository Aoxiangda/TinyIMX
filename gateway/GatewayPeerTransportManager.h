#pragma once

#include "common/net/TimerId.h"
#include "gateway/GatewayPeerTransport.h"
#include "services/registry/GatewayRegistry.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace tinyimx {

class EventLoop;


struct GatewayPeerTransportManagerOptions {
    std::string local_gateway_id;

    std::string local_lease_token;

    std::chrono::milliseconds
        connect_timeout{
            std::chrono::milliseconds(3000)
        };

    std::chrono::milliseconds
        request_timeout{
            std::chrono::milliseconds(3000)
        };

            /*
     * 一次ForwardChat在Request Timeout后
     * 最多允许重新提交多少次。
     *
     * 0:
     *   不自动Retry。
     *
     * 1:
     *   Initial Attempt
     *   +
     *   最多1次Retry。
     *
     * Retry必须保持同一个message_id，
     * 但底层Transport会为每次Attempt
     * 分配新的Packet.seq。
     */
    std::size_t max_request_retries{0};
    /*
     * Peer断线后的第一次重连延迟。
     */

    /*
     * ForwardChat Retry自己的退避参数。
     *
     * 注意：
     * 这与TCP连接重连的
     * reconnect_initial_delay /
     * reconnect_max_delay
     * 是两个完全不同的概念。
     */
    std::chrono::milliseconds
        request_retry_initial_delay{
            std::chrono::milliseconds(100)
        };


    std::chrono::milliseconds
        request_retry_max_delay{
            std::chrono::milliseconds(1000)
        };
    std::chrono::milliseconds
        reconnect_initial_delay{
            std::chrono::milliseconds(200)
        };

    /*
     * 指数退避上限。
     *
     * 200 -> 400 -> 800 -> 1600
     * -> 3200 -> 5000 -> 5000...
     */
    std::chrono::milliseconds
        reconnect_max_delay{
            std::chrono::milliseconds(5000)
        };

    std::size_t max_body_size{
        kDefaultMaxBodySize
    };
};


class GatewayPeerTransportManager
    : public std::enable_shared_from_this<
          GatewayPeerTransportManager
      > {
public:
    using ForwardChatCallback =
        GatewayPeerTransport::
            ForwardChatCallback;

    using ForwardGroupMessageCallback =
        GatewayPeerTransport::ForwardGroupMessageCallback;

    /*
     * Manager在重连之前重新解析Gateway。
     *
     * 生产环境：
     *   GatewayDiscovery::FindById()
     *
     * 测试：
     *   Fake resolver
     */
    using GatewayResolverCallback =
        std::function<
            std::optional<
                GatewayInstanceRecord
            >(
                const std::string&
                    gateway_id
            )
        >;


    GatewayPeerTransportManager(
        EventLoop* loop,
        GatewayPeerTransportManagerOptions
            options
    );

    ~GatewayPeerTransportManager();

    GatewayPeerTransportManager(
        const GatewayPeerTransportManager&
    ) = delete;

    GatewayPeerTransportManager& operator=(
        const GatewayPeerTransportManager&
    ) = delete;


    /*
     * 必须在Start前配置。
     */
    void SetGatewayResolverCallback(
        GatewayResolverCallback callback
    );


    bool Start();

    void Stop();


    bool ForwardChat(
        const GatewayInstanceRecord&
            remote_gateway,
        std::uint64_t message_id,
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        std::string message_body,
        ForwardChatCallback callback
    );

    // M17-B2 group fanout uses durable MySQL retry/lease as its retry boundary.
    // Peer transport performs one asynchronous network submission per durable attempt.
    bool ForwardGroupMessage(
        const GatewayInstanceRecord& remote_gateway,
        std::uint64_t message_id,
        std::uint64_t recipient_user_id,
        ForwardGroupMessageCallback callback
    );


    bool IsRunning() const noexcept;

    std::size_t TransportCount()
        const noexcept;


private:
    /*
     * 一个业务ForwardChat调用
     * 跨多个RPC Attempt共享的状态。
     *
     * 注意：
     *
     * message_id / from / to / body
     * 在Retry过程中绝对不能变化。
     *
     * Packet.seq不放在这里，
     * 因为seq属于每一次Transport Attempt，
     * 由GatewayPeerTransport::NextSequence()
     * 独立生成。
     */
    struct ForwardChatRetryContext {
        GatewayInstanceRecord
            remote_gateway;

        std::uint64_t message_id{0};

        std::uint64_t from_user_id{0};

        std::uint64_t to_user_id{0};

        std::string message_body;

        ForwardChatCallback callback;

        /*
         * 已经真正执行过多少次Retry。
         *
         * Initial Attempt不计入。
         */
        std::size_t retry_count{0};

        /*
         * 保证最终用户callback
         * 最多只完成一次。
         *
         * 只在Manager所属Peer EventLoop
         * 中访问，不需要atomic。
         */
        bool completed{false};
    };

    enum class ForwardChatRetryReason {
        kNone = 0,

        kRequestTimeout,

        kDisconnected,

        kNotConnected,

        kDuplicateInProgress
    };

    struct PeerEntry {
        std::string host;

        std::uint16_t port{0};

        std::shared_ptr<
            GatewayPeerTransport
        > transport;

        /*
         * 0表示下一次使用initial delay。
         */
        std::size_t reconnect_attempt{0};

        TimerId reconnect_timer;
    };


    void StopInLoop();


    void ForwardGroupMessageInLoop(
        GatewayInstanceRecord remote_gateway,
        std::uint64_t message_id,
        std::uint64_t recipient_user_id,
        ForwardGroupMessageCallback callback
    );

    void ForwardChatInLoop(
        GatewayInstanceRecord
            remote_gateway,
        std::uint64_t message_id,
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        std::string message_body,
        ForwardChatCallback callback
    );


    std::shared_ptr<
        GatewayPeerTransport
    >
    GetOrCreateTransport(
        const GatewayInstanceRecord&
            remote_gateway
    );


    std::shared_ptr<
        GatewayPeerTransport
    >
    CreateTransport(
        const GatewayInstanceRecord&
            remote_gateway
    );


    /*
     * GatewayPeerTransport连接状态通知。
     */
    void HandleTransportConnectionState(
        const std::string& gateway_id,
        std::weak_ptr<
            GatewayPeerTransport
        > source_transport,
        bool connected
    );


    /*
     * 安排下一次重连。
     */
    void ScheduleReconnectInLoop(
        const std::string& gateway_id
    );


    /*
     * Timer真正到期后执行。
     */
    void AttemptReconnectInLoop(
        const std::string& gateway_id
    );


    void CancelReconnectTimer(
        PeerEntry* entry
    );


    std::chrono::milliseconds
        ReconnectDelay(
            std::size_t attempt
    ) const;


    void SubmitForwardChatAttemptInLoop(
        const std::shared_ptr<
            ForwardChatRetryContext
        >& context
    );


    void HandleForwardChatAttemptResultInLoop(
        const std::shared_ptr<
            ForwardChatRetryContext
        >& context,
        GatewayPeerTransportResult result
    );


    void CompleteForwardChatInLoop(
        const std::shared_ptr<
            ForwardChatRetryContext
        >& context,
        GatewayPeerTransportResult result
    );


    ForwardChatRetryReason
        ClassifyForwardChatRetry(
            const GatewayPeerTransportResult&
                result
        ) const noexcept;


    std::chrono::milliseconds
        RequestRetryDelay(
            std::size_t retry_number
        ) const;
private:
    EventLoop* loop_{nullptr};

    GatewayPeerTransportManagerOptions
        options_;

    GatewayResolverCallback
        gateway_resolver_callback_;

    std::atomic<bool>
        running_{false};

    std::atomic<std::size_t>
        transport_count_{0};

    /*
     * 只在Peer EventLoop线程访问。
     *
     * 不需要mutex。
     */
    std::unordered_map<
        std::string,
        PeerEntry
    > transports_;
};

}  // namespace tinyimx