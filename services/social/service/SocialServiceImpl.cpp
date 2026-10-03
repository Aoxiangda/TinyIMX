#include "services/social/service/SocialServiceImpl.h"

#include "common/observability/GrpcTracing.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

namespace tinyimx::social {
namespace {

grpc::Status MapFriendApplicationFailure(
    FriendApplicationStatus status,
    const std::string& message
) {
    switch (status) {
        case FriendApplicationStatus::kInvalidArgument:
            return {grpc::StatusCode::INVALID_ARGUMENT, message};
        case FriendApplicationStatus::kInvalidRecord:
            return {grpc::StatusCode::DATA_LOSS, message};
        case FriendApplicationStatus::kStorageError:
            return {grpc::StatusCode::UNAVAILABLE, message};
        case FriendApplicationStatus::kSucceeded:
            return grpc::Status::OK;
    }
    return {grpc::StatusCode::INTERNAL,
            "unknown SocialService friend application status"};
}

std::chrono::milliseconds ResolveFaultDelay(const char* environment_name) {
    const char* value = std::getenv(environment_name);
    if (value == nullptr || value[0] == '\0') {
        return std::chrono::milliseconds::zero();
    }
    try {
        const long long parsed = std::stoll(value);
        if (parsed <= 0) return std::chrono::milliseconds::zero();
        constexpr long long kMaxDelayMs = 60000;
        return std::chrono::milliseconds(parsed > kMaxDelayMs ? kMaxDelayMs : parsed);
    } catch (...) {
        return std::chrono::milliseconds::zero();
    }
}

bool ApplyFaultDelay(grpc::ServerContext* context, const char* environment_name) {
    const auto delay = ResolveFaultDelay(environment_name);
    if (delay <= std::chrono::milliseconds::zero()) return true;
    std::this_thread::sleep_for(delay);
    return context != nullptr && !context->IsCancelled();
}

tinyimx::social::v1::ChatPermissionResult ToProto(
    ChatPermissionApplicationStatus status
) {
    using A = ChatPermissionApplicationStatus;
    using P = tinyimx::social::v1::ChatPermissionResult;
    switch (status) {
        case A::kAllowed: return P::CHAT_PERMISSION_RESULT_ALLOWED;
        case A::kInvalidArgument: return P::CHAT_PERMISSION_RESULT_INVALID_ARGUMENT;
        case A::kNotFriend: return P::CHAT_PERMISSION_RESULT_NOT_FRIEND;
        case A::kBlockedBySelf: return P::CHAT_PERMISSION_RESULT_BLOCKED_BY_SELF;
        case A::kBlockedByPeer: return P::CHAT_PERMISSION_RESULT_BLOCKED_BY_PEER;
        case A::kStorageError: return P::CHAT_PERMISSION_RESULT_STORAGE_ERROR;
    }
    return P::CHAT_PERMISSION_RESULT_UNSPECIFIED;
}

tinyimx::social::v1::FriendRequestCreateResult ToProto(FriendRequestCreateOutcome outcome) {
    using A = FriendRequestCreateOutcome;
    using P = tinyimx::social::v1::FriendRequestCreateResult;
    switch (outcome) {
        case A::kCreated: return P::FRIEND_REQUEST_CREATE_RESULT_CREATED;
        case A::kReopened: return P::FRIEND_REQUEST_CREATE_RESULT_REOPENED;
        case A::kAlreadyPending: return P::FRIEND_REQUEST_CREATE_RESULT_ALREADY_PENDING;
        case A::kReversePending: return P::FRIEND_REQUEST_CREATE_RESULT_REVERSE_PENDING;
        case A::kAlreadyFriend: return P::FRIEND_REQUEST_CREATE_RESULT_ALREADY_FRIEND;
        case A::kBlockedBySelf: return P::FRIEND_REQUEST_CREATE_RESULT_BLOCKED_BY_SELF;
        case A::kBlockedByPeer: return P::FRIEND_REQUEST_CREATE_RESULT_BLOCKED_BY_PEER;
        case A::kSourceUserNotFound: return P::FRIEND_REQUEST_CREATE_RESULT_SOURCE_USER_NOT_FOUND;
        case A::kTargetUserNotFound: return P::FRIEND_REQUEST_CREATE_RESULT_TARGET_USER_NOT_FOUND;
        case A::kSourceUserDisabled: return P::FRIEND_REQUEST_CREATE_RESULT_SOURCE_USER_DISABLED;
        case A::kTargetUserDisabled: return P::FRIEND_REQUEST_CREATE_RESULT_TARGET_USER_DISABLED;
        case A::kRelationDataInconsistent: return P::FRIEND_REQUEST_CREATE_RESULT_RELATION_DATA_INCONSISTENT;
        case A::kRequestDataInconsistent: return P::FRIEND_REQUEST_CREATE_RESULT_REQUEST_DATA_INCONSISTENT;
        case A::kInvalidArgument: return P::FRIEND_REQUEST_CREATE_RESULT_INVALID_ARGUMENT;
        case A::kStorageError: return P::FRIEND_REQUEST_CREATE_RESULT_STORAGE_ERROR;
    }
    return P::FRIEND_REQUEST_CREATE_RESULT_UNSPECIFIED;
}

tinyimx::social::v1::FriendRequestListResult ToProto(FriendRequestListOutcome outcome) {
    using A = FriendRequestListOutcome;
    using P = tinyimx::social::v1::FriendRequestListResult;
    switch (outcome) {
        case A::kSucceeded: return P::FRIEND_REQUEST_LIST_RESULT_SUCCEEDED;
        case A::kInvalidArgument: return P::FRIEND_REQUEST_LIST_RESULT_INVALID_ARGUMENT;
        case A::kInvalidCursor: return P::FRIEND_REQUEST_LIST_RESULT_INVALID_CURSOR;
        case A::kInvalidRecord: return P::FRIEND_REQUEST_LIST_RESULT_INVALID_RECORD;
        case A::kStorageError: return P::FRIEND_REQUEST_LIST_RESULT_STORAGE_ERROR;
    }
    return P::FRIEND_REQUEST_LIST_RESULT_UNSPECIFIED;
}

tinyimx::social::v1::FriendRequestAcceptResult ToProto(FriendRequestAcceptOutcome outcome) {
    using A = FriendRequestAcceptOutcome;
    using P = tinyimx::social::v1::FriendRequestAcceptResult;
    switch (outcome) {
        case A::kAccepted: return P::FRIEND_REQUEST_ACCEPT_RESULT_ACCEPTED;
        case A::kAlreadyAccepted: return P::FRIEND_REQUEST_ACCEPT_RESULT_ALREADY_ACCEPTED;
        case A::kRequestNotFound: return P::FRIEND_REQUEST_ACCEPT_RESULT_REQUEST_NOT_FOUND;
        case A::kNotRequestReceiver: return P::FRIEND_REQUEST_ACCEPT_RESULT_NOT_REQUEST_RECEIVER;
        case A::kRequestNotPending: return P::FRIEND_REQUEST_ACCEPT_RESULT_REQUEST_NOT_PENDING;
        case A::kBlockedBySelf: return P::FRIEND_REQUEST_ACCEPT_RESULT_BLOCKED_BY_SELF;
        case A::kBlockedByPeer: return P::FRIEND_REQUEST_ACCEPT_RESULT_BLOCKED_BY_PEER;
        case A::kRequesterNotFound: return P::FRIEND_REQUEST_ACCEPT_RESULT_REQUESTER_NOT_FOUND;
        case A::kReceiverNotFound: return P::FRIEND_REQUEST_ACCEPT_RESULT_RECEIVER_NOT_FOUND;
        case A::kRequesterDisabled: return P::FRIEND_REQUEST_ACCEPT_RESULT_REQUESTER_DISABLED;
        case A::kReceiverDisabled: return P::FRIEND_REQUEST_ACCEPT_RESULT_RECEIVER_DISABLED;
        case A::kRelationDataInconsistent: return P::FRIEND_REQUEST_ACCEPT_RESULT_RELATION_DATA_INCONSISTENT;
        case A::kRequestDataInconsistent: return P::FRIEND_REQUEST_ACCEPT_RESULT_REQUEST_DATA_INCONSISTENT;
        case A::kInvalidArgument: return P::FRIEND_REQUEST_ACCEPT_RESULT_INVALID_ARGUMENT;
        case A::kStorageError: return P::FRIEND_REQUEST_ACCEPT_RESULT_STORAGE_ERROR;
    }
    return P::FRIEND_REQUEST_ACCEPT_RESULT_UNSPECIFIED;
}

tinyimx::social::v1::FriendRequestRejectResult ToProto(FriendRequestRejectOutcome outcome) {
    using A = FriendRequestRejectOutcome;
    using P = tinyimx::social::v1::FriendRequestRejectResult;
    switch (outcome) {
        case A::kRejected: return P::FRIEND_REQUEST_REJECT_RESULT_REJECTED;
        case A::kAlreadyRejected: return P::FRIEND_REQUEST_REJECT_RESULT_ALREADY_REJECTED;
        case A::kAlreadyAccepted: return P::FRIEND_REQUEST_REJECT_RESULT_ALREADY_ACCEPTED;
        case A::kRequestCancelled: return P::FRIEND_REQUEST_REJECT_RESULT_REQUEST_CANCELLED;
        case A::kRequestNotFound: return P::FRIEND_REQUEST_REJECT_RESULT_REQUEST_NOT_FOUND;
        case A::kNotRequestReceiver: return P::FRIEND_REQUEST_REJECT_RESULT_NOT_REQUEST_RECEIVER;
        case A::kRequestDataInconsistent: return P::FRIEND_REQUEST_REJECT_RESULT_REQUEST_DATA_INCONSISTENT;
        case A::kInvalidArgument: return P::FRIEND_REQUEST_REJECT_RESULT_INVALID_ARGUMENT;
        case A::kStorageError: return P::FRIEND_REQUEST_REJECT_RESULT_STORAGE_ERROR;
    }
    return P::FRIEND_REQUEST_REJECT_RESULT_UNSPECIFIED;
}

void FillFriendRequest(const FriendRequestView& source,
                       tinyimx::social::v1::FriendRequestInfo* target) {
    if (target == nullptr) return;
    target->set_request_id(source.request_id);
    target->set_from_user_id(source.from_user_id);
    target->set_to_user_id(source.to_user_id);
    target->set_request_message(source.request_message);
    target->set_request_status(source.request_status);
    target->set_created_at(source.created_at);
    target->set_handled_at(source.handled_at);
    target->set_updated_at(source.updated_at);
    target->set_from_username(source.from_username);
    target->set_from_nickname(source.from_nickname);
    target->set_from_avatar_url(source.from_avatar_url);
    target->set_from_user_status(source.from_user_status);
}

}  // namespace

SocialServiceImpl::SocialServiceImpl(
    FriendApplicationService* friend_application_service,
    FriendRequestApplicationService* friend_request_application_service
)
    : friend_application_service_(friend_application_service),
      friend_request_application_service_(friend_request_application_service) {}

grpc::Status SocialServiceImpl::ListFriends(
    grpc::ServerContext* context,
    const tinyimx::social::v1::ListFriendsRequest* request,
    tinyimx::social::v1::ListFriendsResponse* response
) {
    if (context == nullptr || request == nullptr || response == nullptr) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "invalid ListFriends RPC arguments"};
    }
    auto rpc_span = observability::StartGrpcServerSpan(
        "tinyimx.social.v1.SocialService", "ListFriends", context);
    const auto finish = [&](grpc::Status status) {
        observability::FinishGrpcServerSpan(
            &rpc_span, status, "tinyimx.social.v1.SocialService", "ListFriends");
        return status;
    };
    if (friend_application_service_ == nullptr) {
        return finish({grpc::StatusCode::UNAVAILABLE,
                       "SocialService friend application service is unavailable"});
    }
    auto result = friend_application_service_->ListFriends(
        request->actor_user_id(), request->limit());
    if (!result.Succeeded()) {
        return finish(MapFriendApplicationFailure(result.status, result.message));
    }
    response->set_has_more(result.has_more);
    for (const auto& friend_view : result.friends) {
        auto* proto_friend = response->add_friends();
        proto_friend->set_friend_user_id(friend_view.friend_user_id);
        proto_friend->set_username(friend_view.username);
        proto_friend->set_nickname(friend_view.nickname);
        proto_friend->set_avatar_url(friend_view.avatar_url);
        proto_friend->set_user_status(friend_view.user_status);
        proto_friend->set_relation_status(friend_view.relation_status);
        proto_friend->set_relation_created_at(friend_view.relation_created_at);
        proto_friend->set_relation_updated_at(friend_view.relation_updated_at);
    }
    return finish(grpc::Status::OK);
}

