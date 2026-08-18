#include "gateway/GatewayPeerTransportManager.h"

#include "common/logging/LogMacros.h"
#include "common/net/EventLoop.h"

#include <algorithm>
#include <utility>

namespace tinyimx {


GatewayPeerTransportManager::
GatewayPeerTransportManager(
    EventLoop* loop,
    GatewayPeerTransportManagerOptions
        options
)
    : loop_(loop),
      options_(
          std::move(options)
      ) {
}


GatewayPeerTransportManager::
~GatewayPeerTransportManager() {
    if (IsRunning()) {
        LOG_WARN(
            "gateway peer transport manager "
            "destroyed while running"
        );
    }
}


void GatewayPeerTransportManager::
SetGatewayResolverCallback(
    GatewayResolverCallback callback
) {
    if (IsRunning()) {
        LOG_WARN(
            "gateway peer resolver ignored: "
            "manager already running"
        );

        return;
    }

    gateway_resolver_callback_ =
        std::move(callback);
}


bool
GatewayPeerTransportManager::Start() {
    if (
        loop_ == nullptr ||
        !loop_->IsValid()
    ) {
        LOG_ERROR(
            "gateway peer transport manager "
            "start failed: invalid loop"
        );

        return false;
    }


    if (
        options_.local_gateway_id.empty() ||
        options_.local_lease_token.empty()
    ) {
        LOG_ERROR(
            "gateway peer transport manager "
            "start failed: invalid local identity"
        );

        return false;
    }


    if (
        options_.reconnect_initial_delay <=
            std::chrono::milliseconds::zero() ||
        options_.reconnect_max_delay <=
            std::chrono::milliseconds::zero() ||
        options_.reconnect_initial_delay >
            options_.reconnect_max_delay
    ) {
        LOG_ERROR(
            "gateway peer transport manager "
            "start failed: invalid reconnect "
            "configuration"
        );

        return false;
    }


    bool expected = false;

    if (
        !running_.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel
        )
    ) {
        return true;
    }

        if (
        options_.max_request_retries > 0 &&
        (
            options_.
                request_retry_initial_delay <=
                std::chrono::
                    milliseconds::zero() ||

            options_.
                request_retry_max_delay <=
                std::chrono::
                    milliseconds::zero() ||

            options_.
                request_retry_initial_delay >
                options_.
                    request_retry_max_delay
        )
    ) {
        LOG_ERROR(
            "gateway peer transport manager "
            "start failed: invalid request "
            "retry configuration"
        );

        return false;
    }


    LOG_INFO(
        "gateway peer transport manager "
        "started"
        << ", local_gateway="
        << options_.local_gateway_id
        << ", reconnect_initial_ms="
        << options_.
            reconnect_initial_delay.count()
        << ", reconnect_max_ms="
        << options_.
            reconnect_max_delay.count()
    );

    return true;
}


void
GatewayPeerTransportManager::Stop() {
    if (
        !running_.exchange(
            false,
            std::memory_order_acq_rel
        )
    ) {
        return;
    }


    const auto self =
        weak_from_this().lock();

    if (
        !self ||
        loop_ == nullptr
    ) {
        return;
    }


    loop_->RunInLoop(
        [self]() {
            self->StopInLoop();
        }
    );
}


bool
GatewayPeerTransportManager::
ForwardChat(
    const GatewayInstanceRecord&
        remote_gateway,
    std::uint64_t message_id,
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    std::string message_body,
    ForwardChatCallback callback
) {
    if (
        !IsRunning() ||
        message_id == 0 ||
        remote_gateway.gateway_id.empty() ||
        remote_gateway.listen_host.empty() ||
        remote_gateway.listen_port == 0 ||
        from_user_id == 0 ||
        to_user_id == 0 ||
        message_body.empty()
    ) {
        return false;
    }


    const auto self =
        weak_from_this().lock();

    if (
        !self ||
        loop_ == nullptr
    ) {
        return false;
    }


    loop_->RunInLoop(
        [
            self,
            remote_gateway,
            message_id,
            from_user_id,
            to_user_id,
            message_body =
                std::move(message_body),
            callback =
                std::move(callback)
        ]() mutable {
            self->ForwardChatInLoop(
                std::move(
                    remote_gateway
                ),
                message_id,
                from_user_id,
                to_user_id,
                std::move(
                    message_body
                ),
                std::move(
                    callback
                )
            );
        }
    );

    return true;
}


