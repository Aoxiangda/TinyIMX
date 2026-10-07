#include "common/db/PermissionBoundaryTrace.h"
#include "services/rpc/SocialRpcClient.h"

#include "common/observability/GrpcTracing.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace tinyimx::rpc {
namespace {

template <typename T>
RpcResult<T> QueryFailure(RpcErrorCode code, std::string message) {
    return RpcResult<T>::Failure(code, std::move(message));
}

void FillMeta(
    const RpcCallOptions& options,
    tinyimx::common::v1::RequestMeta* meta
) {
    if (meta == nullptr) return;
    meta->set_request_id(options.request_id);
    meta->set_trace_id(options.trace_id);
    meta->set_caller_service(options.caller_service);
    meta->set_caller_instance(options.caller_instance);
}

std::optional<ChatPermissionRpcOutcome> FromProto(
    tinyimx::social::v1::ChatPermissionResult value
) {
    using P = tinyimx::social::v1::ChatPermissionResult;
    using O = ChatPermissionRpcOutcome;
    switch (value) {
        case P::CHAT_PERMISSION_RESULT_ALLOWED: return O::kAllowed;
        case P::CHAT_PERMISSION_RESULT_INVALID_ARGUMENT: return O::kInvalidArgument;
        case P::CHAT_PERMISSION_RESULT_NOT_FRIEND: return O::kNotFriend;
        case P::CHAT_PERMISSION_RESULT_BLOCKED_BY_SELF: return O::kBlockedBySelf;
        case P::CHAT_PERMISSION_RESULT_BLOCKED_BY_PEER: return O::kBlockedByPeer;
        case P::CHAT_PERMISSION_RESULT_STORAGE_ERROR: return O::kStorageError;
        case P::CHAT_PERMISSION_RESULT_UNSPECIFIED: break;
    }
    return std::nullopt;
}

std::optional<FriendRequestCreateRpcOutcome> FromProto(
    tinyimx::social::v1::FriendRequestCreateResult value
) {
    using P = tinyimx::social::v1::FriendRequestCreateResult;
    using O = FriendRequestCreateRpcOutcome;
    switch (value) {
        case P::FRIEND_REQUEST_CREATE_RESULT_CREATED: return O::kCreated;
        case P::FRIEND_REQUEST_CREATE_RESULT_REOPENED: return O::kReopened;
        case P::FRIEND_REQUEST_CREATE_RESULT_ALREADY_PENDING: return O::kAlreadyPending;
        case P::FRIEND_REQUEST_CREATE_RESULT_REVERSE_PENDING: return O::kReversePending;
        case P::FRIEND_REQUEST_CREATE_RESULT_ALREADY_FRIEND: return O::kAlreadyFriend;
        case P::FRIEND_REQUEST_CREATE_RESULT_BLOCKED_BY_SELF: return O::kBlockedBySelf;
        case P::FRIEND_REQUEST_CREATE_RESULT_BLOCKED_BY_PEER: return O::kBlockedByPeer;
        case P::FRIEND_REQUEST_CREATE_RESULT_SOURCE_USER_NOT_FOUND: return O::kSourceUserNotFound;
        case P::FRIEND_REQUEST_CREATE_RESULT_TARGET_USER_NOT_FOUND: return O::kTargetUserNotFound;
        case P::FRIEND_REQUEST_CREATE_RESULT_SOURCE_USER_DISABLED: return O::kSourceUserDisabled;
        case P::FRIEND_REQUEST_CREATE_RESULT_TARGET_USER_DISABLED: return O::kTargetUserDisabled;
        case P::FRIEND_REQUEST_CREATE_RESULT_RELATION_DATA_INCONSISTENT: return O::kRelationDataInconsistent;
        case P::FRIEND_REQUEST_CREATE_RESULT_REQUEST_DATA_INCONSISTENT: return O::kRequestDataInconsistent;
        case P::FRIEND_REQUEST_CREATE_RESULT_INVALID_ARGUMENT: return O::kInvalidArgument;
        case P::FRIEND_REQUEST_CREATE_RESULT_STORAGE_ERROR: return O::kStorageError;
        case P::FRIEND_REQUEST_CREATE_RESULT_UNSPECIFIED: break;
    }
    return std::nullopt;
}

std::optional<FriendRequestListRpcOutcome> FromProto(
    tinyimx::social::v1::FriendRequestListResult value
) {
    using P = tinyimx::social::v1::FriendRequestListResult;
    using O = FriendRequestListRpcOutcome;
    switch (value) {
        case P::FRIEND_REQUEST_LIST_RESULT_SUCCEEDED: return O::kSucceeded;
        case P::FRIEND_REQUEST_LIST_RESULT_INVALID_ARGUMENT: return O::kInvalidArgument;
        case P::FRIEND_REQUEST_LIST_RESULT_INVALID_CURSOR: return O::kInvalidCursor;
        case P::FRIEND_REQUEST_LIST_RESULT_INVALID_RECORD: return O::kInvalidRecord;
        case P::FRIEND_REQUEST_LIST_RESULT_STORAGE_ERROR: return O::kStorageError;
        case P::FRIEND_REQUEST_LIST_RESULT_UNSPECIFIED: break;
    }
    return std::nullopt;
}

std::optional<FriendRequestAcceptRpcOutcome> FromProto(
    tinyimx::social::v1::FriendRequestAcceptResult value
) {
    using P = tinyimx::social::v1::FriendRequestAcceptResult;
    using O = FriendRequestAcceptRpcOutcome;
    switch (value) {
        case P::FRIEND_REQUEST_ACCEPT_RESULT_ACCEPTED: return O::kAccepted;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_ALREADY_ACCEPTED: return O::kAlreadyAccepted;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_REQUEST_NOT_FOUND: return O::kRequestNotFound;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_NOT_REQUEST_RECEIVER: return O::kNotRequestReceiver;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_REQUEST_NOT_PENDING: return O::kRequestNotPending;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_BLOCKED_BY_SELF: return O::kBlockedBySelf;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_BLOCKED_BY_PEER: return O::kBlockedByPeer;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_REQUESTER_NOT_FOUND: return O::kRequesterNotFound;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_RECEIVER_NOT_FOUND: return O::kReceiverNotFound;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_REQUESTER_DISABLED: return O::kRequesterDisabled;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_RECEIVER_DISABLED: return O::kReceiverDisabled;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_RELATION_DATA_INCONSISTENT: return O::kRelationDataInconsistent;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_REQUEST_DATA_INCONSISTENT: return O::kRequestDataInconsistent;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_INVALID_ARGUMENT: return O::kInvalidArgument;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_STORAGE_ERROR: return O::kStorageError;
        case P::FRIEND_REQUEST_ACCEPT_RESULT_UNSPECIFIED: break;
    }
    return std::nullopt;
}

std::optional<FriendRequestRejectRpcOutcome> FromProto(
    tinyimx::social::v1::FriendRequestRejectResult value
) {
    using P = tinyimx::social::v1::FriendRequestRejectResult;
    using O = FriendRequestRejectRpcOutcome;
    switch (value) {
        case P::FRIEND_REQUEST_REJECT_RESULT_REJECTED: return O::kRejected;
        case P::FRIEND_REQUEST_REJECT_RESULT_ALREADY_REJECTED: return O::kAlreadyRejected;
        case P::FRIEND_REQUEST_REJECT_RESULT_ALREADY_ACCEPTED: return O::kAlreadyAccepted;
        case P::FRIEND_REQUEST_REJECT_RESULT_REQUEST_CANCELLED: return O::kRequestCancelled;
        case P::FRIEND_REQUEST_REJECT_RESULT_REQUEST_NOT_FOUND: return O::kRequestNotFound;
        case P::FRIEND_REQUEST_REJECT_RESULT_NOT_REQUEST_RECEIVER: return O::kNotRequestReceiver;
        case P::FRIEND_REQUEST_REJECT_RESULT_REQUEST_DATA_INCONSISTENT: return O::kRequestDataInconsistent;
        case P::FRIEND_REQUEST_REJECT_RESULT_INVALID_ARGUMENT: return O::kInvalidArgument;
        case P::FRIEND_REQUEST_REJECT_RESULT_STORAGE_ERROR: return O::kStorageError;
        case P::FRIEND_REQUEST_REJECT_RESULT_UNSPECIFIED: break;
    }
    return std::nullopt;
}

SocialFriendRequestRecord ToRpcRecord(
    const tinyimx::social::v1::FriendRequestInfo& source
) {
    SocialFriendRequestRecord target;
    target.request_id = source.request_id();
    target.from_user_id = source.from_user_id();
    target.to_user_id = source.to_user_id();
    target.request_message = source.request_message();
    target.request_status = source.request_status();
    target.created_at = source.created_at();
    target.handled_at = source.handled_at();
    target.updated_at = source.updated_at();
    target.from_username = source.from_username();
    target.from_nickname = source.from_nickname();
    target.from_avatar_url = source.from_avatar_url();
    target.from_user_status = source.from_user_status();
    return target;
}

}  // namespace

