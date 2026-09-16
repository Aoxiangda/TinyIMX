#include "gateway/GatewayServer.h"

#include "common/logging/LogMacros.h"
#include "common/net/EventLoop.h"
#include "gateway/business/BusinessExecutor.h"
#include "services/rpc/UserRpcClient.h"
#include "services/rpc/MessageRpcClient.h"
#include "services/rpc/GroupRpcClient.h"
#include "services/repository/FriendRequestRepository.h"
#include "services/rpc/SocialRpcClient.h"
#include "services/cache/OnlineStatusCache.h"
#include "services/cache/UnreadCountCache.h"
#include "gateway/GatewayRouteResolver.h"
#include "common/protocol/GatewayPeerProtocol.h"
#include "gateway/GatewayPeerTransportManager.h"
#include "common/protocol/ClientChatProtocol.h"

#include <algorithm>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include <optional>
#include <cstdint>
#include <thread>
namespace {

using Json = nlohmann::json;

bool ParseJsonBody(const tinyimx::Packet& packet,
                   Json* body,
                   std::string* error_message) {
    if (body == nullptr) {
        return false;
    }

    try {
        *body = Json::parse(packet.body);
        return true;
    } catch (const std::exception& e) {
        if (error_message != nullptr) {
            *error_message = e.what();
        }
        return false;
    }
}

bool GetUserIdField(const Json& body,
                    const char* field_name,
                    tinyimx::UserId* user_id,
                    std::string* error_message) {
    if (user_id == nullptr || field_name == nullptr) {
        return false;
    }

    if (!body.contains(field_name)) {
        if (error_message != nullptr) {
            *error_message =
                std::string("missing field: ") + field_name;
        }
        return false;
    }

    if (!body.at(field_name).is_number_unsigned() &&
        !body.at(field_name).is_number_integer()) {
        if (error_message != nullptr) {
            *error_message =
                std::string("invalid user id field: ") + field_name;
        }
        return false;
    }

    const auto value = body.at(field_name).get<std::int64_t>();
    if (value <= 0) {
        if (error_message != nullptr) {
            *error_message =
                std::string("user id must be positive: ") + field_name;
        }
        return false;
    }

    *user_id = static_cast<tinyimx::UserId>(value);
    return true;
}


bool ParseCanonicalChatBody(
    const std::string& content,
    tinyimx::UserId expected_from_user_id,
    tinyimx::UserId expected_to_user_id,
    std::string* text,
    std::string* error_message
) {
    if (text == nullptr) {
        if (error_message != nullptr) {
            *error_message =
                "chat text output is null";
        }

        return false;
    }


    Json body;


    try {
        body =
            Json::parse(
                content
            );
    } catch (const std::exception& e) {
        if (error_message != nullptr) {
            *error_message =
                std::string(
                    "invalid persisted chat json: "
                ) +
                e.what();
        }

        return false;
    }


    if (!body.is_object()) {
        if (error_message != nullptr) {
            *error_message =
                "persisted chat body "
                "must be an object";
        }

        return false;
    }


    tinyimx::UserId
        from_user_id = 0;

    tinyimx::UserId
        to_user_id = 0;


    if (
        !GetUserIdField(
            body,
            "from",
            &from_user_id,
            error_message
        )
    ) {
        return false;
    }


    if (
        !GetUserIdField(
            body,
            "to",
            &to_user_id,
            error_message
        )
    ) {
        return false;
    }


    /*
     * DB Record本身已经保存from/to。
     *
     * content里的canonical identity
     * 必须与Record一致。
     *
     * 如果不一致，说明持久数据发生了漂移/
     * 损坏，不能偷偷向错误用户投递。
     */
    if (
        from_user_id !=
            expected_from_user_id ||
        to_user_id !=
            expected_to_user_id
    ) {
        if (error_message != nullptr) {
            *error_message =
                "persisted chat identity mismatch";
        }

        return false;
    }


    if (
        !body.contains("text") ||
        !body.at("text").is_string()
    ) {
        if (error_message != nullptr) {
            *error_message =
                "missing or invalid persisted "
                "chat text";
        }

        return false;
    }


    const std::string parsed_text =
        body.at("text").
            get<std::string>();


    if (parsed_text.empty()) {
        if (error_message != nullptr) {
            *error_message =
                "persisted chat text "
                "must not be empty";
        }

        return false;
    }


    *text =
        parsed_text;


    if (error_message != nullptr) {
        error_message->clear();
    }


    return true;
}




/*
 * ================================================================
 * M13-B2 Mutation / Reliable Path Dispatch Context
 * ================================================================
 *
 * B2 deliberately keeps the already-accepted M12 business state
 * machines intact and moves their execution domain instead of
 * rewriting them.  A session-bound mutation captures the authenticated
 * identity at admission time.  kMustRun work can therefore continue
 * after disconnect/replacement without re-binding the operation to a
 * newer session.
 *
 * The context is thread_local because each BusinessExecutor worker
 * executes one task callback at a time.  It is only visible while the
 * dispatched handler is executing on that worker.
 */
struct BusinessDispatchContext {
    bool active{false};
    tinyimx::UserId user_id{0};
    tinyimx::SessionEpoch session_epoch{0};
    tinyimx::TcpConnectionPtr connection;
};

thread_local BusinessDispatchContext
    g_business_dispatch_context;

class ScopedBusinessDispatchContext final {
public:
    ScopedBusinessDispatchContext(
        const tinyimx::TcpConnectionPtr& connection,
        tinyimx::UserId user_id,
        tinyimx::SessionEpoch session_epoch
    )
        : previous_(g_business_dispatch_context) {
        g_business_dispatch_context.active = true;
        g_business_dispatch_context.user_id = user_id;
        g_business_dispatch_context.session_epoch = session_epoch;
        g_business_dispatch_context.connection = connection;
    }

    ~ScopedBusinessDispatchContext() {
        g_business_dispatch_context = previous_;
    }

    ScopedBusinessDispatchContext(
        const ScopedBusinessDispatchContext&
    ) = delete;

    ScopedBusinessDispatchContext& operator=(
        const ScopedBusinessDispatchContext&
    ) = delete;

private:
    BusinessDispatchContext previous_;
};

bool InBusinessDispatch() noexcept {
    return g_business_dispatch_context.active;
}

std::optional<tinyimx::UserId>
ResolveBusinessUser(
    tinyimx::SessionManager& session_manager,
    const tinyimx::TcpConnectionPtr& connection
) {
    if (
        g_business_dispatch_context.active &&
        g_business_dispatch_context.user_id != 0 &&
        g_business_dispatch_context.connection == connection
    ) {
        return g_business_dispatch_context.user_id;
    }

    return session_manager.FindUserByConnection(
        connection
    );
}

void UpdateBusinessDispatchSession(
    const tinyimx::TcpConnectionPtr& connection,
    tinyimx::UserId user_id,
    tinyimx::SessionEpoch session_epoch
) noexcept {
    if (
        g_business_dispatch_context.active &&
        g_business_dispatch_context.connection == connection
    ) {
        g_business_dispatch_context.user_id = user_id;
        g_business_dispatch_context.session_epoch = session_epoch;
    }
}

bool BusinessResponseSessionIsCurrent(
    tinyimx::SessionManager& session_manager,
    const tinyimx::TcpConnectionPtr& connection
) {
    if (
        !g_business_dispatch_context.active ||
        g_business_dispatch_context.user_id == 0 ||
        g_business_dispatch_context.connection != connection
    ) {
        return true;
    }

    return session_manager.IsCurrent(
        g_business_dispatch_context.user_id,
        g_business_dispatch_context.session_epoch,
        connection
    );
}

std::uint64_t MixBusinessOrderingKey(
    std::uint64_t value
) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value =
        (value ^ (value >> 30U)) *
        0xbf58476d1ce4e5b9ULL;
    value =
        (value ^ (value >> 27U)) *
        0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

tinyimx::BusinessOrderingKey
MakeUserPairOrderingKey(
    tinyimx::UserId lhs,
    tinyimx::UserId rhs
) noexcept {
    if (lhs > rhs) {
        std::swap(lhs, rhs);
    }

    const std::uint64_t first =
        MixBusinessOrderingKey(lhs);
    const std::uint64_t second =
        MixBusinessOrderingKey(rhs);

    return MixBusinessOrderingKey(
        first ^
        (second + 0x9e3779b97f4a7c15ULL +
         (first << 6U) + (first >> 2U))
    );
}

tinyimx::BusinessOrderingKey
MakeStringOrderingKey(
    const std::string& value
) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char ch : value) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ULL;
    }
    return MixBusinessOrderingKey(hash);
}

tinyimx::BusinessOrderingKey
MakeConnectionOrderingKey(
    const tinyimx::TcpConnectionPtr& connection
) noexcept {
    return MixBusinessOrderingKey(
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(
                connection.get()
            )
        )
    );
}

/*
 * Pre-authentication tasks (login auth) do not have a SessionEpoch yet.
 * Their cancellation fence is therefore the physical connection only.
 */
tinyimx::BusinessSubmitStatus
SubmitConnectionBusinessTask(
    tinyimx::BusinessExecutor* executor,
    const tinyimx::TcpConnectionPtr& connection,
    std::uint32_t request_seq,
    tinyimx::BusinessTimePoint received_at,
    std::string operation,
    std::optional<tinyimx::BusinessOrderingKey> ordering_key,
    tinyimx::BusinessExecutor::Work work
) {
    if (executor == nullptr || !connection || !work) {
        return tinyimx::BusinessSubmitStatus::kInvalidArgument;
    }

    tinyimx::EventLoop* const io_loop =
        connection->GetLoop();
    if (io_loop == nullptr) {
        return tinyimx::BusinessSubmitStatus::kInvalidArgument;
    }

    tinyimx::BusinessExecutor::TaskSpec task;
    task.request.operation = std::move(operation);
    task.request.request_seq = request_seq;
    task.request.received_at = received_at;
    task.request.ordering_key = ordering_key;
    task.cancellation_policy =
        tinyimx::BusinessCancellationPolicy::kCancelable;

    task.still_valid = [connection]() {
        return connection && connection->IsConnected();
    };
    task.completion_still_valid = task.still_valid;
    task.dispatcher = [io_loop](
        tinyimx::BusinessExecutor::Completion completion
    ) {
        io_loop->QueueInLoop(std::move(completion));
    };
    task.work = std::move(work);

    return executor->Submit(std::move(task));
}




/*
 * Connection-bound reliable work without an authenticated client
 * Session (for Gateway-to-Gateway requests).  Once admitted it must run;
 * the peer may disconnect and retry, while durable idempotency keeps the
 * operation safe.
 */
tinyimx::BusinessSubmitStatus
SubmitMustRunConnectionBusinessTask(
    tinyimx::BusinessExecutor* executor,
    const tinyimx::TcpConnectionPtr& connection,
    std::uint32_t request_seq,
    tinyimx::BusinessTimePoint received_at,
    std::string operation,
    std::optional<tinyimx::BusinessOrderingKey> ordering_key,
    tinyimx::BusinessExecutor::Work work
) {
    if (executor == nullptr || !connection || !work) {
        return tinyimx::BusinessSubmitStatus::kInvalidArgument;
    }

    tinyimx::BusinessExecutor::TaskSpec task;
    task.request.operation = std::move(operation);
    task.request.request_seq = request_seq;
    task.request.received_at = received_at;
    task.request.ordering_key = ordering_key;
    task.cancellation_policy =
        tinyimx::BusinessCancellationPolicy::kMustRun;
    task.work = std::move(work);

    return executor->Submit(std::move(task));
}


/*
 * ================================================================
 * M13-B1 Gateway -> Business Runtime Session Adapter
 * ================================================================
 *
 * 统一封装所有“已登录Session绑定型”Business Task的公共语义：
 *
 * - Session user_id / epoch
 * - Before-work cancellation fence
 * - Completion fence
 * - original EventLoop dispatcher
 * - optional ordering key
 *
 * Handler只负责声明业务策略与实际Work，避免每个Handler重复实现
 * Session lifecycle和EventLoop切换。
 */
tinyimx::BusinessSubmitStatus
SubmitSessionBusinessTask(
    tinyimx::BusinessExecutor* executor,
    tinyimx::SessionManager* session_manager,
    const tinyimx::TcpConnectionPtr& connection,
    const tinyimx::SessionSnapshot& session,
    std::uint32_t request_seq,
    tinyimx::BusinessTimePoint received_at,
    std::string operation,
    tinyimx::BusinessCancellationPolicy policy,
    std::optional<tinyimx::BusinessOrderingKey> ordering_key,
    tinyimx::BusinessExecutor::Work work
) {
    if (
        executor == nullptr ||
        session_manager == nullptr ||
        !connection ||
        !session.Valid() ||
        session.connection != connection ||
        !work
    ) {
        return
            tinyimx::BusinessSubmitStatus::
                kInvalidArgument;
    }

    tinyimx::EventLoop* const io_loop =
        connection->GetLoop();

    if (io_loop == nullptr) {
        return
            tinyimx::BusinessSubmitStatus::
                kInvalidArgument;
    }

    tinyimx::BusinessExecutor::TaskSpec task;

    task.request.operation =
        std::move(operation);

    task.request.user_id =
        session.user_id;

    task.request.request_seq =
        request_seq;

    task.request.session_epoch =
        session.epoch;

    task.request.received_at =
        received_at;

    task.request.ordering_key =
        ordering_key;

    task.cancellation_policy =
        policy;

    const tinyimx::UserId user_id =
        session.user_id;

    const tinyimx::SessionEpoch epoch =
        session.epoch;

    task.still_valid =
        [
            session_manager,
            user_id,
            epoch,
            connection
        ]() {
            return
                session_manager->IsCurrent(
                    user_id,
                    epoch,
                    connection
                );
        };

    task.completion_still_valid =
        [
            session_manager,
            user_id,
            epoch,
            connection
        ]() {
            return
                session_manager->IsCurrent(
                    user_id,
                    epoch,
                    connection
                );
        };

    task.dispatcher =
        [io_loop](
            tinyimx::BusinessExecutor::Completion completion
        ) {
            io_loop->QueueInLoop(
                std::move(completion)
            );
        };

    task.work =
        std::move(work);

    return
        executor->Submit(
            std::move(task)
        );
}


const char* UserRpcErrorReason(
    tinyimx::rpc::RpcErrorCode code
) noexcept {
    using tinyimx::rpc::RpcErrorCode;

    switch (code) {
        case RpcErrorCode::kCancelled:
            return "auth_cancelled";
        case RpcErrorCode::kDeadlineExceeded:
            return "auth_timeout";
        case RpcErrorCode::kResourceExhausted:
            return "auth_overloaded";
        case RpcErrorCode::kUnavailable:
            return "auth_unavailable";
        case RpcErrorCode::kDataLoss:
            return "auth_invalid_response";
        case RpcErrorCode::kInvalidArgument:
        case RpcErrorCode::kNotFound:
        case RpcErrorCode::kAlreadyExists:
        case RpcErrorCode::kPermissionDenied:
        case RpcErrorCode::kUnauthenticated:
        case RpcErrorCode::kFailedPrecondition:
        case RpcErrorCode::kAborted:
        case RpcErrorCode::kOutOfRange:
        case RpcErrorCode::kUnimplemented:
        case RpcErrorCode::kInternal:
        case RpcErrorCode::kUnknown:
            return "auth_internal_error";
        case RpcErrorCode::kOk:
            return "ok";
    }

    return "auth_internal_error";
}

const char* UserRpcPublicMessage(
    tinyimx::rpc::RpcErrorCode code
) noexcept {
    using tinyimx::rpc::RpcErrorCode;

    switch (code) {
        case RpcErrorCode::kCancelled:
            return "auth service request cancelled";
        case RpcErrorCode::kDeadlineExceeded:
            return "auth service timeout";
        case RpcErrorCode::kResourceExhausted:
            return "auth service overloaded";
        case RpcErrorCode::kUnavailable:
            return "auth service unavailable";
        default:
            return "auth service request failed";
    }
}


const char* UserProfileRpcErrorReason(
    tinyimx::rpc::RpcErrorCode code
) noexcept {
    using tinyimx::rpc::RpcErrorCode;

    switch (code) {
        case RpcErrorCode::kInvalidArgument:
            return "profile_invalid_request";
        case RpcErrorCode::kCancelled:
            return "profile_cancelled";
        case RpcErrorCode::kDeadlineExceeded:
            return "profile_timeout";
        case RpcErrorCode::kNotFound:
            return "user_not_found";
        case RpcErrorCode::kResourceExhausted:
            return "profile_overloaded";
        case RpcErrorCode::kUnavailable:
            return "profile_unavailable";
        case RpcErrorCode::kDataLoss:
            return "profile_invalid_response";
        case RpcErrorCode::kAlreadyExists:
        case RpcErrorCode::kPermissionDenied:
        case RpcErrorCode::kUnauthenticated:
        case RpcErrorCode::kFailedPrecondition:
        case RpcErrorCode::kAborted:
        case RpcErrorCode::kOutOfRange:
        case RpcErrorCode::kUnimplemented:
        case RpcErrorCode::kInternal:
        case RpcErrorCode::kUnknown:
            return "profile_internal_error";
        case RpcErrorCode::kOk:
            return "ok";
    }

    return "profile_internal_error";
}

const char* UserProfileRpcPublicMessage(
    tinyimx::rpc::RpcErrorCode code
) noexcept {
    using tinyimx::rpc::RpcErrorCode;

    switch (code) {
        case RpcErrorCode::kInvalidArgument:
            return "invalid user profile request";
        case RpcErrorCode::kCancelled:
            return "profile service request cancelled";
        case RpcErrorCode::kDeadlineExceeded:
            return "profile service timeout";
        case RpcErrorCode::kNotFound:
            return "user not found";
        case RpcErrorCode::kResourceExhausted:
            return "profile service overloaded";
        case RpcErrorCode::kUnavailable:
            return "profile service unavailable";
        case RpcErrorCode::kDataLoss:
            return "invalid profile service response";
        default:
            return "profile service request failed";
    }
}

const char* AuthenticateOutcomeReason(
    tinyimx::rpc::AuthenticateRpcOutcome outcome
) noexcept {
    using tinyimx::rpc::AuthenticateRpcOutcome;

    switch (outcome) {
        case AuthenticateRpcOutcome::kAuthenticated:
            return "ok";
        case AuthenticateRpcOutcome::kUserNotFound:
            return "user_not_found";
        case AuthenticateRpcOutcome::kUserDisabled:
            return "user_disabled";
        case AuthenticateRpcOutcome::kPasswordNotSet:
            return "password_not_set";
        case AuthenticateRpcOutcome::kWrongPassword:
            return "wrong_password";
    }

    return "auth_internal_error";
}

const char* MessageRpcErrorReason(
    tinyimx::rpc::RpcErrorCode code
) noexcept {
    using tinyimx::rpc::RpcErrorCode;

    switch (code) {
        case RpcErrorCode::kInvalidArgument:
            return "message_service_invalid_request";
        case RpcErrorCode::kCancelled:
            return "message_service_cancelled";
        case RpcErrorCode::kDeadlineExceeded:
            return "message_service_timeout";
        case RpcErrorCode::kNotFound:
            return "message_not_found";
        case RpcErrorCode::kResourceExhausted:
            return "message_service_overloaded";
        case RpcErrorCode::kUnavailable:
            return "message_service_unavailable";
        case RpcErrorCode::kDataLoss:
            return "message_service_invalid_response";
        case RpcErrorCode::kAlreadyExists:
        case RpcErrorCode::kPermissionDenied:
        case RpcErrorCode::kUnauthenticated:
        case RpcErrorCode::kFailedPrecondition:
        case RpcErrorCode::kAborted:
        case RpcErrorCode::kOutOfRange:
        case RpcErrorCode::kUnimplemented:
        case RpcErrorCode::kInternal:
        case RpcErrorCode::kUnknown:
            return "message_service_internal_error";
        case RpcErrorCode::kOk:
            return "ok";
    }

    return "message_service_internal_error";
}

const char* MessageRpcPublicMessage(
    tinyimx::rpc::RpcErrorCode code
) noexcept {
    using tinyimx::rpc::RpcErrorCode;

    switch (code) {
        case RpcErrorCode::kInvalidArgument:
            return "invalid message service request";
        case RpcErrorCode::kCancelled:
            return "message service request cancelled";
        case RpcErrorCode::kDeadlineExceeded:
            return "message service timeout";
        case RpcErrorCode::kResourceExhausted:
            return "message service overloaded";
        case RpcErrorCode::kUnavailable:
            return "message service unavailable";
        case RpcErrorCode::kDataLoss:
            return "invalid message service response";
        default:
            return "message service request failed";
    }
}

std::uint32_t MessageDeliveryStateToLegacyStatus(
    tinyimx::rpc::MessageDeliveryState state
) noexcept {
    using tinyimx::rpc::MessageDeliveryState;
    switch (state) {
        case MessageDeliveryState::kPending:
            return 0;
        case MessageDeliveryState::kReceiverConfirmed:
            return 1;
        case MessageDeliveryState::kRead:
            return 2;
        case MessageDeliveryState::kFailed:
            return 3;
    }
    return 3;
}

const char* SocialRpcErrorReason(
    tinyimx::rpc::RpcErrorCode code
) noexcept {
    using tinyimx::rpc::RpcErrorCode;

    switch (code) {
        case RpcErrorCode::kInvalidArgument:
            return "invalid_social_request";
        case RpcErrorCode::kCancelled:
            return "social_service_cancelled";
        case RpcErrorCode::kDeadlineExceeded:
            return "social_service_deadline_exceeded";
        case RpcErrorCode::kNotFound:
            return "social_resource_not_found";
        case RpcErrorCode::kPermissionDenied:
            return "social_service_permission_denied";
        case RpcErrorCode::kUnauthenticated:
            return "social_service_unauthenticated";
        case RpcErrorCode::kResourceExhausted:
            return "social_service_overloaded";
        case RpcErrorCode::kFailedPrecondition:
            return "social_service_failed_precondition";
        case RpcErrorCode::kUnavailable:
            // Preserve the existing public FriendList failure vocabulary.
            return "friend_service_unavailable";
        case RpcErrorCode::kDataLoss:
            return "social_service_invalid_record";
        case RpcErrorCode::kInternal:
            return "social_service_internal";
        case RpcErrorCode::kAlreadyExists:
        case RpcErrorCode::kAborted:
        case RpcErrorCode::kOutOfRange:
        case RpcErrorCode::kUnimplemented:
        case RpcErrorCode::kUnknown:
            return "social_service_error";
        case RpcErrorCode::kOk:
            return "ok";
    }

    return "social_service_error";
}




const char* GroupRpcErrorReason(
    tinyimx::rpc::RpcErrorCode code
) noexcept {
    using tinyimx::rpc::RpcErrorCode;
    switch (code) {
        case RpcErrorCode::kInvalidArgument: return "invalid_group_request";
        case RpcErrorCode::kNotFound: return "group_not_found";
        case RpcErrorCode::kAlreadyExists: return "group_member_already_exists";
        case RpcErrorCode::kPermissionDenied: return "group_permission_denied";
        case RpcErrorCode::kResourceExhausted: return "group_capacity_exceeded";
        case RpcErrorCode::kFailedPrecondition: return "group_failed_precondition";
        case RpcErrorCode::kAborted: return "group_version_conflict";
        case RpcErrorCode::kDeadlineExceeded: return "group_service_deadline_exceeded";
        case RpcErrorCode::kUnavailable: return "group_service_unavailable";
        case RpcErrorCode::kDataLoss: return "group_service_invalid_response";
        case RpcErrorCode::kCancelled: return "group_request_cancelled";
        case RpcErrorCode::kUnauthenticated: return "not_logged_in";
        default: return "group_service_error";
    }
}

const char* GroupMutationOutcomeToString(
    tinyimx::rpc::GroupRpcMutationOutcome value
) noexcept {
    using tinyimx::rpc::GroupRpcMutationOutcome;
    switch (value) {
        case GroupRpcMutationOutcome::kApplied: return "applied";
        case GroupRpcMutationOutcome::kReused: return "reused";
        case GroupRpcMutationOutcome::kIdempotencyConflict:
            return "idempotency_conflict";
    }
    return "unknown";
}

const char* GroupStatusToString(tinyimx::rpc::GroupRpcStatus value) noexcept {
    switch (value) {
        case tinyimx::rpc::GroupRpcStatus::kActive: return "active";
        case tinyimx::rpc::GroupRpcStatus::kDisbanded: return "disbanded";
    }
    return "unknown";
}

const char* GroupJoinPolicyToString(
    tinyimx::rpc::GroupRpcJoinPolicy value
) noexcept {
    switch (value) {
        case tinyimx::rpc::GroupRpcJoinPolicy::kInviteOnly: return "invite_only";
        case tinyimx::rpc::GroupRpcJoinPolicy::kOpen: return "open";
    }
    return "unknown";
}

const char* GroupRoleToString(tinyimx::rpc::GroupRpcRole value) noexcept {
    switch (value) {
        case tinyimx::rpc::GroupRpcRole::kOwner: return "owner";
        case tinyimx::rpc::GroupRpcRole::kAdmin: return "admin";
        case tinyimx::rpc::GroupRpcRole::kMember: return "member";
    }
    return "unknown";
}

const char* GroupMemberStatusToString(
    tinyimx::rpc::GroupRpcMemberStatus value
) noexcept {
    switch (value) {
        case tinyimx::rpc::GroupRpcMemberStatus::kActive: return "active";
        case tinyimx::rpc::GroupRpcMemberStatus::kLeft: return "left";
        case tinyimx::rpc::GroupRpcMemberStatus::kKicked: return "kicked";
    }
    return "unknown";
}

Json GroupToJson(const tinyimx::rpc::GroupRpcView& group) {
    return Json{
        {"group_id", group.group_id},
        {"name", group.name},
        {"description", group.description},
        {"avatar_url", group.avatar_url},
        {"owner_user_id", group.owner_user_id},
        {"status", GroupStatusToString(group.status)},
        {"join_policy", GroupJoinPolicyToString(group.join_policy)},
        {"max_members", group.max_members},
        {"version", group.version},
        {"member_version", group.member_version},
        {"created_at", group.created_at},
        {"updated_at", group.updated_at},
        {"disbanded_at", group.disbanded_at},
        {"disbanded_by_user_id", group.disbanded_by_user_id},
    };
}

Json GroupMemberToJson(const tinyimx::rpc::GroupMemberRpcView& member) {
    return Json{
        {"group_id", member.group_id},
        {"user_id", member.user_id},
        {"role", GroupRoleToString(member.role)},
        {"status", GroupMemberStatusToString(member.status)},
        {"membership_epoch", member.membership_epoch},
        {"muted_until", member.muted_until},
        {"joined_at", member.joined_at},
        {"left_at", member.left_at},
        {"updated_at", member.updated_at},
    };
}

bool JsonPositiveU64(
    const Json& body,
    const char* field,
    std::uint64_t* value
) {
    if (!value || !field || !body.contains(field)) return false;
    try {
        if (!body.at(field).is_number_unsigned() &&
            !body.at(field).is_number_integer()) return false;
        const auto candidate = body.at(field).get<std::int64_t>();
        if (candidate <= 0) return false;
        *value = static_cast<std::uint64_t>(candidate);
        return true;
    } catch (...) {
        return false;
    }
}

bool JsonOptionalU64(
    const Json& body,
    const char* field,
    std::uint64_t* value
) {
    if (!value || !field) return false;
    if (!body.contains(field)) {
        *value = 0;
        return true;
    }
    try {
        if (!body.at(field).is_number_unsigned() &&
            !body.at(field).is_number_integer()) return false;
        const auto candidate = body.at(field).get<std::int64_t>();
        if (candidate < 0) return false;
        *value = static_cast<std::uint64_t>(candidate);
        return true;
    } catch (...) {
        return false;
    }
}

bool JsonStringField(
    const Json& body,
    const char* field,
    std::string* value,
    bool allow_empty = false
) {
    if (!value || !field || !body.contains(field) ||
        !body.at(field).is_string()) return false;
    *value = body.at(field).get<std::string>();
    return allow_empty || !value->empty();
}