grpc::Status SocialServiceImpl::CheckPrivateChatPermission(
    grpc::ServerContext* context,
    const tinyimx::social::v1::CheckPrivateChatPermissionRequest* request,
    tinyimx::social::v1::CheckPrivateChatPermissionResponse* response
) {
    if (context == nullptr || request == nullptr || response == nullptr) {
        return {grpc::StatusCode::INVALID_ARGUMENT,
                "invalid CheckPrivateChatPermission RPC arguments"};
    }
    auto rpc_span = observability::StartGrpcServerSpan(
        "tinyimx.social.v1.SocialService", "CheckPrivateChatPermission", context);
    const auto finish = [&](grpc::Status status) {
        observability::FinishGrpcServerSpan(&rpc_span, status,
            "tinyimx.social.v1.SocialService", "CheckPrivateChatPermission");
        return status;
    };
    if (friend_application_service_ == nullptr) {
        return finish({grpc::StatusCode::UNAVAILABLE,
                       "SocialService friend application service is unavailable"});
    }
    const auto result = friend_application_service_->CheckPrivateChatPermission(
        request->from_user_id(), request->to_user_id());
    response->set_result(ToProto(result.status));
    response->set_message(result.message);
    return finish(grpc::Status::OK);
}

grpc::Status SocialServiceImpl::CreateFriendRequest(
    grpc::ServerContext* context,
    const tinyimx::social::v1::CreateFriendRequestRequest* request,
    tinyimx::social::v1::CreateFriendRequestResponse* response
) {
    if (context == nullptr || request == nullptr || response == nullptr) {
        return {grpc::StatusCode::INVALID_ARGUMENT,
                "invalid CreateFriendRequest RPC arguments"};
    }
    auto rpc_span = observability::StartGrpcServerSpan(
        "tinyimx.social.v1.SocialService", "CreateFriendRequest", context);
    const auto finish = [&](grpc::Status status) {
        observability::FinishGrpcServerSpan(
            &rpc_span, status, "tinyimx.social.v1.SocialService", "CreateFriendRequest");
        return status;
    };
    if (friend_request_application_service_ == nullptr) {
        return finish({grpc::StatusCode::UNAVAILABLE,
                "SocialService friend request application service is unavailable"});
    }
    auto result = friend_request_application_service_->Create(
        request->from_user_id(), request->to_user_id(), request->request_message());
    response->set_result(ToProto(result.outcome));
    response->set_request_id(result.request_id);
    response->set_message(result.message);
    if (result.Changed() && !ApplyFaultDelay(
            context, "TINYIMX_FAULT_SOCIAL_CREATE_FRIEND_REQUEST_POST_COMMIT_DELAY_MS")) {
        return finish({grpc::StatusCode::CANCELLED,
                "CreateFriendRequest RPC cancelled after durable result"});
    }
    return finish(grpc::Status::OK);
}