const char* ChatPermissionRpcOutcomeToReason(ChatPermissionRpcOutcome outcome) noexcept {
    switch (outcome) {
        case ChatPermissionRpcOutcome::kAllowed: return "allowed";
        case ChatPermissionRpcOutcome::kInvalidArgument: return "invalid_argument";
        case ChatPermissionRpcOutcome::kNotFriend: return "not_friend";
        case ChatPermissionRpcOutcome::kBlockedBySelf: return "blocked_by_self";
        case ChatPermissionRpcOutcome::kBlockedByPeer: return "blocked_by_peer";
        case ChatPermissionRpcOutcome::kStorageError: return "storage_error";
    }
    return "unknown";
}

const char* FriendRequestCreateRpcOutcomeToReason(FriendRequestCreateRpcOutcome outcome) noexcept {
    switch (outcome) {
        case FriendRequestCreateRpcOutcome::kCreated: return "created";
        case FriendRequestCreateRpcOutcome::kReopened: return "reopened";
        case FriendRequestCreateRpcOutcome::kAlreadyPending: return "already_pending";
        case FriendRequestCreateRpcOutcome::kReversePending: return "reverse_pending";
        case FriendRequestCreateRpcOutcome::kAlreadyFriend: return "already_friend";
        case FriendRequestCreateRpcOutcome::kBlockedBySelf: return "blocked_by_self";
        case FriendRequestCreateRpcOutcome::kBlockedByPeer: return "blocked_by_peer";
        case FriendRequestCreateRpcOutcome::kSourceUserNotFound: return "source_user_not_found";
        case FriendRequestCreateRpcOutcome::kTargetUserNotFound: return "target_user_not_found";
        case FriendRequestCreateRpcOutcome::kSourceUserDisabled: return "source_user_disabled";
        case FriendRequestCreateRpcOutcome::kTargetUserDisabled: return "target_user_disabled";
        case FriendRequestCreateRpcOutcome::kRelationDataInconsistent: return "relation_data_inconsistent";
        case FriendRequestCreateRpcOutcome::kRequestDataInconsistent: return "request_data_inconsistent";
        case FriendRequestCreateRpcOutcome::kInvalidArgument: return "invalid_argument";
        case FriendRequestCreateRpcOutcome::kStorageError: return "storage_error";
    }
    return "unknown";
}

