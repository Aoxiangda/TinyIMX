#pragma once

#include "common/net/InetAddress.h"
#include "common/net/TcpConnection.h"
#include "common/net/TcpServer.h"
#include "common/protocol/ProtocolCodec.h"
#include "gateway/SessionManager.h"
#include "gateway/OfflineMessageStore.h"
#include "gateway/MessageDeliveryDeduplicator.h"
#include "gateway/ReceiverDeliveryTracker.h"
#include "services/repository/FriendRepository.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <chrono>

namespace tinyimx {

class FriendRequestRepository;

class OnlineStatusCache;
class UnreadCountCache;
class GatewayRouteResolver;
class GatewayPeerTransportManager;

class EventLoop;
class BusinessExecutor;
struct BusinessRequestContext;

namespace rpc {
class SocialRpcClient;
class UserRpcClient;
class MessageRpcClient;
}

struct GatewayForwardChatResponse;
/*
    struct GatewayServerOptions {
        std::string name{"tinyimx-gateway"};
        std::size_t max_body_size{kDefaultMaxBodySize};
        bool close_on_decode_error{true};
    };
*/


struct GatewayServerOptions {
    std::string name{"tinyimx-gateway"};

    std::size_t max_body_size{kDefaultMaxBodySize};

    bool close_on_decode_error{true};

    std::size_t io_thread_count{0};

    std::size_t
        max_offline_messages_per_user{
            100
        };

    std::string gateway_id{
        "tinyimx-gateway-1"
    };

    int online_status_ttl_seconds{120};

    /*
    * Receiver Application ACK等待时间。
    *
    * Gateway发送：
    *
    *     kChatDelivery M/D
    *
    * 超过该时间仍未确认，则尝试：
    *
    *     same M / fresh D
    *
    * 当前先使用1500ms。
    *
    * 后续性能阶段根据真实RTT/P99调整，
    * 不能把它当最终经验参数。
    */
    std::chrono::milliseconds
        receiver_ack_timeout{
            1500
        };


    /*
    * Receiver Delivery总Attempt上限。
    *
    * 包含第一次发送。
    *
    * = 3意味着：
    *
    * D1
    * D2
    * D3
    *
    * 不产生D4。
    */
    std::uint32_t
        receiver_delivery_max_attempts{
            3
        };


    /*
    * ============================================================
    * M13 test-only Business Runtime fault injection
    * ============================================================
    *
    * 仅用于验证：
    *
    * blocking Business Work
    *      !=
    * blocking EventLoop
    *
    * 默认0ms，因此正常Gateway行为完全不受影响。
    *
    * 正式测试时由gateway_demo读取：
    *
    * TINYIMX_FAULT_HISTORY_BUSINESS_DELAY_MS
    */
    std::chrono::milliseconds
        history_business_delay_for_test{
            0
        };
};


class GatewayServer {
public:

    using PacketHandler =
        std::function<void(const TcpConnectionPtr&, const Packet&)>;

    using GatewayPeerResponseDropCallback =
        std::function<
            bool(const GatewayForwardChatResponse&)>;

    using GatewayPeerVerifyCallback = std::function<bool(
            const std::string&source_gateway_id,
            const std::string& source_lease_token,
            std::string* error_message)>;

    GatewayServer(EventLoop* loop,
                  const InetAddress& listen_address,
                  GatewayServerOptions options);

    ~GatewayServer();

    GatewayServer(const GatewayServer&) = delete;
    GatewayServer& operator=(const GatewayServer&) = delete;

    bool Start();
    void Stop();

    void SetPacketHandler(PacketHandler handler);

    bool SendPacket(const TcpConnectionPtr& connection,
                    const Packet& packet);

    const std::string& Name() const;
    const InetAddress& ListenAddress() const;
    std::size_t ConnectionCount() const;


    /*
    * M13 Business Execution Runtime。
    *
    * GatewayServer不拥有该BusinessExecutor。
    *
    * 生命周期由Bootstrap层负责，
    * 并且必须保证：
    *
    * BusinessExecutor停止并完成Drain以前，
    * Repository / Cache等业务依赖仍然存活。
    */
    void SetBusinessExecutor(BusinessExecutor* business_executor);
    void SetSocialRpcClient(rpc::SocialRpcClient* social_rpc_client);
    void SetUserRpcClient(rpc::UserRpcClient* user_rpc_client);
    void SetMessageRpcClient(rpc::MessageRpcClient* message_rpc_client);
    void SetFriendRepository(FriendRepository* repository);
    void SetFriendRequestRepository(
        FriendRequestRepository* repository
    );