grpc::Status SocialServiceImpl::ListPendingIncomingFriendRequests(
    grpc::ServerContext* context,
    const tinyimx::social::v1::ListPendingIncomingFriendRequestsRequest* request,
    tinyimx::social::v1::ListPendingIncomingFriendRequestsResponse* response
) {
    if (context == nullptr || request == nullptr || response == nullptr) {
        return {grpc::StatusCode::INVALID_ARGUMENT,
                "invalid ListPendingIncomingFriendRequests RPC arguments"};
    }
    auto rpc_span = observability::StartGrpcServerSpan(
        "tinyimx.social.v1.SocialService", "ListPendingIncomingFriendRequests", context);
    const auto finish = [&](grpc::Status status) {
        observability::FinishGrpcServerSpan(
            &rpc_span, status, "tinyimx.social.v1.SocialService", "ListPendingIncomingFriendRequests");
        return status;
    };
    if (friend_request_application_service_ == nullptr) {
        return finish({grpc::StatusCode::UNAVAILABLE,
                "SocialService friend request application service is unavailable"});
    }
    auto result = friend_request_application_service_->ListPendingIncoming(
        request->receiver_user_id(), request->before_created_at(),
        request->before_request_id(), request->limit());
    response->set_result(ToProto(result.outcome));
    response->set_has_more(result.has_more);
    response->set_next_before_created_at(result.next_before_created_at);
    response->set_next_before_request_id(result.next_before_request_id);
    response->set_message(result.message);
    for (const auto& item : result.requests) {
        FillFriendRequest(item, response->add_requests());
    }
    return finish(grpc::Status::OK);
}