const char* FriendRequestListRpcOutcomeToReason(FriendRequestListRpcOutcome outcome) noexcept {
    switch (outcome) {
        case FriendRequestListRpcOutcome::kSucceeded: return "succeeded";
        case FriendRequestListRpcOutcome::kInvalidArgument: return "invalid_argument";
        case FriendRequestListRpcOutcome::kInvalidCursor: return "invalid_cursor";
        case FriendRequestListRpcOutcome::kInvalidRecord: return "invalid_record";
        case FriendRequestListRpcOutcome::kStorageError: return "storage_error";
    }
    return "unknown";
}

const char* FriendRequestAcceptRpcOutcomeToReason(FriendRequestAcceptRpcOutcome outcome) noexcept {
    switch (outcome) {
        case FriendRequestAcceptRpcOutcome::kAccepted: return "accepted";
        case FriendRequestAcceptRpcOutcome::kAlreadyAccepted: return "already_accepted";
        case FriendRequestAcceptRpcOutcome::kRequestNotFound: return "request_not_found";
        case FriendRequestAcceptRpcOutcome::kNotRequestReceiver: return "not_request_receiver";
        case FriendRequestAcceptRpcOutcome::kRequestNotPending: return "request_not_pending";
        case FriendRequestAcceptRpcOutcome::kBlockedBySelf: return "blocked_by_self";
        case FriendRequestAcceptRpcOutcome::kBlockedByPeer: return "blocked_by_peer";
        case FriendRequestAcceptRpcOutcome::kRequesterNotFound: return "requester_not_found";
        case FriendRequestAcceptRpcOutcome::kReceiverNotFound: return "receiver_not_found";
        case FriendRequestAcceptRpcOutcome::kRequesterDisabled: return "requester_disabled";
        case FriendRequestAcceptRpcOutcome::kReceiverDisabled: return "receiver_disabled";
        case FriendRequestAcceptRpcOutcome::kRelationDataInconsistent: return "relation_data_inconsistent";
        case FriendRequestAcceptRpcOutcome::kRequestDataInconsistent: return "request_data_inconsistent";
        case FriendRequestAcceptRpcOutcome::kInvalidArgument: return "invalid_argument";
        case FriendRequestAcceptRpcOutcome::kStorageError: return "storage_error";
    }
    return "unknown";
}