bool JsonU32WithDefault(
    const Json& body,
    const char* field,
    std::uint32_t default_value,
    std::uint32_t min_value,
    std::uint32_t max_value,
    std::uint32_t* value
) {
    if (!value || !field) return false;
    if (!body.contains(field)) {
        *value = default_value;
        return true;
    }
    try {
        if (!body.at(field).is_number_unsigned() &&
            !body.at(field).is_number_integer()) return false;
        const auto candidate = body.at(field).get<std::int64_t>();
        if (candidate < static_cast<std::int64_t>(min_value) ||
            candidate > static_cast<std::int64_t>(max_value)) return false;
        *value = static_cast<std::uint32_t>(candidate);
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<tinyimx::rpc::GroupRpcJoinPolicy> ParseJoinPolicy(
    const std::string& value
) {
    if (value == "open") return tinyimx::rpc::GroupRpcJoinPolicy::kOpen;
    if (value == "invite_only") return tinyimx::rpc::GroupRpcJoinPolicy::kInviteOnly;
    return std::nullopt;
}

std::optional<tinyimx::rpc::GroupRpcRole> ParseMutableRole(
    const std::string& value
) {
    if (value == "admin") return tinyimx::rpc::GroupRpcRole::kAdmin;
    if (value == "member") return tinyimx::rpc::GroupRpcRole::kMember;
    return std::nullopt;
}

std::optional<tinyimx::MessageType> GroupResponseType(
    tinyimx::MessageType type
) {
    using tinyimx::MessageType;
    switch (type) {
        case MessageType::kCreateGroupRequest: return MessageType::kCreateGroupResponse;
        case MessageType::kGetGroupRequest: return MessageType::kGetGroupResponse;
        case MessageType::kUpdateGroupRequest: return MessageType::kUpdateGroupResponse;
        case MessageType::kDisbandGroupRequest: return MessageType::kDisbandGroupResponse;
        case MessageType::kJoinGroupRequest: return MessageType::kJoinGroupResponse;
        case MessageType::kLeaveGroupRequest: return MessageType::kLeaveGroupResponse;
        case MessageType::kInviteGroupMemberRequest: return MessageType::kInviteGroupMemberResponse;
        case MessageType::kKickGroupMemberRequest: return MessageType::kKickGroupMemberResponse;
        case MessageType::kSetGroupMemberRoleRequest: return MessageType::kSetGroupMemberRoleResponse;
        case MessageType::kSetGroupMemberMuteRequest: return MessageType::kSetGroupMemberMuteResponse;
        case MessageType::kTransferGroupOwnershipRequest: return MessageType::kTransferGroupOwnershipResponse;
        case MessageType::kListGroupMembersRequest: return MessageType::kListGroupMembersResponse;
        case MessageType::kListMyGroupsRequest: return MessageType::kListMyGroupsResponse;
        default: return std::nullopt;
    }
}

const char* GroupOperationName(tinyimx::MessageType type) noexcept {
    using tinyimx::MessageType;
    switch (type) {
        case MessageType::kCreateGroupRequest: return "create_group";
        case MessageType::kGetGroupRequest: return "get_group";
        case MessageType::kUpdateGroupRequest: return "update_group";
        case MessageType::kDisbandGroupRequest: return "disband_group";
        case MessageType::kJoinGroupRequest: return "join_group";
        case MessageType::kLeaveGroupRequest: return "leave_group";
        case MessageType::kInviteGroupMemberRequest: return "invite_group_member";
        case MessageType::kKickGroupMemberRequest: return "kick_group_member";
        case MessageType::kSetGroupMemberRoleRequest: return "set_group_member_role";
        case MessageType::kSetGroupMemberMuteRequest: return "set_group_member_mute";
        case MessageType::kTransferGroupOwnershipRequest: return "transfer_group_ownership";
        case MessageType::kListGroupMembersRequest: return "list_group_members";
        case MessageType::kListMyGroupsRequest: return "list_my_groups";
        default: return "unknown_group_operation";
    }
}

std::optional<tinyimx::BusinessOrderingKey> GroupOrderingKey(
    tinyimx::MessageType type,
    const Json& body
) {
    using tinyimx::MessageType;
    if (type == MessageType::kCreateGroupRequest ||
        type == MessageType::kGetGroupRequest ||
        type == MessageType::kListGroupMembersRequest ||
        type == MessageType::kListMyGroupsRequest) {
        return std::nullopt;
    }
    std::uint64_t group_id = 0;
    if (!JsonPositiveU64(body, "group_id", &group_id)) return std::nullopt;
    constexpr std::uint64_t kGroupDomainSalt = 0x9E3779B97F4A7C15ULL;
    const std::uint64_t mixed = group_id ^ (kGroupDomainSalt + (group_id << 6U) + (group_id >> 2U));
    return static_cast<tinyimx::BusinessOrderingKey>(mixed);
}

Json GroupRpcFailure(const tinyimx::rpc::RpcStatus& status) {
    return Json{
        {"success", false},
        {"reason", GroupRpcErrorReason(status.code)},
        {"message", status.message.empty() ? "group service request failed" : status.message},
    };
}

Json InvalidGroupRequest(const std::string& message) {
    return Json{{"success", false}, {"reason", "invalid_group_request"}, {"message", message}};
}

Json MutationToJson(const tinyimx::rpc::RpcResult<tinyimx::rpc::GroupMutationRpcResponse>& result) {
    if (!result.ok()) return GroupRpcFailure(result.status);
    if (result.value->outcome == tinyimx::rpc::GroupRpcMutationOutcome::kIdempotencyConflict) {
        return Json{{"success", false}, {"reason", "idempotency_conflict"}, {"message", result.value->message}};
    }
    return Json{
        {"success", true},
        {"result", GroupMutationOutcomeToString(result.value->outcome)},
        {"message", result.value->message},
        {"group", GroupToJson(result.value->group)},
    };
}

Json ExecuteGroupRpc(
    tinyimx::rpc::GroupRpcClient* client,
    tinyimx::MessageType type,
    tinyimx::UserId actor_user_id,
    const Json& body,
    const tinyimx::rpc::RpcCallOptions& options
) {
    using namespace tinyimx::rpc;
    if (!client || actor_user_id == 0 || !body.is_object()) {
        return InvalidGroupRequest("invalid group request context");
    }

    std::uint64_t group_id = 0;
    std::uint64_t target_user_id = 0;
    std::uint64_t expected_version = 0;
    std::uint64_t cursor = 0;
    std::string operation_id;

    switch (type) {
        case tinyimx::MessageType::kCreateGroupRequest: {
            CreateGroupRpcRequest request;
            request.actor_user_id = actor_user_id;
            if (!JsonStringField(body, "client_operation_id", &request.client_operation_id) ||
                !JsonStringField(body, "name", &request.name)) {
                return InvalidGroupRequest("CreateGroup requires client_operation_id and name");
            }
            if (body.contains("description") && !JsonStringField(body, "description", &request.description, true)) return InvalidGroupRequest("invalid description");
            if (body.contains("avatar_url") && !JsonStringField(body, "avatar_url", &request.avatar_url, true)) return InvalidGroupRequest("invalid avatar_url");
            std::string join_policy = "invite_only";
            if (body.contains("join_policy") && !JsonStringField(body, "join_policy", &join_policy)) return InvalidGroupRequest("invalid join_policy");
            const auto policy = ParseJoinPolicy(join_policy);
            if (!policy) return InvalidGroupRequest("join_policy must be invite_only or open");
            request.join_policy = *policy;
            if (!JsonU32WithDefault(body, "max_members", 500U, 2U, 500U, &request.max_members))
                return InvalidGroupRequest("max_members must be in [2,500]");
            return MutationToJson(client->CreateGroup(request, options));
        }
        case tinyimx::MessageType::kGetGroupRequest: {
            if (!JsonPositiveU64(body, "group_id", &group_id)) return InvalidGroupRequest("invalid group_id");
            auto result = client->GetGroup({actor_user_id, group_id}, options);
            if (!result.ok()) return GroupRpcFailure(result.status);
            return Json{{"success", true}, {"group", GroupToJson(result.value->group)}};
        }
        case tinyimx::MessageType::kUpdateGroupRequest: {
            UpdateGroupRpcRequest request; request.actor_user_id = actor_user_id;
            if (!JsonStringField(body,"client_operation_id",&request.client_operation_id) ||
                !JsonPositiveU64(body,"group_id",&request.group_id) ||
                !JsonPositiveU64(body,"expected_version",&request.expected_version)) return InvalidGroupRequest("invalid UpdateGroup identity/version");
            if (body.contains("name")) { std::string v; if(!JsonStringField(body,"name",&v)) return InvalidGroupRequest("invalid name"); request.name=v; }
            if (body.contains("description")) { std::string v; if(!JsonStringField(body,"description",&v,true)) return InvalidGroupRequest("invalid description"); request.description=v; }
            if (body.contains("avatar_url")) { std::string v; if(!JsonStringField(body,"avatar_url",&v,true)) return InvalidGroupRequest("invalid avatar_url"); request.avatar_url=v; }
            if (body.contains("join_policy")) { std::string v; if(!JsonStringField(body,"join_policy",&v)) return InvalidGroupRequest("invalid join_policy"); const auto policy=ParseJoinPolicy(v); if(!policy) return InvalidGroupRequest("invalid join_policy"); request.join_policy=*policy; }
            return MutationToJson(client->UpdateGroup(request, options));
        }
        case tinyimx::MessageType::kDisbandGroupRequest: {
            if(!JsonStringField(body,"client_operation_id",&operation_id)||!JsonPositiveU64(body,"group_id",&group_id)||!JsonPositiveU64(body,"expected_version",&expected_version)) return InvalidGroupRequest("invalid DisbandGroup request");
            return MutationToJson(client->DisbandGroup({actor_user_id,operation_id,group_id,expected_version},options));
        }
        case tinyimx::MessageType::kJoinGroupRequest: {
            if(!JsonStringField(body,"client_operation_id",&operation_id)||!JsonPositiveU64(body,"group_id",&group_id)) return InvalidGroupRequest("invalid JoinGroup request");
            return MutationToJson(client->JoinGroup({actor_user_id,operation_id,group_id},options));
        }
        case tinyimx::MessageType::kLeaveGroupRequest: {
            if(!JsonStringField(body,"client_operation_id",&operation_id)||!JsonPositiveU64(body,"group_id",&group_id)) return InvalidGroupRequest("invalid LeaveGroup request");
            return MutationToJson(client->LeaveGroup({actor_user_id,operation_id,group_id},options));
        }
        case tinyimx::MessageType::kInviteGroupMemberRequest: {
            if(!JsonStringField(body,"client_operation_id",&operation_id)||!JsonPositiveU64(body,"group_id",&group_id)||!JsonPositiveU64(body,"target_user_id",&target_user_id)) return InvalidGroupRequest("invalid InviteMember request");
            return MutationToJson(client->InviteMember({actor_user_id,operation_id,group_id,target_user_id},options));
        }
        case tinyimx::MessageType::kKickGroupMemberRequest: {
            if(!JsonStringField(body,"client_operation_id",&operation_id)||!JsonPositiveU64(body,"group_id",&group_id)||!JsonPositiveU64(body,"target_user_id",&target_user_id)) return InvalidGroupRequest("invalid KickMember request");
            return MutationToJson(client->KickMember({actor_user_id,operation_id,group_id,target_user_id},options));
        }
        case tinyimx::MessageType::kSetGroupMemberRoleRequest: {
            std::string role_text;
            if(!JsonStringField(body,"client_operation_id",&operation_id)||!JsonPositiveU64(body,"group_id",&group_id)||!JsonPositiveU64(body,"target_user_id",&target_user_id)||!JsonStringField(body,"role",&role_text)) return InvalidGroupRequest("invalid SetMemberRole request");
            const auto role=ParseMutableRole(role_text); if(!role) return InvalidGroupRequest("role must be admin or member");
            return MutationToJson(client->SetMemberRole({actor_user_id,operation_id,group_id,target_user_id,*role},options));
        }
        case tinyimx::MessageType::kSetGroupMemberMuteRequest: {
            std::string muted_until;
            if(!JsonStringField(body,"client_operation_id",&operation_id)||!JsonPositiveU64(body,"group_id",&group_id)||!JsonPositiveU64(body,"target_user_id",&target_user_id)||!JsonStringField(body,"muted_until",&muted_until,true)) return InvalidGroupRequest("invalid SetMemberMute request");
            return MutationToJson(client->SetMemberMute({actor_user_id,operation_id,group_id,target_user_id,muted_until},options));
        }
        case tinyimx::MessageType::kTransferGroupOwnershipRequest: {
            if(!JsonStringField(body,"client_operation_id",&operation_id)||!JsonPositiveU64(body,"group_id",&group_id)||!JsonPositiveU64(body,"target_user_id",&target_user_id)) return InvalidGroupRequest("invalid TransferOwnership request");
            return MutationToJson(client->TransferOwnership({actor_user_id,operation_id,group_id,target_user_id},options));
        }
        case tinyimx::MessageType::kListGroupMembersRequest: {
            if(!JsonPositiveU64(body,"group_id",&group_id)||!JsonOptionalU64(body,"after_user_id",&cursor)) return InvalidGroupRequest("invalid ListGroupMembers request");
            std::uint32_t limit = 50; if (!JsonU32WithDefault(body,"limit",50U,1U,100U,&limit)) return InvalidGroupRequest("limit must be in [1,100]"); auto result=client->ListGroupMembers({actor_user_id,group_id,cursor,limit},options); if(!result.ok()) return GroupRpcFailure(result.status);
            Json members=Json::array(); for(const auto& member:result.value->members) members.push_back(GroupMemberToJson(member));
            return Json{{"success",true},{"members",std::move(members)},{"has_more",result.value->has_more}};
        }
        case tinyimx::MessageType::kListMyGroupsRequest: {
            if(!JsonOptionalU64(body,"after_group_id",&cursor)) return InvalidGroupRequest("invalid ListMyGroups request");
            std::uint32_t limit = 50; if (!JsonU32WithDefault(body,"limit",50U,1U,100U,&limit)) return InvalidGroupRequest("limit must be in [1,100]"); auto result=client->ListMyGroups({actor_user_id,cursor,limit},options); if(!result.ok()) return GroupRpcFailure(result.status);
            Json groups=Json::array(); for(const auto& group:result.value->groups) groups.push_back(GroupToJson(group));
            return Json{{"success",true},{"groups",std::move(groups)},{"has_more",result.value->has_more}};
        }
        default:
            return InvalidGroupRequest("unsupported group request type");
    }
}

}  // namespace

namespace tinyimx {

/*
    GatewayServer::GatewayServer(EventLoop* loop,
                                const InetAddress& listen_address,
                                GatewayServerOptions options)
        : loop_(loop),
        options_(std::move(options)),
        codec_(options_.max_body_size),
        server_(loop_, listen_address, options_.name) {
*/
GatewayServer::GatewayServer(
    EventLoop* loop,
    const InetAddress& listen_address,
    GatewayServerOptions options
)
    : loop_(loop),
      options_(std::move(options)),
      codec_(options_.max_body_size),
      server_(
          loop_,
          listen_address,
          options_.name,
          options_.io_thread_count
      ),
      offline_message_store_(
          options_.
              max_offline_messages_per_user
      ) {
    server_.SetConnectionCallback([this](
        const TcpConnectionPtr& connection
    ) {
        HandleConnection(connection);
    });

    server_.SetMessageCallback([this](
        const TcpConnectionPtr& connection,
        Buffer* buffer
    ) {
        HandleMessage(connection, buffer);
    });
}


GatewayServer::~GatewayServer() {
    Stop();
}

bool GatewayServer::Start() {
    if (options_.gateway_id.empty()) {
        LOG_ERROR(
            "gateway server start failed"
            << ", reason=empty_gateway_id"
            << ", name=" << options_.name
        );

        return false;
    }

    LOG_INFO(
        "gateway server starting"
        << ", name=" << options_.name
        << ", gateway_id="
        << options_.gateway_id
        << ", listen="
        << server_.ListenAddress().ToString()
        << ", max_body_size="
        << options_.max_body_size
        << ", io_thread_count="
        << options_.io_thread_count
    );

    const bool ok = server_.Start();

    if (!ok) {
        LOG_ERROR(
            "gateway server start failed"
            << ", name=" << options_.name
            << ", gateway_id="
            << options_.gateway_id
        );

        return false;
    }

    LOG_INFO(
        "gateway server started"
        << ", name=" << options_.name
        << ", gateway_id="
        << options_.gateway_id
        << ", listen="
        << server_.ListenAddress().ToString()
        << ", io_thread_count="
        << options_.io_thread_count
    );

    return true;
}

void GatewayServer::Stop() {
    server_.Stop();

    LOG_INFO("gateway server stopped"
             << ", name=" << options_.name);
}

void GatewayServer::SetPacketHandler(PacketHandler handler) {
    packet_handler_ = std::move(handler);
}

void GatewayServer::SetOnlineStatusCache(
    OnlineStatusCache* online_status_cache
) {
    online_status_cache_ = online_status_cache;

    LOG_INFO("gateway online status cache attached"
             << ", enabled=" << (online_status_cache_ != nullptr));
}

void GatewayServer::SetGatewayRouteResolver(
    GatewayRouteResolver*
        gateway_route_resolver
) {
    gateway_route_resolver_ =
        gateway_route_resolver;

    LOG_INFO(
        "gateway route resolver attached"
        << ", enabled="
        << (
            gateway_route_resolver_ !=
            nullptr
        )
    );
}



void GatewayServer::
SetGatewayPeerTransportManager(
    GatewayPeerTransportManager*
        manager
) {
    gateway_peer_transport_manager_ =
        manager;

    LOG_INFO(
        "gateway peer transport manager "
        "attached"
        << ", enabled="
        << (
            gateway_peer_transport_manager_
            != nullptr
        )
    );
}


bool GatewayServer::HasGatewayPeerTransportManager()
    const {
    return
        gateway_peer_transport_manager_
        != nullptr;
}


void GatewayServer::SetGatewayPeerVerifyCallback(
    GatewayPeerVerifyCallback callback) {
    gateway_peer_verify_callback_ =std::move(callback);

    LOG_INFO(
        "gateway peer verifier attached"
        << ", enabled="
        << static_cast<bool>(
            gateway_peer_verify_callback_
        )
    );
}



void GatewayServer::SetGatewayPeerResponseDropCallbackForTest(
    GatewayPeerResponseDropCallback callback
) {
    gateway_peer_response_drop_callback_for_test_ =
        std::move(callback);


    LOG_WARN(
        "gateway peer response fault injection "
        "callback attached"
        << ", enabled="
        << static_cast<bool>(
            gateway_peer_response_drop_callback_for_test_
        )
    );
}



bool GatewayServer::HasOnlineStatusCache() const {
    return online_status_cache_ != nullptr;
}

bool GatewayServer::HasGatewayRouteResolver() const {
    return
        gateway_route_resolver_ !=
        nullptr;
}

void GatewayServer::SetUnreadCountCache(
    UnreadCountCache* unread_count_cache
) {
    unread_count_cache_ = unread_count_cache;

    LOG_INFO("gateway unread count cache attached"
             << ", enabled=" << (unread_count_cache_ != nullptr));
}

bool GatewayServer::HasUnreadCountCache() const {
    return unread_count_cache_ != nullptr;
}

void GatewayServer::SetUnreadProjectionWriteEnabled(bool enabled) {
    unread_projection_write_enabled_ = enabled;
    LOG_INFO(
        "gateway unread projection writer ownership updated"
        << ", gateway_writes=" << unread_projection_write_enabled_
    );
}

std::int64_t GatewayServer::GetTotalUnread(
    UserId user_id
) {
    if (!HasUnreadCountCache()) {
        return 0;
    }

    const GetUnreadCountResult result =
        unread_count_cache_->
            GetTotalUnread(
                user_id
            );

    if (!result.Completed()) {
        LOG_WARN(
            "gateway get total unread failed"
            << ", user_id="
            << user_id
            << ", status="
            << GetUnreadCountStatusToString(
                result.status
            )
            << ", error="
            << result.error_message
        );

        return 0;
    }

    return result.count;
}

std::int64_t GatewayServer::GetPrivateUnread(
    UserId receiver_user_id,
    UserId sender_user_id
) {
    if (!HasUnreadCountCache()) {
        return 0;
    }

    const GetUnreadCountResult result =
        unread_count_cache_->
            GetPrivateUnread(
                receiver_user_id,
                sender_user_id
            );

    if (!result.Completed()) {
        LOG_WARN(
            "gateway get private unread "
            "failed"
            << ", receiver="
            << receiver_user_id
            << ", sender="
            << sender_user_id
            << ", status="
            << GetUnreadCountStatusToString(
                result.status
            )
            << ", error="
            << result.error_message
        );

        return 0;
    }

    return result.count;
}
std::int64_t GatewayServer::IncrementUnread(
    UserId receiver_user_id,
    UserId sender_user_id,
    std::int64_t* total_unread
) {
    if (total_unread != nullptr) {
        *total_unread = 0;
    }

    if (!HasUnreadCountCache()) {
        return 0;
    }

    if (!unread_projection_write_enabled_) {
        if (total_unread != nullptr) {
            *total_unread = GetTotalUnread(receiver_user_id);
        }
        return GetPrivateUnread(receiver_user_id, sender_user_id);
    }

    const IncrementUnreadResult
        increment_result =
            unread_count_cache_->
                IncrementPrivateUnread(
                    receiver_user_id,
                    sender_user_id
                );

    if (!increment_result.Succeeded()) {
        LOG_WARN(
            "gateway increment unread failed"
            << ", receiver="
            << receiver_user_id
            << ", sender="
            << sender_user_id
            << ", status="
            << IncrementUnreadStatusToString(
                increment_result.status
            )
            << ", error="
            << increment_result.error_message
        );

        return 0;
    }

    const GetUnreadCountResult
        total_result =
            unread_count_cache_->
                GetTotalUnread(
                    receiver_user_id
                );

    if (total_result.Completed()) {
        if (total_unread != nullptr) {
            *total_unread =
                total_result.count;
        }
    } else {
        LOG_WARN(
            "gateway get total unread "
            "after increment failed"
            << ", receiver="
            << receiver_user_id
            << ", status="
            << GetUnreadCountStatusToString(
                total_result.status
            )
            << ", error="
            << total_result.error_message
        );
    }

    return increment_result.private_count;
}

void GatewayServer::EnsureUnreadProjection(
    std::uint64_t message_id,
    UserId receiver_user_id,
    UserId sender_user_id,
    bool should_count_as_unread,
    std::int64_t* private_unread,
    std::int64_t* total_unread
) {
    if (private_unread != nullptr) {
        *private_unread = 0;
    }
    if (total_unread != nullptr) {
        *total_unread = 0;
    }

    if (!HasUnreadCountCache()) {
        return;
    }

    if (!unread_projection_write_enabled_) {
        if (private_unread != nullptr) {
            *private_unread = GetPrivateUnread(receiver_user_id, sender_user_id);
        }
        if (total_unread != nullptr) {
            *total_unread = GetTotalUnread(receiver_user_id);
        }
        return;
    }

    const EnsureUnreadProjectionResult projection_result =
        unread_count_cache_->EnsurePrivateUnreadProjection(
            message_id,
            receiver_user_id,
            sender_user_id,
            should_count_as_unread
        );

    if (!projection_result.Succeeded()) {
        LOG_WARN(
            "gateway ensure unread projection failed"
            << ", message_id=" << message_id
            << ", receiver=" << receiver_user_id
            << ", sender=" << sender_user_id
            << ", should_count=" << should_count_as_unread
            << ", status="
            << EnsureUnreadProjectionStatusToString(projection_result.status)
            << ", error=" << projection_result.error_message
        );
    } else {
        LOG_INFO(
            "gateway unread projection reconciled"
            << ", message_id=" << message_id
            << ", receiver=" << receiver_user_id
            << ", sender=" << sender_user_id
            << ", applied=" << projection_result.Applied()
            << ", incremented=" << projection_result.incremented
            << ", should_count=" << should_count_as_unread
        );
    }

    if (private_unread != nullptr) {
        *private_unread = GetPrivateUnread(
            receiver_user_id,
            sender_user_id
        );
    }
    if (total_unread != nullptr) {
        *total_unread = GetTotalUnread(receiver_user_id);
    }
}

void GatewayServer::SetUserRpcClient(
    rpc::UserRpcClient* user_rpc_client
) {
    user_rpc_client_ = user_rpc_client;

    LOG_INFO(
        "gateway user rpc client attached"
        << ", enabled="
        << (user_rpc_client_ != nullptr)
    );
}

bool GatewayServer::HasUserRpcClient() const {
    return user_rpc_client_ != nullptr;
}

void GatewayServer::SetMessageRpcClient(
    rpc::MessageRpcClient* message_rpc_client
) {
    message_rpc_client_ = message_rpc_client;

    LOG_INFO(
        "gateway message rpc client attached"
        << ", enabled="
        << (message_rpc_client_ != nullptr)
    );
}

bool GatewayServer::HasMessageRpcClient() const {
    return message_rpc_client_ != nullptr;
}

void GatewayServer::SetGroupRpcClient(
    rpc::GroupRpcClient* group_rpc_client
) {
    group_rpc_client_ = group_rpc_client;
    LOG_INFO(
        "gateway group rpc client attached"
        << ", enabled=" << (group_rpc_client_ != nullptr)
    );
}

bool GatewayServer::HasGroupRpcClient() const {
    return group_rpc_client_ != nullptr;
}


void GatewayServer::SetBusinessExecutor(
    BusinessExecutor* business_executor
) {
    business_executor_ =
        business_executor;

    LOG_INFO(
        "gateway business executor attached"
        << ", enabled="
        << (
            business_executor_ !=
            nullptr
        )
    );
}


void GatewayServer::SetSocialRpcClient(
    rpc::SocialRpcClient* social_rpc_client
) {
    social_rpc_client_ = social_rpc_client;

    LOG_INFO(
        "gateway social rpc client attached"
        << ", enabled="
        << (social_rpc_client_ != nullptr)
    );
}


bool GatewayServer::HasSocialRpcClient() const {
    return social_rpc_client_ != nullptr;
}


bool GatewayServer::HasBusinessExecutor() const {
    return business_executor_ != nullptr;
}


void GatewayServer::SetFriendRepository(
    FriendRepository* repository
) {
    friend_repository_ = repository;

    LOG_INFO("gateway friend repository attached"
             << ", enabled=" << (friend_repository_ != nullptr));
}

bool GatewayServer::HasFriendRepository() const {
    return friend_repository_ != nullptr;
}

void GatewayServer::SetFriendRequestRepository(
    FriendRequestRepository* repository
) {
    friend_request_repository_ = repository;

    LOG_INFO("gateway friend request repository attached"
             << ", enabled="
             << (friend_request_repository_ != nullptr));
}

bool GatewayServer::HasFriendRequestRepository() const {
    return friend_request_repository_ != nullptr;
}

bool GatewayServer::SendPacket(const TcpConnectionPtr& connection,
                               const Packet& packet) {
    /*
     * M13-B2 completion/session fence for responses emitted from a
     * Business worker.  It only applies when the send target is the
     * original request connection captured by the dispatch context.
     * Receiver-delivery connections are different and are not filtered.
     */
    if (!BusinessResponseSessionIsCurrent(
            session_manager_,
            connection)) {
        LOG_INFO(
            "gateway dropped stale business response"
            << ", user_id="
            << g_business_dispatch_context.user_id
            << ", session_epoch="
            << g_business_dispatch_context.session_epoch
            << ", packet_type="
            << MessageTypeToString(packet.type)
            << ", packet_seq="
            << packet.seq
        );
        return false;
    }

    if (!connection || !connection->IsConnected()) {
        LOG_WARN("gateway send packet ignored: invalid connection");
        return false;
    }

    Buffer output;
    std::string error_message;

    if (!codec_.Encode(packet, &output, &error_message)) {
        LOG_ERROR("gateway encode packet failed"
                  << ", type=" << MessageTypeToString(packet.type)
                  << ", seq=" << packet.seq
                  << ", error=" << error_message);
        return false;
    }

    const std::string bytes = output.RetrieveAllAsString();
    connection->Send(bytes);

    return true;
}


std::uint32_t
GatewayServer::NextReceiverDeliverySeq()
    noexcept {
    /*
     * fetch_add使用relaxed即可。
     *
     * 我们这里只需要：
     *
     * 每个调用者拿到不同的数值，
     *
     * 不依赖这个atomic建立其他内存对象之间的
     * happens-before关系。
     */
    for (;;) {
        const std::uint32_t seq =
            next_receiver_delivery_seq_.
                fetch_add(
                    1,
                    std::memory_order_relaxed
                );


        /*
         * Packet.seq == 0
         * 统一视为无效/未分配身份。
         *
         * uint32_t最终发生wrap时：
         *
         * UINT32_MAX
         *      ↓
         *      0
         *      ↓
         *      1
         *
         * 这里直接跳过0。
         */
        if (seq != 0) {
            return seq;
        }
    }
}


bool GatewayServer::
BuildReceiverChatDeliveryPacket(
    std::uint64_t message_id,
    UserId from_user_id,
    UserId to_user_id,
    const std::string& text,
    Packet* packet,
    std::string* error_message
) {
    if (packet == nullptr) {
        if (error_message != nullptr) {
            *error_message =
                "receiver delivery packet "
                "output is null";
        }

        return false;
    }


    /*
     * 业务字段的合法性由已经完成的
     * ServerChatDelivery协议层统一负责。
     *
     * Gateway这里不要复制第二套校验规则。
     */
    ServerChatDelivery delivery;

    delivery.message_id =
        message_id;

    delivery.from_user_id =
        from_user_id;

    delivery.to_user_id =
        to_user_id;

    delivery.text =
        text;


    std::string body;
    std::string serialize_error;


    if (
        !SerializeServerChatDelivery(
            delivery,
            &body,
            &serialize_error
        )
    ) {
        if (error_message != nullptr) {
            *error_message =
                std::move(
                    serialize_error
                );
        }

        return false;
    }


    /*
     * 先构造临时Packet。
     *
     * 只有所有步骤成功以后，
     * 才覆盖调用方提供的output。
     */
    Packet built_packet;

    built_packet.type =
        MessageType::kChatDelivery;

    built_packet.seq =
        NextReceiverDeliverySeq();

    built_packet.body =
        std::move(body);


    *packet =
        std::move(
            built_packet
        );


    if (error_message != nullptr) {
        error_message->clear();
    }


    return true;
}


GatewayServer::
ReceiverDeliverySubmitStatus
GatewayServer::
SubmitReceiverChatDelivery(
    const TcpConnectionPtr& connection,
    std::uint64_t message_id,
    UserId from_user_id,
    UserId to_user_id,
    const std::string& text,
    Packet* submitted_packet,
    std::string* error_message
) {
    auto set_error =
        [error_message](
            const std::string& message
        ) {
            if (error_message != nullptr) {
                *error_message =
                    message;
            }
        };


    if (
        !connection ||
        !connection->IsConnected()
    ) {
        set_error(
            "receiver connection unavailable"
        );

        return
            ReceiverDeliverySubmitStatus::
                kConnectionUnavailable;
    }


    /*
     * ============================================================
     * 1. 先构造kChatDelivery
     * ============================================================
     *
     * 此时还没有任何网络副作用，
     * 也还没有登记Tracker。
     */
    Packet delivery_packet;

    std::string build_error;


    if (
        !BuildReceiverChatDeliveryPacket(
            message_id,
            from_user_id,
            to_user_id,
            text,
            &delivery_packet,
            &build_error
        )
    ) {
        set_error(
            build_error
        );

        return
            ReceiverDeliverySubmitStatus::
                kBuildFailed;
    }


    /*
     * ============================================================
     * 2. 在RegisterAttempt以前完成Protocol Encode
     * ============================================================
     *
     * 防止：
     *
     * Tracker认为D真实存在
     * 但实际上Packet连编码都失败。
     */
    Buffer output;

    std::string encode_error;


    if (
        !codec_.Encode(
            delivery_packet,
            &output,
            &encode_error
        )
    ) {
        set_error(
            encode_error
        );

        return
            ReceiverDeliverySubmitStatus::
                kEncodeFailed;
    }


    const std::string bytes =
        output.RetrieveAllAsString();


    /*
     * Encode可能花费了一点时间。
     *
     * 在真正登记Attempt以前再确认一次
     * connection当前仍然Connected。
     */
    if (!connection->IsConnected()) {
        set_error(
            "receiver connection became "
            "unavailable before attempt registration"
        );

        return
            ReceiverDeliverySubmitStatus::
                kConnectionUnavailable;
    }


    /*
     * ============================================================
     * 3. Register必须早于真正Send
     * ============================================================
     *
     * 否则Receiver极快返回ACK时，
     * Gateway可能先处理ACK、
     * 后登记Attempt。
     */
    const
        ReceiverDeliveryRegisterStatus
        register_status =
            receiver_delivery_tracker_.
                RegisterAttempt(
                    message_id,
                    to_user_id,
                    delivery_packet.seq
                );


    if (
        register_status ==
        ReceiverDeliveryRegisterStatus::
            kAlreadyConfirmed
    ) {
        set_error(
            "receiver message already confirmed"
        );

        return
            ReceiverDeliverySubmitStatus::
                kAlreadyConfirmed;
    }


    if (
        register_status !=
            ReceiverDeliveryRegisterStatus::
                kRegistered &&
        register_status !=
            ReceiverDeliveryRegisterStatus::
                kRetryRegistered
    ) {
        if (
            register_status ==
            ReceiverDeliveryRegisterStatus::
                kDuplicateAttempt
        ) {
            set_error(
                "duplicate receiver delivery seq"
            );
        } else if (
            register_status ==
            ReceiverDeliveryRegisterStatus::
                kReceiverMismatch
        ) {
            set_error(
                "receiver delivery identity mismatch"
            );
        } else {
            set_error(
                "receiver delivery tracker "
                "rejected attempt"
            );
        }


        return
            ReceiverDeliverySubmitStatus::
                kTrackerRejected;
    }


    /*
     * ============================================================
     * 4. Attempt登记完成以后才允许提交网络发送
     * ============================================================
     *
     * 注意：
     *
     * TcpConnection::Send是void。
     *
     * 如果在提交以后连接断开，
     * Gateway无法证明字节到底有没有到达Receiver。
     *
     * 所以这个Attempt不能回滚，
     * 而应该进入WaitingAck，
     * 由后续Timeout/Retry处理。
     */
    connection->Send(
        bytes
    );

    /*
    * Send已经提交以后开始ACK计时。
    *
    * 如果Timer调度失败：
    *
    * 网络Attempt已经存在，
    * 不能回滚Tracker。
    *
    * 因为Receiver可能已经真正收到D。
    */
    if (
        !ScheduleReceiverDeliveryAckTimeout(
            connection,
            message_id,
            from_user_id,
            to_user_id,
            text,
            delivery_packet.seq
        )
    ) {
        LOG_ERROR(
            "gateway submitted receiver "
            "delivery but failed to arm "
            "ack timeout"
            << ", message_id="
            << message_id
            << ", delivery_seq="
            << delivery_packet.seq
            << ", from="
            << from_user_id
            << ", to="
            << to_user_id
        );
    }


    if (submitted_packet != nullptr) {
        *submitted_packet =
            delivery_packet;
    }


    if (error_message != nullptr) {
        error_message->clear();
    }


    return
        ReceiverDeliverySubmitStatus::
            kSubmitted;
}


GatewayServer::
ReceiverDeliverySubmitStatus
GatewayServer::
SubmitReceiverChatDeliveryRetry(
    const TcpConnectionPtr& connection,
    std::uint64_t message_id,
    UserId from_user_id,
    UserId to_user_id,
    const std::string& text,
    std::uint32_t timed_out_delivery_seq,
    Packet* submitted_packet,
    std::string* error_message
) {
    auto set_error =
        [error_message](
            const std::string& message
        ) {
            if (error_message != nullptr) {
                *error_message =
                    message;
            }
        };


    if (
        !connection ||
        !connection->IsConnected()
    ) {
        set_error(
            "receiver connection unavailable"
        );

        return
            ReceiverDeliverySubmitStatus::
                kConnectionUnavailable;
    }


    /*
     * 1. 构造fresh D。
     *
     * message_id M保持不变。
     */
    Packet retry_packet;

    std::string build_error;


    if (
        !BuildReceiverChatDeliveryPacket(
            message_id,
            from_user_id,
            to_user_id,
            text,
            &retry_packet,
            &build_error
        )
    ) {
        set_error(
            build_error
        );

        return
            ReceiverDeliverySubmitStatus::
                kBuildFailed;
    }


    /*
     * 2. Encode必须在Tracker状态推进以前完成。
     */
    Buffer output;

    std::string encode_error;


    if (
        !codec_.Encode(
            retry_packet,
            &output,
            &encode_error
        )
    ) {
        set_error(
            encode_error
        );

        return
            ReceiverDeliverySubmitStatus::
                kEncodeFailed;
    }


    const std::string bytes =
        output.RetrieveAllAsString();


    if (!connection->IsConnected()) {
        set_error(
            "receiver connection became "
            "unavailable before retry registration"
        );

        return
            ReceiverDeliverySubmitStatus::
                kConnectionUnavailable;
    }


    /*
     * 3. F3-a新加入的原子Retry迁移。
     *
     * 这一步同时校验：
     *
     * current == timed_out D
     * 尚未Confirmed
     * attempt_count < max
     * new D没有重复
     */
    const
        ReceiverDeliveryRetryRegisterStatus
        retry_status =
            receiver_delivery_tracker_.
                RegisterRetryAttempt(
                    message_id,
                    to_user_id,
                    timed_out_delivery_seq,
                    retry_packet.seq,
                    options_.
                        receiver_delivery_max_attempts
                );


    if (
        retry_status ==
        ReceiverDeliveryRetryRegisterStatus::
            kAlreadyConfirmed
    ) {
        set_error(
            "receiver message already confirmed"
        );

        return
            ReceiverDeliverySubmitStatus::
                kAlreadyConfirmed;
    }


    if (
        retry_status ==
        ReceiverDeliveryRetryRegisterStatus::
            kStaleAttempt
    ) {
        set_error(
            "receiver delivery timeout is stale"
        );

        return
            ReceiverDeliverySubmitStatus::
                kStaleAttempt;
    }


    if (
        retry_status ==
        ReceiverDeliveryRetryRegisterStatus::
            kAttemptLimitReached
    ) {
        set_error(
            "receiver delivery attempt limit reached"
        );

        return
            ReceiverDeliverySubmitStatus::
                kAttemptLimitReached;
    }


    if (
        retry_status !=
        ReceiverDeliveryRetryRegisterStatus::
            kRetryRegistered
    ) {
        set_error(
            "receiver delivery tracker "
            "rejected retry attempt"
        );

        return
            ReceiverDeliverySubmitStatus::
                kTrackerRejected;
    }


    /*
     * 4. Tracker已经原子推进：
     *
     * D_old → D_new
     *
     * 此后才允许网络提交。
     */
    connection->Send(
        bytes
    );


    /*
     * 5. 为新的D_new继续安排ACK Timeout。
     */
    if (
        !ScheduleReceiverDeliveryAckTimeout(
            connection,
            message_id,
            from_user_id,
            to_user_id,
            text,
            retry_packet.seq
        )
    ) {
        LOG_ERROR(
            "gateway submitted receiver retry "
            "but failed to arm next ack timeout"
            << ", message_id="
            << message_id
            << ", timed_out_delivery_seq="
            << timed_out_delivery_seq
            << ", new_delivery_seq="
            << retry_packet.seq
            << ", receiver="
            << to_user_id
        );
    }


    if (submitted_packet != nullptr) {
        *submitted_packet =
            retry_packet;
    }


    if (error_message != nullptr) {
        error_message->clear();
    }


    return
        ReceiverDeliverySubmitStatus::
            kSubmitted;
}


void GatewayServer::
HandleReceiverDeliveryAckTimeout(
    std::uint64_t message_id,
    UserId from_user_id,
    UserId to_user_id,
    const std::string& text,
    std::uint32_t timed_out_delivery_seq
) {
    if (
        message_id == 0 ||
        from_user_id == 0 ||
        to_user_id == 0 ||
        timed_out_delivery_seq == 0
    ) {
        return;
    }


    /*
     * ============================================================
     * 1. Fast-path检查
     * ============================================================
     *
     * 注意它不是最终并发裁决。
     *
     * 最终裁决仍由：
     *
     * RegisterRetryAttempt()
     *
     * 在mutex里完成。
     */
    ReceiverDeliverySnapshot snapshot;


    if (
        !receiver_delivery_tracker_.
            GetSnapshot(
                message_id,
                &snapshot
            )
    ) {
        return;
    }


    /*
     * Receiver ACK已经成功。
     *
     * Timer自然到期即可，无需再重试。
     */
    if (snapshot.confirmed) {
        return;
    }


    /*
     * 这个Timer已经不是current Attempt。
     *
     * 例如：
     *
     * D1 timer
     * D1 timeout → D2
     *
     * 又碰到旧D1 callback。
     */
    if (
        snapshot.current_delivery_seq !=
        timed_out_delivery_seq
    ) {
        return;
    }


    /*
     * ============================================================
     * 2. 找Receiver当前Session
     * ============================================================
     *
     * 不保存旧TcpConnection。
     *
     * 如果用户已经Reconnect，
     * 这里拿的是新的Current Connection。
     */
    TcpConnectionPtr connection =
        session_manager_.
            FindConnection(
                to_user_id
            );


    if (
        !connection ||
        !connection->IsConnected()
    ) {
        LOG_INFO(
            "gateway receiver ack timeout "
            "retry paused because receiver "
            "has no active session"
            << ", message_id="
            << message_id
            << ", delivery_seq="
            << timed_out_delivery_seq
            << ", receiver="
            << to_user_id
        );

        /*
         * F4/F6会负责Reconnect + Pending Replay。
         *
         * 当前绝不能假装ReceiverConfirmed。
         */
        return;
    }


    Packet retry_packet;

    std::string retry_error;


    const
        ReceiverDeliverySubmitStatus
        retry_status =
            SubmitReceiverChatDeliveryRetry(
                connection,
                message_id,
                from_user_id,
                to_user_id,
                text,
                timed_out_delivery_seq,
                &retry_packet,
                &retry_error
            );


    if (
        retry_status ==
        ReceiverDeliverySubmitStatus::
            kSubmitted
    ) {
        LOG_WARN(
            "gateway receiver ack timeout "
            "triggered delivery retry"
            << ", message_id="
            << message_id
            << ", old_delivery_seq="
            << timed_out_delivery_seq
            << ", new_delivery_seq="
            << retry_packet.seq
            << ", receiver="
            << to_user_id
        );

        return;
    }


    /*
     * ACK在Timer触发附近到达。
     *
     * 正常竞态，不是错误。
     */
    if (
        retry_status ==
        ReceiverDeliverySubmitStatus::
            kAlreadyConfirmed
    ) {
        return;
    }


    /*
     * 旧Timer。
     *
     * 也是正常竞态。
     */
    if (
        retry_status ==
        ReceiverDeliverySubmitStatus::
            kStaleAttempt
    ) {
        return;
    }


    /*
     * 达到主动Retry上限。
     *
     * 注意：
     *
     * Tracker仍然保留历史D1/D2/D3。
     *
     * 任意一个真实Late ACK仍可以确认M。
     */
    if (
        retry_status ==
        ReceiverDeliverySubmitStatus::
            kAttemptLimitReached
    ) {
        LOG_WARN(
            "gateway receiver delivery "
            "retry attempt limit reached"
            << ", message_id="
            << message_id
            << ", current_delivery_seq="
            << timed_out_delivery_seq
            << ", receiver="
            << to_user_id
            << ", max_attempts="
            << options_.
                receiver_delivery_max_attempts
        );

        return;
    }


    if (
        retry_status ==
        ReceiverDeliverySubmitStatus::
            kConnectionUnavailable
    ) {
        LOG_INFO(
            "gateway receiver connection "
            "became unavailable during "
            "ack-timeout retry"
            << ", message_id="
            << message_id
            << ", delivery_seq="
            << timed_out_delivery_seq
            << ", receiver="
            << to_user_id
        );

        return;
    }


    LOG_ERROR(
        "gateway receiver ack timeout "
        "retry failed"
        << ", message_id="
        << message_id
        << ", delivery_seq="
        << timed_out_delivery_seq
        << ", receiver="
        << to_user_id
        << ", submit_status="
        << static_cast<int>(
            retry_status
        )
        << ", error="
        << retry_error
    );
}


bool GatewayServer::
ScheduleReceiverDeliveryAckTimeout(
    const TcpConnectionPtr& connection,
    std::uint64_t message_id,
    UserId from_user_id,
    UserId to_user_id,
    const std::string& text,
    std::uint32_t delivery_seq
) {
    if (
        !connection ||
        message_id == 0 ||
        from_user_id == 0 ||
        to_user_id == 0 ||
        delivery_seq == 0
    ) {
        LOG_WARN(
            "gateway cannot schedule "
            "receiver delivery ack timeout"
            << ", message_id="
            << message_id
            << ", delivery_seq="
            << delivery_seq
            << ", from="
            << from_user_id
            << ", to="
            << to_user_id
        );

        return false;
    }


    EventLoop* timer_loop =
        connection->GetLoop();


    if (timer_loop == nullptr) {
        LOG_ERROR(
            "gateway receiver delivery "
            "has no event loop for ack timer"
            << ", message_id="
            << message_id
            << ", delivery_seq="
            << delivery_seq
            << ", receiver="
            << to_user_id
        );

        return false;
    }


    if (
        options_.
            receiver_ack_timeout.
            count() <= 0
    ) {
        LOG_ERROR(
            "gateway receiver ack timeout "
            "configuration is invalid"
            << ", timeout_ms="
            << options_.
                receiver_ack_timeout.
                count()
        );

        return false;
    }


    const auto timeout =
        options_.receiver_ack_timeout;


    const TimerId timer_id =
        timer_loop->RunAfter(
            timeout,
            [
                this,
                message_id,
                from_user_id,
                to_user_id,
                text,
                delivery_seq
            ]() {
                HandleReceiverDeliveryAckTimeout(
                    message_id,
                    from_user_id,
                    to_user_id,
                    text,
                    delivery_seq
                );
            }
        );


    if (!timer_id.IsValid()) {
        LOG_ERROR(
            "gateway failed to schedule "
            "receiver delivery ack timeout"
            << ", message_id="
            << message_id
            << ", delivery_seq="
            << delivery_seq
            << ", receiver="
            << to_user_id
            << ", timeout_ms="
            << timeout.count()
        );

        return false;
    }


    LOG_INFO(
        "gateway scheduled receiver "
        "delivery ack timeout"
        << ", message_id="
        << message_id
        << ", delivery_seq="
        << delivery_seq
        << ", receiver="
        << to_user_id
        << ", timeout_ms="
        << timeout.count()
    );


    return true;
}


const std::string& GatewayServer::Name() const {
    return options_.name;
}

const InetAddress& GatewayServer::ListenAddress() const {
    return server_.ListenAddress();
}

std::size_t GatewayServer::ConnectionCount() const {
    return server_.ConnectionCount();
}

void GatewayServer::SetUserOfflineIfMatch(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    if (!HasOnlineStatusCache()) {
        return;
    }

    if (user_id == 0 ||
        !connection) {
        return;
    }

    const SetOfflineIfMatchResult result =
        online_status_cache_->
            SetOfflineIfMatch(
                user_id,
                options_.gateway_id,
                connection->Name()
            );

    if (result.Completed()) {
        return;
    }

    LOG_WARN(
        "gateway conditional user "
        "offline failed"
        << ", user_id=" << user_id
        << ", gateway_id="
        << options_.gateway_id
        << ", connection="
        << connection->Name()
        << ", status="
        << SetOfflineIfMatchStatusToString(
            result.status
        )
        << ", error="
        << result.error_message
    );
}

void GatewayServer::HandleConnection(
    const TcpConnectionPtr& connection
) {
    if (!connection) {
        return;
    }

    if (connection->IsConnected()) {
        LOG_INFO(
            "gateway client connected"
            << ", name=" << options_.name
            << ", peer="
            << connection->
                PeerAddress().
                ToString()
            << ", connection_count="
            << server_.ConnectionCount()
        );

        return;
    }

    const SessionUnbindResult
        unbind_result =
            session_manager_.
                UnbindIfCurrent(
                    connection
                );

    if (
        unbind_result.unbound &&
        HasBusinessExecutor() &&
        HasOnlineStatusCache()
    ) {
        BusinessExecutor::TaskSpec task;

        task.request.operation =
            "gateway.presence.offline";

        task.request.user_id =
            unbind_result.user_id;

        task.request.session_epoch =
            unbind_result.epoch;

        /*
         * Session已经解绑，cleanup正是因为ownership结束才需要执行。
         * 因此不能再使用IsCurrent cancellation fence。
         */
        task.cancellation_policy =
            BusinessCancellationPolicy::
                kMustRun;

        task.work =
            [
                this,
                user_id =
                    unbind_result.user_id,
                connection
            ](
                const BusinessExecutor::
                    ExecutionContext&
            ) -> BusinessExecutor::Completion {
                SetUserOfflineIfMatch(
                    user_id,
                    connection
                );

                return {};
            };

        const BusinessSubmitStatus status =
            business_executor_->Submit(
                std::move(task)
            );

        if (
            status !=
            BusinessSubmitStatus::kAccepted
        ) {
            LOG_WARN(
                "gateway async offline cleanup not admitted"
                << ", user_id="
                << unbind_result.user_id
                << ", connection="
                << connection->Name()
                << ", status="
                << BusinessSubmitStatusToString(
                    status
                )
            );
        }
    }

    LOG_INFO(
        "gateway client disconnected"
        << ", name=" << options_.name
        << ", peer="
        << connection->
            PeerAddress().
            ToString()
        << ", connection="
        << connection->Name()
        << ", session_unbound="
        << unbind_result.unbound
        << ", user_id="
        << unbind_result.user_id
        << ", online_count="
        << session_manager_.
            OnlineCount()
    );
}

void GatewayServer::HandleMessage(const TcpConnectionPtr& connection,
                                  Buffer* buffer) {
    if (!connection || buffer == nullptr) {
        return;
    }

    const DecodeResult decode_result = codec_.Decode(buffer);

    if (decode_result.status == DecodeStatus::kNeedMoreData) {
        return;
    }

    if (decode_result.status != DecodeStatus::kOk) {
        LOG_WARN("gateway decode packet failed"
                 << ", peer=" << connection->PeerAddress().ToString()
                 << ", status="
                 << DecodeStatusToString(decode_result.status)
                 << ", error=" << decode_result.error_message);

        const Packet error_packet =
            MakeErrorPacket(0, decode_result.error_message);

        SendPacket(connection, error_packet);

        if (options_.close_on_decode_error) {
            connection->Shutdown();
        }

        return;
    }

    for (const auto& packet : decode_result.packets) {
        LOG_INFO("gateway received packet"
                 << ", peer=" << connection->PeerAddress().ToString()
                 << ", type=" << MessageTypeToString(packet.type)
                 << ", seq=" << packet.seq
                 << ", body_size=" << packet.body.size());

        HandlePacket(connection, packet);
    }
}
/*
    void GatewayServer::HandlePacket(const TcpConnectionPtr& connection,
                                 const Packet& packet) {
        if (packet_handler_) {
            packet_handler_(connection, packet);
            return;
        }

        const Packet response = BuildDefaultResponse(packet);
        SendPacket(connection, response);
    }


*/

void GatewayServer::HandlePacket(const TcpConnectionPtr& connection,
                                 const Packet& packet) {
    if (packet_handler_) {
        packet_handler_(connection, packet);
        return;
    }

    switch (packet.type) {
        case MessageType::kLoginRequest:
            HandleLoginRequest(connection, packet);
            return;

        case MessageType::kChatMessage:
            HandleChatMessage(connection, packet);
            return;

        case MessageType::kChatDeliveryAck:
            HandleReceiverChatDeliveryAck(
                connection,
                packet);
            return;

        case MessageType::
            kGatewayForwardChatRequest:
            HandleGatewayForwardChatRequest(
                connection,
                packet
            );
            return;

        case MessageType::kReadRequest:
            HandleReadRequest(connection, packet);
            break;

        case MessageType::kHistoryRequest:
            HandleHistoryRequest(connection, packet);
            break;

        case MessageType::kConversationListRequest:
            HandleConversationListRequest(connection, packet);
            break;

        case MessageType::kUserProfileRequest:
            HandleUserProfileRequest(connection, packet);
            break;

        case MessageType::kFriendListRequest:
            HandleFriendListRequest(connection, packet);
            break;


        case MessageType::kFriendRequestCreateRequest:
            HandleFriendRequestCreateRequest(
                connection,
                packet
            );
            break;

        case MessageType::kFriendRequestListRequest:
            HandleFriendRequestListRequest(
                connection,
                packet
            );
            break;

        case MessageType::kFriendRequestAcceptRequest:
            HandleFriendRequestAcceptRequest(
                connection,
                packet
            );
            break;

        case MessageType::kFriendRequestRejectRequest:
            HandleFriendRequestRejectRequest(
                connection,
                packet
            );
            break;

        case MessageType::kCreateGroupRequest:
        case MessageType::kGetGroupRequest:
        case MessageType::kUpdateGroupRequest:
        case MessageType::kDisbandGroupRequest:
        case MessageType::kJoinGroupRequest:
        case MessageType::kLeaveGroupRequest:
        case MessageType::kInviteGroupMemberRequest:
        case MessageType::kKickGroupMemberRequest:
        case MessageType::kSetGroupMemberRoleRequest:
        case MessageType::kSetGroupMemberMuteRequest:
        case MessageType::kTransferGroupOwnershipRequest:
        case MessageType::kListGroupMembersRequest:
        case MessageType::kListMyGroupsRequest:
            HandleGroupControlRequest(connection, packet);
            break;

        case MessageType::kHeartbeat:
            HandleHeartbeat(connection, packet);
            return;

        default:
            SendPacket(
                connection,
                MakeErrorPacket(packet.seq, "unsupported message type")
            );
            return;
    }
}


void GatewayServer::HandleReceiverChatDeliveryAck(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    if (!connection || packet.seq == 0) {
        return;
    }

    ReceiverChatDeliveryAck ack;
    std::string deserialize_error;
    if (!DeserializeReceiverChatDeliveryAck(
            packet.body, &ack, &deserialize_error)) {
        LOG_WARN("gateway rejected invalid receiver delivery ack"
                 << ", delivery_seq=" << packet.seq
                 << ", error=" << deserialize_error);
        return;
    }

    const auto session = session_manager_.FindSessionByConnection(connection);
    if (!session.has_value()) {
        LOG_WARN("gateway rejected receiver delivery ack from unauthenticated connection"
                 << ", message_id=" << ack.message_id
                 << ", delivery_seq=" << packet.seq);
        return;
    }

    if (!HasBusinessExecutor()) {
        LOG_ERROR("gateway cannot dispatch receiver delivery ack: business executor unavailable"
                  << ", message_id=" << ack.message_id);
        return;
    }

    const BusinessSubmitStatus submit_status =
        SubmitSessionBusinessTask(
            business_executor_,
            &session_manager_,
            connection,
            *session,
            packet.seq,
            BusinessClock::now(),
            "gateway.receiver_ack",
            BusinessCancellationPolicy::kMustRun,
            static_cast<BusinessOrderingKey>(ack.message_id),
            [this, connection, packet,
             dispatch_user_id = session->user_id,
             dispatch_epoch = session->epoch](
                const BusinessExecutor::ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                ScopedBusinessDispatchContext dispatch_scope(
                    connection, dispatch_user_id, dispatch_epoch);
                ExecuteReceiverChatDeliveryAck(
                    connection, packet, context.Request());
                return {};
            }
        );

    if (submit_status != BusinessSubmitStatus::kAccepted) {
        LOG_WARN("gateway receiver ack business task rejected"
                 << ", message_id=" << ack.message_id
                 << ", status="
                 << BusinessSubmitStatusToString(submit_status));
    }
}

void GatewayServer::ExecuteReceiverChatDeliveryAck(
    const TcpConnectionPtr& connection,
    const Packet& packet,
    const BusinessRequestContext& business_request
) {
    if (!connection || packet.seq == 0) {
        return;
    }

    ReceiverChatDeliveryAck ack;
    std::string deserialize_error;
    if (!DeserializeReceiverChatDeliveryAck(
            packet.body, &ack, &deserialize_error)) {
        return;
    }

    const auto receiver_user_id =
        ResolveBusinessUser(session_manager_, connection);
    if (!receiver_user_id.has_value()) {
        return;
    }
    const UserId receiver = *receiver_user_id;

    if (!HasMessageRpcClient()) {
        LOG_ERROR("gateway cannot validate receiver delivery ack: MessageService unavailable"
                  << ", message_id=" << ack.message_id
                  << ", delivery_seq=" << packet.seq
                  << ", receiver=" << receiver);
        return;
    }

    auto make_call_options = [&](const char* operation) {
        rpc::RpcCallOptions options;
        const std::uint64_t rpc_id = next_internal_rpc_id_.fetch_add(
            1, std::memory_order_relaxed);
        options.request_id = options_.gateway_id + ":message:" + operation +
            ":req:" + std::to_string(rpc_id);
        options.trace_id = options_.gateway_id + ":receiver-ack:trace:" +
            std::to_string(rpc_id) + ":M:" + std::to_string(ack.message_id);
        options.caller_service = "gateway";
        options.caller_instance = options_.gateway_id;
        const auto remaining = business_request.RemainingTime();
        options.remaining_timeout =
            remaining == std::chrono::milliseconds::max()
                ? std::chrono::milliseconds{0}
                : remaining;
        return options;
    };

    rpc::GetPrivateMessageRpcRequest get_request;
    get_request.message_id = ack.message_id;
    auto get_result = message_rpc_client_->GetPrivateMessage(
        get_request, make_call_options("get-private-message-for-ack"));
    if (!get_result.ok()) {
        LOG_WARN("gateway receiver delivery ack durable lookup failed"
                 << ", message_id=" << ack.message_id
                 << ", delivery_seq=" << packet.seq
                 << ", receiver=" << receiver
                 << ", rpc_code="
                 << static_cast<int>(get_result.status.code)
                 << ", error=" << get_result.status.message);
        return;
    }

    const rpc::MessageRpcRecord& message = get_result.value->record;
    if (message.to_user_id != receiver) {
        LOG_WARN("gateway rejected receiver delivery ack identity mismatch"
                 << ", message_id=" << ack.message_id
                 << ", delivery_seq=" << packet.seq
                 << ", authenticated_receiver=" << receiver
                 << ", persisted_receiver=" << message.to_user_id
                 << ", sender=" << message.from_user_id);
        return;
    }

    const ReceiverDeliveryAckStatus ack_status =
        receiver_delivery_tracker_.Acknowledge(
            ack.message_id, receiver, packet.seq);

    auto persist_receiver_confirmation = [&]() -> bool {
        rpc::ConfirmReceiverRpcRequest confirm_request;
        confirm_request.message_id = ack.message_id;
        confirm_request.receiver_user_id = receiver;

        const auto result = message_rpc_client_->ConfirmReceiver(
            confirm_request,
            make_call_options("confirm-receiver"));

        if (!result.ok()) {
            // Runtime ACK evidence is already monotonic. Once the mutation RPC
            // was attempted, DB may have committed even if the response was
            // lost. Never roll back ReceiverDeliveryTracker here.
            if (result.attempted) {
                LOG_WARN("gateway receiver confirmation durable state uncertain"
                         << ", reason=receiver_confirmation_uncertain"
                         << ", message_id=" << ack.message_id
                         << ", delivery_seq=" << packet.seq
                         << ", receiver=" << receiver
                         << ", error=" << result.status.message);
            } else {
                LOG_WARN("gateway receiver confirmation failed before mutation boundary"
                         << ", message_id=" << ack.message_id
                         << ", receiver=" << receiver
                         << ", error=" << result.status.message);
            }
            return false;
        }

        LOG_INFO("gateway durable receiver confirmation advanced"
                 << ", message_id=" << ack.message_id
                 << ", delivery_seq=" << packet.seq
                 << ", receiver=" << receiver
                 << ", affected_rows=" << result.value->affected_rows);
        return true;
    };

    if (ack_status == ReceiverDeliveryAckStatus::kConfirmed) {
        const bool durable_confirmed = persist_receiver_confirmation();
        LOG_INFO("gateway receiver delivery ack confirmed"
                 << ", message_id=" << ack.message_id
                 << ", delivery_seq=" << packet.seq
                 << ", receiver=" << receiver
                 << ", sender=" << message.from_user_id
                 << ", durable_confirmed=" << durable_confirmed);
        return;
    }

    if (ack_status == ReceiverDeliveryAckStatus::kDuplicate) {
        // Duplicate ACK is also a repair opportunity after an uncertain or
        // failed durable confirmation attempt.
        const bool durable_repaired = persist_receiver_confirmation();
        LOG_INFO("gateway ignored duplicate receiver delivery ack"
                 << ", message_id=" << ack.message_id
                 << ", delivery_seq=" << packet.seq
                 << ", receiver=" << receiver
                 << ", durable_repaired=" << durable_repaired);
        return;
    }

    if (ack_status == ReceiverDeliveryAckStatus::kReceiverMismatch) {
        LOG_WARN("gateway receiver delivery tracker rejected receiver mismatch"
                 << ", message_id=" << ack.message_id
                 << ", delivery_seq=" << packet.seq
                 << ", receiver=" << receiver);
        return;
    }
    if (ack_status == ReceiverDeliveryAckStatus::kUnknownAttempt) {
        LOG_WARN("gateway rejected receiver delivery ack for unknown attempt"
                 << ", message_id=" << ack.message_id
                 << ", delivery_seq=" << packet.seq
                 << ", receiver=" << receiver);
        return;
    }
    if (ack_status == ReceiverDeliveryAckStatus::kUnknownMessage) {
        LOG_WARN("gateway receiver delivery ack has no active tracker entry"
                 << ", message_id=" << ack.message_id
                 << ", delivery_seq=" << packet.seq
                 << ", receiver=" << receiver);
        return;
    }

    LOG_WARN("gateway rejected invalid receiver delivery ack"
             << ", message_id=" << ack.message_id
             << ", delivery_seq=" << packet.seq
             << ", receiver=" << receiver);
}


bool GatewayServer::ClearUnread(
    UserId reader_user_id,
    UserId peer_user_id,
    std::int64_t* total_unread
) {
    if (total_unread != nullptr) {
        *total_unread = 0;
    }

    if (!HasUnreadCountCache()) {
        return true;
    }

    if (!unread_projection_write_enabled_) {
        if (total_unread != nullptr) {
            *total_unread = GetTotalUnread(reader_user_id);
        }
        return true;
    }

    const ClearUnreadResult
        clear_result =
            unread_count_cache_->
                ClearPrivateUnread(
                    reader_user_id,
                    peer_user_id
                );

    if (!clear_result.Completed()) {
        LOG_WARN(
            "gateway clear unread failed"
            << ", reader="
            << reader_user_id
            << ", peer="
            << peer_user_id
            << ", status="
            << ClearUnreadStatusToString(
                clear_result.status
            )
            << ", error="
            << clear_result.error_message
        );

        return false;
    }

    /*
     * private键存在并完成清除时，
     * Lua已经返回清除后的总未读数。
     */
    if (clear_result.Cleared()) {
        if (total_unread != nullptr) {
            *total_unread =
                clear_result.total_count;
        }

        return true;
    }

    /*
     * private键不存在时，Lua返回not_found。
     *
     * 但用户仍可能有其他发送者对应的未读消息，
     * 所以这里查询真实总未读数。
     */
    const GetUnreadCountResult
        total_result =
            unread_count_cache_->
                GetTotalUnread(
                    reader_user_id
                );

    if (total_result.Completed()) {
        if (total_unread != nullptr) {
            *total_unread =
                total_result.count;
        }
    } else {
        LOG_WARN(
            "gateway get total unread "
            "after clear not_found failed"
            << ", reader="
            << reader_user_id
            << ", status="
            << GetUnreadCountStatusToString(
                total_result.status
            )
            << ", error="
            << total_result.error_message
        );
    }

    return true;
}

void GatewayServer::HandleLoginRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    if (!HasBusinessExecutor()) {
        Json response_body;
        response_body["success"] = false;
        response_body["message"] = "business runtime unavailable";
        response_body["reason"] = "business_runtime_unavailable";
        response_body["online_count"] = session_manager_.OnlineCount();
        response_body["offline_count"] = 0;
        response_body["total_unread"] = 0;

        Packet response;
        response.type = MessageType::kLoginResponse;
        response.seq = packet.seq;
        response.body = response_body.dump();
        SendPacket(connection, response);
        return;
    }

    // Cheap Reactor-side probe only. Full validation remains on the
    // BusinessExecutor worker. The username provides process-local ordering
    // for concurrent Login attempts of the same account.
    std::string dispatch_username;
    try {
        const Json dispatch_body = Json::parse(packet.body);
        if (
            dispatch_body.is_object() &&
            dispatch_body.contains("username") &&
            dispatch_body.at("username").is_string()
        ) {
            dispatch_username =
                dispatch_body.at("username").get<std::string>();
        }
    } catch (...) {
        dispatch_username.clear();
    }

    const BusinessOrderingKey login_ordering_key =
        dispatch_username.empty()
            ? MakeConnectionOrderingKey(connection)
            : MakeStringOrderingKey(dispatch_username);

    const BusinessSubmitStatus submit_status =
        SubmitConnectionBusinessTask(
            business_executor_,
            connection,
            packet.seq,
            BusinessClock::now(),
            "gateway.login.rpc",
            login_ordering_key,
            [this, connection, packet](
                const BusinessExecutor::ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                if (context.CancellationRequested()) {
                    return {};
                }

                ScopedBusinessDispatchContext dispatch_scope(
                    connection,
                    0,
                    0
                );

                Json body;
                std::string error_message;

                auto send_login_response =
                    [this, &connection, &packet](
                        bool success,
                        const std::string& message,
                        const std::string& reason,
                        UserId user_id,
                        const std::string& username,
                        std::size_t online_count,
                        std::size_t offline_count,
                        std::int64_t total_unread
                    ) {
                        Json response_body;
                        response_body["success"] = success;
                        response_body["message"] = message;

                        if (!reason.empty()) {
                            response_body["reason"] = reason;
                        }

                        if (success) {
                            response_body["user_id"] = user_id;
                            response_body["username"] = username;
                        }

                        response_body["online_count"] = online_count;
                        response_body["offline_count"] = offline_count;
                        response_body["total_unread"] = total_unread;

                        Packet response;
                        response.type = MessageType::kLoginResponse;
                        response.seq = packet.seq;
                        response.body = response_body.dump();
                        SendPacket(connection, response);
                    };

                if (!ParseJsonBody(packet, &body, &error_message)) {
                    send_login_response(
                        false,
                        "invalid login json",
                        "invalid_json",
                        0,
                        "",
                        session_manager_.OnlineCount(),
                        0,
                        0
                    );
                    return {};
                }

                if (!body.contains("username") ||
                    !body.at("username").is_string()) {
                    send_login_response(
                        false,
                        "missing or invalid username",
                        "invalid_username",
                        0,
                        "",
                        session_manager_.OnlineCount(),
                        0,
                        0
                    );

                    LOG_WARN(
                        "gateway login rejected: missing or invalid username"
                        << ", peer="
                        << connection->PeerAddress().ToString()
                    );
                    return {};
                }

                if (!body.contains("password") ||
                    !body.at("password").is_string()) {
                    send_login_response(
                        false,
                        "missing or invalid password",
                        "invalid_password",
                        0,
                        "",
                        session_manager_.OnlineCount(),
                        0,
                        0
                    );

                    LOG_WARN(
                        "gateway login rejected: missing or invalid password"
                        << ", username="
                        << body.value("username", "")
                        << ", peer="
                        << connection->PeerAddress().ToString()
                    );
                    return {};
                }

                const std::string username =
                    body.at("username").get<std::string>();
                const std::string password =
                    body.at("password").get<std::string>();

                if (username.empty()) {
                    send_login_response(
                        false,
                        "missing or invalid username",
                        "invalid_username",
                        0,
                        "",
                        session_manager_.OnlineCount(),
                        0,
                        0
                    );
                    return {};
                }

                if (password.empty()) {
                    send_login_response(
                        false,
                        "missing or invalid password",
                        "invalid_password",
                        0,
                        "",
                        session_manager_.OnlineCount(),
                        0,
                        0
                    );
                    return {};
                }

                if (!HasUserRpcClient()) {
                    send_login_response(
                        false,
                        "auth service unavailable",
                        "auth_unavailable",
                        0,
                        "",
                        session_manager_.OnlineCount(),
                        0,
                        0
                    );
                    return {};
                }

                const auto remaining =
                    context.Request().RemainingTime();

                rpc::RpcCallOptions call_options;
                const std::uint64_t rpc_id =
                    next_internal_rpc_id_.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );

                call_options.request_id =
                    options_.gateway_id +
                    ":user:authenticate:req:" +
                    std::to_string(rpc_id);
                call_options.trace_id =
                    options_.gateway_id +
                    ":login:trace:" +
                    std::to_string(rpc_id) +
                    ":client-seq:" +
                    std::to_string(packet.seq);
                call_options.caller_service = "gateway";
                call_options.caller_instance = options_.gateway_id;

                if (
                    remaining ==
                    std::chrono::milliseconds::max()
                ) {
                    // Fail closed rather than inventing a fresh RPC timeout.
                    call_options.remaining_timeout =
                        std::chrono::milliseconds{0};
                } else {
                    call_options.remaining_timeout = remaining;
                }

                rpc::AuthenticateRpcRequest rpc_request;
                rpc_request.username = username;
                rpc_request.password = password;

                const auto rpc_result =
                    user_rpc_client_->Authenticate(
                        rpc_request,
                        call_options
                    );

                if (!rpc_result.ok()) {
                    const char* const reason =
                        UserRpcErrorReason(
                            rpc_result.status.code
                        );

                    send_login_response(
                        false,
                        UserRpcPublicMessage(
                            rpc_result.status.code
                        ),
                        reason,
                        0,
                        "",
                        session_manager_.OnlineCount(),
                        0,
                        0
                    );

                    LOG_WARN(
                        "gateway login RPC failed"
                        << ", username=" << username
                        << ", request_seq=" << packet.seq
                        << ", rpc_request_id="
                        << call_options.request_id
                        << ", reason=" << reason
                        << ", message="
                        << rpc_result.status.message
                    );
                    return {};
                }

                const rpc::AuthenticateRpcResponse& auth =
                    *rpc_result.value;

                if (!auth.authenticated()) {
                    const char* const reason =
                        AuthenticateOutcomeReason(
                            auth.outcome
                        );

                    send_login_response(
                        false,
                        auth.message.empty()
                            ? "login rejected"
                            : auth.message,
                        reason,
                        0,
                        "",
                        session_manager_.OnlineCount(),
                        0,
                        0
                    );

                    LOG_WARN(
                        "gateway login rejected by UserService"
                        << ", username=" << username
                        << ", reason=" << reason
                        << ", rpc_request_id="
                        << call_options.request_id
                        << ", peer="
                        << connection->PeerAddress().ToString()
                    );
                    return {};
                }

                // Authentication may have blocked on gRPC/MySQL. Re-check the
                // original M13 request/connection fence before activating any
                // Gateway session or presence state.
                if (context.CancellationRequested() ||
                    !connection ||
                    !connection->IsConnected()) {
                    LOG_INFO(
                        "gateway dropped login activation after UserService RPC"
                        << ", username=" << username
                        << ", rpc_request_id="
                        << call_options.request_id
                    );
                    return {};
                }

                const rpc::UserProfileRpcView& profile =
                    *auth.profile;
                const UserId user_id =
                    static_cast<UserId>(profile.user_id);
                const std::string verified_username =
                    profile.username;

                const SessionBindResult bind_result =
                    session_manager_.BindOrReplace(
                        user_id,
                        connection
                    );

                if (!bind_result.success) {
                    send_login_response(
                        false,
                        "session bind failed",
                        "session_bind_failed",
                        0,
                        "",
                        session_manager_.OnlineCount(),
                        0,
                        0
                    );

                    LOG_WARN(
                        "gateway login rejected: session bind failed"
                        << ", user_id=" << user_id
                        << ", username=" << verified_username
                        << ", peer="
                        << connection->PeerAddress().ToString()
                    );
                    return {};
                }

                if (bind_result.replaced &&
                    bind_result.old_connection &&
                    bind_result.old_connection != connection) {
                    NotifyLoginReplaced(
                        user_id,
                        bind_result.old_connection
                    );
                }

                UpdateBusinessDispatchSession(
                    connection,
                    user_id,
                    bind_result.epoch
                );

                if (!session_manager_.IsCurrent(
                        user_id,
                        bind_result.epoch,
                        connection)) {
                    LOG_INFO(
                        "gateway dropped stale login activation"
                        << ", user_id=" << user_id
                        << ", epoch=" << bind_result.epoch
                    );
                    return {};
                }

                SetUserOnline(user_id, connection);

                // MessageService is an enrichment dependency for Login, not
                // part of authentication acceptance. A degraded/offline
                // MessageService must not turn into an auth outage.
                std::size_t offline_count = 0;
                if (HasMessageRpcClient()) {
                    rpc::CountPendingRpcRequest count_request;
                    count_request.to_user_id = user_id;

                    rpc::RpcCallOptions count_options;
                    const std::uint64_t count_rpc_id =
                        next_internal_rpc_id_.fetch_add(
                            1, std::memory_order_relaxed);
                    count_options.request_id =
                        options_.gateway_id +
                        ":message:count-pending:req:" +
                        std::to_string(count_rpc_id);
                    count_options.trace_id =
                        options_.gateway_id +
                        ":login-pending:trace:" +
                        std::to_string(count_rpc_id) +
                        ":user:" + std::to_string(user_id);
                    count_options.caller_service = "gateway";
                    count_options.caller_instance = options_.gateway_id;

                    const auto remaining_after_auth =
                        context.Request().RemainingTime();
                    constexpr auto kLoginEnrichmentBudget =
                        std::chrono::milliseconds{300};
                    if (remaining_after_auth ==
                        std::chrono::milliseconds::max()) {
                        count_options.remaining_timeout =
                            kLoginEnrichmentBudget;
                    } else {
                        count_options.remaining_timeout =
                            std::min(remaining_after_auth,
                                     kLoginEnrichmentBudget);
                    }

                    const auto count_result =
                        message_rpc_client_->CountPending(
                            count_request, count_options);
                    if (count_result.ok()) {
                        offline_count = static_cast<std::size_t>(
                            count_result.value->count);
                    } else {
                        LOG_WARN(
                            "gateway login pending-count enrichment degraded"
                            << ", user_id=" << user_id
                            << ", rpc_request_id="
                            << count_options.request_id
                            << ", rpc_code="
                            << static_cast<int>(
                                   count_result.status.code)
                            << ", message="
                            << count_result.status.message
                        );
                    }
                } else {
                    LOG_WARN(
                        "gateway login pending-count enrichment unavailable"
                        << ", user_id=" << user_id
                    );
                }

                send_login_response(
                    true,
                    "login accepted",
                    "",
                    user_id,
                    verified_username,
                    session_manager_.OnlineCount(),
                    offline_count,
                    GetTotalUnread(user_id)
                );

                // Replay is post-login best effort and receives its own bounded
                // BusinessExecutor task/deadline. It must not extend or revoke
                // the already accepted Login operation.
                if (HasMessageRpcClient()) {
                    PushPersistentOfflineMessages(user_id, connection);
                } else {
                    LOG_WARN(
                        "gateway postponed durable offline replay: "
                        "MessageService client unavailable"
                        << ", user_id=" << user_id
                    );
                }

                LOG_INFO(
                    "gateway user logged in via UserService"
                    << ", user_id=" << user_id
                    << ", username=" << verified_username
                    << ", rpc_request_id="
                    << call_options.request_id
                    << ", peer="
                    << connection->PeerAddress().ToString()
                    << ", online_count="
                    << session_manager_.OnlineCount()
                );

                return {};
            }
        );

    if (submit_status == BusinessSubmitStatus::kAccepted) {
        return;
    }

    Json response_body;
    response_body["success"] = false;
    response_body["message"] = "login business task rejected";
    response_body["reason"] =
        BusinessSubmitStatusToString(submit_status);
    response_body["online_count"] = session_manager_.OnlineCount();
    response_body["offline_count"] = 0;
    response_body["total_unread"] = 0;

    Packet response;
    response.type = MessageType::kLoginResponse;
    response.seq = packet.seq;
    response.body = response_body.dump();
    SendPacket(connection, response);
}