    void SetOnlineStatusCache(OnlineStatusCache* online_status_cache);
    void SetGatewayRouteResolver(GatewayRouteResolver* route_resolver);
    void SetGatewayPeerVerifyCallback(GatewayPeerVerifyCallback callback);
    void SetUnreadCountCache(UnreadCountCache* unread_count_cache);
    void SetUnreadProjectionWriteEnabled(bool enabled);
    void SetGatewayPeerTransportManager(GatewayPeerTransportManager*manager);

    void SetGatewayPeerResponseDropCallbackForTest(
        GatewayPeerResponseDropCallback callback
    );
    /*
    private:
    void HandleConnection(const TcpConnectionPtr& connection);
    void HandleMessage(const TcpConnectionPtr& connection,
                       Buffer* buffer);

    void HandlePacket(const TcpConnectionPtr& connection,
                      const Packet& packet);

    Packet BuildDefaultResponse(const Packet& request) const;
    Packet MakeErrorPacket(std::uint32_t seq,
                           const std::string& message) const;
*/


private:
    void HandleConnection(const TcpConnectionPtr& connection);
    void HandleMessage(const TcpConnectionPtr& connection,
                       Buffer* buffer);

    void HandlePacket(const TcpConnectionPtr& connection,
                      const Packet& packet);

    void HandleLoginRequest(const TcpConnectionPtr& connection,
                            const Packet& packet);

    void HandleChatMessage(const TcpConnectionPtr& connection,
                           const Packet& packet);

    void ExecuteChatMessage(
        const TcpConnectionPtr& connection,
        const Packet& packet,
        const BusinessRequestContext& business_request
    );

    /*
    * Receiver Client -> Gateway
    *
    * 处理Gateway此前发送的：
    *
    *     kChatDelivery
    *
    * 对应的应用协议层确认：
    *
    *     kChatDeliveryAck
    *
    * Packet.seq:
    *     Receiver Delivery Attempt身份。
    *
    * body.message_id:
    *     稳定Server Message身份。
    *
    * Receiver身份绝不信任body，
    * 必须来自SessionManager。
    */
    void HandleReceiverChatDeliveryAck(const TcpConnectionPtr& connection,
                                       const Packet& packet);
    void ExecuteReceiverChatDeliveryAck(
        const TcpConnectionPtr& connection,
        const Packet& packet,
        const BusinessRequestContext& business_request
    );

    void HandleGatewayForwardChatRequest(const TcpConnectionPtr& connection,
                                         const Packet& packet);
    void ExecuteGatewayForwardChatRequest(
        const TcpConnectionPtr& connection,
        const Packet& packet,
        const BusinessRequestContext& business_request
    );

    void HandleReadRequest(const TcpConnectionPtr& connection,
                           const Packet& packet);
    void ExecuteReadRequest(
        const TcpConnectionPtr& connection,
        const Packet& packet,
        const BusinessRequestContext& business_request
    );

    void HandleHistoryRequest(const TcpConnectionPtr& connection,
                              const Packet& packet);

    void HandleUserProfileRequest(const TcpConnectionPtr& connection,
                                  const Packet& packet);

    void HandleFriendListRequest(const TcpConnectionPtr& connection,
                                 const Packet& packet);

    void HandleConversationListRequest(const TcpConnectionPtr& connection,
                                       const Packet& packet);

    void HandleFriendRequestCreateRequest(
        const TcpConnectionPtr& connection,
        const Packet& packet
    );

    void HandleFriendRequestListRequest(
        const TcpConnectionPtr& connection,
        const Packet& packet
    );

    void HandleFriendRequestAcceptRequest(
        const TcpConnectionPtr& connection,
        const Packet& packet
    );

    void HandleFriendRequestRejectRequest(
        const TcpConnectionPtr& connection,
        const Packet& packet
    );

    bool ClearUnread(UserId reader_user_id,
                     UserId peer_user_id,
                     std::int64_t* total_unread);

    void HandleHeartbeat(const TcpConnectionPtr& connection,
                         const Packet& packet);

    Packet MakeErrorPacket(std::uint32_t seq,
                           const std::string& message) const;

    /*
    * 为Gateway -> Receiver的一次网络投递Attempt
    * 分配新的Packet.seq。
    *
    * 注意：
    *
    * 它不是：
    *
    * - Sender Request seq
    * - Gateway Peer RPC seq
    * - server message_id
    *
    * 0保留为无效seq，因此生成器必须跳过0。
    */
    std::uint32_t
        NextReceiverDeliverySeq() noexcept;