const char* FriendRequestRejectRpcOutcomeToReason(FriendRequestRejectRpcOutcome outcome) noexcept {
    switch (outcome) {
        case FriendRequestRejectRpcOutcome::kRejected: return "rejected";
        case FriendRequestRejectRpcOutcome::kAlreadyRejected: return "already_rejected";
        case FriendRequestRejectRpcOutcome::kAlreadyAccepted: return "already_accepted";
        case FriendRequestRejectRpcOutcome::kRequestCancelled: return "request_cancelled";
        case FriendRequestRejectRpcOutcome::kRequestNotFound: return "request_not_found";
        case FriendRequestRejectRpcOutcome::kNotRequestReceiver: return "not_request_receiver";
        case FriendRequestRejectRpcOutcome::kRequestDataInconsistent: return "request_data_inconsistent";
        case FriendRequestRejectRpcOutcome::kInvalidArgument: return "invalid_argument";
        case FriendRequestRejectRpcOutcome::kStorageError: return "storage_error";
    }
    return "unknown";
}

SocialRpcClient::SocialRpcClient(
    std::shared_ptr<const ServiceEndpointProvider> endpoint_provider
) : endpoint_provider_(std::move(endpoint_provider)) {}

RpcResult<ListFriendsRpcResponse> SocialRpcClient::ListFriends(
    const ListFriendsRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.actor_user_id == 0 || request.limit == 0) {
        return QueryFailure<ListFriendsRpcResponse>(
            RpcErrorCode::kInvalidArgument, "invalid ListFriends request");
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return QueryFailure<ListFriendsRpcResponse>(
            RpcErrorCode::kDeadlineExceeded, "ListFriends remaining RPC budget is exhausted");
    }
    if (!endpoint_provider_) {
        return QueryFailure<ListFriendsRpcResponse>(
            RpcErrorCode::kUnavailable, "SocialService endpoint provider is not configured");
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kSocial);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return QueryFailure<ListFriendsRpcResponse>(
            RpcErrorCode::kUnavailable, "SocialService endpoint is unavailable");
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return QueryFailure<ListFriendsRpcResponse>(
            RpcErrorCode::kUnavailable, "SocialService gRPC stub could not be created");
    }

    tinyimx::social::v1::ListFriendsRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_actor_user_id(request.actor_user_id);
    proto_request.set_limit(request.limit);
    tinyimx::social::v1::ListFriendsResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout);
    auto rpc_span = observability::StartGrpcClientSpan(
        "tinyimx.social.v1.SocialService", "ListFriends", &context);
    const grpc::Status grpc_status = stub->ListFriends(&context, proto_request, &proto_response);
    observability::FinishGrpcClientSpan(
        &rpc_span, grpc_status, "tinyimx.social.v1.SocialService", "ListFriends");
    if (!grpc_status.ok()) {
        const auto mapped = MapGrpcStatus(grpc_status);
        return QueryFailure<ListFriendsRpcResponse>(mapped.code, mapped.message);
    }

    ListFriendsRpcResponse response;
    response.has_more = proto_response.has_more();
    response.friends.reserve(static_cast<std::size_t>(proto_response.friends_size()));
    for (const auto& proto_friend : proto_response.friends()) {
        SocialFriend friend_info;
        friend_info.friend_user_id = proto_friend.friend_user_id();
        friend_info.username = proto_friend.username();
        friend_info.nickname = proto_friend.nickname();
        friend_info.avatar_url = proto_friend.avatar_url();
        friend_info.user_status = proto_friend.user_status();
        friend_info.relation_status = proto_friend.relation_status();
        friend_info.relation_created_at = proto_friend.relation_created_at();
        friend_info.relation_updated_at = proto_friend.relation_updated_at();
        response.friends.push_back(std::move(friend_info));
    }
    return RpcResult<ListFriendsRpcResponse>::Success(std::move(response));
}