grpc::Status SocialServiceImpl::AcceptFriendRequest(
    grpc::ServerContext* context,
    const tinyimx::social::v1::AcceptFriendRequestRequest* request,
    tinyimx::social::v1::AcceptFriendRequestResponse* response
) {
    if (context == nullptr || request == nullptr || response == nullptr) {
        return {grpc::StatusCode::INVALID_ARGUMENT,
                "invalid AcceptFriendRequest RPC arguments"};
    }
    auto rpc_span = observability::StartGrpcServerSpan(
        "tinyimx.social.v1.SocialService", "AcceptFriendRequest", context);
    const auto finish = [&](grpc::Status status) {
        observability::FinishGrpcServerSpan(
            &rpc_span, status, "tinyimx.social.v1.SocialService", "AcceptFriendRequest");
        return status;
    };
    if (friend_request_application_service_ == nullptr) {
        return finish({grpc::StatusCode::UNAVAILABLE,
                "SocialService friend request application service is unavailable"});
    }
    auto result = friend_request_application_service_->Accept(
        request->request_id(), request->handler_user_id());
    response->set_result(ToProto(result.outcome));
    response->set_request_id(result.request_id);
    response->set_requester_user_id(result.requester_user_id);
    response->set_receiver_user_id(result.receiver_user_id);
    response->set_message(result.message);
    if (result.Changed() && !ApplyFaultDelay(
            context, "TINYIMX_FAULT_SOCIAL_ACCEPT_FRIEND_REQUEST_POST_COMMIT_DELAY_MS")) {
        return finish({grpc::StatusCode::CANCELLED,
                "AcceptFriendRequest RPC cancelled after durable result"});
    }
    return finish(grpc::Status::OK);
}