void GatewayServer::HandleChatMessage(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    ClientChatRequest dispatch_request;
    std::string dispatch_error;

    if (!DeserializeClientChatRequest(
            packet.body,
            &dispatch_request,
            &dispatch_error
        )) {
        ClientChatAck ack;
        ack.success = false;
        ack.delivered = false;
        ack.reason = "invalid_chat_request";

        std::string ack_body;
        std::string serialize_error;
        if (SerializeClientChatAck(ack, &ack_body, &serialize_error)) {
            Packet response;
            response.type = MessageType::kChatAck;
            response.seq = packet.seq;
            response.body = std::move(ack_body);
            SendPacket(connection, response);
        }
        return;
    }

    const auto dispatch_session =
        session_manager_.FindSessionByConnection(connection);

    if (!dispatch_session.has_value()) {
        ClientChatAck ack;
        ack.success = false;
        ack.delivered = false;
        ack.client_message_id = dispatch_request.client_message_id;
        ack.to_user_id = dispatch_request.to_user_id;
        ack.reason = "sender_not_logged_in";

        std::string ack_body;
        std::string serialize_error;
        if (SerializeClientChatAck(ack, &ack_body, &serialize_error)) {
            Packet response;
            response.type = MessageType::kChatAck;
            response.seq = packet.seq;
            response.body = std::move(ack_body);
            SendPacket(connection, response);
        }
        return;
    }

    if (!HasBusinessExecutor()) {
        ClientChatAck ack;
        ack.success = false;
        ack.delivered = false;
        ack.client_message_id = dispatch_request.client_message_id;
        ack.from_user_id = dispatch_session->user_id;
        ack.to_user_id = dispatch_request.to_user_id;
        ack.reason = "business_runtime_unavailable";

        std::string ack_body;
        std::string serialize_error;
        if (SerializeClientChatAck(ack, &ack_body, &serialize_error)) {
            Packet response;
            response.type = MessageType::kChatAck;
            response.seq = packet.seq;
            response.body = std::move(ack_body);
            SendPacket(connection, response);
        }
        return;
    }

    const BusinessSubmitStatus submit_status =
        SubmitSessionBusinessTask(
            business_executor_,
            &session_manager_,
            connection,
            *dispatch_session,
            packet.seq,
            BusinessClock::now(),
            "gateway.chat.rpc",
            BusinessCancellationPolicy::kMustRun,
            MakeUserPairOrderingKey(
                dispatch_session->user_id,
                dispatch_request.to_user_id
            ),
            [
                this,
                connection,
                packet,
                dispatch_user_id = dispatch_session->user_id,
                dispatch_epoch = dispatch_session->epoch
            ](
                const BusinessExecutor::ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                ScopedBusinessDispatchContext dispatch_scope(
                    connection,
                    dispatch_user_id,
                    dispatch_epoch
                );

                ExecuteChatMessage(
                    connection,
                    packet,
                    context.Request()
                );
                return {};
            }
        );

    if (submit_status == BusinessSubmitStatus::kAccepted) {
        return;
    }

    ClientChatAck ack;
    ack.success = false;
    ack.delivered = false;
    ack.client_message_id = dispatch_request.client_message_id;
    ack.from_user_id = dispatch_session->user_id;
    ack.to_user_id = dispatch_request.to_user_id;
    ack.reason = BusinessSubmitStatusToString(submit_status);

    std::string ack_body;
    std::string serialize_error;
    if (SerializeClientChatAck(ack, &ack_body, &serialize_error)) {
        Packet response;
        response.type = MessageType::kChatAck;
        response.seq = packet.seq;
        response.body = std::move(ack_body);
        SendPacket(connection, response);
    }
}