RpcResult<CheckPrivateChatPermissionRpcResponse>
SocialRpcClient::CheckPrivateChatPermission(
    const CheckPrivateChatPermissionRpcRequest& request,
    const RpcCallOptions& options
) const {
    diagnostics::PermissionBoundaryTrace trace(1,request.from_user_id,request.to_user_id,options.request_id,options.caller_instance);
    if (request.from_user_id == 0 || request.to_user_id == 0 ||
        request.from_user_id == request.to_user_id) {
        return QueryFailure<CheckPrivateChatPermissionRpcResponse>(
            RpcErrorCode::kInvalidArgument, "invalid CheckPrivateChatPermission request");
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return QueryFailure<CheckPrivateChatPermissionRpcResponse>(
            RpcErrorCode::kDeadlineExceeded,
            "CheckPrivateChatPermission remaining RPC budget is exhausted");
    }
    if (!endpoint_provider_) {
        return QueryFailure<CheckPrivateChatPermissionRpcResponse>(
            RpcErrorCode::kUnavailable, "SocialService endpoint provider is not configured");
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kSocial);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return QueryFailure<CheckPrivateChatPermissionRpcResponse>(
            RpcErrorCode::kUnavailable, "SocialService endpoint is unavailable");
    }
    trace.Mark(1);
    auto stub = GetOrCreateStub(*endpoint);
    trace.Mark(2);
    if (!stub) {
        return QueryFailure<CheckPrivateChatPermissionRpcResponse>(
            RpcErrorCode::kUnavailable, "SocialService gRPC stub could not be created");
    }

    tinyimx::social::v1::CheckPrivateChatPermissionRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_from_user_id(request.from_user_id);
    proto_request.set_to_user_id(request.to_user_id);
    tinyimx::social::v1::CheckPrivateChatPermissionResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout);
    auto rpc_span = observability::StartGrpcClientSpan(
        "tinyimx.social.v1.SocialService", "CheckPrivateChatPermission", &context);
    trace.Mark(3);
    const auto grpc_status = stub->CheckPrivateChatPermission(&context, proto_request, &proto_response);
    trace.Mark(4);
    trace.Result(static_cast<int>(grpc_status.error_code()));
    observability::FinishGrpcClientSpan(
        &rpc_span, grpc_status, "tinyimx.social.v1.SocialService", "CheckPrivateChatPermission");
    trace.Mark(5);
    if (!grpc_status.ok()) {
        const auto mapped = MapGrpcStatus(grpc_status);
        return QueryFailure<CheckPrivateChatPermissionRpcResponse>(mapped.code, mapped.message);
    }
    const auto outcome = FromProto(proto_response.result());
    if (!outcome.has_value()) {
        return QueryFailure<CheckPrivateChatPermissionRpcResponse>(
            RpcErrorCode::kDataLoss, "SocialService returned unspecified chat permission result");
    }
    CheckPrivateChatPermissionRpcResponse response;
    response.outcome = *outcome;
    response.message = proto_response.message();
    return RpcResult<CheckPrivateChatPermissionRpcResponse>::Success(std::move(response));
}