grpc::Status SocialServiceImpl::RejectFriendRequest(
    grpc::ServerContext* context,
    const tinyimx::social::v1::RejectFriendRequestRequest* request,
    tinyimx::social::v1::RejectFriendRequestResponse* response
) {
    if (context == nullptr || request == nullptr || response == nullptr) {
        return {grpc::StatusCode::INVALID_ARGUMENT,
                "invalid RejectFriendRequest RPC arguments"};
    }
    auto rpc_span = observability::StartGrpcServerSpan(
        "tinyimx.social.v1.SocialService", "RejectFriendRequest", context);
    const auto finish = [&](grpc::Status status) {
        observability::FinishGrpcServerSpan(
            &rpc_span, status, "tinyimx.social.v1.SocialService", "RejectFriendRequest");
        return status;
    };
    if (friend_request_application_service_ == nullptr) {
        return finish({grpc::StatusCode::UNAVAILABLE,
                "SocialService friend request application service is unavailable"});
    }
    auto result = friend_request_application_service_->Reject(
        request->request_id(), request->handler_user_id());
    response->set_result(ToProto(result.outcome));
    response->set_request_id(result.request_id);
    response->set_requester_user_id(result.requester_user_id);
    response->set_receiver_user_id(result.receiver_user_id);
    response->set_message(result.message);
    if (result.Changed() && !ApplyFaultDelay(
            context, "TINYIMX_FAULT_SOCIAL_REJECT_FRIEND_REQUEST_POST_COMMIT_DELAY_MS")) {
        return finish({grpc::StatusCode::CANCELLED,
                "RejectFriendRequest RPC cancelled after durable result"});
    }
    return finish(grpc::Status::OK);
}

}  // namespace tinyimx::social