bool
GatewayPeerTransportManager::
IsRunning() const noexcept {
    return running_.load(
        std::memory_order_acquire
    );
}


std::size_t
GatewayPeerTransportManager::
TransportCount() const noexcept {
    return transport_count_.load(
        std::memory_order_acquire
    );
}


void
GatewayPeerTransportManager::
StopInLoop() {
    for (
        auto& item :
        transports_
    ) {
        PeerEntry& entry =
            item.second;

        CancelReconnectTimer(
            &entry
        );

        if (entry.transport) {
            entry.transport->Stop();
        }
    }


    transports_.clear();

    transport_count_.store(
        0,
        std::memory_order_release
    );


    LOG_INFO(
        "gateway peer transport manager "
        "stopped"
    );
}


void
GatewayPeerTransportManager::
ForwardChatInLoop(
    GatewayInstanceRecord
        remote_gateway,
    std::uint64_t message_id,
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    std::string message_body,
    ForwardChatCallback callback
) {
    auto context =
        std::make_shared<
            ForwardChatRetryContext
        >();


    context->remote_gateway =
        std::move(
            remote_gateway
        );

    context->message_id =
        message_id;

    context->from_user_id =
        from_user_id;

    context->to_user_id =
        to_user_id;

    context->message_body =
        std::move(
            message_body
        );

    context->callback =
        std::move(
            callback
        );


    /*
     * Initial Attempt。
     *
     * retry_count此时仍然为0。
     */
    SubmitForwardChatAttemptInLoop(
        context
    );
}



void
GatewayPeerTransportManager::
SubmitForwardChatAttemptInLoop(
    const std::shared_ptr<
        ForwardChatRetryContext
    >& context
) {
    if (
        !context ||
        context->completed
    ) {
        return;
    }


    if (!IsRunning()) {
        GatewayPeerTransportResult result;

        result.status =
            GatewayPeerTransportStatus::
                kStopped;

        result.error_message =
            "gateway peer manager stopped";


        CompleteForwardChatInLoop(
            context,
            std::move(result)
        );

        return;
    }


    auto transport =
        GetOrCreateTransport(
            context->remote_gateway
        );


    if (!transport) {
        GatewayPeerTransportResult result;

        result.status =
            GatewayPeerTransportStatus::
                kNotConnected;

        result.error_message =
            "unable to create gateway "
            "peer transport";


        HandleForwardChatAttemptResultInLoop(
            context,
            std::move(result)
        );

        return;
    }


    const std::weak_ptr<
        GatewayPeerTransportManager
    > weak_self =
        weak_from_this();


    const bool submitted =
        transport->ForwardChat(
            /*
             * 最关键的不变量：
             *
             * 每一次Retry仍然使用
             * 同一个业务message_id。
             */
            context->message_id,

            context->from_user_id,

            context->to_user_id,

            /*
             * 这里不能move掉context里的body，
             * 因为下一次Retry还需要重新使用。
             */
            context->message_body,

            [
                weak_self,
                context
            ](
                GatewayPeerTransportResult
                    result
            ) mutable {
                const auto self =
                    weak_self.lock();


                if (!self) {
                    return;
                }


                /*
                 * Transport callback当前本来就在
                 * Peer EventLoop线程执行。
                 *
                 * 仍统一经过RunInLoop，
                 * 保持Manager状态只在自己的
                 * EventLoop线程处理。
                 */
                self->loop_->RunInLoop(
                    [
                        self,
                        context,
                        result =
                            std::move(result)
                    ]() mutable {
                        self->
                            HandleForwardChatAttemptResultInLoop(
                                context,
                                std::move(
                                    result
                                )
                            );
                    }
                );
            }
        );


    if (!submitted) {
        GatewayPeerTransportResult result;

        result.status =
            GatewayPeerTransportStatus::
                kNotConnected;

        result.error_message =
            "gateway peer transport "
            "rejected request";


        HandleForwardChatAttemptResultInLoop(
            context,
            std::move(result)
        );

        return;
    }


    LOG_INFO(
        "gateway peer forward attempt "
        "submitted"
        << ", remote_gateway="
        << context->
            remote_gateway.gateway_id
        << ", message_id="
        << context->message_id
        << ", retry_count="
        << context->retry_count
    );
}