CreateFriendRequestRpcCallResult SocialRpcClient::CreateFriendRequest(
    const CreateFriendRequestRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.from_user_id == 0 || request.to_user_id == 0 ||
        request.from_user_id == request.to_user_id || request.request_message.size() > 255) {
        return CreateFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kInvalidArgument, "invalid CreateFriendRequest request", false);
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return CreateFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "CreateFriendRequest remaining RPC budget is exhausted", false);
    }
    if (!endpoint_provider_) {
        return CreateFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kUnavailable, "SocialService endpoint provider is not configured", false);
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kSocial);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return CreateFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kUnavailable, "SocialService endpoint is unavailable", false);
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return CreateFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kUnavailable, "SocialService gRPC stub could not be created", false);
    }

    tinyimx::social::v1::CreateFriendRequestRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_from_user_id(request.from_user_id);
    proto_request.set_to_user_id(request.to_user_id);
    proto_request.set_request_message(request.request_message);
    tinyimx::social::v1::CreateFriendRequestResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout);
    auto rpc_span = observability::StartGrpcClientSpan(
        "tinyimx.social.v1.SocialService", "CreateFriendRequest", &context);
    const auto grpc_status = stub->CreateFriendRequest(&context, proto_request, &proto_response);
    observability::FinishGrpcClientSpan(
        &rpc_span, grpc_status, "tinyimx.social.v1.SocialService", "CreateFriendRequest");
    if (!grpc_status.ok()) {
        const auto mapped = MapGrpcStatus(grpc_status);
        return CreateFriendRequestRpcCallResult::Failure(mapped.code, mapped.message, true);
    }
    const auto outcome = FromProto(proto_response.result());
    if (!outcome.has_value()) {
        return CreateFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kDataLoss,
            "SocialService returned unspecified friend request create result", true);
    }
    CreateFriendRequestRpcResponse response;
    response.outcome = *outcome;
    response.request_id = proto_response.request_id();
    response.message = proto_response.message();
    return CreateFriendRequestRpcCallResult::Success(std::move(response));
}

RpcResult<ListPendingIncomingFriendRequestsRpcResponse>
SocialRpcClient::ListPendingIncomingFriendRequests(
    const ListPendingIncomingFriendRequestsRpcRequest& request,
    const RpcCallOptions& options
) const {
    const bool first_page = request.before_created_at.empty() && request.before_request_id == 0;
    const bool next_page = !request.before_created_at.empty() && request.before_request_id != 0;
    if (request.receiver_user_id == 0 || request.limit == 0 || request.limit > 50 ||
        (!first_page && !next_page)) {
        return QueryFailure<ListPendingIncomingFriendRequestsRpcResponse>(
            RpcErrorCode::kInvalidArgument,
            "invalid ListPendingIncomingFriendRequests request");
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return QueryFailure<ListPendingIncomingFriendRequestsRpcResponse>(
            RpcErrorCode::kDeadlineExceeded,
            "ListPendingIncomingFriendRequests remaining RPC budget is exhausted");
    }
    if (!endpoint_provider_) {
        return QueryFailure<ListPendingIncomingFriendRequestsRpcResponse>(
            RpcErrorCode::kUnavailable, "SocialService endpoint provider is not configured");
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kSocial);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return QueryFailure<ListPendingIncomingFriendRequestsRpcResponse>(
            RpcErrorCode::kUnavailable, "SocialService endpoint is unavailable");
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return QueryFailure<ListPendingIncomingFriendRequestsRpcResponse>(
            RpcErrorCode::kUnavailable, "SocialService gRPC stub could not be created");
    }

    tinyimx::social::v1::ListPendingIncomingFriendRequestsRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_receiver_user_id(request.receiver_user_id);
    proto_request.set_before_created_at(request.before_created_at);
    proto_request.set_before_request_id(request.before_request_id);
    proto_request.set_limit(request.limit);
    tinyimx::social::v1::ListPendingIncomingFriendRequestsResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout);
    auto rpc_span = observability::StartGrpcClientSpan(
        "tinyimx.social.v1.SocialService", "ListPendingIncomingFriendRequests", &context);
    const auto grpc_status = stub->ListPendingIncomingFriendRequests(&context, proto_request, &proto_response);
    observability::FinishGrpcClientSpan(
        &rpc_span, grpc_status, "tinyimx.social.v1.SocialService", "ListPendingIncomingFriendRequests");
    if (!grpc_status.ok()) {
        const auto mapped = MapGrpcStatus(grpc_status);
        return QueryFailure<ListPendingIncomingFriendRequestsRpcResponse>(mapped.code, mapped.message);
    }
    const auto outcome = FromProto(proto_response.result());
    if (!outcome.has_value()) {
        return QueryFailure<ListPendingIncomingFriendRequestsRpcResponse>(
            RpcErrorCode::kDataLoss, "SocialService returned unspecified friend request list result");
    }
    ListPendingIncomingFriendRequestsRpcResponse response;
    response.outcome = *outcome;
    response.has_more = proto_response.has_more();
    response.next_before_created_at = proto_response.next_before_created_at();
    response.next_before_request_id = proto_response.next_before_request_id();
    response.message = proto_response.message();
    response.requests.reserve(static_cast<std::size_t>(proto_response.requests_size()));
    for (const auto& item : proto_response.requests()) {
        response.requests.push_back(ToRpcRecord(item));
    }
    return RpcResult<ListPendingIncomingFriendRequestsRpcResponse>::Success(std::move(response));
}