        enum class ReceiverDeliverySubmitStatus {
        /*
        * Delivery Attempt已经登记，
        * 并已经提交给TcpConnection发送路径。
        */
        kSubmitted = 0,

        /*
        * Tracker已经确认该message。
        *
        * 不允许重新打开WaitingAck。
        */
        kAlreadyConfirmed,

        /*
        * 当前connection在提交前已经不可用。
        *
        * 此状态不会登记Attempt。
        */

        kConnectionUnavailable,
        /*
        * Timeout callback针对的Attempt
        * 已经不是current Attempt。
        */
        kStaleAttempt,

        /*
        * 已达到receiver_delivery_max_attempts。
        */
        kAttemptLimitReached,
        /*
        * kChatDelivery构造失败。
        */
        kBuildFailed,

        /*
        * ProtocolCodec编码失败。
        *
        * 此时还没有登记Attempt。
        */
        kEncodeFailed,

        /*
        * Tracker拒绝本次Attempt。
        *
        * 例如：
        * receiver identity冲突，
        * delivery_seq重复，
        * 参数非法。
        */
        kTrackerRejected
    };



    /*
    * 构造Gateway -> Receiver的正式投递Packet。
    *
    * Packet.seq：
    *     一次Receiver Delivery Attempt身份。
    *
    * body.message_id：
    *     稳定Server Business Message身份。
    *
    * 同一个message_id未来Retry时：
    *
    * message_id保持不变，
    * Packet.seq重新生成。
    */
    bool BuildReceiverChatDeliveryPacket(
        std::uint64_t message_id,
        UserId from_user_id,
        UserId to_user_id,
        const std::string& text,
        Packet* packet,
        std::string* error_message = nullptr
    );


    /*
    * Gateway -> Receiver正式发送入口。
    *
    * 统一完成：
    *
    * Build
    *   ↓
    * Encode
    *   ↓
    * RegisterAttempt
    *   ↓
    * TcpConnection::Send
    *
    * 这样三个业务入口不会分别复制
    * ACK Tracking逻辑。
    */
    ReceiverDeliverySubmitStatus
        SubmitReceiverChatDelivery(
            const TcpConnectionPtr& connection,
            std::uint64_t message_id,
            UserId from_user_id,
            UserId to_user_id,
            const std::string& text,
            Packet* submitted_packet = nullptr,
            std::string* error_message = nullptr
        );


    /*
    * 为一次已经真实提交的Receiver Delivery
    * 安排ACK Timeout。
    *
    * Timer挂到该Receiver Connection所属
    * EventLoop/Sub-Reactor。
    */
    bool ScheduleReceiverDeliveryAckTimeout(
        const TcpConnectionPtr& connection,
        std::uint64_t message_id,
        UserId from_user_id,
        UserId to_user_id,
        const std::string& text,
        std::uint32_t delivery_seq
    );


    /*
    * ACK Timeout真正触发后的状态处理。
    *
    * timed_out_delivery_seq非常重要：
    *
    * callback必须知道：
    *
    * “我是D1的Timer”
    *
    * 而不是仅仅知道：
    *
    * “我是M的Timer”。
    */
    void HandleReceiverDeliveryAckTimeout(
        std::uint64_t message_id,
        UserId from_user_id,
        UserId to_user_id,
        const std::string& text,
        std::uint32_t timed_out_delivery_seq
    );


    /*
    * Timeout场景专用Retry提交入口。
    *
    * 与普通Submit最大的区别：
    *
    * 普通：
    * RegisterAttempt()
    *
    * Retry：
    * RegisterRetryAttempt(
    *     expected_old_seq,
    *     fresh_new_seq
    * )
    *
    * 从而原子防止并发/迟到Timeout
    * 制造多个Retry。
    */
    ReceiverDeliverySubmitStatus
    SubmitReceiverChatDeliveryRetry(
        const TcpConnectionPtr& connection,
        std::uint64_t message_id,
        UserId from_user_id,
        UserId to_user_id,
        const std::string& text,
        std::uint32_t timed_out_delivery_seq,
        Packet* submitted_packet = nullptr,
        std::string* error_message = nullptr
    );


    void PushOfflineMessages(UserId user_id,
                         const TcpConnectionPtr& connection);

    void PushPersistentOfflineMessages(UserId user_id,
                                   const TcpConnectionPtr& connection);
    void ExecutePersistentOfflineReplay(
        UserId user_id,
        const TcpConnectionPtr& connection,
        const BusinessRequestContext& business_request
    );

    bool HasBusinessExecutor() const;
    bool HasSocialRpcClient() const;
    bool HasUserRpcClient() const;
    bool HasMessageRpcClient() const;
    bool HasFriendRepository() const;
    bool HasFriendRequestRepository() const;