void GatewayServer::ExecuteChatMessage(
    const TcpConnectionPtr& connection,
    const Packet& packet,
    const BusinessRequestContext& business_request
) {
    auto send_client_chat_ack =
    [
        this,
        &connection,
        &packet
    ](
        const ClientChatAck& ack_value
    ) -> bool {
        std::string ack_body;
        std::string serialize_error;


        if (
            !SerializeClientChatAck(
                ack_value,
                &ack_body,
                &serialize_error
            )
        ) {
            LOG_ERROR(
                "gateway serialize client chat ack failed"
                << ", seq="
                << packet.seq
                << ", error="
                << serialize_error
            );


            SendPacket(
                connection,
                MakeErrorPacket(
                    packet.seq,
                    "chat ack serialization failed"
                )
            );


            return false;
        }


        Packet ack;

        ack.type =
            MessageType::kChatAck;

        ack.seq =
            packet.seq;

        ack.body =
            std::move(ack_body);


        return SendPacket(
            connection,
            ack
        );
    };


    ClientChatRequest request;

    std::string error_message;


    if (
        !DeserializeClientChatRequest(
            packet.body,
            &request,
            &error_message
        )
    ) {
        ClientChatAck ack;

        ack.success = false;
        ack.delivered = false;

        ack.reason =
            "invalid_chat_request";


        send_client_chat_ack(
            ack
        );


        LOG_WARN(
            "gateway rejected chat message: "
            "invalid client chat request"
            << ", peer="
            << connection->
                PeerAddress().
                ToString()
            << ", seq="
            << packet.seq
            << ", error="
            << error_message
        );


        return;
    }
    const std::optional<UserId> logged_user_id =
        ResolveBusinessUser(
                session_manager_,
                connection
            );

    if (!logged_user_id.has_value()) {
        ClientChatAck ack;

        ack.success = false;
        ack.delivered = false;

        ack.client_message_id =
            request.client_message_id;

        ack.to_user_id =
            request.to_user_id;

        ack.reason =
            "sender_not_logged_in";


        send_client_chat_ack(
            ack
        );


        LOG_WARN(
            "gateway rejected chat message: "
            "sender not logged in"
            << ", client_message_id="
            << request.client_message_id
            << ", peer="
            << connection->
                PeerAddress().
                ToString()
        );


        return;
    }
    const UserId from_user_id = logged_user_id.value();


    if (
        request.claimed_from_user_id.
            has_value() &&
        request.claimed_from_user_id.
            value() !=
            from_user_id
    ) {
        ClientChatAck ack;

        ack.success = false;
        ack.delivered = false;

        ack.client_message_id =
            request.client_message_id;

        ack.from_user_id =
            from_user_id;

        ack.to_user_id =
            request.to_user_id;

        ack.reason =
            "from_user_mismatch";


        send_client_chat_ack(
            ack
        );


        LOG_WARN(
            "gateway rejected chat message: "
            "from user mismatch"
            << ", login_user_id="
            << from_user_id
            << ", claimed_from="
            << request.
                claimed_from_user_id.
                value()
            << ", client_message_id="
            << request.client_message_id
            << ", peer="
            << connection->
                PeerAddress().
                ToString()
        );


        return;
    }

    const UserId to_user_id = request.to_user_id;

    if (to_user_id == from_user_id) {
        ClientChatAck ack;

        ack.success = false;
        ack.delivered = false;

        ack.client_message_id =
            request.client_message_id;

        ack.from_user_id =
            from_user_id;

        ack.to_user_id =
            to_user_id;

        ack.reason =
            "invalid_to_user";


        send_client_chat_ack(
            ack
        );


        return;
    }

    if (!HasFriendRepository()) {
    Json ack_body;
    ack_body["success"] = false;
    ack_body["delivered"] = false;
    ack_body["client_message_id"] = request.client_message_id;
    ack_body["reason"] = "relation_service_unavailable";
    ack_body["from"] = from_user_id;
    ack_body["to"] = to_user_id;

    Packet ack;
    ack.type = MessageType::kChatAck;
    ack.seq = packet.seq;
    ack.body = ack_body.dump();

    SendPacket(connection, ack);

    LOG_WARN("gateway rejected chat message: relation service unavailable"
             << ", from=" << from_user_id
             << ", to=" << to_user_id);

        return;
    }

    const ChatPermissionResult permission_result =
        friend_repository_->CheckPrivateChatPermission(
            from_user_id,
            to_user_id
        );

    if (!permission_result.Allowed()) {
        Json ack_body;
        ack_body["success"] = false;
        ack_body["delivered"] = false;
        ack_body["client_message_id"] = request.client_message_id;
        ack_body["reason"] =
            ChatPermissionStatusToString(permission_result.status);
        ack_body["message"] = permission_result.message;
        ack_body["from"] = from_user_id;
        ack_body["to"] = to_user_id;
        ack_body["stored_offline"] = false;
        ack_body["stored_persistent"] = false;
        ack_body["receiver_private_unread"] = 0;
        ack_body["receiver_total_unread"] = 0;

        Packet ack;
        ack.type = MessageType::kChatAck;
        ack.seq = packet.seq;
        ack.body = ack_body.dump();

        SendPacket(connection, ack);

        LOG_WARN("gateway rejected chat message: permission denied"
                << ", from=" << from_user_id
                << ", to=" << to_user_id
                << ", reason="
                << ChatPermissionStatusToString(permission_result.status)
                << ", message=" << permission_result.message);

        return;
    }
    Json server_body{
        {
            "from",
            from_user_id
        },
        {
            "to",
            to_user_id
        },
        {
            "text",
            request.text
        }
    };

    const std::string
        server_body_text =
            server_body.dump();

    const auto persist_message_via_service =
        [&]() -> rpc::PersistPrivateMessageRpcCallResult {
            if (!HasMessageRpcClient()) {
                return rpc::PersistPrivateMessageRpcCallResult::Failure(
                    rpc::RpcErrorCode::kUnavailable,
                    "MessageService RPC client is unavailable",
                    false
                );
            }

            rpc::PersistPrivateMessageRpcRequest rpc_request;
            rpc_request.from_user_id = from_user_id;
            rpc_request.to_user_id = to_user_id;
            rpc_request.client_message_id = request.client_message_id;
            rpc_request.message_type =
                static_cast<std::uint32_t>(rpc::PrivateMessageContentType::kText);
            rpc_request.content = server_body_text;

            const std::uint64_t rpc_id =
                next_internal_rpc_id_.fetch_add(
                    1,
                    std::memory_order_relaxed
                );

            rpc::RpcCallOptions call_options;
            call_options.request_id =
                options_.gateway_id +
                ":message:persist:req:" +
                std::to_string(rpc_id);
            call_options.trace_id =
                options_.gateway_id +
                ":message-persist:trace:" +
                std::to_string(rpc_id) +
                ":client-seq:" +
                std::to_string(packet.seq);
            call_options.caller_service = "gateway";
            call_options.caller_instance = options_.gateway_id;

            const auto remaining = business_request.RemainingTime();
            call_options.remaining_timeout =
                remaining == std::chrono::milliseconds::max()
                    ? std::chrono::milliseconds{0}
                    : remaining;

            return message_rpc_client_->PersistPrivateMessage(
                rpc_request,
                call_options
            );
        };

    const auto persistence_failure_reason =
        [](
            const rpc::PersistPrivateMessageRpcCallResult& result
        ) -> const char* {
            if (result.attempted) {
                return "message_persistence_uncertain";
            }

            switch (result.status.code) {
                case rpc::RpcErrorCode::kDeadlineExceeded:
                    return "message_persistence_deadline_expired";
                case rpc::RpcErrorCode::kUnavailable:
                    return "message_persistence_unavailable";
                case rpc::RpcErrorCode::kInvalidArgument:
                    return "message_persistence_failed";
                default:
                    return "message_persistence_failed";
            }
        };

    if (HasGatewayRouteResolver()) {
        const GatewayRouteResult route =
            gateway_route_resolver_->Resolve(
                to_user_id
            );

        /*
        * 目标在线于其他 Gateway。
        *
        * 这一阶段还没有 Gateway-to-Gateway
        * Transport，所以不能真正发送。
        *
        * 但最重要的是：
        * 不能再把它错误当成离线用户。
        */
        if (route.IsRemote()) {
            if (
                !route.remote_gateway.
                    has_value()
            ) {
                Packet ack;

                ack.type =
                    MessageType::kChatAck;

                ack.seq =
                    packet.seq;

                ack.body =
                    Json{
                        {"success", false},
                        {"delivered", false},
                        {"stored_offline", false},
                        {"stored_persistent", false},
                        {"message_id", 0},
                        {"from", from_user_id},
                        {"to", to_user_id},
                        {"reason",
                        "remote_gateway_missing"}
                    }.dump();

                SendPacket(
                    connection,
                    ack
                );

                return;
            }


            const GatewayInstanceRecord
                remote_gateway =
                    route.remote_gateway.value();


            /*
            * 1. 先做持久化。
            *
            * 跨节点投递开始时先保存Pending，
            * 防止网络投递过程中消息直接丢失。
            */
            std::uint64_t server_message_id = 0;

            bool stored_persistent = false;

            /*
            * false:
            * 当前Client发送首次创建了服务端消息。
            *
            * true:
            * 当前Client请求是对已经存在业务消息的Retry。
            */
            bool client_request_reused = false;

            const auto persist_call =
                persist_message_via_service();

            if (!persist_call.ok()) {
                ClientChatAck ack;
                ack.success = false;
                ack.delivered = false;
                ack.client_message_id = request.client_message_id;
                ack.from_user_id = from_user_id;
                ack.to_user_id = to_user_id;
                ack.remote_gateway_id = remote_gateway.gateway_id;
                ack.remote_host = remote_gateway.listen_host;
                ack.remote_port = remote_gateway.listen_port;
                ack.reason = persistence_failure_reason(persist_call);

                send_client_chat_ack(ack);

                LOG_WARN(
                    "gateway remote MessageService persistence failed"
                    << ", from=" << from_user_id
                    << ", to=" << to_user_id
                    << ", client_message_id=" << request.client_message_id
                    << ", attempted=" << persist_call.attempted
                    << ", rpc_status="
                    << static_cast<int>(persist_call.status.code)
                    << ", reason=" << ack.reason
                    << ", message=" << persist_call.status.message
                );
                return;
            }

            const auto& persist_result = *persist_call.value;
            if (persist_result.Conflict()) {
                ClientChatAck ack;
                ack.success = false;
                ack.delivered = false;
                ack.client_message_id = request.client_message_id;
                ack.message_id = persist_result.message_id;
                ack.from_user_id = from_user_id;
                ack.to_user_id = to_user_id;
                ack.remote_gateway_id = remote_gateway.gateway_id;
                ack.remote_host = remote_gateway.listen_host;
                ack.remote_port = remote_gateway.listen_port;
                ack.reason = "client_message_id_conflict";
                send_client_chat_ack(ack);

                LOG_WARN(
                    "gateway remote MessageService idempotency conflict"
                    << ", from=" << from_user_id
                    << ", to=" << to_user_id
                    << ", client_message_id=" << request.client_message_id
                    << ", existing_message_id=" << persist_result.message_id
                );
                return;
            }

            server_message_id = persist_result.message_id;
            stored_persistent = true;
            client_request_reused = persist_result.Reused();

            LOG_INFO(
                "gateway remote MessageService persistence accepted"
                << ", client_message_id=" << request.client_message_id
                << ", message_id=" << server_message_id
                << ", created=" << persist_result.Created()
                << ", reused=" << persist_result.Reused()
                << ", from=" << from_user_id
                << ", to=" << to_user_id
            );

            std::int64_t receiver_private_unread = 0;
            std::int64_t receiver_total_unread = 0;

            const bool should_count_as_unread =
                persist_result.record.delivery_state !=
                    rpc::MessageDeliveryState::kRead;

            EnsureUnreadProjection(
                server_message_id,
                to_user_id,
                from_user_id,
                should_count_as_unread,
                &receiver_private_unread,
                &receiver_total_unread
            );

            if (
                !HasGatewayPeerTransportManager()
            ) {
                Packet ack;

                ack.type =
                    MessageType::kChatAck;

                ack.seq =
                    packet.seq;

                const bool accepted =
                    stored_persistent;

                ack.body =
                    Json{
                        {"success", accepted},
                        {"delivered", false},
                        {"stored_offline",
                        stored_persistent},
                        {"stored_persistent",
                        stored_persistent},
                        {"client_message_id",
                        request.client_message_id},
                        {"reused",
                        client_request_reused},
                        {"message_id",
                        server_message_id},
                        {"from", from_user_id},
                        {"to", to_user_id},
                        {"remote_gateway_id",
                        remote_gateway.gateway_id},
                        {"reason",
                        stored_persistent
                            ? "remote_transport_unavailable_stored_pending"
                            : "remote_transport_unavailable"},
                        {"receiver_private_unread",
                        receiver_private_unread},
                        {"receiver_total_unread",
                        receiver_total_unread}
                    }.dump();

                SendPacket(
                    connection,
                    ack
                );

                return;
            }


            EventLoop* sender_loop =
                connection
                    ? connection->GetLoop()
                    : nullptr;

            const SessionEpoch sender_session_epoch =
                g_business_dispatch_context.session_epoch;


            const bool submitted =
                gateway_peer_transport_manager_->
                    ForwardChat(
                        remote_gateway,
                        server_message_id,
                        from_user_id,
                        to_user_id,
                        server_body_text,

                        [
                            this,
                            connection,
                            sender_loop,
                            from_user_id,
                            sender_session_epoch,
                            to_user_id,
                            remote_gateway,
                            server_message_id,
                            stored_persistent,
                            receiver_private_unread,
                            receiver_total_unread,

                            client_message_id =
                                request.client_message_id,

                            client_request_reused,

                            original_seq =
                                packet.seq
                        ](
                            GatewayPeerTransportResult
                                result
                        ) mutable {

                            auto complete =
                                [
                                    this,
                                    connection,
                                    from_user_id,
                                    sender_session_epoch,
                                    to_user_id,
                                    remote_gateway,
                                    server_message_id,
                                    stored_persistent,
                                    receiver_private_unread,
                                    receiver_total_unread,

                                    client_message_id,
                                    client_request_reused,

                                    original_seq,
                                    result =
                                        std::move(result)
                                ](
                                    const BusinessRequestContext*
                                        completion_request
                                ) mutable {

                                    /*
                                    * Peer Response只能证明：
                                    *
                                    * Gateway B是否接受/提交了Receiver Delivery。
                                    *
                                    * Receiver是否真正确认，
                                    * 必须以共享MySQL的ReceiverConfirmed状态为准。
                                    */
                                    bool peer_delivery_submitted =
                                        false;


                                    bool receiver_confirmed =
                                        false;


                                    bool stored_offline =
                                        false;


                                    std::string reason;


                                    if (
                                        result.Succeeded() &&
                                        result.response.
                                            Delivered()
                                    ) {
                                        /*
                                        * Peer层的kDelivered目前是历史命名。
                                        *
                                        * 它只证明：
                                        *
                                        * Gateway B已经接受并提交了
                                        * Receiver Delivery。
                                        *
                                        * 不能直接映射为Sender delivered=true。
                                        */
                                        peer_delivery_submitted =
                                            true;


                                        reason =
                                            "remote_delivery_awaiting_receiver_ack";


                                        /*
                                        * A和B共享MySQL。
                                        *
                                        * 如果Receiver ACK非常快，
                                        * B可能已经把DB推进到ReceiverConfirmed。
                                        *
                                        * 因此这里读取一次最新Durable Truth。
                                        *
                                        * 不等待、不轮询Receiver ACK。
                                        */
                                        if (
                                            stored_persistent &&
                                            server_message_id != 0 &&
                                            HasMessageRpcClient() &&
                                            completion_request != nullptr
                                        ) {
                                            rpc::GetPrivateMessageRpcRequest
                                                get_request;
                                            get_request.message_id =
                                                server_message_id;

                                            rpc::RpcCallOptions get_options;
                                            const std::uint64_t probe_rpc_id =
                                                next_internal_rpc_id_.fetch_add(
                                                    1,
                                                    std::memory_order_relaxed
                                                );
                                            get_options.request_id =
                                                options_.gateway_id +
                                                ":message:remote-probe:req:" +
                                                std::to_string(probe_rpc_id);
                                            get_options.trace_id =
                                                options_.gateway_id +
                                                ":remote-probe:trace:" +
                                                std::to_string(probe_rpc_id) +
                                                ":M:" +
                                                std::to_string(
                                                    server_message_id);
                                            get_options.caller_service =
                                                "gateway";
                                            get_options.caller_instance =
                                                options_.gateway_id;

                                            const auto remaining =
                                                completion_request->
                                                    RemainingTime();
                                            get_options.remaining_timeout =
                                                remaining ==
                                                    std::chrono::milliseconds::max()
                                                    ? std::chrono::milliseconds{0}
                                                    : remaining;

                                            const auto confirmation_result =
                                                message_rpc_client_->
                                                    GetPrivateMessage(
                                                        get_request,
                                                        get_options
                                                    );

                                            if (confirmation_result.ok()) {
                                                const auto state =
                                                    confirmation_result.value->
                                                        record.delivery_state;
                                                receiver_confirmed =
                                                    state == rpc::MessageDeliveryState::
                                                        kReceiverConfirmed ||
                                                    state == rpc::MessageDeliveryState::
                                                        kRead;
                                                if (receiver_confirmed) {
                                                    reason =
                                                        "remote_receiver_confirmed";
                                                }
                                            } else {
                                                // Persisted responsibility is
                                                // already established. A failed
                                                // read probe may only make the
                                                // delivered bit conservative; it
                                                // must never revoke success.
                                                LOG_WARN(
                                                    "gateway remote receiver "
                                                    "confirmation RPC lookup failed"
                                                    << ", message_id="
                                                    << server_message_id
                                                    << ", from="
                                                    << from_user_id
                                                    << ", to="
                                                    << to_user_id
                                                    << ", remote_gateway="
                                                    << remote_gateway.gateway_id
                                                    << ", error="
                                                    << confirmation_result.
                                                        status.message
                                                );
                                            }
                                        }
                                    } else {
                                        /*
                                        * Peer没有成功接受本地Delivery。
                                        *
                                        * 如果消息已经持久化，
                                        * Server仍然承担后续恢复责任。
                                        */
                                        stored_offline =
                                            stored_persistent;


                                        if (result.Succeeded()) {
                                            reason =
                                                "remote_" +
                                                GatewayForwardChatStatusToString(
                                                    result.response.status
                                                );
                                        } else {
                                            reason =
                                                "remote_transport_" +
                                                GatewayPeerTransportStatusToString(
                                                    result.status
                                                );
                                        }
                                    }


                                    /*
                                    * 没有MySQL时，只有真实投递成功
                                    * 才认为消息被接受并计Unread。
                                    */
                                    std::int64_t
                                        private_unread =
                                            receiver_private_unread;

                                    std::int64_t
                                        total_unread =
                                            receiver_total_unread;



                                    const bool accepted =
                                        peer_delivery_submitted ||
                                        stored_persistent;


                                    Json ack_body;

                                    ack_body["success"] =
                                        accepted;

                                    /*
                                    * Sender delivered只代表：
                                    *
                                    * Receiver Application ACK
                                    *
                                    * 不能再代表Peer Gateway已经Send。
                                    */
                                    ack_body["delivered"] =
                                        receiver_confirmed;

                                    ack_body["stored_offline"] =
                                        stored_offline;

                                    ack_body[
                                        "stored_persistent"
                                    ] =
                                        stored_persistent;


                                    /*
                                    * M12 Client Send Idempotency
                                    *
                                    * client_message_id：
                                    * Client一次逻辑发送的稳定业务ID。
                                    *
                                    * reused：
                                    * 当前Client Request是否复用了
                                    * 已经存在的server message。
                                    */
                                    ack_body["client_message_id"] =
                                        client_message_id;

                                    ack_body["reused"] =
                                        client_request_reused;


                                    ack_body["message_id"] =
                                        server_message_id;

                                    ack_body["from"] =
                                        from_user_id;

                                    ack_body["to"] =
                                        to_user_id;

                                    ack_body[
                                        "remote_gateway_id"
                                    ] =
                                        remote_gateway.
                                            gateway_id;

                                    ack_body[
                                        "remote_host"
                                    ] =
                                        remote_gateway.
                                            listen_host;

                                    ack_body[
                                        "remote_port"
                                    ] =
                                        remote_gateway.
                                            listen_port;

                                    ack_body["reason"] =
                                        reason;

                                    ack_body[
                                        "receiver_private_unread"
                                    ] =
                                        private_unread;

                                    ack_body[
                                        "receiver_total_unread"
                                    ] =
                                        total_unread;


                                    Packet ack;

                                    ack.type =
                                        MessageType::
                                            kChatAck;

                                    ack.seq =
                                        original_seq;

                                    ack.body =
                                        ack_body.dump();

                                    SendPacket(
                                        connection,
                                        ack
                                    );


                                    LOG_INFO(
                                        "gateway remote chat completed"
                                        << ", from="
                                        << from_user_id
                                        << ", to="
                                        << to_user_id
                                        << ", remote_gateway="
                                        << remote_gateway.gateway_id
                                        << ", peer_delivery_submitted="
                                        << peer_delivery_submitted
                                        << ", receiver_confirmed="
                                        << receiver_confirmed
                                        << ", reason="
                                        << reason
                                    );
                                };


                            /*
                             * M13-B2:
                             * Peer callback本身运行在Peer Reactor。
                             * complete内部可能读取MySQL durable truth，
                             * 因此不能再把它投回Client Sub-Reactor执行。
                             *
                             * SendPacket最终会通过TcpConnection::Send
                             * 安全handoff到connection所属EventLoop。
                             */
                            if (HasBusinessExecutor()) {
                                BusinessExecutor::TaskSpec completion_task;
                                completion_task.request.operation =
                                    "gateway.chat.remote_complete";
                                completion_task.request.user_id =
                                    from_user_id;
                                completion_task.request.ordering_key =
                                    static_cast<BusinessOrderingKey>(
                                        server_message_id
                                    );
                                /*
                                 * This task is not a second client-scoped
                                 * request.  PersistPrivateMessage has already
                                 * committed M before peer forwarding starts,
                                 * so peer response-loss/retry belongs to the
                                 * durable kMustRun continuation of that accepted
                                 * Chat.  Reusing the original Chat deadline here
                                 * would reject this continuation exactly when the
                                 * first peer response times out (3s) and the
                                 * bounded retry starts.  Let BusinessExecutor
                                 * assign a fresh internal continuation budget;
                                 * SendPacket still enforces SessionEpoch before
                                 * any client-visible ACK is emitted.
                                 */
                                completion_task.cancellation_policy =
                                    BusinessCancellationPolicy::kMustRun;
                                completion_task.work =
                                    [
                                        complete = std::move(complete),
                                        connection,
                                        from_user_id,
                                        sender_session_epoch
                                    ](
                                        const BusinessExecutor::ExecutionContext&
                                            context
                                    ) mutable -> BusinessExecutor::Completion {
                                        ScopedBusinessDispatchContext dispatch_scope(
                                            connection,
                                            from_user_id,
                                            sender_session_epoch
                                        );
                                        complete(&context.Request());
                                        return {};
                                    };

                                const BusinessSubmitStatus completion_status =
                                    business_executor_->Submit(
                                        std::move(completion_task)
                                    );

                                if (
                                    completion_status !=
                                    BusinessSubmitStatus::kAccepted
                                ) {
                                    LOG_WARN(
                                        "gateway remote chat completion task rejected"
                                        << ", message_id="
                                        << server_message_id
                                        << ", status="
                                        << BusinessSubmitStatusToString(
                                            completion_status
                                        )
                                    );
                                }
                            } else {
                                complete(nullptr);
                            }
                        }
                    );


            if (!submitted) {
                Packet ack;

                ack.type =
                    MessageType::kChatAck;

                ack.seq =
                    packet.seq;

                ack.body =
                    Json{
                        {"success",
                        stored_persistent},
                        {"delivered", false},
                        {"stored_offline",
                        stored_persistent},
                        {"stored_persistent",
                        stored_persistent},
                        {"message_id",
                        server_message_id},
                        {"from", from_user_id},
                        {"to", to_user_id},
                        {"remote_gateway_id",
                        remote_gateway.gateway_id},
                        {"reason",
                        stored_persistent
                            ? "remote_submit_failed_stored_pending"
                            : "remote_submit_failed"},
                        {"receiver_private_unread",
                        receiver_private_unread},
                        {"receiver_total_unread",
                        receiver_total_unread}
                    }.dump();

                SendPacket(
                    connection,
                    ack
                );
            }

            return;
        }
        /*
        * OnlineStatus 还存在，
        * 但对应 Gateway 已从 Discovery 消失。
        *
        * 当前把它继续交给原来的
        * pending/offline persistence 逻辑。
        */
        if (
            route.status ==
            GatewayRouteStatus::
                kGatewayUnavailable
        ) {
            LOG_WARN(
                "gateway target route unavailable"
                << ", from="
                << from_user_id
                << ", to="
                << to_user_id
                << ", error="
                << route.error_message
            );
        }

        /*
        * Redis / Resolver 暂时错误时，
        * 不直接拒绝当前单机能力，
        * 继续使用原 SessionManager 路径。
        */
        if (
            !route.Resolved() &&
            route.status !=
                GatewayRouteStatus::
                    kGatewayUnavailable
        ) {
            LOG_WARN(
                "gateway route resolve failed, "
                "fallback to local session path"
                << ", from="
                << from_user_id
                << ", to="
                << to_user_id
                << ", status="
                << GatewayRouteStatusToString(
                    route.status
                )
                << ", error="
                << route.error_message
            );
        }
    }

       /*
     * ============================================================
     * M12-v1.1-f-2-a
     * Same-Gateway Local Sequential Idempotency
     * ============================================================
     */

    TcpConnectionPtr target_connection =
        session_manager_.
            FindConnection(
                to_user_id
            );


    /*
     * target_online：
     *
     * 只表示当前Gateway上存在一个
     * 可用的Receiver Connection。
     *
     * 注意：
     *
     * target_online != delivered
     *
     * 在线并不能说明消息已经真正完成Push。
     */
    const bool target_online =
        target_connection &&
        target_connection->
            IsConnected();


    bool delivered = false;

    bool stored_offline = false;

    bool stored_persistent = false;

    bool client_request_reused = false;

    std::uint64_t
        server_message_id = 0;


    /*
     * ============================================================
     * 1. Reliable Client Send必须有持久化Repository
     * ============================================================
     *
     * M12要求：
     *
     * same
     * (from_user_id, client_message_id)
     *
     * 必须映射到same server_message_id。
     *
     * 没有MySQL UNIQUE约束时无法保证该语义，
     * 因此这里不再静默降级到纯内存发送。
     */
    const auto persist_call =
        persist_message_via_service();

    if (!persist_call.ok()) {
        ClientChatAck ack;
        ack.success = false;
        ack.delivered = false;
        ack.client_message_id = request.client_message_id;
        ack.from_user_id = from_user_id;
        ack.to_user_id = to_user_id;
        ack.reason = persistence_failure_reason(persist_call);
        send_client_chat_ack(ack);

        LOG_WARN(
            "gateway local MessageService persistence failed"
            << ", client_message_id=" << request.client_message_id
            << ", from=" << from_user_id
            << ", to=" << to_user_id
            << ", attempted=" << persist_call.attempted
            << ", rpc_status="
            << static_cast<int>(persist_call.status.code)
            << ", reason=" << ack.reason
            << ", message=" << persist_call.status.message
        );
        return;
    }

    const auto& persist_result = *persist_call.value;

    if (persist_result.Conflict()) {
        ClientChatAck ack;
        ack.success = false;
        ack.delivered = false;
        ack.client_message_id = request.client_message_id;
        ack.message_id = persist_result.message_id;
        ack.from_user_id = from_user_id;
        ack.to_user_id = to_user_id;
        ack.reason = "client_message_id_conflict";
        send_client_chat_ack(ack);

        LOG_WARN(
            "gateway local MessageService idempotency conflict"
            << ", client_message_id=" << request.client_message_id
            << ", from=" << from_user_id
            << ", to=" << to_user_id
            << ", existing_message_id=" << persist_result.message_id
        );
        return;
    }

    server_message_id = persist_result.message_id;
    stored_persistent = true;
    client_request_reused = persist_result.Reused();

    LOG_INFO(
        "gateway local MessageService persistence accepted"
        << ", client_message_id=" << request.client_message_id
        << ", message_id=" << server_message_id
        << ", created=" << persist_result.Created()
        << ", reused=" << persist_result.Reused()
        << ", from=" << from_user_id
        << ", to=" << to_user_id
    );

    /*
     * ============================================================
     * M14-C2: Idempotent Unread Projection must run before any
     * local Reused early-return.
     * ============================================================
     *
     * Critical uncertainty recovery case:
     *
     *   DB COMMIT(M)
     *       -> Persist RPC response lost
     *       -> first Gateway attempt never projected unread
     *       -> receiver may confirm M
     *       -> sender retries same C
     *       -> MessageService returns Reused + same M
     *
     * If we returned early for Reused+ReceiverConfirmed before this
     * gate, the Redis projection could remain permanently missing.
     * Therefore every accepted durable result, including Reused/Read,
     * passes through the stable-M idempotent projection gate. Duplicate
     * applications of the same M are suppressed by the projection marker;
     * Read records create a processed marker without incrementing counters.
     */
    std::int64_t receiver_private_unread = 0;
    std::int64_t receiver_total_unread = 0;

    const bool should_count_as_unread =
        persist_result.record.delivery_state !=
            rpc::MessageDeliveryState::kRead;

    EnsureUnreadProjection(
        server_message_id,
        to_user_id,
        from_user_id,
        should_count_as_unread,
        &receiver_private_unread,
        &receiver_total_unread
    );

    /*
     * ============================================================
     * 5. 判断持久消息过去是否已经完成Delivery
     * ============================================================
     *
     * PrivateMessageRecord.delivery_status
     * 当前是uint32_t。
     */
    const std::uint32_t
        persisted_delivery_status =
            MessageDeliveryStateToLegacyStatus(
                persist_result.record.delivery_state
            );


    const bool already_receiver_confirmed =
        persist_result.record.delivery_state ==
            rpc::MessageDeliveryState::kReceiverConfirmed ||
        persist_result.record.delivery_state ==
            rpc::MessageDeliveryState::kRead;


    /*
     * ============================================================
     * 6. 顺序Retry：
     *
     * Reused + Delivered/Read
     *
     * 表示这条逻辑业务消息过去已经完成过Push。
     *
     * 此时绝对不能：
     *
     * SendPacket(receiver)
     * IncrementUnread()
     * ============================================================
     */
    if (
        client_request_reused &&
        already_receiver_confirmed
    ){
        delivered = true;


        const std::int64_t
            receiver_private_unread =
                GetPrivateUnread(
                    to_user_id,
                    from_user_id
                );


        const std::int64_t
            receiver_total_unread =
                GetTotalUnread(
                    to_user_id
                );


        ClientChatAck ack;

        ack.success = true;

        ack.delivered = true;

        ack.stored_offline = false;

        ack.stored_persistent = true;

        ack.reused = true;

        ack.client_message_id =
            request.client_message_id;

        ack.message_id =
            server_message_id;

        ack.from_user_id =
            from_user_id;

        ack.to_user_id =
            to_user_id;

        ack.receiver_private_unread =
            receiver_private_unread;

        ack.receiver_total_unread =
            receiver_total_unread;

        ack.reason =
            "local_already_receiver_confirmed";


        send_client_chat_ack(
            ack
        );


        LOG_INFO(
            "gateway suppressed already "
            "receiver-confirmed local client retry"
            << ", client_message_id="
            << request.client_message_id
            << ", message_id="
            << server_message_id
            << ", persisted_status="
            << persisted_delivery_status
            << ", from="
            << from_user_id
            << ", to="
            << to_user_id
        );


        return;
    }


    /*
     * 到这里意味着：
     *
     * Created + Pending
     *
     * 或
     *
     * Reused + Pending
     *
     * 都需要继续尝试推进Delivery。
     */


     /*
    * 当前请求是否发现：
    *
    * 同一server message已经在本进程内
    * 完成过Receiver-visible Push。
    *
    * 典型场景：
    *
    * SendPacket成功
    *     ↓
    * DB MarkDelivered失败
    *     ↓
    * Memory已经Delivered
    *     ↓
    * Client Retry
    *
    * 这时绝不能重新Push，
    * 但可以重新尝试修复MySQL状态。
    */
    bool local_delivery_already_completed = false;

    /*
     * ============================================================
     * 7. Local Online Delivery
     * ============================================================
     */
        /*
     * ============================================================
     * 7. Local Online Delivery Ownership
     * ============================================================
     *
     * MySQL UNIQUE解决：
     *
     * 一次Logical Send只能创建一个server message。
     *
     * 这里解决：
     *
     * 同一个server message同一时刻
     * 只能有一个执行者真正调用SendPacket。
     */
    if (target_online) {
        const
            MessageDeliveryDedupBeginStatus
            delivery_begin_status =
                message_delivery_deduplicator_.
                    Begin(
                        server_message_id
                    );


        /*
         * ========================================================
         * 7.1 当前线程获得唯一Delivery Ownership
         * ========================================================
         */
        if (
            delivery_begin_status ==
            MessageDeliveryDedupBeginStatus::
                kAcquired
        ) {
            LOG_INFO(
                "gateway local delivery ownership acquired"
                << ", client_message_id="
                << request.client_message_id
                << ", message_id="
                << server_message_id
                << ", from="
                << from_user_id
                << ", to="
                << to_user_id
            );


            /*
            * ============================================================
            * M12-v1.4:
            * Build independent Gateway -> Receiver Delivery
            * ============================================================
            *
            * 这里绝对不能继续：
            *
            * receiver_packet.seq = sender_packet.seq
            *
            * Receiver Delivery属于新的Network Attempt Domain。
            */

            Packet forward_packet;

            std::string
                delivery_submit_error;


            const
                ReceiverDeliverySubmitStatus
                delivery_submit_status =
                    SubmitReceiverChatDelivery(
                        target_connection,
                        server_message_id,
                        from_user_id,
                        to_user_id,
                        request.text,
                        &forward_packet,
                        &delivery_submit_error
                    );


            const bool
                receiver_already_confirmed =
                    delivery_submit_status ==
                    ReceiverDeliverySubmitStatus::
                        kAlreadyConfirmed;


            delivered =
                delivery_submit_status ==
                    ReceiverDeliverySubmitStatus::
                        kSubmitted ||
                receiver_already_confirmed;


            if (
                delivery_submit_status ==
                ReceiverDeliverySubmitStatus::
                    kSubmitted
            ) {
                LOG_INFO(
                    "gateway submitted tracked local "
                    "receiver chat delivery"
                    << ", client_message_id="
                    << request.client_message_id
                    << ", message_id="
                    << server_message_id
                    << ", delivery_seq="
                    << forward_packet.seq
                    << ", from="
                    << from_user_id
                    << ", to="
                    << to_user_id
                );
            } else if (
                receiver_already_confirmed
            ) {
                /*
                * Tracker已经确认过该消息，
                * 不应该重新Push。
                */
                local_delivery_already_completed =
                    true;


                LOG_INFO(
                    "gateway suppressed local receiver "
                    "delivery already confirmed by tracker"
                    << ", client_message_id="
                    << request.client_message_id
                    << ", message_id="
                    << server_message_id
                    << ", from="
                    << from_user_id
                    << ", to="
                    << to_user_id
                );
            }

            /*
             * SendPacket返回false：
             *
             * 当前实现意味着：
             *
             * - connection不可用
             * 或
             * - Protocol Encode失败
             *
             * 尚未调用connection->Send()，
             * 因此可以安全释放Ownership。
             */
            if (!delivered) {
                const bool aborted =
                    message_delivery_deduplicator_.
                        Abort(
                            server_message_id
                        );


                if (!aborted) {
                    LOG_ERROR(
                        "gateway local delivery "
                        "ownership abort failed"
                        << ", client_message_id="
                        << request.client_message_id
                        << ", message_id="
                        << server_message_id
                        << ", from="
                        << from_user_id
                        << ", to="
                        << to_user_id
                    );
                }


                LOG_WARN(
                    "gateway local tracked receiver "
                    "delivery submission failed and "
                    "ownership released"
                    << ", client_message_id="
                    << request.client_message_id
                    << ", message_id="
                    << server_message_id
                    << ", from="
                    << from_user_id
                    << ", to="
                    << to_user_id
                    << ", submit_status="
                    << static_cast<int>(
                        delivery_submit_status
                    )
                    << ", error="
                    << delivery_submit_error
                );
            } else {
                /*
                * 当前存在两种成功语义：
                *
                * 1. kSubmitted
                *
                *    Delivery Attempt已经登记，
                *    并提交给TcpConnection。
                *
                * 2. kAlreadyConfirmed
                *
                *    Tracker已经知道Receiver确认过M，
                *    因此无需再次Send。
                *
                * 当前F2阶段暂时继续保留原有
                * durable MarkDelivered行为。
                *
                * F6会把真正的持久化Delivered推进点
                * 移到Receiver ACK Handler。
                */





                /*
                 * 无论MySQL MarkDelivered成功与否，
                 * SendPacket已经成功以后，
                 * 当前进程必须记住：
                 *
                 * 这条消息已经产生过投递副作用。
                 */
                const bool memory_marked =
                    message_delivery_deduplicator_.
                        MarkDelivered(
                            server_message_id
                        );


                if (!memory_marked) {
                    LOG_ERROR(
                        "gateway local message pushed "
                        "but memory delivery state "
                        "advance failed"
                        << ", client_message_id="
                        << request.client_message_id
                        << ", message_id="
                        << server_message_id
                        << ", from="
                        << from_user_id
                        << ", to="
                        << to_user_id
                    );
                }


                LOG_INFO(
                    "gateway local receiver delivery "
                    "submitted and awaiting receiver ack"
                    << ", client_message_id="
                    << request.client_message_id
                    << ", message_id="
                    << server_message_id
                    << ", memory_send_side_effect_marked="
                    << memory_marked
                    << ", from="
                    << from_user_id
                    << ", to="
                    << to_user_id
                );
            }
        }


        /*
         * ========================================================
         * 7.2 同一条Message正在由其他线程投递
         * ========================================================
         *
         * 最关键原则：
         *
         * 当前线程没有Execution Ownership，
         * 因此绝对不能调用SendPacket。
         */
        else if (
            delivery_begin_status ==
            MessageDeliveryDedupBeginStatus::
                kAlreadyProcessing
        ) {
            const std::int64_t
                receiver_private_unread =
                    GetPrivateUnread(
                        to_user_id,
                        from_user_id
                    );


            const std::int64_t
                receiver_total_unread =
                    GetTotalUnread(
                        to_user_id
                    );


            ClientChatAck ack;

            /*
             * 消息已经成功进入MySQL，
             * 所以Logical Send已经被Server接受。
             */
            ack.success = true;

            /*
             * 但当前线程不能确认Owner
             * 最终是否已经完成Delivery。
             */
            ack.delivered = false;

            ack.stored_offline = false;

            ack.stored_persistent = true;

            ack.reused =
                client_request_reused;

            ack.client_message_id =
                request.client_message_id;

            ack.message_id =
                server_message_id;

            ack.from_user_id =
                from_user_id;

            ack.to_user_id =
                to_user_id;

            ack.receiver_private_unread =
                receiver_private_unread;

            ack.receiver_total_unread =
                receiver_total_unread;

            ack.reason =
                "local_delivery_in_progress";


            send_client_chat_ack(
                ack
            );


            LOG_INFO(
                "gateway suppressed concurrent "
                "local delivery attempt"
                << ", client_message_id="
                << request.client_message_id
                << ", message_id="
                << server_message_id
                << ", from="
                << from_user_id
                << ", to="
                << to_user_id
            );


            return;
        }


        /*
         * ========================================================
         * 7.3 当前进程已经完成过这条消息的投递
         * ========================================================
         *
         * 这里最典型的是：
         *
         * 第一次Send成功
         *     ↓
         * MySQL MarkDelivered失败
         *     ↓
         * Memory MarkDelivered成功
         *     ↓
         * Retry
         *
         * DB仍Pending，但内存明确知道：
         * 不能再次Push。
         */


        else if (
            delivery_begin_status ==
            MessageDeliveryDedupBeginStatus::
                kAlreadyDelivered
        ) {
            /*
            * 注意：
            *
            * 这里的AlreadyDelivered是
            * MessageDeliveryDeduplicator历史命名。
            *
            * 新语义仅表示：
            *
            * Receiver-visible Send Side Effect
            * 已经提交过。
            *
            * 并不代表ReceiverConfirmed。
            */
            delivered = true;

            local_delivery_already_completed =
                true;


            LOG_INFO(
                "gateway suppressed duplicate local "
                "receiver send by memory ownership state"
                << ", client_message_id="
                << request.client_message_id
                << ", message_id="
                << server_message_id
                << ", receiver_confirmation="
                "still determined by tracker/mysql"
                << ", from="
                << from_user_id
                << ", to="
                << to_user_id
            );
        }


        /*
         * ========================================================
         * 7.4 理论上不应发生
         * ========================================================
         *
         * server_message_id来自数据库，
         * 正常情况下一定 > 0。
         */
        else {
            ClientChatAck ack;

            ack.success = false;

            ack.delivered = false;

            ack.stored_offline = false;

            ack.stored_persistent = true;

            ack.reused =
                client_request_reused;

            ack.client_message_id =
                request.client_message_id;

            ack.message_id =
                server_message_id;

            ack.from_user_id =
                from_user_id;

            ack.to_user_id =
                to_user_id;

            ack.reason =
                "invalid_local_delivery_message_id";


            send_client_chat_ack(
                ack
            );


            LOG_ERROR(
                "gateway local delivery ownership "
                "begin rejected invalid message id"
                << ", client_message_id="
                << request.client_message_id
                << ", message_id="
                << server_message_id
                << ", from="
                << from_user_id
                << ", to="
                << to_user_id
            );


            return;
        }
    }


    /*
     * ============================================================
     * 8. Offline / Local Push Failed
     * ============================================================
     *
     * 只要消息已经持久化为Pending，
     * 后续Receiver登录时
     * PushPersistentOfflineMessages()
     * 可以继续恢复。
     *
     * 因此MySQL是当前可靠离线消息事实源。
     */
    if (!delivered) {
        stored_offline =
            stored_persistent;
    }


    /*
    * ============================================================
    * 10. Receiver Confirmation Snapshot
    * ============================================================
    *
    * DB快照可能还是Pending，
    * 但Receiver ACK有可能在本次处理过程中
    * 已经从另一个Sub-Reactor到达。
    */
    bool receiver_confirmed_now =
        already_receiver_confirmed;


    ReceiverDeliverySnapshot
        receiver_delivery_snapshot;


    if (
        receiver_delivery_tracker_.
            GetSnapshot(
                server_message_id,
                &receiver_delivery_snapshot
            ) &&
        receiver_delivery_snapshot.confirmed
    ) {
        receiver_confirmed_now = true;
    }


    /*
    * ============================================================
    * 11. Client ACK
    * ============================================================
    */
    ClientChatAck ack;


    /*
     * 一旦已经进入MySQL，
     * 当前逻辑发送已经被Server接受。
     *
     * delivered只表示当前Delivery是否已经完成。
     */
    ack.success =
        stored_persistent;

    ack.delivered =
        receiver_confirmed_now;

    ack.stored_offline =
        stored_offline;

    ack.stored_persistent =
        stored_persistent;

    ack.reused =
        client_request_reused;

    ack.client_message_id =
        request.client_message_id;

    ack.message_id =
        server_message_id;

    ack.from_user_id =
        from_user_id;

    ack.to_user_id =
        to_user_id;

    ack.receiver_private_unread =
        receiver_private_unread;

    ack.receiver_total_unread =
        receiver_total_unread;


    if (receiver_confirmed_now) {
        if (client_request_reused) {
            ack.reason =
                "local_already_receiver_confirmed";
        } else {
            ack.reason =
                "local_receiver_confirmed";
        }
    } else if (
        delivered ||
        local_delivery_already_completed
    ) {
        /*
        * 已经存在Receiver-visible Send副作用，
        * 但是Receiver应用层ACK尚未确认。
        */
        ack.reason =
            "local_delivery_awaiting_receiver_ack";
    } else if (target_online) {
        ack.reason =
            "local_push_failed_stored_pending";
    } else {
        ack.reason =
            "target_user_offline";
    }


    send_client_chat_ack(
        ack
    );


    LOG_INFO(
        "gateway local chat handled"
        << ", client_message_id="
        << request.client_message_id
        << ", message_id="
        << server_message_id
        << ", from="
        << from_user_id
        << ", to="
        << to_user_id
        << ", target_online="
        << target_online
        << ", send_side_effect_committed="
        << delivered
        << ", stored_offline="
        << stored_offline
        << ", stored_persistent="
        << stored_persistent
        << ", reused="
        << client_request_reused
        << ", receiver_private_unread="
        << receiver_private_unread
        << ", receiver_total_unread="
        << receiver_total_unread
    );
}


void GatewayServer::HandleGatewayForwardChatRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    if (!connection) {
        return;
    }

    GatewayForwardChatRequest dispatch_request;
    std::string dispatch_error;
    if (!DeserializeGatewayForwardChatRequest(
            packet.body, &dispatch_request, &dispatch_error)) {
        // Malformed input cannot reach a blocking dependency. Reuse the
        // business implementation only for its protocol error mapping.
        BusinessRequestContext invalid_request;
        invalid_request.operation = "gateway.peer_forward_chat.invalid";
        invalid_request.request_seq = packet.seq;
        ExecuteGatewayForwardChatRequest(
            connection, packet, invalid_request);
        return;
    }

    if (!HasBusinessExecutor()) {
        GatewayForwardChatResponse response;
        response.status = GatewayForwardChatStatus::kInternalError;
        response.target_gateway_id = options_.gateway_id;
        response.message_id = dispatch_request.message_id;
        response.error_message = "business runtime unavailable";
        std::string body;
        std::string error;
        if (SerializeGatewayForwardChatResponse(response, &body, &error)) {
            Packet response_packet;
            response_packet.type = MessageType::kGatewayForwardChatResponse;
            response_packet.seq = packet.seq;
            response_packet.body = std::move(body);
            SendPacket(connection, response_packet);
        }
        return;
    }

    const BusinessSubmitStatus submit_status =
        SubmitMustRunConnectionBusinessTask(
            business_executor_,
            connection,
            packet.seq,
            BusinessClock::now(),
            "gateway.peer_forward_chat",
            static_cast<BusinessOrderingKey>(dispatch_request.message_id),
            [this, connection, packet](
                const BusinessExecutor::ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                ScopedBusinessDispatchContext dispatch_scope(
                    connection, 0, 0);
                ExecuteGatewayForwardChatRequest(
                    connection, packet, context.Request());
                return {};
            }
        );

    if (submit_status != BusinessSubmitStatus::kAccepted) {
        LOG_WARN("gateway peer forward business task rejected"
                 << ", message_id=" << dispatch_request.message_id
                 << ", status="
                 << BusinessSubmitStatusToString(submit_status));
    }
}

void GatewayServer::ExecuteGatewayForwardChatRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet,
    const BusinessRequestContext& business_request
) {


    GatewayForwardChatResponse response;

    response.status =
        GatewayForwardChatStatus::
            kInternalError;

    response.target_gateway_id =
        options_.gateway_id;


    /*
     * 所有路径统一使用同一个
     * RPC Response。
     *
     * 最重要：
     * Response.seq 必须等于 Request.seq。
     */
    auto send_response =
        [
            this,
            &connection,
            &packet
        ](
            const GatewayForwardChatResponse&
                response_value
        ) {
            std::string body;
            std::string error_message;

            if (
                !SerializeGatewayForwardChatResponse(
                    response_value,
                    &body,
                    &error_message
                )
            ) {
                LOG_ERROR(
                    "gateway failed to serialize "
                    "forward chat response"
                    << ", seq="
                    << packet.seq
                    << ", error="
                    << error_message
                );

                return;
            }

            Packet response_packet;

            response_packet.type =
                MessageType::
                    kGatewayForwardChatResponse;

            response_packet.seq =
                packet.seq;

            response_packet.body =
                std::move(body);


            /*
            * Fault Injection Test Seam
            *
            * 这里只模拟：
            *
            * B业务已经执行完成，
            * 但Peer RPC Response丢失。
            *
            * 默认callback为空，所以正常运行完全不受影响。
            */
            if (
                gateway_peer_response_drop_callback_for_test_ &&
                gateway_peer_response_drop_callback_for_test_(
                    response_value
                )
            ) {
                LOG_WARN(
                    "gateway peer response dropped by "
                    "fault injection"
                    << ", message_id="
                    << response_value.message_id
                    << ", status="
                    << static_cast<std::uint32_t>(
                        response_value.status
                    )
                    << ", duplicate="
                    << response_value.duplicate
                    << ", seq="
                    << packet.seq
                );


                return;
            }


            SendPacket(
                connection,
                response_packet
            );
        };


    /*
     * 1. 解析内部RPC请求。
     */
    GatewayForwardChatRequest request;

    std::string error_message;

    if (
        !DeserializeGatewayForwardChatRequest(
            packet.body,
            &request,
            &error_message
        )
    ) {
        response.status =
            GatewayForwardChatStatus::
                kInvalidRequest;

        response.error_message =
            error_message;

        send_response(response);

        LOG_WARN(
            "gateway rejected invalid "
            "forward chat request"
            << ", seq="
            << packet.seq
            << ", peer="
            << (
                connection
                    ? connection->
                        PeerAddress().
                        ToString()
                    : std::string{}
            )
            << ", error="
            << error_message
        );

        return;
    }


    response.message_id =
        request.message_id;

    response.to_user_id =
        request.to_user_id;

    response.duplicate =
        false;


    /*
     * 2. 内部协议不允许走普通用户Session。
     *
     * 一个已经登录为用户的连接，
     * 不应该同时充当Gateway Peer。
     */
    if (
        session_manager_.
            FindUserByConnection(
                connection
            ).
            has_value()
    ) {
        response.status =
            GatewayForwardChatStatus::
                kUnauthorized;

        response.error_message =
            "user session cannot send "
            "gateway internal request";

        send_response(response);

        LOG_WARN(
            "gateway rejected internal "
            "request from user session"
            << ", source_gateway="
            << request.source_gateway_id
            << ", to="
            << request.to_user_id
        );

        return;
    }


    /*
     * 3. 必须配置Gateway Peer鉴权器。
     *
     * 默认拒绝，而不是默认信任。
     */
    if (!gateway_peer_verify_callback_) {
        response.status =
            GatewayForwardChatStatus::
                kInternalError;

        response.error_message =
            "gateway peer verifier unavailable";

        send_response(response);

        LOG_ERROR(
            "gateway peer verifier unavailable"
            << ", source_gateway="
            << request.source_gateway_id
            << ", to="
            << request.to_user_id
        );

        return;
    }



    /*
     * 4. 验证：
     *
     * source_gateway_id
     * +
     * source_lease_token
     *
     * 当前生产实现走
     * GatewayDiscovery本地snapshot。
     */
    std::string verify_error;

    if (
        !gateway_peer_verify_callback_(
            request.source_gateway_id,
            request.source_lease_token,
            &verify_error
        )
    ) {
        response.status =
            GatewayForwardChatStatus::
                kUnauthorized;

        response.error_message =
            verify_error.empty()
                ? "gateway peer unauthorized"
                : verify_error;

        send_response(response);

        LOG_WARN(
            "gateway rejected unauthorized "
            "peer request"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", to="
            << request.to_user_id
            << ", error="
            << response.error_message
        );

        return;
    }
    /*
     * Gateway Peer身份已经真正验证通过。
     *
     * 只有合法Peer现在才允许进入
     * message_id幂等状态机。
     */
  const MessageDeliveryDedupBeginStatus
        dedup_status =
            message_delivery_deduplicator_.
                Begin(
                    request.message_id
                );

                /*
        * 同一个业务message_id以前
        * 已经完成过本地投递。
        *
        * 本次绝对不能再次Push给用户。
        */
        if (
            dedup_status ==
            MessageDeliveryDedupBeginStatus::
                kAlreadyDelivered
        ) {
            response.status =
                GatewayForwardChatStatus::
                    kDelivered;

            response.duplicate =
                true;

            response.error_message.clear();


            send_response(response);


            LOG_INFO(
                "gateway suppressed already "
                "delivered peer message"
                << ", message_id="
                << request.message_id
                << ", source_gateway="
                << request.source_gateway_id
                << ", from="
                << request.from_user_id
                << ", to="
                << request.to_user_id
            );


            return;
        }

                /*
        * 同一个message_id已经有
        * 另外一个Sub-Reactor线程
        * 正在处理。
        *
        * 当前请求不能再执行一次。
        */
        if (
            dedup_status ==
            MessageDeliveryDedupBeginStatus::
                kAlreadyProcessing
        ) {
            response.status =
                GatewayForwardChatStatus::
                    kDuplicateInProgress;

            response.duplicate =
                true;

            response.error_message =
                "same message is already "
                "being delivered";


            send_response(response);


            LOG_INFO(
                "gateway suppressed concurrent "
                "duplicate peer message"
                << ", message_id="
                << request.message_id
                << ", source_gateway="
                << request.source_gateway_id
                << ", to="
                << request.to_user_id
            );


            return;
        }

        if (
            dedup_status !=
            MessageDeliveryDedupBeginStatus::
                kAcquired
        ) {
            response.status =
                GatewayForwardChatStatus::
                    kInvalidRequest;

            response.duplicate =
                false;

            response.error_message =
                "invalid message id";


            send_response(response);


            return;
        }

    /*
     * 5. Memory dedup miss -> durable MessageService truth.
     *
     * Gateway-B must not trust Gateway-A's business payload. M/from/to/content
     * and delivery state are validated against MessageService. A dependency
     * failure is fail-closed and releases only the in-memory Processing lease;
     * it never mutates durable state.
     */
    if (!HasMessageRpcClient()) {
        message_delivery_deduplicator_.Abort(request.message_id);
        response.status = GatewayForwardChatStatus::kInternalError;
        response.duplicate = false;
        response.error_message = "MessageService unavailable";
        send_response(response);
        LOG_ERROR("gateway peer durable validation MessageService unavailable"
                  << ", message_id=" << request.message_id
                  << ", source_gateway=" << request.source_gateway_id);
        return;
    }

    rpc::GetPrivateMessageRpcRequest get_request;
    get_request.message_id = request.message_id;

    rpc::RpcCallOptions get_options;
    const std::uint64_t get_rpc_id = next_internal_rpc_id_.fetch_add(
        1, std::memory_order_relaxed);
    get_options.request_id = options_.gateway_id +
        ":message:peer-validate:req:" + std::to_string(get_rpc_id);
    get_options.trace_id = options_.gateway_id +
        ":peer-validate:trace:" + std::to_string(get_rpc_id) +
        ":M:" + std::to_string(request.message_id);
    get_options.caller_service = "gateway";
    get_options.caller_instance = options_.gateway_id;
    const auto remaining = business_request.RemainingTime();
    get_options.remaining_timeout =
        remaining == std::chrono::milliseconds::max()
            ? std::chrono::milliseconds{0}
            : remaining;

    const auto persisted_result = message_rpc_client_->GetPrivateMessage(
        get_request, get_options);
    if (!persisted_result.ok()) {
        message_delivery_deduplicator_.Abort(request.message_id);
        response.status =
            persisted_result.status.code == rpc::RpcErrorCode::kNotFound
                ? GatewayForwardChatStatus::kInvalidRequest
                : GatewayForwardChatStatus::kInternalError;
        response.duplicate = false;
        response.error_message =
            persisted_result.status.message.empty()
                ? "durable message lookup failed"
                : persisted_result.status.message;
        send_response(response);
        LOG_WARN("gateway peer durable MessageService lookup failed"
                 << ", message_id=" << request.message_id
                 << ", source_gateway=" << request.source_gateway_id
                 << ", rpc_code="
                 << static_cast<int>(persisted_result.status.code)
                 << ", error=" << response.error_message);
        return;
    }

    const rpc::MessageRpcRecord& persisted_message =
        persisted_result.value->record;
    const std::uint32_t expected_message_type =
        static_cast<std::uint32_t>(rpc::PrivateMessageContentType::kText);

    if (persisted_message.message_id != request.message_id ||
        persisted_message.from_user_id != request.from_user_id ||
        persisted_message.to_user_id != request.to_user_id ||
        persisted_message.message_type != expected_message_type ||
        persisted_message.content != request.message_body) {
        message_delivery_deduplicator_.Abort(request.message_id);
        response.status = GatewayForwardChatStatus::kInvalidRequest;
        response.duplicate = false;
        response.error_message = "persisted message identity mismatch";
        send_response(response);
        LOG_WARN("gateway rejected peer message identity mismatch"
                 << ", message_id=" << request.message_id
                 << ", request_from=" << request.from_user_id
                 << ", persisted_from=" << persisted_message.from_user_id
                 << ", request_to=" << request.to_user_id
                 << ", persisted_to=" << persisted_message.to_user_id);
        return;
    }

    const rpc::MessageDeliveryState persisted_state =
        persisted_message.delivery_state;

    if (persisted_state == rpc::MessageDeliveryState::kReceiverConfirmed ||
        persisted_state == rpc::MessageDeliveryState::kRead) {
        const bool restored =
            message_delivery_deduplicator_.MarkDelivered(request.message_id);
        if (!restored) {
            LOG_ERROR("gateway durable duplicate memory restore failed"
                      << ", message_id=" << request.message_id);
        }
        response.status = GatewayForwardChatStatus::kDelivered;
        response.duplicate = true;
        response.error_message.clear();
        send_response(response);
        LOG_INFO("gateway suppressed durable duplicate peer message"
                 << ", message_id=" << request.message_id
                 << ", source_gateway=" << request.source_gateway_id
                 << ", persisted_state="
                 << static_cast<int>(persisted_state)
                 << ", from=" << request.from_user_id
                 << ", to=" << request.to_user_id);
        return;
    }

    if (persisted_state == rpc::MessageDeliveryState::kFailed) {
        message_delivery_deduplicator_.Abort(request.message_id);
        response.status = GatewayForwardChatStatus::kInternalError;
        response.duplicate = false;
        response.error_message = "persisted message is failed";
        send_response(response);
        LOG_WARN("gateway rejected peer message in failed persisted state"
                 << ", message_id=" << request.message_id
                 << ", source_gateway=" << request.source_gateway_id);
        return;
    }

    if (persisted_state != rpc::MessageDeliveryState::kPending) {
        message_delivery_deduplicator_.Abort(request.message_id);
        response.status = GatewayForwardChatStatus::kInternalError;
        response.duplicate = false;
        response.error_message = "unexpected persisted delivery state";
        send_response(response);
        LOG_ERROR("gateway peer message has unexpected persisted state"
                  << ", message_id=" << request.message_id
                  << ", delivery_state="
                  << static_cast<int>(persisted_state));
        return;
    }

    /*
     * 5. 找本机用户Session。
     *
     * 注意：
     *
     * 这里绝对不是Redis OnlineStatus。
     *
     * 请求已经被路由到当前Gateway，
     * 服务端最终投递必须以
     * 本机SessionManager为准。
     */
    TcpConnectionPtr target_connection =
        session_manager_.
            FindConnection(
                request.to_user_id
            );

    if (
        !target_connection ||
        !target_connection->
            IsConnected()
    ) {
                /*
        * 当前没有产生消息投递副作用，
        * 所以释放message_id执行权，
        * 允许未来重新Retry。
        */
        message_delivery_deduplicator_.
            Abort(
                request.message_id
            );
        response.status =
            GatewayForwardChatStatus::
                kTargetNotConnected;

        response.error_message =
            "target user has no active "
            "local session";

        send_response(response);

        LOG_INFO(
            "gateway forward chat target "
            "not connected"
            << ", source_gateway="
            << request.source_gateway_id
            << ", message_id="
            << request.message_id
            << ", to="
            << request.to_user_id
        );

        return;
    }


    /*
    * ============================================================
    * 6. Gateway Peer RPC Domain
    *        ->
    *    Receiver Delivery Domain
    * ============================================================
    *
    * packet.seq：
    *     Gateway A -> Gateway B
    *     当前一次Peer RPC Attempt身份。
    *
    * 它绝对不能继续传播给Receiver。
    *
    * Receiver需要新的：
    *
    *     kChatDelivery
    *     +
    *     independent delivery seq
    *     +
    *     stable body.message_id
    */


    /*
    * persisted_message.content保存的是
    * Server canonical chat body。
    *
    * 这里使用已经通过数据库身份校验的
    * persisted record作为Server Truth，
    * 而不是直接信任Peer提交的业务body。
    */
    std::string message_text;

    std::string parse_error;


    if (
        !ParseCanonicalChatBody(
            persisted_message.content,
            persisted_message.from_user_id,
            persisted_message.to_user_id,
            &message_text,
            &parse_error
        )
    ) {
        /*
        * 目前尚未产生Receiver业务副作用，
        * 可以安全释放message_id ownership。
        */
        message_delivery_deduplicator_.
            Abort(
                request.message_id
            );


        response.status =
            GatewayForwardChatStatus::
                kInternalError;

        response.duplicate =
            false;

        response.error_message =
            "invalid persisted chat body: " +
            parse_error;


        send_response(
            response
        );


        LOG_ERROR(
            "gateway peer receiver delivery "
            "rejected invalid persisted chat body"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", from="
            << persisted_message.from_user_id
            << ", to="
            << persisted_message.to_user_id
            << ", error="
            << parse_error
        );


        return;
    }


    Packet forward_packet;

    std::string
        delivery_submit_error;


    const
        ReceiverDeliverySubmitStatus
        delivery_submit_status =
            SubmitReceiverChatDelivery(
                target_connection,
                persisted_message.message_id,
                persisted_message.from_user_id,
                persisted_message.to_user_id,
                message_text,
                &forward_packet,
                &delivery_submit_error
            );


    const bool
        receiver_already_confirmed =
            delivery_submit_status ==
            ReceiverDeliverySubmitStatus::
                kAlreadyConfirmed;


    if (
        delivery_submit_status !=
            ReceiverDeliverySubmitStatus::
                kSubmitted &&
        !receiver_already_confirmed
    ) {
        /*
        * 还没有产生Receiver Send提交。
        *
        * 当前Peer Delivery Ownership可以安全释放。
        */
        message_delivery_deduplicator_.
            Abort(
                request.message_id
            );


        if (
            delivery_submit_status ==
            ReceiverDeliverySubmitStatus::
                kConnectionUnavailable
        ) {
            response.status =
                GatewayForwardChatStatus::
                    kTargetNotConnected;
        } else {
            response.status =
                GatewayForwardChatStatus::
                    kInternalError;
        }


        response.duplicate =
            false;

        response.error_message =
            delivery_submit_error;


        send_response(
            response
        );


        LOG_WARN(
            "gateway peer tracked receiver "
            "delivery submission failed"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", peer_seq="
            << packet.seq
            << ", from="
            << persisted_message.from_user_id
            << ", to="
            << persisted_message.to_user_id
            << ", submit_status="
            << static_cast<int>(
                delivery_submit_status
            )
            << ", error="
            << delivery_submit_error
        );


        return;
    }


    if (receiver_already_confirmed) {
        /*
        * Tracker已经拥有Receiver确认事实。
        *
        * 当前Peer RPC属于业务重复请求，
        * 不再Push Receiver。
        */
        response.duplicate =
            true;


        LOG_INFO(
            "gateway peer receiver delivery "
            "already confirmed by tracker"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", peer_seq="
            << packet.seq
            << ", from="
            << persisted_message.from_user_id
            << ", to="
            << persisted_message.to_user_id
        );
    } else {
        LOG_INFO(
            "gateway submitted tracked peer "
            "receiver chat delivery"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", peer_seq="
            << packet.seq
            << ", delivery_seq="
            << forward_packet.seq
            << ", from="
            << persisted_message.from_user_id
            << ", to="
            << persisted_message.to_user_id
        );
    }


    /*
    * ============================================================
    * Receiver Delivery已经提交。
    * ============================================================
    *
    * 注意：
    *
    * 这里仅允许推进Process-local Send Side Effect状态。
    *
    * 绝对不能推进MySQL ReceiverConfirmed。
    *
    * Durable状态仍保持：
    *
    *     Pending
    *
    * 只有Receiver kChatDeliveryAck
    * 才允许：
    *
    *     Pending -> ReceiverConfirmed
    */
    const bool dedup_marked =
        message_delivery_deduplicator_.
            MarkDelivered(
                request.message_id
            );


    if (!dedup_marked) {
        /*
        * 理论上：
        *
        * Begin()已经kAcquired，
        * 所以这里必须能成功Mark。
        *
        * 如果失败属于内部状态机异常。
        *
        * 但用户消息已经发送出去，
        * 绝不能因为这个内部异常
        * 返回“未投递”诱导上游直接重发。
        */
        LOG_ERROR(
            "gateway peer delivery "
            "dedup mark failed"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", to="
            << request.to_user_id
        );
    }
    response.status =
        GatewayForwardChatStatus::
            kDelivered;

    response.error_message.clear();

    response.duplicate = receiver_already_confirmed;

    send_response(response);

    LOG_INFO(
        "gateway forwarded remote chat "
        "to local session"
        << ", message_id="
        << request.message_id
        << ", source_gateway="
        << request.source_gateway_id
        << ", from="
        << request.from_user_id
        << ", to="
        << request.to_user_id
        << ", peer_seq="
        << packet.seq
        << ", delivery_seq="
        << forward_packet.seq
    );

}