void
GatewayPeerTransportManager::
HandleForwardChatAttemptResultInLoop(
    const std::shared_ptr<
        ForwardChatRetryContext
    >& context,
    GatewayPeerTransportResult result
) {
    if (
        !context ||
        context->completed
    ) {
        return;
    }


    const ForwardChatRetryReason
        retry_reason =
            ClassifyForwardChatRetry(
                result
            );


    if (
        retry_reason ==
            ForwardChatRetryReason::
                kNone ||
        context->retry_count >=
            options_.
                max_request_retries
    ) {
        CompleteForwardChatInLoop(
            context,
            std::move(result)
        );

        return;
    }


    ++context->retry_count;


    const auto delay =
        RequestRetryDelay(
            context->retry_count
        );


    const char*
        retry_reason_name =
            "unknown";


    switch (retry_reason) {
        case ForwardChatRetryReason::
            kRequestTimeout:
            retry_reason_name =
                "request_timeout";
            break;


        case ForwardChatRetryReason::
            kDisconnected:
            retry_reason_name =
                "disconnected";
            break;


        case ForwardChatRetryReason::
            kNotConnected:
            retry_reason_name =
                "not_connected";
            break;


        case ForwardChatRetryReason::
            kDuplicateInProgress:
            retry_reason_name =
                "duplicate_in_progress";
            break;


        case ForwardChatRetryReason::
            kNone:

        default:
            retry_reason_name =
                "none";
            break;
    }


    LOG_WARN(
        "gateway peer forward retry "
        "scheduled"
        << ", remote_gateway="
        << context->
            remote_gateway.gateway_id
        << ", message_id="
        << context->message_id
        << ", retry="
        << context->retry_count
        << ", max_retries="
        << options_.
            max_request_retries
        << ", reason="
        << retry_reason_name
        << ", delay_ms="
        << delay.count()
    );


    const std::weak_ptr<
        GatewayPeerTransportManager
    > weak_self =
        weak_from_this();


    const TimerId retry_timer =
        loop_->RunAfter(
            delay,
            [
                weak_self,
                context
            ]() {
                const auto self =
                    weak_self.lock();


                if (!self) {
                    return;
                }


                if (
                    !self->IsRunning() ||
                    context->completed
                ) {
                    return;
                }


                /*
                 * Retry真正开始之前，
                 * 再刷新一次Gateway实例信息。
                 *
                 * 这样等待Backoff期间如果：
                 *
                 * Gateway重启
                 * Endpoint变化
                 * Registry刷新
                 *
                 * 我们能尽量使用最新节点信息。
                 */
                if (
                    self->
                        gateway_resolver_callback_
                ) {
                    const auto refreshed =
                        self->
                            gateway_resolver_callback_(
                                context->
                                    remote_gateway.
                                    gateway_id
                            );


                    if (
                        refreshed.has_value()
                    ) {
                        context->
                            remote_gateway =
                                refreshed.value();
                    }
                }


                self->
                    SubmitForwardChatAttemptInLoop(
                        context
                    );
            }
        );


    /*
     * Timer都无法建立，
     * 就不能假装Retry已经安排成功。
     *
     * 返回本次真实结果给调用者。
     */
    if (!retry_timer.IsValid()) {
        LOG_ERROR(
            "gateway peer forward retry "
            "timer creation failed"
            << ", remote_gateway="
            << context->
                remote_gateway.gateway_id
            << ", message_id="
            << context->message_id
        );


        CompleteForwardChatInLoop(
            context,
            std::move(result)
        );
    }
}