    bool HasOnlineStatusCache() const;
    bool HasGatewayRouteResolver() const;
    bool HasUnreadCountCache() const;
    bool HasGatewayPeerTransportManager() const;
    void NotifyLoginReplaced(UserId user_id,
                             const TcpConnectionPtr& old_connection);

    void SetUserOnline(UserId user_id,
                    const TcpConnectionPtr& connection);

    void SetUserOfflineIfMatch(UserId user_id, const TcpConnectionPtr& connection);

    std::int64_t GetTotalUnread(UserId user_id);

    std::int64_t GetPrivateUnread(UserId receiver_user_id,
                                  UserId sender_user_id);

    std::int64_t IncrementUnread(UserId receiver_user_id,
                                 UserId sender_user_id,
                                 std::int64_t* total_unread);

    void EnsureUnreadProjection(
        std::uint64_t message_id,
        UserId receiver_user_id,
        UserId sender_user_id,
        bool should_count_as_unread,
        std::int64_t* private_unread,
        std::int64_t* total_unread
    );

    void RefreshUserOnlineIfMatch(
        UserId user_id,
        const TcpConnectionPtr& connection
    );

private:
    EventLoop* loop_{nullptr};

    GatewayServerOptions options_;

    ProtocolCodec codec_;
    TcpServer server_;


    /*
    * Gateway进程内的Receiver Delivery Attempt seq生成器。
    *
    * 它只负责短期Network Attempt Correlation。
    *
    * 不承担稳定业务身份。
    *
    * Gateway进程重启后允许重新从1开始：
    * 真正跨Retry/跨重启稳定的身份是message_id。
    */
    std::atomic<std::uint32_t>
        next_receiver_delivery_seq_{1};


    SessionManager session_manager_;
    OfflineMessageStore offline_message_store_;

        /*
    * Gateway-to-Gateway消息的
    * 进程内业务幂等控制器。
    *
    * message_id:
    *   kProcessing
    *   kDelivered
    */
    /*
    * Gateway进程内统一消息投递执行权控制器。
    *
    * Local Delivery与Peer Delivery
    * 后续共同使用同一个server message_id语义：
    *
    * NotSeen
    *    ↓
    * Processing
    *    ↓
    * Delivered
    *
    * 目的：
    *
    * 同一个server message_id在当前Gateway中
    * 只能有一个执行者真正产生Receiver-visible
    * SendPacket副作用。
    */
    MessageDeliveryDeduplicator message_delivery_deduplicator_;
    ReceiverDeliveryTracker receiver_delivery_tracker_;

    /*
    * M13 Business Runtime。
    *
    * Non-owning pointer。
    *
    * GatewayServer只使用，不负责delete。
    * Bootstrap层负责Start / Shutdown / lifetime。
    */
    BusinessExecutor* business_executor_{nullptr};

    /*
    * M14-A3 internal RPC dependency. Non-owning.
    * Bootstrap owns SocialRpcClient and must keep it alive until the
    * BusinessExecutor has fully drained accepted work.
    */
    rpc::SocialRpcClient* social_rpc_client_{nullptr};

    /*
    * M14-B2 internal UserService RPC dependency. Non-owning.
    * Bootstrap owns UserRpcClient and must keep it alive until the
    * BusinessExecutor has fully drained accepted Login work.
    */
    rpc::UserRpcClient* user_rpc_client_{nullptr};

    /*
     * M14-C1 durable MessageService read dependency. Non-owning.
     * C1 uses it only for History/ConversationList; C2/C3 migrate the
     * remaining durable mutation/replay paths without changing ownership of
     * Session, Presence, Packet.seq(D), routing or actual delivery.
     */
    rpc::MessageRpcClient* message_rpc_client_{nullptr};

    std::atomic<std::uint64_t>
        next_internal_rpc_id_{1};

    FriendRepository* friend_repository_{nullptr};
    FriendRequestRepository* friend_request_repository_{nullptr};

    OnlineStatusCache* online_status_cache_{nullptr};
    GatewayRouteResolver* gateway_route_resolver_{nullptr};
    GatewayPeerVerifyCallback gateway_peer_verify_callback_;
    GatewayPeerResponseDropCallback gateway_peer_response_drop_callback_for_test_;
    GatewayPeerTransportManager* gateway_peer_transport_manager_{nullptr};
    UnreadCountCache* unread_count_cache_{nullptr};
    bool unread_projection_write_enabled_{true};
    PacketHandler packet_handler_;
};

}  // namespace tinyimx