void GatewayServer::HandleReadRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    if (!connection) {
        return;
    }

    const auto session = session_manager_.FindSessionByConnection(connection);
    if (!session.has_value()) {
        Json body;
        body["success"] = false;
        body["message"] = "not logged in";
        Packet response;
        response.type = MessageType::kReadResponse;
        response.seq = packet.seq;
        response.body = body.dump();
        SendPacket(connection, response);
        return;
    }

    UserId peer_user_id = 0;
    try {
        const Json body = Json::parse(packet.body);
        if (body.is_object()) {
            GetUserIdField(body, "peer_user_id", &peer_user_id, nullptr);
        }
    } catch (...) {
        peer_user_id = 0;
    }

    if (!HasBusinessExecutor()) {
        Json body;
        body["success"] = false;
        body["message"] = "business runtime rejected request";
        body["reason"] = "business_runtime_unavailable";
        Packet response;
        response.type = MessageType::kReadResponse;
        response.seq = packet.seq;
        response.body = body.dump();
        SendPacket(connection, response);
        return;
    }

    const BusinessSubmitStatus submit_status =
        SubmitSessionBusinessTask(
            business_executor_,
            &session_manager_,
            connection,
            *session,
            packet.seq,
            BusinessClock::now(),
            "gateway.read",
            BusinessCancellationPolicy::kMustRun,
            MakeUserPairOrderingKey(session->user_id, peer_user_id),
            [this, connection, packet,
             dispatch_user_id = session->user_id,
             dispatch_epoch = session->epoch](
                const BusinessExecutor::ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                ScopedBusinessDispatchContext dispatch_scope(
                    connection, dispatch_user_id, dispatch_epoch);
                ExecuteReadRequest(connection, packet, context.Request());
                return {};
            }
        );

    if (submit_status != BusinessSubmitStatus::kAccepted) {
        Json body;
        body["success"] = false;
        body["message"] = "business runtime rejected request";
        body["reason"] = BusinessSubmitStatusToString(submit_status);
        Packet response;
        response.type = MessageType::kReadResponse;
        response.seq = packet.seq;
        response.body = body.dump();
        SendPacket(connection, response);
    }
}

void GatewayServer::ExecuteReadRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet,
    const BusinessRequestContext& business_request
) {



    Json response_body;
    response_body["success"] = false;

    // kMustRun preserves an accepted durable mutation even if the original
    // session is later replaced. The response, however, is session-scoped:
    // never send a stale completion to a replaced connection/epoch.
    auto send_read_response = [this, &connection](const Packet& response) {
        if (!connection || !connection->IsConnected()) {
            return false;
        }
        if (!BusinessResponseSessionIsCurrent(session_manager_, connection)) {
            LOG_INFO("gateway fenced stale read response");
            return false;
        }
        return SendPacket(connection, response);
    };

    const auto login_user_id =
        ResolveBusinessUser(session_manager_, connection);

    if (!login_user_id.has_value()) {
        response_body["message"] = "not logged in";

        Packet response;
        response.type = MessageType::kReadResponse;
        response.seq = packet.seq;
        response.body = response_body.dump();

        send_read_response(response);
        return;
    }

    Json request_body;

    try {
        request_body = Json::parse(packet.body);
    } catch (const std::exception& e) {
        response_body["message"] = "invalid json body";

        Packet response;
        response.type = MessageType::kReadResponse;
        response.seq = packet.seq;
        response.body = response_body.dump();

        send_read_response(response);

        LOG_WARN("gateway read request parse failed"
                 << ", error=" << e.what());
        return;
    }

    //const UserId reader_user_id = login_user_id.value();
    //const UserId peer_user_id =
    //    request_body.value("peer_user_id", 0ULL);

    const UserId reader_user_id = login_user_id.value();

    if (request_body.contains("user_id")) {
        UserId client_reader_user_id = 0;

        if (!GetUserIdField(
                request_body,
                "user_id",
                &client_reader_user_id,
                nullptr)) {
            response_body["message"] = "invalid user_id";

            Packet response;
            response.type = MessageType::kReadResponse;
            response.seq = packet.seq;
            response.body = response_body.dump();

            send_read_response(response);
            return;
        }

        if (client_reader_user_id != reader_user_id) {
            response_body["message"] = "reader user mismatch";
            response_body["reason"] = "reader_user_mismatch";
            response_body["login_user_id"] = reader_user_id;
            response_body["client_user_id"] = client_reader_user_id;

            Packet response;
            response.type = MessageType::kReadResponse;
            response.seq = packet.seq;
            response.body = response_body.dump();

            send_read_response(response);

            LOG_WARN("gateway rejected read request: reader user mismatch"
                    << ", login_user_id=" << reader_user_id
                    << ", client_user_id=" << client_reader_user_id);
            return;
        }
    }

    const UserId peer_user_id =
        request_body.value("peer_user_id", 0ULL);

    if (peer_user_id == 0 || peer_user_id == reader_user_id) {
        response_body["message"] = "invalid peer_user_id";
        response_body["user_id"] = reader_user_id;
        response_body["peer_user_id"] = peer_user_id;

        Packet response;
        response.type = MessageType::kReadResponse;
        response.seq = packet.seq;
        response.body = response_body.dump();

        send_read_response(response);
        return;
    }

    if (!HasMessageRpcClient()) {
        response_body["message"] = "message service unavailable";
        response_body["reason"] = "message_service_unavailable";
        response_body["user_id"] = reader_user_id;
        response_body["peer_user_id"] = peer_user_id;
        response_body["private_unread"] = 0;
        response_body["total_unread"] = GetTotalUnread(reader_user_id);
        response_body["marked_read_count"] = 0;
        Packet response;
        response.type = MessageType::kReadResponse;
        response.seq = packet.seq;
        response.body = response_body.dump();
        send_read_response(response);
        return;
    }

    rpc::MarkDialogReadRpcRequest mark_request;
    mark_request.reader_user_id = reader_user_id;
    mark_request.peer_user_id = peer_user_id;

    rpc::RpcCallOptions mark_options;
    const std::uint64_t mark_rpc_id = next_internal_rpc_id_.fetch_add(
        1, std::memory_order_relaxed);
    mark_options.request_id = options_.gateway_id +
        ":message:mark-read:req:" + std::to_string(mark_rpc_id);
    mark_options.trace_id = options_.gateway_id +
        ":mark-read:trace:" + std::to_string(mark_rpc_id) +
        ":reader:" + std::to_string(reader_user_id) +
        ":peer:" + std::to_string(peer_user_id);
    mark_options.caller_service = "gateway";
    mark_options.caller_instance = options_.gateway_id;
    const auto remaining = business_request.RemainingTime();
    mark_options.remaining_timeout =
        remaining == std::chrono::milliseconds::max()
            ? std::chrono::milliseconds{0}
            : remaining;

    const auto mark_result = message_rpc_client_->MarkDialogRead(
        mark_request, mark_options);
    if (!mark_result.ok()) {
        // attempted=true means the DB mutation may already have committed.
        // Redis is only a projection, so never clear it until a retry receives
        // a confirmed durable result from MessageService.
        response_body["success"] = false;
        response_body["message"] = mark_result.attempted
            ? "read durable state uncertain"
            : "read durable state update failed";
        response_body["reason"] = mark_result.attempted
            ? "read_state_uncertain"
            : (mark_result.status.code == rpc::RpcErrorCode::kDeadlineExceeded
                   ? "read_state_deadline_expired"
                   : "read_state_unavailable");
        response_body["user_id"] = reader_user_id;
        response_body["peer_user_id"] = peer_user_id;
        response_body["private_unread"] = 0;
        response_body["total_unread"] = GetTotalUnread(reader_user_id);
        response_body["marked_read_count"] = 0;
        Packet response;
        response.type = MessageType::kReadResponse;
        response.seq = packet.seq;
        response.body = response_body.dump();
        send_read_response(response);
        LOG_WARN("gateway mark dialog read RPC failed"
                 << ", reader=" << reader_user_id
                 << ", peer=" << peer_user_id
                 << ", attempted=" << mark_result.attempted
                 << ", error=" << mark_result.status.message);
        return;
    }

    const std::uint64_t marked_read_count =
        mark_result.value->affected_rows;

    std::int64_t total_unread = 0;
    const bool cleared =
        ClearUnread(
            reader_user_id,
            peer_user_id,
            &total_unread
        );

    response_body["success"] = cleared;
    response_body["message"] = cleared
        ? "read accepted"
        : "clear unread failed";
    response_body["user_id"] = reader_user_id;
    response_body["peer_user_id"] = peer_user_id;
    response_body["private_unread"] = 0;
    response_body["total_unread"] = total_unread;
    response_body["marked_read_count"] = marked_read_count;

    Packet response;
    response.type = MessageType::kReadResponse;
    response.seq = packet.seq;
    response.body = response_body.dump();

    send_read_response(response);

    LOG_INFO("gateway read request handled"
             << ", reader=" << reader_user_id
             << ", peer=" << peer_user_id
             << ", cleared=" << cleared
             << ", total_unread=" << total_unread
             << ", marked_read_count=" << marked_read_count);

}

void GatewayServer::HandleHistoryRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    const BusinessTimePoint request_received_at = BusinessClock::now();
    const std::uint32_t request_seq = packet.seq;

    Json response_body;
    response_body["success"] = false;
    response_body["messages"] = Json::array();
    response_body["has_more"] = false;

    auto send_response = [this, connection, request_seq](const Json& body) {
        Packet response;
        response.type = MessageType::kHistoryResponse;
        response.seq = request_seq;
        response.body = body.dump();
        SendPacket(connection, response);
    };

    Json request_body;
    std::string error_message;
    if (!ParseJsonBody(packet, &request_body, &error_message)) {
        response_body["message"] = "invalid history json";
        response_body["reason"] = "invalid_json";
        send_response(response_body);
        return;
    }

    // Freeze user_id + SessionEpoch before crossing into the business queue.
    const auto session_snapshot =
        session_manager_.FindSessionByConnection(connection);
    if (!session_snapshot.has_value()) {
        response_body["message"] =
            "history request rejected: not logged in";
        response_body["reason"] = "not_logged_in";
        send_response(response_body);
        return;
    }

    const UserId self_user_id = session_snapshot->user_id;
    const SessionEpoch session_epoch = session_snapshot->epoch;

    UserId peer_user_id = 0;
    if (!GetUserIdField(
            request_body,
            "peer_user_id",
            &peer_user_id,
            &error_message
        )) {
        response_body["message"] = error_message;
        response_body["reason"] = "invalid_peer_user_id";
        response_body["user_id"] = self_user_id;
        send_response(response_body);
        return;
    }

    if (peer_user_id == self_user_id) {
        response_body["message"] = "peer user equals self";
        response_body["reason"] = "invalid_peer_user_id";
        response_body["user_id"] = self_user_id;
        response_body["peer_user_id"] = peer_user_id;
        send_response(response_body);
        return;
    }

    std::uint64_t before_message_id = 0;
    if (request_body.contains("before_message_id")) {
        if (!request_body.at("before_message_id").is_number_unsigned()) {
            response_body["message"] = "invalid before_message_id";
            response_body["reason"] = "invalid_before_message_id";
            response_body["user_id"] = self_user_id;
            response_body["peer_user_id"] = peer_user_id;
            send_response(response_body);
            return;
        }
        before_message_id =
            request_body.at("before_message_id").get<std::uint64_t>();
    }

    std::size_t limit = 20;
    if (request_body.contains("limit")) {
        if (!request_body.at("limit").is_number_unsigned()) {
            response_body["message"] = "invalid limit";
            response_body["reason"] = "invalid_limit";
            response_body["user_id"] = self_user_id;
            response_body["peer_user_id"] = peer_user_id;
            send_response(response_body);
            return;
        }
        limit = request_body.at("limit").get<std::size_t>();
    }
    if (limit == 0) {
        limit = 20;
    }
    if (limit > 50) {
        limit = 50;
    }

    if (!HasMessageRpcClient()) {
        response_body["message"] = "message service unavailable";
        response_body["reason"] = "message_service_unavailable";
        response_body["user_id"] = self_user_id;
        response_body["peer_user_id"] = peer_user_id;
        send_response(response_body);
        return;
    }

    // C1 deliberately leaves Social permission at the Gateway boundary. The
    // durable MessageService must not copy FriendRepository SQL or call
    // SocialService for every message read.
    if (!HasFriendRepository()) {
        response_body["message"] = "relation repository unavailable";
        response_body["reason"] = "relation_service_unavailable";
        response_body["user_id"] = self_user_id;
        response_body["peer_user_id"] = peer_user_id;
        send_response(response_body);
        return;
    }

    if (!HasBusinessExecutor()) {
        response_body["message"] = "business runtime unavailable";
        response_body["reason"] = "business_runtime_unavailable";
        response_body["user_id"] = self_user_id;
        response_body["peer_user_id"] = peer_user_id;
        send_response(response_body);
        return;
    }

    const std::uint64_t rpc_id =
        next_internal_rpc_id_.fetch_add(1, std::memory_order_relaxed);
    const std::string rpc_request_id =
        options_.gateway_id + ":message:list-history:req:" +
        std::to_string(rpc_id);
    const std::string trace_id =
        options_.gateway_id + ":message-history:trace:" +
        std::to_string(rpc_id) + ":client-seq:" +
        std::to_string(request_seq);

    const BusinessSubmitStatus submit_status =
        SubmitSessionBusinessTask(
            business_executor_,
            &session_manager_,
            connection,
            *session_snapshot,
            request_seq,
            request_received_at,
            "gateway.history.rpc",
            BusinessCancellationPolicy::kCancelable,
            std::nullopt,
            [
                this,
                connection,
                request_seq,
                self_user_id,
                peer_user_id,
                before_message_id,
                limit,
                rpc_request_id,
                trace_id
            ](
                const BusinessExecutor::ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                Json async_body;
                async_body["success"] = false;
                async_body["messages"] = Json::array();
                async_body["has_more"] = false;
                async_body["user_id"] = self_user_id;
                async_body["peer_user_id"] = peer_user_id;

                // Preserve M13's deterministic worker-delay test. The sleep is
                // still on a BusinessExecutor worker, never the Reactor.
                const auto delay = options_.history_business_delay_for_test;
                if (delay > std::chrono::milliseconds::zero()) {
                    std::this_thread::sleep_for(delay);
                }

                if (context.CancellationRequested()) {
                    return {};
                }

                const ChatPermissionResult permission_result =
                    friend_repository_->CheckPrivateChatPermission(
                        self_user_id,
                        peer_user_id
                    );
                if (!permission_result.Allowed()) {
                    async_body["message"] = permission_result.message;
                    async_body["reason"] =
                        ChatPermissionStatusToString(permission_result.status);

                    return BusinessExecutor::Completion(
                        [this, connection, request_seq,
                         body = std::move(async_body)]() mutable {
                            if (!connection || !connection->IsConnected()) {
                                return;
                            }
                            Packet response;
                            response.type = MessageType::kHistoryResponse;
                            response.seq = request_seq;
                            response.body = body.dump();
                            SendPacket(connection, response);
                        }
                    );
                }

                if (context.CancellationRequested()) {
                    return {};
                }

                rpc::ListHistoryRpcRequest rpc_request;
                rpc_request.actor_user_id = self_user_id;
                rpc_request.peer_user_id = peer_user_id;
                rpc_request.before_message_id = before_message_id;
                rpc_request.limit = static_cast<std::uint32_t>(limit);

                rpc::RpcCallOptions call_options;
                call_options.request_id = rpc_request_id;
                call_options.trace_id = trace_id;
                call_options.caller_service = "gateway";
                call_options.caller_instance = options_.gateway_id;

                const auto remaining = context.Request().RemainingTime();
                call_options.remaining_timeout =
                    remaining == std::chrono::milliseconds::max()
                        ? std::chrono::milliseconds{0}
                        : remaining;

                const auto rpc_result =
                    message_rpc_client_->ListHistory(
                        rpc_request,
                        call_options
                    );

                if (!rpc_result.ok()) {
                    async_body["message"] =
                        MessageRpcPublicMessage(rpc_result.status.code);
                    async_body["reason"] =
                        MessageRpcErrorReason(rpc_result.status.code);

                    LOG_WARN(
                        "gateway history MessageService RPC failed"
                        << ", user_id=" << self_user_id
                        << ", peer_user_id=" << peer_user_id
                        << ", request_seq=" << request_seq
                        << ", rpc_request_id=" << rpc_request_id
                        << ", reason="
                        << MessageRpcErrorReason(rpc_result.status.code)
                        << ", message=" << rpc_result.status.message
                    );

                    return BusinessExecutor::Completion(
                        [this, connection, request_seq,
                         body = std::move(async_body)]() mutable {
                            if (!connection || !connection->IsConnected()) {
                                return;
                            }
                            Packet response;
                            response.type = MessageType::kHistoryResponse;
                            response.seq = request_seq;
                            response.body = body.dump();
                            SendPacket(connection, response);
                        }
                    );
                }

                if (context.CancellationRequested()) {
                    return {};
                }

                Json message_array = Json::array();
                for (const auto& message : rpc_result.value->messages) {
                    Json item;
                    item["message_id"] = message.message_id;
                    item["from"] = message.from_user_id;
                    item["to"] = message.to_user_id;
                    item["message_type"] = message.message_type;
                    item["content"] = message.content;
                    item["delivery_status"] =
                        MessageDeliveryStateToLegacyStatus(
                            message.delivery_state
                        );
                    item["created_at"] = message.created_at;
                    item["delivered_at"] =
                        message.receiver_confirmed_at;
                    item["read_at"] = message.read_at;
                    message_array.push_back(std::move(item));
                }

                const std::size_t returned_count =
                    rpc_result.value->messages.size();
                const bool has_more = rpc_result.value->has_more;

                async_body["success"] = true;
                async_body["message"] = "history accepted";
                async_body["before_message_id"] = before_message_id;
                async_body["limit"] = limit;
                async_body["has_more"] = has_more;
                async_body["messages"] = std::move(message_array);

                return BusinessExecutor::Completion(
                    [
                        this,
                        connection,
                        request_seq,
                        self_user_id,
                        peer_user_id,
                        limit,
                        returned_count,
                        has_more,
                        rpc_request_id,
                        body = std::move(async_body)
                    ]() mutable {
                        if (!connection || !connection->IsConnected()) {
                            return;
                        }

                        Packet response;
                        response.type = MessageType::kHistoryResponse;
                        response.seq = request_seq;
                        response.body = body.dump();
                        SendPacket(connection, response);

                        LOG_INFO(
                            "gateway history MessageService completion sent"
                            << ", user_id=" << self_user_id
                            << ", peer_user_id=" << peer_user_id
                            << ", limit=" << limit
                            << ", returned=" << returned_count
                            << ", has_more=" << has_more
                            << ", rpc_request_id=" << rpc_request_id
                        );
                    }
                );
            }
        );

    if (submit_status == BusinessSubmitStatus::kAccepted) {
        return;
    }

    response_body["user_id"] = self_user_id;
    response_body["peer_user_id"] = peer_user_id;

    switch (submit_status) {
        case BusinessSubmitStatus::kOverloaded:
        case BusinessSubmitStatus::kHotKeyOverloaded:
            response_body["message"] = "business runtime overloaded";
            response_body["reason"] = "business_runtime_overloaded";
            break;
        case BusinessSubmitStatus::kDeadlineExpired:
            response_body["message"] = "history deadline expired";
            response_body["reason"] = "business_deadline_expired";
            break;
        case BusinessSubmitStatus::kShuttingDown:
            response_body["message"] = "business runtime shutting down";
            response_body["reason"] = "business_runtime_shutting_down";
            break;
        case BusinessSubmitStatus::kInvalidArgument:
            response_body["message"] = "invalid business runtime task";
            response_body["reason"] = "business_runtime_invalid_task";
            break;
        case BusinessSubmitStatus::kAccepted:
            return;
    }

    send_response(response_body);
}

void GatewayServer::HandleConversationListRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    const BusinessTimePoint request_received_at = BusinessClock::now();
    const std::uint32_t request_seq = packet.seq;

    Json response_body;
    response_body["success"] = false;
    response_body["conversations"] = Json::array();
    response_body["has_more"] = false;

    auto send_response = [this, connection, request_seq](const Json& body) {
        Packet response;
        response.type = MessageType::kConversationListResponse;
        response.seq = request_seq;
        response.body = body.dump();
        SendPacket(connection, response);
    };

    Json request_body;
    std::string error_message;
    if (!ParseJsonBody(packet, &request_body, &error_message)) {
        response_body["message"] = "invalid conversation list json";
        response_body["reason"] = "invalid_json";
        send_response(response_body);
        return;
    }

    const auto session_snapshot =
        session_manager_.FindSessionByConnection(connection);
    if (!session_snapshot.has_value()) {
        response_body["message"] =
            "conversation list request rejected: not logged in";
        response_body["reason"] = "not_logged_in";
        send_response(response_body);
        return;
    }

    const UserId self_user_id = session_snapshot->user_id;

    std::size_t limit = 20;
    if (request_body.contains("limit")) {
        if (!request_body.at("limit").is_number_unsigned()) {
            response_body["message"] = "invalid limit";
            response_body["reason"] = "invalid_limit";
            response_body["user_id"] = self_user_id;
            send_response(response_body);
            return;
        }
        limit = request_body.at("limit").get<std::size_t>();
    }
    if (limit == 0) {
        limit = 20;
    }
    if (limit > 50) {
        limit = 50;
    }

    if (!HasMessageRpcClient()) {
        response_body["message"] = "message service unavailable";
        response_body["reason"] = "message_service_unavailable";
        response_body["user_id"] = self_user_id;
        send_response(response_body);
        return;
    }

    if (!HasBusinessExecutor()) {
        response_body["message"] = "business runtime unavailable";
        response_body["reason"] = "business_runtime_unavailable";
        response_body["user_id"] = self_user_id;
        send_response(response_body);
        return;
    }

    const std::uint64_t rpc_id =
        next_internal_rpc_id_.fetch_add(1, std::memory_order_relaxed);
    const std::string rpc_request_id =
        options_.gateway_id + ":message:list-conversations:req:" +
        std::to_string(rpc_id);
    const std::string trace_id =
        options_.gateway_id + ":message-conversations:trace:" +
        std::to_string(rpc_id) + ":client-seq:" +
        std::to_string(request_seq);

    const BusinessSubmitStatus submit_status =
        SubmitSessionBusinessTask(
            business_executor_,
            &session_manager_,
            connection,
            *session_snapshot,
            request_seq,
            request_received_at,
            "gateway.conversation_list.rpc",
            BusinessCancellationPolicy::kCancelable,
            std::nullopt,
            [
                this,
                connection,
                request_seq,
                self_user_id,
                limit,
                rpc_request_id,
                trace_id
            ](
                const BusinessExecutor::ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                Json async_body;
                async_body["success"] = false;
                async_body["conversations"] = Json::array();
                async_body["has_more"] = false;
                async_body["user_id"] = self_user_id;

                if (context.CancellationRequested()) {
                    return {};
                }

                rpc::ListConversationsRpcRequest rpc_request;
                rpc_request.actor_user_id = self_user_id;
                rpc_request.limit = static_cast<std::uint32_t>(limit);

                rpc::RpcCallOptions call_options;
                call_options.request_id = rpc_request_id;
                call_options.trace_id = trace_id;
                call_options.caller_service = "gateway";
                call_options.caller_instance = options_.gateway_id;

                const auto remaining = context.Request().RemainingTime();
                call_options.remaining_timeout =
                    remaining == std::chrono::milliseconds::max()
                        ? std::chrono::milliseconds{0}
                        : remaining;

                const auto rpc_result =
                    message_rpc_client_->ListConversations(
                        rpc_request,
                        call_options
                    );

                if (!rpc_result.ok()) {
                    async_body["message"] =
                        MessageRpcPublicMessage(rpc_result.status.code);
                    async_body["reason"] =
                        MessageRpcErrorReason(rpc_result.status.code);

                    LOG_WARN(
                        "gateway conversation MessageService RPC failed"
                        << ", user_id=" << self_user_id
                        << ", request_seq=" << request_seq
                        << ", rpc_request_id=" << rpc_request_id
                        << ", reason="
                        << MessageRpcErrorReason(rpc_result.status.code)
                        << ", message=" << rpc_result.status.message
                    );

                    return BusinessExecutor::Completion(
                        [this, connection, request_seq,
                         body = std::move(async_body)]() mutable {
                            if (!connection || !connection->IsConnected()) {
                                return;
                            }
                            Packet response;
                            response.type = MessageType::kConversationListResponse;
                            response.seq = request_seq;
                            response.body = body.dump();
                            SendPacket(connection, response);
                        }
                    );
                }

                if (context.CancellationRequested()) {
                    return {};
                }

                Json conversation_array = Json::array();
                for (const auto& conversation :
                     rpc_result.value->conversations) {
                    if (context.CancellationRequested()) {
                        return {};
                    }

                    Json item;
                    item["peer_user_id"] = conversation.peer_user_id;
                    item["last_message_id"] = conversation.last_message_id;
                    item["last_client_message_id"] =
                        conversation.last_client_message_id;
                    item["last_from"] = conversation.last_from_user_id;
                    item["last_to"] = conversation.last_to_user_id;
                    item["last_message_type"] =
                        conversation.last_message_type;
                    item["last_content"] = conversation.last_content;
                    item["last_delivery_status"] =
                        MessageDeliveryStateToLegacyStatus(
                            conversation.last_delivery_state
                        );
                    item["last_created_at"] =
                        conversation.last_created_at;
                    item["last_delivered_at"] =
                        conversation.last_receiver_confirmed_at;
                    item["last_read_at"] = conversation.last_read_at;

                    // Unread is still the existing Redis-derived projection in
                    // C1. Only the durable conversation read crosses the new
                    // MessageService boundary.
                    item["unread_count"] = GetPrivateUnread(
                        self_user_id,
                        conversation.peer_user_id
                    );

                    conversation_array.push_back(std::move(item));
                }

                const std::size_t returned_count =
                    rpc_result.value->conversations.size();
                const bool has_more = rpc_result.value->has_more;

                async_body["success"] = true;
                async_body["message"] = "conversation list accepted";
                async_body["limit"] = limit;
                async_body["has_more"] = has_more;
                async_body["conversations"] =
                    std::move(conversation_array);

                return BusinessExecutor::Completion(
                    [
                        this,
                        connection,
                        request_seq,
                        self_user_id,
                        limit,
                        returned_count,
                        has_more,
                        rpc_request_id,
                        body = std::move(async_body)
                    ]() mutable {
                        if (!connection || !connection->IsConnected()) {
                            return;
                        }

                        Packet response;
                        response.type = MessageType::kConversationListResponse;
                        response.seq = request_seq;
                        response.body = body.dump();
                        SendPacket(connection, response);

                        LOG_INFO(
                            "gateway conversation MessageService completion sent"
                            << ", user_id=" << self_user_id
                            << ", limit=" << limit
                            << ", returned=" << returned_count
                            << ", has_more=" << has_more
                            << ", rpc_request_id=" << rpc_request_id
                        );
                    }
                );
            }
        );

    if (submit_status == BusinessSubmitStatus::kAccepted) {
        return;
    }

    response_body["user_id"] = self_user_id;
    switch (submit_status) {
        case BusinessSubmitStatus::kOverloaded:
        case BusinessSubmitStatus::kHotKeyOverloaded:
            response_body["message"] = "business runtime overloaded";
            response_body["reason"] = "business_runtime_overloaded";
            break;
        case BusinessSubmitStatus::kDeadlineExpired:
            response_body["message"] = "conversation list deadline expired";
            response_body["reason"] = "business_deadline_expired";
            break;
        case BusinessSubmitStatus::kShuttingDown:
            response_body["message"] = "business runtime shutting down";
            response_body["reason"] = "business_runtime_shutting_down";
            break;
        case BusinessSubmitStatus::kInvalidArgument:
            response_body["message"] = "business runtime unavailable";
            response_body["reason"] = "business_runtime_unavailable";
            break;
        case BusinessSubmitStatus::kAccepted:
            return;
    }

    send_response(response_body);
}