void
GatewayPeerTransportManager::
CompleteForwardChatInLoop(
    const std::shared_ptr<
        ForwardChatRetryContext
    >& context,
    GatewayPeerTransportResult result
) {
    if (
        !context ||
        context->completed
    ) {
        return;
    }


    context->completed =
        true;


    ForwardChatCallback callback =
        std::move(
            context->callback
        );


    if (callback) {
        callback(
            std::move(result)
        );
    }
}


GatewayPeerTransportManager::
ForwardChatRetryReason
GatewayPeerTransportManager::
ClassifyForwardChatRetry(
    const GatewayPeerTransportResult&
        result
) const noexcept {
    /*
     * 第一层：
     * Transport状态。
     */
    switch (result.status) {
        case GatewayPeerTransportStatus::
            kRequestTimeout:
            return
                ForwardChatRetryReason::
                    kRequestTimeout;


        case GatewayPeerTransportStatus::
            kDisconnected:
            return
                ForwardChatRetryReason::
                    kDisconnected;


        case GatewayPeerTransportStatus::
            kNotConnected:
            return
                ForwardChatRetryReason::
                    kNotConnected;


        case GatewayPeerTransportStatus::
            kOk:
            /*
             * Transport成功，
             * 继续检查远端业务Response。
             */
            break;


        /*
         * 以下错误当前都不进行
         * Manager自动Retry。
         */
        case GatewayPeerTransportStatus::
            kInvalidArgument:

        case GatewayPeerTransportStatus::
            kNotRunning:

        case GatewayPeerTransportStatus::
            kEncodeError:

        case GatewayPeerTransportStatus::
            kProtocolError:

        case GatewayPeerTransportStatus::
            kStopped:

        default:
            return
                ForwardChatRetryReason::
                    kNone;
    }


    /*
     * 第二层：
     * Gateway B明确返回的业务状态。
     */
    switch (result.response.status) {
        case GatewayForwardChatStatus::
            kDuplicateInProgress:
            return
                ForwardChatRetryReason::
                    kDuplicateInProgress;


        /*
         * Delivered已经完成。
         */
        case GatewayForwardChatStatus::
            kDelivered:


        /*
         * 以下状态均交给GatewayServer/
         * Route层进一步决定，
         * Manager不盲目重试原节点。
         */
        case GatewayForwardChatStatus::
            kTargetNotLocal:

        case GatewayForwardChatStatus::
            kTargetNotConnected:

        case GatewayForwardChatStatus::
            kUnauthorized:

        case GatewayForwardChatStatus::
            kInvalidRequest:

        case GatewayForwardChatStatus::
            kInternalError:

        default:
            return
                ForwardChatRetryReason::
                    kNone;
    }
}


std::chrono::milliseconds
GatewayPeerTransportManager::
RequestRetryDelay(
    std::size_t retry_number
) const {
    auto delay =
        options_.
            request_retry_initial_delay;


    const auto max_delay =
        options_.
            request_retry_max_delay;


    /*
     * retry_number:
     *
     * 1 -> initial
     * 2 -> initial * 2
     * 3 -> initial * 4
     */
    for (
        std::size_t index = 1;
        index < retry_number &&
        delay < max_delay;
        ++index
    ) {
        /*
         * 防止count()*2溢出，
         * 也保证永远不超过max。
         */
        if (
            delay.count() >=
            max_delay.count() / 2
        ) {
            delay =
                max_delay;

            break;
        }


        delay *= 2;
    }


    return std::min(
        delay,
        max_delay
    );
}


std::shared_ptr<
    GatewayPeerTransport