AcceptFriendRequestRpcCallResult SocialRpcClient::AcceptFriendRequest(
    const AcceptFriendRequestRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.request_id == 0 || request.handler_user_id == 0) {
        return AcceptFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kInvalidArgument, "invalid AcceptFriendRequest request", false);
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return AcceptFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "AcceptFriendRequest remaining RPC budget is exhausted", false);
    }
    if (!endpoint_provider_) {
        return AcceptFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kUnavailable, "SocialService endpoint provider is not configured", false);
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kSocial);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return AcceptFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kUnavailable, "SocialService endpoint is unavailable", false);
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return AcceptFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kUnavailable, "SocialService gRPC stub could not be created", false);
    }

    tinyimx::social::v1::AcceptFriendRequestRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_request_id(request.request_id);
    proto_request.set_handler_user_id(request.handler_user_id);
    tinyimx::social::v1::AcceptFriendRequestResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout);
    auto rpc_span = observability::StartGrpcClientSpan(
        "tinyimx.social.v1.SocialService", "AcceptFriendRequest", &context);
    const auto grpc_status = stub->AcceptFriendRequest(&context, proto_request, &proto_response);
    observability::FinishGrpcClientSpan(
        &rpc_span, grpc_status, "tinyimx.social.v1.SocialService", "AcceptFriendRequest");
    if (!grpc_status.ok()) {
        const auto mapped = MapGrpcStatus(grpc_status);
        return AcceptFriendRequestRpcCallResult::Failure(mapped.code, mapped.message, true);
    }
    const auto outcome = FromProto(proto_response.result());
    if (!outcome.has_value()) {
        return AcceptFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kDataLoss,
            "SocialService returned unspecified friend request accept result", true);
    }
    AcceptFriendRequestRpcResponse response;
    response.outcome = *outcome;
    response.request_id = proto_response.request_id();
    response.requester_user_id = proto_response.requester_user_id();
    response.receiver_user_id = proto_response.receiver_user_id();
    response.message = proto_response.message();
    return AcceptFriendRequestRpcCallResult::Success(std::move(response));
}

RejectFriendRequestRpcCallResult SocialRpcClient::RejectFriendRequest(
    const RejectFriendRequestRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.request_id == 0 || request.handler_user_id == 0) {
        return RejectFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kInvalidArgument, "invalid RejectFriendRequest request", false);
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return RejectFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "RejectFriendRequest remaining RPC budget is exhausted", false);
    }
    if (!endpoint_provider_) {
        return RejectFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kUnavailable, "SocialService endpoint provider is not configured", false);
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kSocial);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return RejectFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kUnavailable, "SocialService endpoint is unavailable", false);
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return RejectFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kUnavailable, "SocialService gRPC stub could not be created", false);
    }

    tinyimx::social::v1::RejectFriendRequestRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_request_id(request.request_id);
    proto_request.set_handler_user_id(request.handler_user_id);
    tinyimx::social::v1::RejectFriendRequestResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout);
    auto rpc_span = observability::StartGrpcClientSpan(
        "tinyimx.social.v1.SocialService", "RejectFriendRequest", &context);
    const auto grpc_status = stub->RejectFriendRequest(&context, proto_request, &proto_response);
    observability::FinishGrpcClientSpan(
        &rpc_span, grpc_status, "tinyimx.social.v1.SocialService", "RejectFriendRequest");
    if (!grpc_status.ok()) {
        const auto mapped = MapGrpcStatus(grpc_status);
        return RejectFriendRequestRpcCallResult::Failure(mapped.code, mapped.message, true);
    }
    const auto outcome = FromProto(proto_response.result());
    if (!outcome.has_value()) {
        return RejectFriendRequestRpcCallResult::Failure(
            RpcErrorCode::kDataLoss,
            "SocialService returned unspecified friend request reject result", true);
    }
    RejectFriendRequestRpcResponse response;
    response.outcome = *outcome;
    response.request_id = proto_response.request_id();
    response.requester_user_id = proto_response.requester_user_id();
    response.receiver_user_id = proto_response.receiver_user_id();
    response.message = proto_response.message();
    return RejectFriendRequestRpcCallResult::Success(std::move(response));
}