void GatewayServer::HandleUserProfileRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    const BusinessTimePoint request_received_at =
        BusinessClock::now();

    const std::uint32_t request_seq = packet.seq;

    Json response_body;
    response_body["success"] = false;

    auto send_response =
        [
            this,
            connection,
            request_seq
        ](
            const Json& body
        ) {
            Packet response;
            response.type =
                MessageType::kUserProfileResponse;
            response.seq = request_seq;
            response.body = body.dump();
            SendPacket(connection, response);
        };

    Json request_body;
    std::string error_message;

    if (
        !ParseJsonBody(
            packet,
            &request_body,
            &error_message
        ) ||
        !request_body.is_object()
    ) {
        response_body["message"] =
            "invalid user profile json";
        response_body["reason"] =
            "invalid_json";
        send_response(response_body);
        return;
    }

    /*
     * B3 exposes only authenticated SELF profile.
     * Actor identity always comes from the Gateway Session and never from
     * client-provided JSON. Reject identity-like fields explicitly instead
     * of silently ignoring them, so clients cannot mistake this endpoint for
     * an arbitrary-user lookup API.
     */
    if (
        request_body.contains("user_id") ||
        request_body.contains("target_user_id")
    ) {
        response_body["message"] =
            "target user id is not supported by self profile request";
        response_body["reason"] =
            "target_user_id_not_supported";
        send_response(response_body);
        return;
    }

    if (!request_body.empty()) {
        response_body["message"] =
            "user profile request body must be empty";
        response_body["reason"] =
            "invalid_profile_request";
        send_response(response_body);
        return;
    }

    /*
     * Freeze the authenticated logical Session before crossing into M13.
     * SubmitSessionBusinessTask copies user_id + SessionEpoch into its
     * before-work and completion fences, so a replaced login cannot receive
     * a stale profile completion.
     */
    const auto session_snapshot =
        session_manager_.FindSessionByConnection(
            connection
        );

    if (!session_snapshot.has_value()) {
        response_body["message"] =
            "user profile request rejected: not logged in";
        response_body["reason"] =
            "not_logged_in";
        send_response(response_body);

        LOG_WARN(
            "gateway rejected user profile request: not logged in"
            << ", peer="
            << connection->PeerAddress().ToString()
        );
        return;
    }

    const UserId self_user_id =
        session_snapshot->user_id;

    if (!HasUserRpcClient()) {
        response_body["message"] =
            "profile service unavailable";
        response_body["reason"] =
            "profile_unavailable";
        response_body["user_id"] = self_user_id;
        send_response(response_body);
        return;
    }

    if (!HasBusinessExecutor()) {
        response_body["message"] =
            "business runtime unavailable";
        response_body["reason"] =
            "business_runtime_unavailable";
        response_body["user_id"] = self_user_id;
        send_response(response_body);
        return;
    }

    const std::uint64_t rpc_id =
        next_internal_rpc_id_.fetch_add(
            1,
            std::memory_order_relaxed
        );

    const std::string rpc_request_id =
        options_.gateway_id +
        ":user:get-profile:req:" +
        std::to_string(rpc_id);

    const std::string trace_id =
        options_.gateway_id +
        ":user-profile:trace:" +
        std::to_string(rpc_id) +
        ":client-seq:" +
        std::to_string(request_seq);

    const BusinessSubmitStatus submit_status =
        SubmitSessionBusinessTask(
            business_executor_,
            &session_manager_,
            connection,
            *session_snapshot,
            request_seq,
            request_received_at,
            "gateway.user_profile.rpc",
            BusinessCancellationPolicy::kCancelable,
            std::nullopt,
            [
                this,
                connection,
                request_seq,
                self_user_id,
                rpc_request_id,
                trace_id
            ](
                const BusinessExecutor::ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                if (context.CancellationRequested()) {
                    return {};
                }

                rpc::GetUserProfileRpcRequest rpc_request;
                rpc_request.user_id = self_user_id;

                rpc::RpcCallOptions call_options;
                call_options.request_id = rpc_request_id;
                call_options.trace_id = trace_id;
                call_options.caller_service = "gateway";
                call_options.caller_instance =
                    options_.gateway_id;

                const auto remaining =
                    context.Request().RemainingTime();

                if (
                    remaining ==
                    std::chrono::milliseconds::max()
                ) {
                    // Never invent a fresh RPC timeout if the M13 E2E
                    // deadline invariant is missing.
                    call_options.remaining_timeout =
                        std::chrono::milliseconds{0};
                } else {
                    call_options.remaining_timeout = remaining;
                }

                const auto rpc_result =
                    user_rpc_client_->GetUserProfile(
                        rpc_request,
                        call_options
                    );

                Json async_body;
                async_body["success"] = false;

                if (!rpc_result.ok()) {
                    async_body["message"] =
                        UserProfileRpcPublicMessage(
                            rpc_result.status.code
                        );
                    async_body["reason"] =
                        UserProfileRpcErrorReason(
                            rpc_result.status.code
                        );

                    LOG_WARN(
                        "gateway user profile RPC failed"
                        << ", user_id=" << self_user_id
                        << ", request_seq=" << request_seq
                        << ", rpc_request_id=" << rpc_request_id
                        << ", reason="
                        << UserProfileRpcErrorReason(
                               rpc_result.status.code
                           )
                        << ", message="
                        << rpc_result.status.message
                    );

                    return BusinessExecutor::Completion(
                        [
                            this,
                            connection,
                            request_seq,
                            body = std::move(async_body)
                        ]() mutable {
                            if (
                                !connection ||
                                !connection->IsConnected()
                            ) {
                                return;
                            }

                            Packet response;
                            response.type =
                                MessageType::kUserProfileResponse;
                            response.seq = request_seq;
                            response.body = body.dump();
                            SendPacket(connection, response);
                        }
                    );
                }

                if (context.CancellationRequested()) {
                    return {};
                }

                const rpc::UserProfileRpcView& profile =
                    rpc_result.value->profile;

                /*
                 * Identity integrity check at the service boundary: a self
                 * profile request must never be allowed to return another
                 * durable user identity, even if an upstream bug/misroute
                 * occurs.
                 */
                if (profile.user_id != self_user_id) {
                    async_body["message"] =
                        "invalid profile service response";
                    async_body["reason"] =
                        "profile_invalid_response";

                    LOG_ERROR(
                        "gateway rejected mismatched user profile identity"
                        << ", expected_user_id=" << self_user_id
                        << ", actual_user_id=" << profile.user_id
                        << ", rpc_request_id=" << rpc_request_id
                    );

                    return BusinessExecutor::Completion(
                        [
                            this,
                            connection,
                            request_seq,
                            body = std::move(async_body)
                        ]() mutable {
                            if (
                                !connection ||
                                !connection->IsConnected()
                            ) {
                                return;
                            }

                            Packet response;
                            response.type =
                                MessageType::kUserProfileResponse;
                            response.seq = request_seq;
                            response.body = body.dump();
                            SendPacket(connection, response);
                        }
                    );
                }

                Json profile_body;
                profile_body["user_id"] = profile.user_id;
                profile_body["username"] = profile.username;
                profile_body["nickname"] = profile.nickname;
                profile_body["avatar_url"] = profile.avatar_url;
                profile_body["user_status"] = profile.user_status;

                async_body["success"] = true;
                async_body["message"] =
                    "user profile accepted";
                async_body["profile"] =
                    std::move(profile_body);

                return BusinessExecutor::Completion(
                    [
                        this,
                        connection,
                        request_seq,
                        self_user_id,
                        rpc_request_id,
                        body = std::move(async_body)
                    ]() mutable {
                        if (
                            !connection ||
                            !connection->IsConnected()
                        ) {
                            return;
                        }

                        Packet response;
                        response.type =
                            MessageType::kUserProfileResponse;
                        response.seq = request_seq;
                        response.body = body.dump();
                        SendPacket(connection, response);

                        LOG_INFO(
                            "gateway user profile RPC completion sent"
                            << ", user_id=" << self_user_id
                            << ", rpc_request_id=" << rpc_request_id
                        );
                    }
                );
            }
        );

    if (submit_status == BusinessSubmitStatus::kAccepted) {
        return;
    }

    response_body["user_id"] = self_user_id;

    switch (submit_status) {
        case BusinessSubmitStatus::kOverloaded:
        case BusinessSubmitStatus::kHotKeyOverloaded:
            response_body["message"] =
                "business runtime overloaded";
            response_body["reason"] =
                "business_runtime_overloaded";
            break;

        case BusinessSubmitStatus::kDeadlineExpired:
            response_body["message"] =
                "user profile deadline expired";
            response_body["reason"] =
                "business_deadline_expired";
            break;

        case BusinessSubmitStatus::kShuttingDown:
            response_body["message"] =
                "business runtime shutting down";
            response_body["reason"] =
                "business_runtime_shutting_down";
            break;

        case BusinessSubmitStatus::kInvalidArgument:
            response_body["message"] =
                "business runtime unavailable";
            response_body["reason"] =
                "business_runtime_unavailable";
            break;

        case BusinessSubmitStatus::kAccepted:
            return;
    }

    LOG_WARN(
        "gateway user profile RPC task rejected"
        << ", user_id=" << self_user_id
        << ", request_seq=" << request_seq
        << ", status="
        << BusinessSubmitStatusToString(
               submit_status
           )
    );

    send_response(response_body);
}

void GatewayServer::HandleFriendListRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    const BusinessTimePoint request_received_at =
        BusinessClock::now();

    const std::uint32_t request_seq =
        packet.seq;

    Json response_body;
    response_body["success"] = false;
    response_body["friends"] = Json::array();
    response_body["has_more"] = false;

    auto send_response =
        [
            this,
            connection,
            request_seq
        ](
            const Json& body
        ) {
            Packet response;
            response.type =
                MessageType::kFriendListResponse;
            response.seq = request_seq;
            response.body = body.dump();

            SendPacket(connection, response);
        };

    Json request_body;
    std::string error_message;

    if (
        !ParseJsonBody(
            packet,
            &request_body,
            &error_message
        )
    ) {
        response_body["message"] =
            "invalid friend list json";
        response_body["reason"] =
            "invalid_json";
        send_response(response_body);
        return;
    }

    /*
     * Freeze authenticated Session ownership before crossing into M13's
     * asynchronous Business Runtime. actor_user_id is never trusted from
     * client JSON.
     */
    const auto session_snapshot =
        session_manager_.
            FindSessionByConnection(
                connection
            );

    if (!session_snapshot.has_value()) {
        response_body["message"] =
            "friend list request rejected: not logged in";
        response_body["reason"] =
            "not_logged_in";
        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend list request: not logged in"
            << ", peer="
            << connection->PeerAddress().ToString()
        );
        return;
    }

    const UserId self_user_id =
        session_snapshot->user_id;

    std::size_t limit = 20;

    if (request_body.contains("limit")) {
        if (
            !request_body.at("limit").
                is_number_unsigned()
        ) {
            response_body["message"] =
                "invalid limit";
            response_body["reason"] =
                "invalid_limit";
            response_body["user_id"] =
                self_user_id;
            send_response(response_body);
            return;
        }

        limit =
            request_body.at("limit").
                get<std::size_t>();
    }

    if (limit == 0) {
        limit = 20;
    }

    if (limit > 100) {
        limit = 100;
    }

    if (!HasSocialRpcClient()) {
        response_body["message"] =
            "friend service unavailable";
        response_body["reason"] =
            "friend_service_unavailable";
        response_body["user_id"] =
            self_user_id;
        send_response(response_body);
        return;
    }

    if (!HasBusinessExecutor()) {
        response_body["message"] =
            "business runtime unavailable";
        response_body["reason"] =
            "business_runtime_unavailable";
        response_body["user_id"] =
            self_user_id;
        send_response(response_body);
        return;
    }

    const std::uint64_t rpc_id =
        next_internal_rpc_id_.fetch_add(
            1,
            std::memory_order_relaxed
        );

    const std::string rpc_request_id =
        options_.gateway_id +
        ":social:list-friends:req:" +
        std::to_string(rpc_id);

    const std::string trace_id =
        options_.gateway_id +
        ":friend-list:trace:" +
        std::to_string(rpc_id) +
        ":client-seq:" +
        std::to_string(request_seq);

    const BusinessSubmitStatus submit_status =
        SubmitSessionBusinessTask(
            business_executor_,
            &session_manager_,
            connection,
            *session_snapshot,
            request_seq,
            request_received_at,
            "gateway.friend_list.rpc",
            BusinessCancellationPolicy::
                kCancelable,
            std::nullopt,
            [
                this,
                connection,
                request_seq,
                self_user_id,
                limit,
                rpc_request_id,
                trace_id
            ](
                const BusinessExecutor::
                    ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                Json async_body;
                async_body["success"] = false;
                async_body["friends"] =
                    Json::array();
                async_body["has_more"] = false;
                async_body["user_id"] =
                    self_user_id;

                if (context.CancellationRequested()) {
                    return {};
                }

                rpc::ListFriendsRpcRequest rpc_request;
                rpc_request.actor_user_id =
                    self_user_id;
                rpc_request.limit =
                    static_cast<std::uint32_t>(limit);

                rpc::RpcCallOptions call_options;
                call_options.request_id =
                    rpc_request_id;
                call_options.trace_id = trace_id;
                call_options.caller_service =
                    "gateway";
                call_options.caller_instance =
                    options_.gateway_id;

                const auto remaining =
                    context.Request().RemainingTime();

                if (
                    remaining ==
                    std::chrono::milliseconds::max()
                ) {
                    // BusinessExecutor normally applies its default E2E
                    // deadline. Fail closed instead of inventing a fresh RPC
                    // timeout if that invariant is ever broken.
                    call_options.remaining_timeout =
                        std::chrono::milliseconds{0};
                } else {
                    call_options.remaining_timeout =
                        remaining;
                }

                auto rpc_result =
                    social_rpc_client_->ListFriends(
                        rpc_request,
                        call_options
                    );

                if (!rpc_result.ok()) {
                    async_body["message"] =
                        rpc_result.status.message.empty()
                            ? "friend service request failed"
                            : rpc_result.status.message;
                    async_body["reason"] =
                        SocialRpcErrorReason(
                            rpc_result.status.code
                        );

                    LOG_WARN(
                        "gateway friend list RPC failed"
                        << ", user_id=" << self_user_id
                        << ", request_seq=" << request_seq
                        << ", rpc_request_id=" << rpc_request_id
                        << ", reason="
                        << SocialRpcErrorReason(
                            rpc_result.status.code
                        )
                        << ", message="
                        << rpc_result.status.message
                    );

                    return BusinessExecutor::Completion(
                        [
                            this,
                            connection,
                            request_seq,
                            body = std::move(async_body)
                        ]() mutable {
                            if (
                                !connection ||
                                !connection->IsConnected()
                            ) {
                                return;
                            }

                            Packet response;
                            response.type =
                                MessageType::kFriendListResponse;
                            response.seq = request_seq;
                            response.body = body.dump();
                            SendPacket(connection, response);
                        }
                    );
                }

                if (context.CancellationRequested()) {
                    return {};
                }

                Json friend_array = Json::array();

                for (
                    const auto& friend_record :
                    rpc_result.value->friends
                ) {
                    Json item;
                    item["friend_user_id"] =
                        friend_record.friend_user_id;
                    item["username"] =
                        friend_record.username;
                    item["nickname"] =
                        friend_record.nickname;
                    item["avatar_url"] =
                        friend_record.avatar_url;
                    item["user_status"] =
                        friend_record.user_status;
                    item["relation_status"] =
                        friend_record.relation_status;
                    item["relation_created_at"] =
                        friend_record.relation_created_at;
                    item["relation_updated_at"] =
                        friend_record.relation_updated_at;

                    friend_array.push_back(
                        std::move(item)
                    );
                }

                const std::size_t returned_count =
                    rpc_result.value->friends.size();
                const bool has_more =
                    rpc_result.value->has_more;

                async_body["success"] = true;
                async_body["message"] =
                    "friend list accepted";
                async_body["limit"] = limit;
                async_body["has_more"] = has_more;
                async_body["friends"] =
                    std::move(friend_array);

                return BusinessExecutor::Completion(
                    [
                        this,
                        connection,
                        request_seq,
                        self_user_id,
                        limit,
                        returned_count,
                        has_more,
                        rpc_request_id,
                        body = std::move(async_body)
                    ]() mutable {
                        if (
                            !connection ||
                            !connection->IsConnected()
                        ) {
                            return;
                        }

                        Packet response;
                        response.type =
                            MessageType::kFriendListResponse;
                        response.seq = request_seq;
                        response.body = body.dump();

                        SendPacket(connection, response);

                        LOG_INFO(
                            "gateway friend list RPC completion sent"
                            << ", user_id=" << self_user_id
                            << ", limit=" << limit
                            << ", returned=" << returned_count
                            << ", has_more=" << has_more
                            << ", rpc_request_id=" << rpc_request_id
                        );
                    }
                );
            }
        );

    if (
        submit_status ==
        BusinessSubmitStatus::kAccepted
    ) {
        return;
    }

    response_body["user_id"] = self_user_id;

    switch (submit_status) {
        case BusinessSubmitStatus::kOverloaded:
        case BusinessSubmitStatus::kHotKeyOverloaded:
            response_body["message"] =
                "business runtime overloaded";
            response_body["reason"] =
                "business_runtime_overloaded";
            break;

        case BusinessSubmitStatus::kDeadlineExpired:
            response_body["message"] =
                "friend list deadline expired";
            response_body["reason"] =
                "business_deadline_expired";
            break;

        case BusinessSubmitStatus::kShuttingDown:
            response_body["message"] =
                "business runtime shutting down";
            response_body["reason"] =
                "business_runtime_shutting_down";
            break;

        case BusinessSubmitStatus::kInvalidArgument:
            response_body["message"] =
                "business runtime unavailable";
            response_body["reason"] =
                "business_runtime_unavailable";
            break;

        case BusinessSubmitStatus::kAccepted:
            return;
    }

    LOG_WARN(
        "gateway friend list RPC task rejected"
        << ", user_id=" << self_user_id
        << ", request_seq=" << request_seq
        << ", status="
        << BusinessSubmitStatusToString(
            submit_status
        )
    );

    send_response(response_body);
}

void GatewayServer::HandleFriendRequestCreateRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {

    if (!InBusinessDispatch()) {
        Json dispatch_body;
        UserId dispatch_to_user_id = 0;
        try {
            dispatch_body = Json::parse(packet.body);
            if (dispatch_body.is_object()) {
                GetUserIdField(dispatch_body, "to_user_id",
                               &dispatch_to_user_id, nullptr);
            }
        } catch (...) {
            dispatch_to_user_id = 0;
        }

        const auto dispatch_session =
            session_manager_.FindSessionByConnection(connection);

        if (dispatch_session.has_value() && HasBusinessExecutor()) {
            const BusinessSubmitStatus submit_status =
                SubmitSessionBusinessTask(
                    business_executor_,
                    &session_manager_,
                    connection,
                    *dispatch_session,
                    packet.seq,
                    BusinessClock::now(),
                    "gateway.friend_request.create",
                    BusinessCancellationPolicy::kMustRun,
                    MakeUserPairOrderingKey(dispatch_session->user_id, dispatch_to_user_id),
                    [this, connection, packet,
                     dispatch_user_id = dispatch_session->user_id,
                     dispatch_epoch = dispatch_session->epoch](
                        const BusinessExecutor::ExecutionContext&
                    ) -> BusinessExecutor::Completion {
                        ScopedBusinessDispatchContext dispatch_scope(
                            connection,
                            dispatch_user_id,
                            dispatch_epoch
                        );
                        HandleFriendRequestCreateRequest(connection, packet);
                        return {};
                    }
                );

            if (submit_status == BusinessSubmitStatus::kAccepted) {
                return;
            }

            Json rejection_body;
            rejection_body["success"] = false;
            rejection_body["message"] = "business runtime rejected request";
            rejection_body["reason"] =
                BusinessSubmitStatusToString(submit_status);
            Packet rejection;
            rejection.type = MessageType::kFriendRequestCreateResponse;
            rejection.seq = packet.seq;
            rejection.body = rejection_body.dump();
            SendPacket(connection, rejection);
            return;
        }
    }

    Json response_body;
    response_body["success"] = false;
    response_body["changed"] = false;
    response_body["request_id"] = 0;

    auto send_response = [this, &connection, &packet](
        const Json& body
    ) {
        Packet response;
        response.type =
            MessageType::kFriendRequestCreateResponse;
        response.seq = packet.seq;
        response.body = body.dump();

        SendPacket(connection, response);
    };

    Json request_body;
    std::string error_message;

    if (!ParseJsonBody(packet, &request_body, &error_message) ||
        !request_body.is_object()) {
        response_body["message"] =
            "invalid friend request create json";
        response_body["reason"] = "invalid_json";
        send_response(response_body);
        return;
    }

    const auto login_user_id =
        ResolveBusinessUser(session_manager_, connection);

    if (!login_user_id.has_value()) {
        response_body["message"] =
            "friend request create rejected: not logged in";
        response_body["reason"] = "not_logged_in";
        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request create: not logged in"
            << ", peer="
            << connection->PeerAddress().ToString()
        );

        return;
    }

    const UserId self_user_id = login_user_id.value();
    response_body["user_id"] = self_user_id;

    if (request_body.contains("from_user_id")) {
        response_body["message"] =
            "from_user_id must not be provided by client";
        response_body["reason"] =
            "forbidden_identity_field";
        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request create: "
            "client supplied from_user_id"
            << ", login_user_id=" << self_user_id
        );

        return;
    }

    UserId to_user_id = 0;

    if (!GetUserIdField(
            request_body,
            "to_user_id",
            &to_user_id,
            &error_message)) {
        response_body["message"] = error_message;
        response_body["reason"] = "invalid_to_user_id";
        send_response(response_body);
        return;
    }

    response_body["to_user_id"] = to_user_id;

    if (to_user_id == self_user_id) {
        response_body["message"] =
            "cannot send friend request to self";
        response_body["reason"] = "invalid_argument";
        send_response(response_body);
        return;
    }

    std::string request_message;

    if (request_body.contains("request_message")) {
        if (!request_body.at("request_message").is_string()) {
            response_body["message"] =
                "request_message must be a string";
            response_body["reason"] =
                "invalid_request_message";
            send_response(response_body);
            return;
        }

        request_message =
            request_body.at("request_message").get<std::string>();
    }

    if (request_message.size() > 255) {
        response_body["message"] =
            "friend request message is too long";
        response_body["reason"] =
            "invalid_request_message";
        send_response(response_body);
        return;
    }

    if (!HasFriendRequestRepository()) {
        response_body["message"] =
            "friend request repository unavailable";
        response_body["reason"] =
            "friend_request_service_unavailable";
        send_response(response_body);
        return;
    }

    const CreateFriendRequestResult result =
        friend_request_repository_->CreateFriendRequest(
            self_user_id,
            to_user_id,
            request_message
        );

    const bool changed =
        result.status == CreateFriendRequestStatus::kCreated ||
        result.status == CreateFriendRequestStatus::kReopened;

    const bool success =
        changed ||
        result.status ==
            CreateFriendRequestStatus::kAlreadyPending;

    response_body["success"] = success;
    response_body["changed"] = changed;
    response_body["message"] = result.message;
    response_body["reason"] =
        CreateFriendRequestStatusToString(result.status);
    response_body["request_id"] = result.request_id;
    response_body["from_user_id"] = self_user_id;
    response_body["to_user_id"] = to_user_id;

    send_response(response_body);

    LOG_INFO(
        "gateway friend request create handled"
        << ", from_user_id=" << self_user_id
        << ", to_user_id=" << to_user_id
        << ", request_id=" << result.request_id
        << ", status="
        << CreateFriendRequestStatusToString(result.status)
        << ", success=" << success
        << ", changed=" << changed
    );
}

void GatewayServer::HandleFriendRequestListRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    const BusinessTimePoint request_received_at =
        BusinessClock::now();

    const std::uint32_t request_seq =
        packet.seq;

    Json response_body;
    response_body["success"] = false;
    response_body["requests"] = Json::array();
    response_body["has_more"] = false;
    response_body["next_before_created_at"] = "";
    response_body["next_before_request_id"] = 0;

    auto send_response =
        [
            this,
            connection,
            request_seq
        ](
            const Json& body
        ) {
            Packet response;
            response.type =
                MessageType::
                    kFriendRequestListResponse;
            response.seq =
                request_seq;
            response.body =
                body.dump();

            SendPacket(
                connection,
                response
            );
        };

    Json request_body;
    std::string error_message;

    if (
        !ParseJsonBody(
            packet,
            &request_body,
            &error_message
        ) ||
        !request_body.is_object()
    ) {
        response_body["message"] =
            "invalid friend request list json";
        response_body["reason"] =
            "invalid_json";
        send_response(response_body);
        return;
    }

    const auto session_snapshot =
        session_manager_.
            FindSessionByConnection(
                connection
            );

    if (!session_snapshot.has_value()) {
        response_body["message"] =
            "friend request list rejected: not logged in";
        response_body["reason"] =
            "not_logged_in";
        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request list: not logged in"
            << ", peer="
            << connection->PeerAddress().ToString()
        );

        return;
    }

    const UserId self_user_id =
        session_snapshot->user_id;

    response_body["user_id"] =
        self_user_id;

    if (
        request_body.contains(
            "receiver_user_id"
        )
    ) {
        response_body["message"] =
            "receiver_user_id must not be provided by client";
        response_body["reason"] =
            "forbidden_identity_field";
        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request list: client supplied receiver_user_id"
            << ", login_user_id="
            << self_user_id
        );

        return;
    }

    std::size_t limit = 20;

    if (request_body.contains("limit")) {
        if (
            !request_body.at("limit").
                is_number_unsigned()
        ) {
            response_body["message"] =
                "invalid limit";
            response_body["reason"] =
                "invalid_limit";
            send_response(response_body);
            return;
        }

        limit =
            request_body.at("limit").
                get<std::size_t>();
    }

    if (limit == 0) {
        limit = 20;
    }

    if (limit > 50) {
        limit = 50;
    }

    std::string before_created_at;

    if (
        request_body.contains(
            "before_created_at"
        )
    ) {
        if (
            !request_body.at(
                "before_created_at"
            ).is_string()
        ) {
            response_body["message"] =
                "invalid before_created_at";
            response_body["reason"] =
                "invalid_pagination_cursor";
            send_response(response_body);
            return;
        }

        before_created_at =
            request_body.at(
                "before_created_at"
            ).get<std::string>();
    }

    std::uint64_t before_request_id = 0;

    if (
        request_body.contains(
            "before_request_id"
        )
    ) {
        if (
            !request_body.at(
                "before_request_id"
            ).is_number_unsigned()
        ) {
            response_body["message"] =
                "invalid before_request_id";
            response_body["reason"] =
                "invalid_pagination_cursor";
            send_response(response_body);
            return;
        }

        before_request_id =
            request_body.at(
                "before_request_id"
            ).get<std::uint64_t>();
    }

    const bool first_page =
        before_created_at.empty() &&
        before_request_id == 0;

    const bool next_page =
        !before_created_at.empty() &&
        before_request_id != 0;

    if (!first_page && !next_page) {
        response_body["message"] =
            "before_created_at and before_request_id must be provided together";
        response_body["reason"] =
            "invalid_pagination_cursor";
        send_response(response_body);
        return;
    }

    response_body["limit"] = limit;
    response_body["before_created_at"] =
        before_created_at;
    response_body["before_request_id"] =
        before_request_id;

    if (!HasFriendRequestRepository()) {
        response_body["message"] =
            "friend request repository unavailable";
        response_body["reason"] =
            "friend_request_service_unavailable";
        send_response(response_body);
        return;
    }

    if (!HasBusinessExecutor()) {
        response_body["message"] =
            "business runtime unavailable";
        response_body["reason"] =
            "business_runtime_unavailable";
        send_response(response_body);
        return;
    }

    const std::size_t query_limit =
        limit + 1;

    const BusinessSubmitStatus submit_status =
        SubmitSessionBusinessTask(
            business_executor_,
            &session_manager_,
            connection,
            *session_snapshot,
            request_seq,
            request_received_at,
            "gateway.friend_request_list",
            BusinessCancellationPolicy::
                kCancelable,
            std::nullopt,
            [
                this,
                connection,
                request_seq,
                self_user_id,
                before_created_at,
                before_request_id,
                limit,
                query_limit
            ](
                const BusinessExecutor::
                    ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                Json async_body;
                async_body["success"] = false;
                async_body["requests"] =
                    Json::array();
                async_body["has_more"] = false;
                async_body["next_before_created_at"] =
                    "";
                async_body["next_before_request_id"] =
                    0;
                async_body["user_id"] =
                    self_user_id;
                async_body["limit"] =
                    limit;
                async_body["before_created_at"] =
                    before_created_at;
                async_body["before_request_id"] =
                    before_request_id;

                if (
                    context.CancellationRequested()
                ) {
                    return {};
                }

                auto list_result =
                    friend_request_repository_->
                        ListPendingIncomingRequests(
                            self_user_id,
                            before_created_at,
                            before_request_id,
                            query_limit
                        );

                if (!list_result.Succeeded()) {
                    async_body["message"] =
                        list_result.message;
                    async_body["reason"] =
                        ListPendingIncomingRequestsStatusToString(
                            list_result.status
                        );

                    return
                        BusinessExecutor::Completion(
                            [
                                this,
                                connection,
                                request_seq,
                                body =
                                    std::move(async_body)
                            ]() mutable {
                                if (
                                    !connection ||
                                    !connection->
                                        IsConnected()
                                ) {
                                    return;
                                }

                                Packet response;
                                response.type =
                                    MessageType::
                                        kFriendRequestListResponse;
                                response.seq =
                                    request_seq;
                                response.body =
                                    body.dump();

                                SendPacket(
                                    connection,
                                    response
                                );
                            }
                        );
                }

                if (
                    context.CancellationRequested()
                ) {
                    return {};
                }

                auto requests =
                    std::move(
                        list_result.records
                    );

                bool has_more = false;

                if (requests.size() > limit) {
                    has_more = true;
                    requests.resize(limit);
                }

                Json request_array =
                    Json::array();

                for (
                    const auto& record :
                        requests
                ) {
                    Json item;

                    item["request_id"] =
                        record.request_id;
                    item["from_user_id"] =
                        record.from_user_id;
                    item["to_user_id"] =
                        record.to_user_id;
                    item["request_message"] =
                        record.request_message;
                    item["request_status"] =
                        static_cast<std::uint32_t>(
                            record.request_status
                        );
                    item["request_status_name"] =
                        "pending";
                    item["created_at"] =
                        record.created_at;
                    item["handled_at"] =
                        record.handled_at;
                    item["updated_at"] =
                        record.updated_at;
                    item["from_username"] =
                        record.from_username;
                    item["from_nickname"] =
                        record.from_nickname;
                    item["from_avatar_url"] =
                        record.from_avatar_url;
                    item["from_user_status"] =
                        record.from_user_status;

                    request_array.push_back(
                        std::move(item)
                    );
                }

                if (
                    has_more &&
                    !requests.empty()
                ) {
                    async_body[
                        "next_before_created_at"
                    ] =
                        requests.back().
                            created_at;
                    async_body[
                        "next_before_request_id"
                    ] =
                        requests.back().
                            request_id;
                }

                const std::size_t returned_count =
                    requests.size();

                async_body["success"] = true;
                async_body["message"] =
                    "friend request list accepted";
                async_body["reason"] = "ok";
                async_body["has_more"] =
                    has_more;
                async_body["requests"] =
                    std::move(request_array);

                return
                    BusinessExecutor::Completion(
                        [
                            this,
                            connection,
                            request_seq,
                            self_user_id,
                            before_created_at,
                            before_request_id,
                            limit,
                            returned_count,
                            has_more,
                            body =
                                std::move(async_body)
                        ]() mutable {
                            if (
                                !connection ||
                                !connection->
                                    IsConnected()
                            ) {
                                return;
                            }

                            Packet response;
                            response.type =
                                MessageType::
                                    kFriendRequestListResponse;
                            response.seq =
                                request_seq;
                            response.body =
                                body.dump();

                            SendPacket(
                                connection,
                                response
                            );

                            LOG_INFO(
                                "gateway friend request list completion sent"
                                << ", user_id="
                                << self_user_id
                                << ", before_created_at="
                                << before_created_at
                                << ", before_request_id="
                                << before_request_id
                                << ", limit="
                                << limit
                                << ", returned="
                                << returned_count
                                << ", has_more="
                                << has_more
                            );
                        }
                    );
            }
        );

    if (
        submit_status ==
        BusinessSubmitStatus::kAccepted
    ) {
        return;
    }

    switch (submit_status) {
        case BusinessSubmitStatus::kOverloaded:
        case BusinessSubmitStatus::
            kHotKeyOverloaded:
            response_body["message"] =
                "business runtime overloaded";
            response_body["reason"] =
                "business_runtime_overloaded";
            break;

        case BusinessSubmitStatus::
            kDeadlineExpired:
            response_body["message"] =
                "friend request list deadline expired";
            response_body["reason"] =
                "business_deadline_expired";
            break;

        case BusinessSubmitStatus::
            kShuttingDown:
            response_body["message"] =
                "business runtime shutting down";
            response_body["reason"] =
                "business_runtime_shutting_down";
            break;

        case BusinessSubmitStatus::
            kInvalidArgument:
            response_body["message"] =
                "business runtime unavailable";
            response_body["reason"] =
                "business_runtime_unavailable";
            break;

        case BusinessSubmitStatus::kAccepted:
            return;
    }

    LOG_WARN(
        "gateway friend request list task rejected"
        << ", user_id="
        << self_user_id
        << ", request_seq="
        << request_seq
        << ", status="
        << BusinessSubmitStatusToString(
            submit_status
        )
    );

    send_response(response_body);
}

void GatewayServer::HandleFriendRequestAcceptRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {

    if (!InBusinessDispatch()) {
        Json dispatch_body;
        std::uint64_t dispatch_request_id = 0;
        try {
            dispatch_body = Json::parse(packet.body);
            if (dispatch_body.is_object() &&
                dispatch_body.contains("request_id") &&
                dispatch_body.at("request_id").is_number_unsigned()) {
                dispatch_request_id =
                    dispatch_body.at("request_id").get<std::uint64_t>();
            }
        } catch (...) {
            dispatch_request_id = 0;
        }

        const auto dispatch_session =
            session_manager_.FindSessionByConnection(connection);

        if (dispatch_session.has_value() && HasBusinessExecutor()) {
            const BusinessSubmitStatus submit_status =
                SubmitSessionBusinessTask(
                    business_executor_,
                    &session_manager_,
                    connection,
                    *dispatch_session,
                    packet.seq,
                    BusinessClock::now(),
                    "gateway.friend_request.accept",
                    BusinessCancellationPolicy::kMustRun,
                    static_cast<BusinessOrderingKey>(dispatch_request_id),
                    [this, connection, packet,
                     dispatch_user_id = dispatch_session->user_id,
                     dispatch_epoch = dispatch_session->epoch](
                        const BusinessExecutor::ExecutionContext&
                    ) -> BusinessExecutor::Completion {
                        ScopedBusinessDispatchContext dispatch_scope(
                            connection,
                            dispatch_user_id,
                            dispatch_epoch
                        );
                        HandleFriendRequestAcceptRequest(connection, packet);
                        return {};
                    }
                );

            if (submit_status == BusinessSubmitStatus::kAccepted) {
                return;
            }

            Json rejection_body;
            rejection_body["success"] = false;
            rejection_body["message"] = "business runtime rejected request";
            rejection_body["reason"] =
                BusinessSubmitStatusToString(submit_status);
            Packet rejection;
            rejection.type = MessageType::kFriendRequestAcceptResponse;
            rejection.seq = packet.seq;
            rejection.body = rejection_body.dump();
            SendPacket(connection, rejection);
            return;
        }
    }

    Json response_body;

    response_body["success"] = false;
    response_body["changed"] = false;
    response_body["request_id"] = 0;

    auto send_response =
        [this, &connection, &packet](
            const Json& body
        ) {
            Packet response;

            response.type =
                MessageType::
                    kFriendRequestAcceptResponse;

            response.seq = packet.seq;
            response.body = body.dump();

            SendPacket(
                connection,
                response
            );
        };

    Json request_body;
    std::string error_message;

    if (!ParseJsonBody(
            packet,
            &request_body,
            &error_message
        ) ||
        !request_body.is_object()) {
        response_body["message"] =
            "invalid friend request accept json";

        response_body["reason"] =
            "invalid_json";

        send_response(response_body);
        return;
    }

    const auto login_user_id =
        ResolveBusinessUser(
                session_manager_,
                connection
            );

    if (!login_user_id.has_value()) {
        response_body["message"] =
            "friend request accept rejected: "
            "not logged in";

        response_body["reason"] =
            "not_logged_in";

        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request "
            "accept: not logged in"
            << ", peer="
            << connection->
                   PeerAddress().
                   ToString()
        );

        return;
    }

    const UserId self_user_id =
        login_user_id.value();

    response_body["user_id"] =
        self_user_id;

    if (request_body.contains(
            "handler_user_id")) {
        response_body["message"] =
            "handler_user_id must not be "
            "provided by client";

        response_body["reason"] =
            "forbidden_identity_field";

        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request "
            "accept: client supplied "
            "handler_user_id"
            << ", login_user_id="
            << self_user_id
        );

        return;
    }

    if (!request_body.contains(
            "request_id") ||
        !request_body.at(
            "request_id"
        ).is_number_unsigned()) {
        response_body["message"] =
            "invalid request_id";

        response_body["reason"] =
            "invalid_request_id";

        send_response(response_body);
        return;
    }

    const std::uint64_t request_id =
        request_body.at(
            "request_id"
        ).get<std::uint64_t>();

    response_body["request_id"] =
        request_id;

    if (request_id == 0) {
        response_body["message"] =
            "request_id must not be zero";

        response_body["reason"] =
            "invalid_request_id";

        send_response(response_body);
        return;
    }

    if (!HasFriendRequestRepository()) {
        response_body["message"] =
            "friend request repository "
            "unavailable";

        response_body["reason"] =
            "friend_request_service_unavailable";

        send_response(response_body);
        return;
    }

    const AcceptFriendRequestResult result =
        friend_request_repository_->
            AcceptFriendRequest(
                request_id,
                self_user_id
            );

    const bool changed =
        result.status ==
            AcceptFriendRequestStatus::
                kAccepted;

    const bool success =
        changed ||
        result.status ==
            AcceptFriendRequestStatus::
                kAlreadyAccepted;

    response_body["success"] =
        success;

    response_body["changed"] =
        changed;

    response_body["message"] =
        result.message;

    response_body["reason"] =
        AcceptFriendRequestStatusToString(
            result.status
        );

    send_response(response_body);

    LOG_INFO(
        "gateway friend request accept handled"
        << ", handler_user_id="
        << self_user_id
        << ", request_id="
        << request_id
        << ", status="
        << AcceptFriendRequestStatusToString(
               result.status
           )
        << ", success="
        << success
        << ", changed="
        << changed
    );
}

void GatewayServer::HandleFriendRequestRejectRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {

    if (!InBusinessDispatch()) {
        Json dispatch_body;
        std::uint64_t dispatch_request_id = 0;
        try {
            dispatch_body = Json::parse(packet.body);
            if (dispatch_body.is_object() &&
                dispatch_body.contains("request_id") &&
                dispatch_body.at("request_id").is_number_unsigned()) {
                dispatch_request_id =
                    dispatch_body.at("request_id").get<std::uint64_t>();
            }
        } catch (...) {
            dispatch_request_id = 0;
        }

        const auto dispatch_session =
            session_manager_.FindSessionByConnection(connection);

        if (dispatch_session.has_value() && HasBusinessExecutor()) {
            const BusinessSubmitStatus submit_status =
                SubmitSessionBusinessTask(
                    business_executor_,
                    &session_manager_,
                    connection,
                    *dispatch_session,
                    packet.seq,
                    BusinessClock::now(),
                    "gateway.friend_request.reject",
                    BusinessCancellationPolicy::kMustRun,
                    static_cast<BusinessOrderingKey>(dispatch_request_id),
                    [this, connection, packet,
                     dispatch_user_id = dispatch_session->user_id,
                     dispatch_epoch = dispatch_session->epoch](
                        const BusinessExecutor::ExecutionContext&
                    ) -> BusinessExecutor::Completion {
                        ScopedBusinessDispatchContext dispatch_scope(
                            connection,
                            dispatch_user_id,
                            dispatch_epoch
                        );
                        HandleFriendRequestRejectRequest(connection, packet);
                        return {};
                    }
                );

            if (submit_status == BusinessSubmitStatus::kAccepted) {
                return;
            }

            Json rejection_body;
            rejection_body["success"] = false;
            rejection_body["message"] = "business runtime rejected request";
            rejection_body["reason"] =
                BusinessSubmitStatusToString(submit_status);
            Packet rejection;
            rejection.type = MessageType::kFriendRequestRejectResponse;
            rejection.seq = packet.seq;
            rejection.body = rejection_body.dump();
            SendPacket(connection, rejection);
            return;
        }
    }

    Json response_body;

    response_body["success"] = false;
    response_body["changed"] = false;
    response_body["request_id"] = 0;

    auto send_response = [this, &connection, &packet] (
        const Json& body
    ) {
        Packet response;

        response.type = MessageType::kFriendRequestRejectResponse;

        response.seq = packet.seq;
        response.body = body.dump();

        SendPacket(connection, response);
    };

    Json request_body;
    std::string error_message;

    if (!ParseJsonBody(packet, &request_body, &error_message) ||
        !request_body.is_object()) {
            response_body["message"] = "invalid friend request reject json";
            response_body["reason"] = "invalid_json";

            send_response(response_body);
            return;
        }

        const auto login_user_id =
        ResolveBusinessUser(
                session_manager_,
                connection
            );

    if (!login_user_id.has_value()) {
        response_body["message"] =
            "friend request reject operation "
            "rejected: not logged in";

        response_body["reason"] =
            "not_logged_in";

        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request "
            "reject: not logged in"
            << ", peer="
            << connection->
                   PeerAddress().
                   ToString()
        );

        return;
    }

    const UserId self_user_id =
        login_user_id.value();

    response_body["user_id"] =
        self_user_id;

    if (request_body.contains(
            "handler_user_id")) {
        response_body["message"] =
            "handler_user_id must not be "
            "provided by client";

        response_body["reason"] =
            "forbidden_identity_field";

        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request "
            "reject: client supplied "
            "handler_user_id"
            << ", login_user_id="
            << self_user_id
        );

        return;
    }

    if (!request_body.contains(
            "request_id") ||
        !request_body.at(
            "request_id"
        ).is_number_unsigned()) {
        response_body["message"] =
            "invalid request_id";

        response_body["reason"] =
            "invalid_request_id";

        send_response(response_body);
        return;
    }

    const std::uint64_t request_id =
        request_body.at(
            "request_id"
        ).get<std::uint64_t>();

    response_body["request_id"] =
        request_id;

    if (request_id == 0) {
        response_body["message"] =
            "request_id must not be zero";

        response_body["reason"] =
            "invalid_request_id";

        send_response(response_body);
        return;
    }

    if (!HasFriendRequestRepository()) {
        response_body["message"] =
            "friend request repository "
            "unavailable";

        response_body["reason"] =
            "friend_request_service_unavailable";

        send_response(response_body);
        return;
    }

    const RejectFriendRequestResult result =
        friend_request_repository_->
            RejectFriendRequest(
                request_id,
                self_user_id
            );

    const bool success =
        result.RejectedOrAlreadyRejected();

    const bool changed =
        result.status ==
            RejectFriendRequestStatus::
                kRejected;

    response_body["success"] =
        success;

    response_body["changed"] =
        changed;

    response_body["message"] =
        result.message;

    response_body["reason"] =
        RejectFriendRequestStatusToString(
            result.status
        );

    send_response(response_body);

    LOG_INFO(
        "gateway friend request reject handled"
        << ", handler_user_id="
        << self_user_id
        << ", request_id="
        << request_id
        << ", status="
        << RejectFriendRequestStatusToString(
               result.status
           )
        << ", success="
        << success
        << ", changed="
        << changed
    );
}


void GatewayServer::HandleGroupControlRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    const BusinessTimePoint request_received_at = BusinessClock::now();
    const auto response_type = GroupResponseType(packet.type);
    if (!response_type.has_value()) {
        SendPacket(connection, MakeErrorPacket(packet.seq, "unsupported group request type"));
        return;
    }

    auto send_response = [this, connection, request_seq = packet.seq, type = *response_type](const Json& body) {
        if (!connection || !connection->IsConnected()) return;
        Packet response;
        response.type = type;
        response.seq = request_seq;
        response.body = body.dump();
        SendPacket(connection, response);
    };

    Json request_body;
    std::string parse_error;
    if (!ParseJsonBody(packet, &request_body, &parse_error) || !request_body.is_object()) {
        send_response(Json{{"success",false},{"reason","invalid_json"},{"message","invalid group request json"}});
        return;
    }

    const auto session_snapshot = session_manager_.FindSessionByConnection(connection);
    if (!session_snapshot.has_value()) {
        send_response(Json{{"success",false},{"reason","not_logged_in"},{"message","group request rejected: not logged in"}});
        return;
    }
    if (!HasGroupRpcClient()) {
        send_response(Json{{"success",false},{"reason","group_service_unavailable"},{"message","group service unavailable"}});
        return;
    }
    if (!HasBusinessExecutor()) {
        send_response(Json{{"success",false},{"reason","business_runtime_unavailable"},{"message","business runtime unavailable"}});
        return;
    }

    const UserId actor_user_id = session_snapshot->user_id;
    const std::uint64_t rpc_id = next_internal_rpc_id_.fetch_add(1, std::memory_order_relaxed);
    const std::string operation = GroupOperationName(packet.type);
    const std::string rpc_request_id = options_.gateway_id + ":group:" + operation + ":req:" + std::to_string(rpc_id);
    const std::string trace_id = options_.gateway_id + ":group:trace:" + std::to_string(rpc_id) + ":client-seq:" + std::to_string(packet.seq);
    const auto ordering_key = GroupOrderingKey(packet.type, request_body);

    const BusinessSubmitStatus submit_status = SubmitSessionBusinessTask(
        business_executor_,
        &session_manager_,
        connection,
        *session_snapshot,
        packet.seq,
        request_received_at,
        std::string("gateway.group.") + operation + ".rpc",
        BusinessCancellationPolicy::kCancelable,
        ordering_key,
        [
            this,
            connection,
            request_seq = packet.seq,
            response_type = *response_type,
            request_type = packet.type,
            actor_user_id,
            request_body = std::move(request_body),
            rpc_request_id,
            trace_id
        ](const BusinessExecutor::ExecutionContext& context) mutable -> BusinessExecutor::Completion {
            if (context.CancellationRequested()) return {};

            rpc::RpcCallOptions call_options;
            call_options.request_id = rpc_request_id;
            call_options.trace_id = trace_id;
            call_options.caller_service = "gateway";
            call_options.caller_instance = options_.gateway_id;
            const auto remaining = context.Request().RemainingTime();
            call_options.remaining_timeout =
                remaining == std::chrono::milliseconds::max()
                    ? std::chrono::milliseconds{0}
                    : remaining;

            Json async_body = ExecuteGroupRpc(
                group_rpc_client_,
                request_type,
                actor_user_id,
                request_body,
                call_options
            );

            if (context.CancellationRequested()) return {};

            return BusinessExecutor::Completion(
                [this, connection, request_seq, response_type, body = std::move(async_body)]() mutable {
                    if (!connection || !connection->IsConnected()) return;
                    Packet response;
                    response.type = response_type;
                    response.seq = request_seq;
                    response.body = body.dump();
                    SendPacket(connection, response);
                }
            );
        }
    );

    if (submit_status == BusinessSubmitStatus::kAccepted) return;

    Json rejected{{"success",false}};
    switch (submit_status) {
        case BusinessSubmitStatus::kOverloaded:
        case BusinessSubmitStatus::kHotKeyOverloaded:
            rejected["reason"] = "business_runtime_overloaded";
            rejected["message"] = "business runtime overloaded";
            break;
        case BusinessSubmitStatus::kDeadlineExpired:
            rejected["reason"] = "business_deadline_expired";
            rejected["message"] = "group request deadline expired";
            break;
        case BusinessSubmitStatus::kShuttingDown:
            rejected["reason"] = "business_runtime_shutting_down";
            rejected["message"] = "business runtime shutting down";
            break;
        case BusinessSubmitStatus::kInvalidArgument:
            rejected["reason"] = "business_runtime_unavailable";
            rejected["message"] = "business runtime unavailable";
            break;
        case BusinessSubmitStatus::kAccepted:
            return;
    }
    send_response(rejected);
}

void GatewayServer::HandleHeartbeat(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    /*
     * Heartbeat transport fast-path：
     * Pong不再等待Redis presence refresh。
     */
    Json response_body;
    response_body["pong"] = true;

    Packet response;
    response.type =
        MessageType::kHeartbeat;
    response.seq =
        packet.seq;
    response.body =
        response_body.dump();

    SendPacket(
        connection,
        response
    );

    /*
     * Presence refresh是best-effort maintenance。
     * 一次Runtime admission失败不应反向让heartbeat失败。
     */
    if (
        !HasBusinessExecutor() ||
        !HasOnlineStatusCache()
    ) {
        return;
    }

    const auto session_snapshot =
        session_manager_.
            FindSessionByConnection(
                connection
            );

    if (!session_snapshot.has_value()) {
        return;
    }

    const UserId user_id =
        session_snapshot->user_id;

    const BusinessSubmitStatus status =
        SubmitSessionBusinessTask(
            business_executor_,
            &session_manager_,
            connection,
            *session_snapshot,
            packet.seq,
            BusinessClock::now(),
            "gateway.presence.refresh",
            BusinessCancellationPolicy::
                kCancelable,
            std::nullopt,
            [
                this,
                user_id,
                connection
            ](
                const BusinessExecutor::
                    ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                if (
                    context.CancellationRequested()
                ) {
                    return {};
                }

                RefreshUserOnlineIfMatch(
                    user_id,
                    connection
                );

                return {};
            }
        );

    if (
        status !=
        BusinessSubmitStatus::kAccepted
    ) {
        LOG_WARN(
            "gateway heartbeat presence refresh not admitted"
            << ", user_id="
            << user_id
            << ", request_seq="
            << packet.seq
            << ", status="
            << BusinessSubmitStatusToString(
                status
            )
        );
    }
}

void GatewayServer::PushOfflineMessages(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    if (!connection || !connection->IsConnected()) {
        return;
    }

    std::vector<Packet> offline_packets =
        offline_message_store_.PopAll(user_id);

    if (offline_packets.empty()) {
        return;
    }

    LOG_INFO("gateway pushing offline messages"
             << ", user_id=" << user_id
             << ", count=" << offline_packets.size());

    for (const auto& offline_packet : offline_packets) {
        SendPacket(connection, offline_packet);
    }
}

void GatewayServer::NotifyLoginReplaced(
    UserId user_id,
    const TcpConnectionPtr& old_connection
) {
    if (!old_connection) {
        return;
    }

    if (!old_connection->IsConnected()) {
        return;
    }

    Json body;
    body["success"] = false;
    body["reason"] = "login_replaced";
    body["message"] = "account logged in from another connection";
    body["user_id"] = user_id;

    Packet packet;
    packet.type = MessageType::kError;
    packet.seq = 0;
    packet.body = body.dump();

    SendPacket(old_connection, packet);

    LOG_WARN("gateway replaced old login connection"
             << ", user_id=" << user_id
             << ", old_connection=" << old_connection->Name()
             << ", peer=" << old_connection->PeerAddress().ToString());

    old_connection->Shutdown();
}

void GatewayServer::SetUserOnline(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    if (!HasOnlineStatusCache()) {
        return;
    }

    if (!connection) {
        return;
    }

const SetOnlineResult result = online_status_cache_->
            SetOnline(
                user_id,
                options_.gateway_id,
                connection->Name(),
                options_.
                    online_status_ttl_seconds
            );

    if (result.Succeeded()) {
        return;
    }

    LOG_WARN(
        "gateway set user online "
        "status failed"
        << ", user_id=" << user_id
        << ", gateway_id="
        << options_.gateway_id
        << ", connection="
        << connection->Name()
        << ", status="
        << SetOnlineStatusToString(
            result.status
        )
        << ", error="
        << result.error_message
    );
}

void GatewayServer::RefreshUserOnlineIfMatch(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    if (!HasOnlineStatusCache()) {
        return;
    }

    if (user_id == 0 ||
        !connection) {
        return;
    }

    const RefreshOnlineIfMatchResult result =
        online_status_cache_->
            RefreshOnlineIfMatch(
                user_id,
                options_.gateway_id,
                connection->Name(),
                options_.
                    online_status_ttl_seconds
            );

    if (result.Refreshed()) {
        return;
    }

    if (result.status ==
        RefreshOnlineIfMatchStatus::
            kMismatch) {
        LOG_INFO(
            "gateway ignored stale online "
            "status refresh"
            << ", user_id="
            << user_id
            << ", gateway_id="
            << options_.gateway_id
            << ", connection="
            << connection->Name()
        );

        return;
    }

    LOG_WARN(
        "gateway refresh user online "
        "status failed"
        << ", user_id="
        << user_id
        << ", gateway_id="
        << options_.gateway_id
        << ", connection="
        << connection->Name()
        << ", status="
        << RefreshOnlineIfMatchStatusToString(
            result.status
        )
        << ", error="
        << result.error_message
    );
}

void GatewayServer::PushPersistentOfflineMessages(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    if (user_id == 0 || !connection || !connection->IsConnected()) {
        return;
    }
    if (!HasMessageRpcClient() || !HasBusinessExecutor()) {
        LOG_WARN("gateway postponed persistent offline replay: dependency unavailable"
                 << ", user_id=" << user_id
                 << ", message_rpc=" << HasMessageRpcClient()
                 << ", business_runtime=" << HasBusinessExecutor());
        return;
    }

    const auto session = session_manager_.FindSessionByConnection(connection);
    if (!session.has_value() || session->user_id != user_id) {
        return;
    }

    const BusinessSubmitStatus status =
        SubmitSessionBusinessTask(
            business_executor_,
            &session_manager_,
            connection,
            *session,
            0,
            BusinessClock::now(),
            "gateway.pending_replay",
            BusinessCancellationPolicy::kCancelable,
            static_cast<BusinessOrderingKey>(user_id),
            [this, user_id, connection,
             dispatch_epoch = session->epoch](
                const BusinessExecutor::ExecutionContext& context
            ) -> BusinessExecutor::Completion {
                ScopedBusinessDispatchContext dispatch_scope(
                    connection, user_id, dispatch_epoch);
                ExecutePersistentOfflineReplay(
                    user_id, connection, context.Request());
                return {};
            }
        );

    if (status != BusinessSubmitStatus::kAccepted) {
        LOG_WARN("gateway persistent offline replay task rejected"
                 << ", user_id=" << user_id
                 << ", status=" << BusinessSubmitStatusToString(status));
    }
}

void GatewayServer::ExecutePersistentOfflineReplay(
    UserId user_id,
    const TcpConnectionPtr& connection,
    const BusinessRequestContext& business_request
) {
    if (user_id == 0 || !connection || !connection->IsConnected() ||
        !HasMessageRpcClient()) {
        return;
    }

    constexpr std::uint32_t kPendingReplayPageSize = 100;
    std::uint64_t after_message_id = 0;
    std::size_t page_index = 0;
    std::size_t total_scanned = 0;

    auto session_is_current = [&]() {
        const TcpConnectionPtr active =
            session_manager_.FindConnection(user_id);
        return active && active == connection && active->IsConnected();
    };

    auto make_options = [&](const char* operation) {
        rpc::RpcCallOptions options;
        const std::uint64_t rpc_id = next_internal_rpc_id_.fetch_add(
            1, std::memory_order_relaxed);
        options.request_id = options_.gateway_id + ":message:" + operation +
            ":req:" + std::to_string(rpc_id);
        options.trace_id = options_.gateway_id + ":pending-replay:trace:" +
            std::to_string(rpc_id) + ":user:" + std::to_string(user_id);
        options.caller_service = "gateway";
        options.caller_instance = options_.gateway_id;
        const auto remaining = business_request.RemainingTime();
        options.remaining_timeout =
            remaining == std::chrono::milliseconds::max()
                ? std::chrono::milliseconds{0}
                : remaining;
        return options;
    };

    while (session_is_current() && !business_request.DeadlineExpired()) {
        ++page_index;

        rpc::ListPendingAfterRpcRequest list_request;
        list_request.to_user_id = user_id;
        list_request.after_message_id = after_message_id;
        list_request.limit = kPendingReplayPageSize;

        const auto list_result = message_rpc_client_->ListPendingAfter(
            list_request, make_options("list-pending-after"));
        if (!list_result.ok()) {
            LOG_WARN("gateway persistent offline replay MessageService query failed"
                     << ", user_id=" << user_id
                     << ", after_message_id=" << after_message_id
                     << ", page_index=" << page_index
                     << ", error=" << list_result.status.message);
            return;
        }

        const auto& pending_messages = list_result.value->messages;
        if (pending_messages.empty()) {
            break;
        }

        std::uint64_t page_last_message_id = after_message_id;
        std::vector<std::uint64_t> receiver_confirmed_repair_ids;
        receiver_confirmed_repair_ids.reserve(pending_messages.size());

        for (const rpc::MessageRpcRecord& message : pending_messages) {
            if (!session_is_current() || business_request.DeadlineExpired()) {
                LOG_INFO("gateway persistent offline replay stopped: session/deadline stale"
                         << ", user_id=" << user_id
                         << ", page_index=" << page_index);
                return;
            }

            // ListPendingAfter client validates strict ascending M. Cursor is
            // only scan progress; it does not imply delivery/confirmation.
            page_last_message_id = message.message_id;

            std::string message_text;
            std::string parse_error;
            if (!ParseCanonicalChatBody(
                    message.content,
                    message.from_user_id,
                    message.to_user_id,
                    &message_text,
                    &parse_error)) {
                LOG_WARN("gateway rejected invalid persistent offline chat body"
                         << ", user_id=" << user_id
                         << ", message_id=" << message.message_id
                         << ", error=" << parse_error);
                continue;
            }

            Packet submitted_packet;
            std::string submit_error;
            const ReceiverDeliverySubmitStatus submit_status =
                SubmitReceiverChatDelivery(
                    connection,
                    message.message_id,
                    message.from_user_id,
                    message.to_user_id,
                    message_text,
                    &submitted_packet,
                    &submit_error);

            if (submit_status == ReceiverDeliverySubmitStatus::kSubmitted) {
                LOG_INFO("gateway submitted persistent offline receiver delivery"
                         << ", user_id=" << user_id
                         << ", message_id=" << message.message_id
                         << ", delivery_seq=" << submitted_packet.seq
                         << ", page_index=" << page_index);
                continue;
            }

            if (submit_status ==
                ReceiverDeliverySubmitStatus::kAlreadyConfirmed) {
                // Runtime contains real ACK evidence while durable state is
                // still Pending. Batch repair is monotonic and idempotent.
                receiver_confirmed_repair_ids.push_back(message.message_id);
                continue;
            }

            if (submit_status ==
                ReceiverDeliverySubmitStatus::kConnectionUnavailable) {
                return;
            }

            LOG_WARN("gateway persistent offline delivery submission failed"
                     << ", user_id=" << user_id
                     << ", message_id=" << message.message_id
                     << ", submit_status="
                     << static_cast<int>(submit_status)
                     << ", error=" << submit_error
                     << ", page_index=" << page_index);
        }

        if (!receiver_confirmed_repair_ids.empty()) {
            rpc::ConfirmReceiverBatchRpcRequest repair_request;
            repair_request.receiver_user_id = user_id;
            repair_request.message_ids = receiver_confirmed_repair_ids;

            const auto repair_result =
                message_rpc_client_->ConfirmReceiverBatch(
                    repair_request,
                    make_options("confirm-receiver-batch"));
            if (!repair_result.ok()) {
                // No rollback: tracker evidence is runtime truth. A later
                // replay/duplicate ACK repairs durable state. attempted=true
                // is explicitly durability-uncertain.
                LOG_WARN("gateway persistent offline durable repair failed"
                         << ", user_id=" << user_id
                         << ", requested_count="
                         << receiver_confirmed_repair_ids.size()
                         << ", attempted=" << repair_result.attempted
                         << ", error=" << repair_result.status.message
                         << ", page_index=" << page_index);
            } else {
                LOG_INFO("gateway persistent offline durable repair completed"
                         << ", user_id=" << user_id
                         << ", requested_count="
                         << receiver_confirmed_repair_ids.size()
                         << ", affected_rows="
                         << repair_result.value->affected_rows
                         << ", page_index=" << page_index);
            }
        }

        total_scanned += pending_messages.size();
        if (!list_result.value->has_more) {
            break;
        }
        if (page_last_message_id <= after_message_id) {
            LOG_ERROR("gateway persistent replay cursor failed to advance"
                      << ", user_id=" << user_id
                      << ", cursor=" << after_message_id
                      << ", page_last=" << page_last_message_id);
            return;
        }
        after_message_id = page_last_message_id;
    }

    if (total_scanned != 0) {
        LOG_INFO("gateway persistent offline replay completed"
                 << ", user_id=" << user_id
                 << ", page_count=" << page_index
                 << ", total_scanned=" << total_scanned);
    }
}

/*
    Packet GatewayServer::MakeErrorPacket(std::uint32_t seq,
                    const std::string& message) const {
        Packet packet;
        packet.type = MessageType::kError;
        packet.seq = seq;

        packet.body =
            std::string(R"({"success":false,"message":")") +
            message +
            R"("})";

        return packet;
    }
*/
Packet GatewayServer::MakeErrorPacket(std::uint32_t seq,
                                      const std::string& message) const {
    Packet packet;
    packet.type = MessageType::kError;
    packet.seq = seq;

    packet.body =
        std::string(R"({"success":false,"message":")") +
        message +
        R"("})";

    return packet;
}

}  // namespace tinyimx