>
GatewayPeerTransportManager::
GetOrCreateTransport(
    const GatewayInstanceRecord&
        remote_gateway
) {
    if (
        remote_gateway.gateway_id ==
        options_.local_gateway_id
    ) {
        return {};
    }


    auto iterator =
        transports_.find(
            remote_gateway.gateway_id
        );


    if (
        iterator != transports_.end()
    ) {
        PeerEntry& entry =
            iterator->second;


        const bool endpoint_changed =
            entry.host !=
                remote_gateway.listen_host ||
            entry.port !=
                remote_gateway.listen_port;


        if (!endpoint_changed) {
            if (
                entry.transport &&
                entry.transport->IsRunning()
            ) {
                return entry.transport;
            }


            /*
             * 同一Endpoint，但连接已经挂掉。
             *
             * 直接复用原Transport重新Start，
             * 不创建第二个对象。
             */
            if (
                entry.transport &&
                entry.transport->Start()
            ) {
                return entry.transport;
            }
        }


        /*
         * Endpoint变化，或者旧Transport
         * 已经无法重新启动。
         *
         * 先从Manager Map中摘掉，
         * 再Stop旧Transport。
         *
         * 这样旧Transport产生的
         * disconnected callback不会污染
         * 新entry。
         */
        auto old_transport =
            entry.transport;

        CancelReconnectTimer(
            &entry
        );

        transports_.erase(
            iterator
        );

        transport_count_.store(
            transports_.size(),
            std::memory_order_release
        );

        if (old_transport) {
            old_transport->Stop();
        }
    }


    return CreateTransport(
        remote_gateway
    );
}


std::shared_ptr<
    GatewayPeerTransport
>
GatewayPeerTransportManager::
CreateTransport(
    const GatewayInstanceRecord&
        remote_gateway
) {
    GatewayPeerTransportOptions
        transport_options;


    transport_options.local_gateway_id =
        options_.local_gateway_id;

    transport_options.local_lease_token =
        options_.local_lease_token;

    transport_options.remote_gateway_id =
        remote_gateway.gateway_id;

    transport_options.remote_host =
        remote_gateway.listen_host;

    transport_options.remote_port =
        remote_gateway.listen_port;

    transport_options.connect_timeout =
        options_.connect_timeout;

    transport_options.request_timeout =
        options_.request_timeout;

    transport_options.max_body_size =
        options_.max_body_size;


    auto transport =
        std::make_shared<
            GatewayPeerTransport
        >(
            loop_,
            std::move(
                transport_options
            )
        );


    /*
     * 必须先放进Map，再Start。
     *
     * localhost连接有可能非常快，
     * Start之后ConnectionCallback
     * 可能很快发生。
     */
    PeerEntry entry;

    entry.host =
        remote_gateway.listen_host;

    entry.port =
        remote_gateway.listen_port;

    entry.transport =
        transport;


    transports_[
        remote_gateway.gateway_id
    ] =
        std::move(entry);


    transport_count_.store(
        transports_.size(),
        std::memory_order_release
    );


    const std::weak_ptr<
        GatewayPeerTransportManager
    > weak_manager =
        weak_from_this();

    const std::weak_ptr<
        GatewayPeerTransport
    > weak_transport =
        transport;

    const std::string gateway_id =
        remote_gateway.gateway_id;

    transport->
        SetConnectionStateCallback(
            [
                weak_manager,
                weak_transport,
                gateway_id
            ](
                bool connected
            ) {
                const auto manager =
                    weak_manager.lock();

                if (!manager) {
                    return;
                }

                manager->
                    HandleTransportConnectionState(
                        gateway_id,
                        weak_transport,
                        connected
                    );
            }
        );


    if (!transport->Start()) {
        auto iterator =
            transports_.find(
                remote_gateway.gateway_id
            );

        if (
            iterator != transports_.end() &&
            iterator->second.transport ==
                transport
        ) {
            transports_.erase(
                iterator
            );

            transport_count_.store(
                transports_.size(),
                std::memory_order_release
            );
        }

        return {};
    }


    LOG_INFO(
        "gateway peer transport created"
        << ", remote_gateway="
        << remote_gateway.gateway_id
        << ", remote="
        << remote_gateway.listen_host
        << ':'
        << remote_gateway.listen_port
    );


    return transport;
}