std::size_t SocialRpcClient::CachedTargetCountForTest() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return stub_cache_.size();
}

std::uint64_t SocialRpcClient::StubCreationCountForTest() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return stub_creation_count_;
}

std::shared_ptr<SocialRpcClient::SocialStubInterface>
SocialRpcClient::GetOrCreateStub(const ServiceEndpoint& endpoint) const {
    if (endpoint.target.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(cache_mutex_);
    const std::uint64_t use_sequence = ++cache_use_sequence_;
    const auto existing = stub_cache_.find(endpoint.target);
    if (existing != stub_cache_.end()) {
        existing->second.last_used = use_sequence;
        return existing->second.stub;
    }
    auto channel = grpc::CreateChannel(endpoint.target, grpc::InsecureChannelCredentials());
    if (!channel) return nullptr;
    auto unique_stub = tinyimx::social::v1::SocialService::NewStub(channel);
    if (!unique_stub) return nullptr;
    std::shared_ptr<SocialStubInterface> new_stub(std::move(unique_stub));
    if (stub_cache_.size() >= kMaxCachedTargets) {
        const auto victim = std::min_element(
            stub_cache_.begin(), stub_cache_.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.second.last_used < rhs.second.last_used;
            });
        if (victim != stub_cache_.end()) stub_cache_.erase(victim);
    }
    CachedStubEntry entry;
    entry.channel = std::move(channel);
    entry.stub = new_stub;
    entry.last_used = use_sequence;
    stub_cache_.emplace(endpoint.target, std::move(entry));
    ++stub_creation_count_;
    return new_stub;
}

RpcStatus SocialRpcClient::MapGrpcStatus(const grpc::Status& status) {
    if (status.ok()) return RpcStatus::Ok();
    RpcErrorCode code = RpcErrorCode::kUnknown;
    switch (status.error_code()) {
        case grpc::StatusCode::INVALID_ARGUMENT: code = RpcErrorCode::kInvalidArgument; break;
        case grpc::StatusCode::CANCELLED: code = RpcErrorCode::kCancelled; break;
        case grpc::StatusCode::DEADLINE_EXCEEDED: code = RpcErrorCode::kDeadlineExceeded; break;
        case grpc::StatusCode::NOT_FOUND: code = RpcErrorCode::kNotFound; break;
        case grpc::StatusCode::ALREADY_EXISTS: code = RpcErrorCode::kAlreadyExists; break;
        case grpc::StatusCode::PERMISSION_DENIED: code = RpcErrorCode::kPermissionDenied; break;
        case grpc::StatusCode::UNAUTHENTICATED: code = RpcErrorCode::kUnauthenticated; break;
        case grpc::StatusCode::RESOURCE_EXHAUSTED: code = RpcErrorCode::kResourceExhausted; break;
        case grpc::StatusCode::FAILED_PRECONDITION: code = RpcErrorCode::kFailedPrecondition; break;
        case grpc::StatusCode::ABORTED: code = RpcErrorCode::kAborted; break;
        case grpc::StatusCode::OUT_OF_RANGE: code = RpcErrorCode::kOutOfRange; break;
        case grpc::StatusCode::UNIMPLEMENTED: code = RpcErrorCode::kUnimplemented; break;
        case grpc::StatusCode::UNAVAILABLE: code = RpcErrorCode::kUnavailable; break;
        case grpc::StatusCode::INTERNAL: code = RpcErrorCode::kInternal; break;
        case grpc::StatusCode::DATA_LOSS: code = RpcErrorCode::kDataLoss; break;
        case grpc::StatusCode::OK: code = RpcErrorCode::kOk; break;
        default: code = RpcErrorCode::kUnknown; break;
    }
    return {code, status.error_message()};
}

}  // namespace tinyimx::rpc