void
GatewayPeerTransportManager::
HandleTransportConnectionState(
    const std::string& gateway_id,
    std::weak_ptr<
        GatewayPeerTransport
    > source_transport,
    bool connected
) {
    /*
     * 正常情况下Callback本来就在
     * Manager所属Peer EventLoop。
     *
     * 这里保留跨线程安全入口。
     */
    if (
        loop_ != nullptr &&
        !loop_->IsInLoopThread()
    ) {
        const auto self =
            weak_from_this().lock();

        if (!self) {
            return;
        }

        loop_->RunInLoop(
            [
                self,
                gateway_id,
                source_transport,
                connected
            ]() mutable {
                self->
                    HandleTransportConnectionState(
                        gateway_id,
                        std::move(
                            source_transport
                        ),
                        connected
                    );
            }
        );

        return;
    }


    auto iterator =
        transports_.find(
            gateway_id
        );

    if (
        iterator ==
        transports_.end()
    ) {
        return;
    }


    const auto source =
        source_transport.lock();

    if (
        !source ||
        iterator->second.transport !=
            source
    ) {
        /*
         * 旧Transport的迟到回调。
         *
         * 例如endpoint已经变化并创建了
         * 新Transport。
         */
        return;
    }


    PeerEntry& entry =
        iterator->second;


    if (connected) {
        CancelReconnectTimer(
            &entry
        );

        entry.reconnect_attempt = 0;


        LOG_INFO(
            "gateway peer connection healthy"
            << ", remote_gateway="
            << gateway_id
            << ", remote="
            << entry.host
            << ':'
            << entry.port
        );

        return;
    }


    if (!IsRunning()) {
        return;
    }


    LOG_WARN(
        "gateway peer connection lost"
        << ", remote_gateway="
        << gateway_id
        << ", remote="
        << entry.host
        << ':'
        << entry.port
    );


    ScheduleReconnectInLoop(
        gateway_id
    );
}


void
GatewayPeerTransportManager::
ScheduleReconnectInLoop(
    const std::string& gateway_id
) {
    if (!IsRunning()) {
        return;
    }


    auto iterator =
        transports_.find(
            gateway_id
        );

    if (
        iterator ==
        transports_.end()
    ) {
        return;
    }


    PeerEntry& entry =
        iterator->second;


    if (
        entry.transport &&
        entry.transport->IsConnected()
    ) {
        return;
    }


    /*
     * 已经有Timer就不能再安排第二个。
     */
    if (
        entry.reconnect_timer.IsValid()
    ) {
        return;
    }


    const std::size_t attempt =
        entry.reconnect_attempt;

    const auto delay =
        ReconnectDelay(
            attempt
        );


    const std::weak_ptr<
        GatewayPeerTransportManager
    > weak_self =
        weak_from_this();


    entry.reconnect_timer =
        loop_->RunAfter(
            delay,
            [
                weak_self,
                gateway_id
            ]() {
                const auto self =
                    weak_self.lock();

                if (self) {
                    self->
                        AttemptReconnectInLoop(
                            gateway_id
                        );
                }
            }
        );


    if (
        !entry.reconnect_timer.IsValid()
    ) {
        LOG_ERROR(
            "gateway peer reconnect timer "
            "creation failed"
            << ", remote_gateway="
            << gateway_id
        );

        return;
    }


    ++entry.reconnect_attempt;


    LOG_INFO(
        "gateway peer reconnect scheduled"
        << ", remote_gateway="
        << gateway_id
        << ", attempt="
        << entry.reconnect_attempt
        << ", delay_ms="
        << delay.count()
    );
}


void
GatewayPeerTransportManager::
AttemptReconnectInLoop(
    const std::string& gateway_id
) {
    if (!IsRunning()) {
        return;
    }


    auto iterator =
        transports_.find(
            gateway_id
        );

    if (
        iterator ==
        transports_.end()
    ) {
        return;
    }


    PeerEntry& entry =
        iterator->second;

    /*
     * 当前Timer已经触发。
     */
    entry.reconnect_timer = {};


    if (
        entry.transport &&
        entry.transport->IsConnected()
    ) {
        entry.reconnect_attempt = 0;
        return;
    }


    GatewayInstanceRecord
        resolved_gateway;

    resolved_gateway.gateway_id =
        gateway_id;

    resolved_gateway.listen_host =
        entry.host;

    resolved_gateway.listen_port =
        entry.port;


    /*
     * 如果生产环境提供Discovery Resolver，
     * 每次重连都刷新地址。
     */
    if (gateway_resolver_callback_) {
        const auto resolved =
            gateway_resolver_callback_(
                gateway_id
            );


        if (!resolved.has_value()) {
            LOG_WARN(
                "gateway peer reconnect "
                "stopped: peer not found "
                "in discovery"
                << ", remote_gateway="
                << gateway_id
            );


            auto old_transport =
                entry.transport;

            transports_.erase(
                iterator
            );

            transport_count_.store(
                transports_.size(),
                std::memory_order_release
            );

            if (old_transport) {
                old_transport->Stop();
            }

            /*
             * 不在这里无限轮询已经从
             * Registry消失的节点。
             *
             * 节点重新注册以后，
             * 下一条业务Route会携带新Record，
             * GetOrCreateTransport会重新创建。
             */
            return;
        }


        resolved_gateway =
            resolved.value();


        if (
            resolved_gateway.gateway_id !=
                gateway_id ||
            resolved_gateway.
                listen_host.empty() ||
            resolved_gateway.
                listen_port == 0
        ) {
            LOG_WARN(
                "gateway peer reconnect "
                "resolver returned "
                "invalid record"
                << ", remote_gateway="
                << gateway_id
            );

            ScheduleReconnectInLoop(
                gateway_id
            );

            return;
        }
    }


    const bool endpoint_changed =
        resolved_gateway.listen_host !=
            entry.host ||
        resolved_gateway.listen_port !=
            entry.port;


    if (endpoint_changed) {
        LOG_INFO(
            "gateway peer endpoint changed"
            << ", remote_gateway="
            << gateway_id
            << ", old="
            << entry.host
            << ':'
            << entry.port
            << ", new="
            << resolved_gateway.
                listen_host
            << ':'
            << resolved_gateway.
                listen_port
        );


        /*
         * GetOrCreateTransport内部会：
         *
         * 旧Transport摘除
         * → Stop
         * → 新Transport
         * → Start
         */
        auto transport =
            GetOrCreateTransport(
                resolved_gateway
            );

        if (!transport) {
            /*
             * 正常connect失败是异步回调，
             * 不会走到这里。
             *
             * 这里主要处理本地创建失败。
             */
            LOG_WARN(
                "gateway peer endpoint "
                "replacement failed"
                << ", remote_gateway="
                << gateway_id
            );
        }

        return;
    }


    /*
     * Endpoint没变：
     * 直接重启同一个Transport。
     */
    if (
        entry.transport &&
        entry.transport->Start()
    ) {
        LOG_INFO(
            "gateway peer reconnect attempt "
            "started"
            << ", remote_gateway="
            << gateway_id
            << ", remote="
            << entry.host
            << ':'
            << entry.port
        );

        return;
    }


    /*
     * 只有Start本身无法提交时，
     * 才主动安排下一轮。
     *
     * 正常ECONNREFUSED会通过Transport
     * connection callback回来，再Schedule。
     */
    ScheduleReconnectInLoop(
        gateway_id
    );
}


void
GatewayPeerTransportManager::
CancelReconnectTimer(
    PeerEntry* entry
) {
    if (
        entry == nullptr ||
        !entry->reconnect_timer.IsValid()
    ) {
        return;
    }


    if (loop_ != nullptr) {
        loop_->Cancel(
            entry->reconnect_timer
        );
    }


    entry->reconnect_timer = {};
}


std::chrono::milliseconds
GatewayPeerTransportManager::
ReconnectDelay(
    std::size_t attempt
) const {
    auto delay =
        options_.reconnect_initial_delay;

    const auto max_delay =
        options_.reconnect_max_delay;


    for (
        std::size_t index = 0;
        index < attempt &&
        delay < max_delay;
        ++index
    ) {
        /*
         * 防止count()*2溢出。
         */
        if (
            delay.count() >=
            max_delay.count() / 2
        ) {
            delay =
                max_delay;

            break;
        }


        delay *= 2;
    }


    return std::min(
        delay,
        max_delay
    );
}

}  // namespace tinyimx