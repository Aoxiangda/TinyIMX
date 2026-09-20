#include "services/rpc/MessageRpcClient.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace tinyimx::rpc {
namespace {

constexpr std::uint32_t kMaxReadPageLimit = 50;
constexpr std::uint32_t kMaxPendingPageLimit = 100;
constexpr std::size_t kMaxConfirmBatchSize = 100;

void FillMeta(
    const RpcCallOptions& options,
    tinyimx::common::v1::RequestMeta* meta
) {
    if (meta == nullptr) {
        return;
    }

    meta->set_request_id(options.request_id);
    meta->set_trace_id(options.trace_id);
    meta->set_caller_service(options.caller_service);
    meta->set_caller_instance(options.caller_instance);
}

std::optional<MessageDeliveryState> FromProtoState(
    tinyimx::message::v1::MessageDeliveryState state
) {
    switch (state) {
        case tinyimx::message::v1::MESSAGE_DELIVERY_STATE_PENDING:
            return MessageDeliveryState::kPending;
        case tinyimx::message::v1::MESSAGE_DELIVERY_STATE_RECEIVER_CONFIRMED:
            return MessageDeliveryState::kReceiverConfirmed;
        case tinyimx::message::v1::MESSAGE_DELIVERY_STATE_READ:
            return MessageDeliveryState::kRead;
        case tinyimx::message::v1::MESSAGE_DELIVERY_STATE_FAILED:
            return MessageDeliveryState::kFailed;
        case tinyimx::message::v1::MESSAGE_DELIVERY_STATE_UNSPECIFIED:
            return std::nullopt;
    }

    return std::nullopt;
}

std::optional<MessageRpcRecord> ToRpcRecord(
    const tinyimx::message::v1::MessageRecord& source
) {
    const auto state = FromProtoState(source.delivery_state());
    if (!state.has_value()) {
        return std::nullopt;
    }

    MessageRpcRecord target;
    target.message_id = source.message_id();
    target.client_message_id = source.client_message_id();
    target.from_user_id = source.from_user_id();
    target.to_user_id = source.to_user_id();
    target.message_type = source.message_type();
    target.content = source.content();
    target.delivery_state = *state;
    target.created_at = source.created_at();
    target.receiver_confirmed_at = source.receiver_confirmed_at();
    target.read_at = source.read_at();
    return target;
}



std::optional<GroupMessageRpcRecord> ToRpcRecord(
    const tinyimx::message::v1::GroupMessageRecord& source
) {
    GroupMessageRpcRecord target;
    target.message_id = source.message_id();
    target.client_message_id = source.client_message_id();
    target.group_id = source.group_id();
    target.from_user_id = source.from_user_id();
    target.message_type = source.message_type();
    target.content = source.content();
    target.membership_epoch = source.membership_epoch();
    target.member_version = source.member_version();
    target.authorized_role = source.authorized_role();
    target.created_at = source.created_at();
    if (target.message_id == 0 || target.client_message_id.empty() ||
        target.group_id == 0 || target.from_user_id == 0 ||
        target.message_type < 1 || target.message_type > 3 ||
        target.membership_epoch == 0 || target.member_version == 0 ||
        target.authorized_role < 1 || target.authorized_role > 3 ||
        target.created_at.empty()) {
        return std::nullopt;
    }
    return target;
}

std::optional<GroupDeliveryRpcState> FromProtoGroupDeliveryState(
    tinyimx::message::v1::GroupMessageDeliveryState state
) {
    switch (state) {
        case tinyimx::message::v1::GROUP_MESSAGE_DELIVERY_STATE_PENDING:
            return GroupDeliveryRpcState::kPending;
        case tinyimx::message::v1::GROUP_MESSAGE_DELIVERY_STATE_DEFERRED_OFFLINE:
            return GroupDeliveryRpcState::kDeferredOffline;
        case tinyimx::message::v1::GROUP_MESSAGE_DELIVERY_STATE_DELIVERED:
            return GroupDeliveryRpcState::kDelivered;
        default:
            return std::nullopt;
    }
}

std::optional<GroupDeliveryWorkRpcRecord> ToRpcRecord(
    const tinyimx::message::v1::GroupDeliveryWorkItem& source
) {
    const auto message = ToRpcRecord(source.message());
    const auto state = FromProtoGroupDeliveryState(source.delivery().delivery_state());
    if (!message.has_value() || !state.has_value()) return std::nullopt;
    GroupDeliveryWorkRpcRecord out;
    out.message = *message;
    const auto& d = source.delivery();
    out.delivery.message_id = d.message_id();
    out.delivery.group_id = d.group_id();
    out.delivery.recipient_user_id = d.recipient_user_id();
    out.delivery.delivery_state = *state;
    out.delivery.attempt_count = d.attempt_count();
    out.delivery.last_gateway_id = d.last_gateway_id();
    out.delivery.lease_owner = d.lease_owner();
    out.delivery.lease_token = d.lease_token();
    out.delivery.lease_until = d.lease_until();
    out.delivery.next_retry_at = d.next_retry_at();
    out.delivery.last_error_code = d.last_error_code();
    out.delivery.created_at = d.created_at();
    out.delivery.updated_at = d.updated_at();
    out.delivery.delivered_at = d.delivered_at();
    if (out.delivery.message_id == 0 || out.delivery.recipient_user_id == 0 ||
        out.delivery.message_id != out.message.message_id ||
        out.delivery.group_id != out.message.group_id) return std::nullopt;
    return out;
}

std::optional<ConversationRpcRecord> ToRpcRecord(
    const tinyimx::message::v1::ConversationRecord& source
) {
    const auto state = FromProtoState(source.last_delivery_state());
    if (!state.has_value()) {
        return std::nullopt;
    }

    ConversationRpcRecord target;
    target.peer_user_id = source.peer_user_id();
    target.last_message_id = source.last_message_id();
    target.last_client_message_id = source.last_client_message_id();
    target.last_from_user_id = source.last_from_user_id();
    target.last_to_user_id = source.last_to_user_id();
    target.last_message_type = source.last_message_type();
    target.last_content = source.last_content();
    target.last_delivery_state = *state;
    target.last_created_at = source.last_created_at();
    target.last_receiver_confirmed_at =
        source.last_receiver_confirmed_at();
    target.last_read_at = source.last_read_at();
    return target;
}

bool ValidMessageForDialog(
    const MessageRpcRecord& message,
    std::uint64_t actor_user_id,
    std::uint64_t peer_user_id
) {
    if (message.message_id == 0 ||
        message.from_user_id == 0 ||
        message.to_user_id == 0 ||
        message.message_type == 0 ||
        message.created_at.empty()) {
        return false;
    }

    return
        (message.from_user_id == actor_user_id &&
         message.to_user_id == peer_user_id) ||
        (message.from_user_id == peer_user_id &&
         message.to_user_id == actor_user_id);
}

bool ValidConversationForActor(
    const ConversationRpcRecord& conversation,
    std::uint64_t actor_user_id
) {
    if (conversation.peer_user_id == 0 ||
        conversation.peer_user_id == actor_user_id ||
        conversation.last_message_id == 0 ||
        conversation.last_from_user_id == 0 ||
        conversation.last_to_user_id == 0 ||
        conversation.last_message_type == 0 ||
        conversation.last_created_at.empty()) {
        return false;
    }

    return
        (conversation.last_from_user_id == actor_user_id &&
         conversation.last_to_user_id == conversation.peer_user_id) ||
        (conversation.last_to_user_id == actor_user_id &&
         conversation.last_from_user_id == conversation.peer_user_id);
}

}  // namespace

MessageRpcClient::MessageRpcClient(
    std::shared_ptr<const ServiceEndpointProvider> endpoint_provider
)
    : endpoint_provider_(std::move(endpoint_provider)) {
}


PersistPrivateMessageRpcCallResult
MessageRpcClient::PersistPrivateMessage(
    const PersistPrivateMessageRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.from_user_id == 0 ||
        request.to_user_id == 0 ||
        request.from_user_id == request.to_user_id ||
        request.client_message_id.empty() ||
        request.client_message_id.size() > 64 ||
        request.message_type < 1 ||
        request.message_type > 3 ||
        request.content.empty()) {
        return PersistPrivateMessageRpcCallResult::Failure(
            RpcErrorCode::kInvalidArgument,
            "invalid MessageService PersistPrivateMessage request",
            false
        );
    }

    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return PersistPrivateMessageRpcCallResult::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "PersistPrivateMessage remaining RPC budget is exhausted",
            false
        );
    }

    if (!endpoint_provider_) {
        return PersistPrivateMessageRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint provider is not configured",
            false
        );
    }

    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return PersistPrivateMessageRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint is unavailable",
            false
        );
    }

    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return PersistPrivateMessageRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService gRPC stub could not be created",
            false
        );
    }

    tinyimx::message::v1::PersistPrivateMessageRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_from_user_id(request.from_user_id);
    proto_request.set_to_user_id(request.to_user_id);
    proto_request.set_client_message_id(request.client_message_id);
    proto_request.set_message_type(request.message_type);
    proto_request.set_content(request.content);

    tinyimx::message::v1::PersistPrivateMessageResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + options.remaining_timeout
    );

    const grpc::Status grpc_status = stub->PersistPrivateMessage(
        &context,
        proto_request,
        &proto_response
    );

    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return PersistPrivateMessageRpcCallResult::Failure(
            mapped.code,
            mapped.message,
            true
        );
    }

    PersistPrivateMessageRpcResponse output;
    switch (proto_response.result()) {
        case tinyimx::message::v1::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED:
            output.outcome = PersistPrivateMessageRpcOutcome::kCreated;
            break;
        case tinyimx::message::v1::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED:
            output.outcome = PersistPrivateMessageRpcOutcome::kReused;
            break;
        case tinyimx::message::v1::PERSIST_PRIVATE_MESSAGE_RESULT_IDEMPOTENCY_CONFLICT:
            output.outcome = PersistPrivateMessageRpcOutcome::kIdempotencyConflict;
            break;
        case tinyimx::message::v1::PERSIST_PRIVATE_MESSAGE_RESULT_UNSPECIFIED:
        default:
            return PersistPrivateMessageRpcCallResult::Failure(
                RpcErrorCode::kDataLoss,
                "MessageService returned unspecified persistence result",
                true
            );
    }

    output.message_id = proto_response.message_id();
    output.message = proto_response.message();

    const auto record = ToRpcRecord(proto_response.record());
    if (!record.has_value() ||
        output.message_id == 0 ||
        record->message_id != output.message_id ||
        record->from_user_id != request.from_user_id ||
        record->client_message_id != request.client_message_id) {
        return PersistPrivateMessageRpcCallResult::Failure(
            RpcErrorCode::kDataLoss,
            "MessageService returned invalid persistence identity",
            true
        );
    }

    output.record = *record;

    if (output.Accepted()) {
        if (record->to_user_id != request.to_user_id ||
            record->message_type != request.message_type ||
            record->content != request.content ||
            (output.Created() &&
             record->delivery_state != MessageDeliveryState::kPending)) {
            return PersistPrivateMessageRpcCallResult::Failure(
                RpcErrorCode::kDataLoss,
                "MessageService returned inconsistent accepted persistence record",
                true
            );
        }
    } else {
        const bool immutable_identity_differs =
            record->to_user_id != request.to_user_id ||
            record->message_type != request.message_type ||
            record->content != request.content;
        if (!immutable_identity_differs) {
            return PersistPrivateMessageRpcCallResult::Failure(
                RpcErrorCode::kDataLoss,
                "MessageService returned false idempotency conflict",
                true
            );
        }
    }

    return PersistPrivateMessageRpcCallResult::Success(std::move(output));
}



PersistGroupMessageRpcCallResult
MessageRpcClient::PersistGroupMessage(
    const PersistGroupMessageRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.from_user_id == 0 || request.group_id == 0 ||
        request.client_message_id.empty() || request.client_message_id.size() > 64 ||
        request.message_type < 1 || request.message_type > 3 ||
        request.content.empty()) {
        return PersistGroupMessageRpcCallResult::Failure(
            RpcErrorCode::kInvalidArgument,
            "invalid MessageService PersistGroupMessage request",
            false);
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return PersistGroupMessageRpcCallResult::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "PersistGroupMessage remaining RPC budget is exhausted",
            false);
    }
    if (!endpoint_provider_) {
        return PersistGroupMessageRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint provider is not configured",
            false);
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return PersistGroupMessageRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint is unavailable",
            false);
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return PersistGroupMessageRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService gRPC stub could not be created",
            false);
    }

    tinyimx::message::v1::PersistGroupMessageRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_from_user_id(request.from_user_id);
    proto_request.set_group_id(request.group_id);
    proto_request.set_client_message_id(request.client_message_id);
    proto_request.set_message_type(request.message_type);
    proto_request.set_content(request.content);

    tinyimx::message::v1::PersistGroupMessageResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout);
    const grpc::Status grpc_status = stub->PersistGroupMessage(
        &context, proto_request, &proto_response);
    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return PersistGroupMessageRpcCallResult::Failure(
            mapped.code, mapped.message, true);
    }

    PersistGroupMessageRpcResponse output;
    switch (proto_response.result()) {
        case tinyimx::message::v1::PERSIST_GROUP_MESSAGE_RESULT_CREATED:
            output.outcome = PersistGroupMessageRpcOutcome::kCreated;
            break;
        case tinyimx::message::v1::PERSIST_GROUP_MESSAGE_RESULT_REUSED:
            output.outcome = PersistGroupMessageRpcOutcome::kReused;
            break;
        case tinyimx::message::v1::PERSIST_GROUP_MESSAGE_RESULT_IDEMPOTENCY_CONFLICT:
            output.outcome = PersistGroupMessageRpcOutcome::kIdempotencyConflict;
            break;
        case tinyimx::message::v1::PERSIST_GROUP_MESSAGE_RESULT_UNSPECIFIED:
        default:
            return PersistGroupMessageRpcCallResult::Failure(
                RpcErrorCode::kDataLoss,
                "MessageService returned unspecified group persistence result",
                true);
    }
    output.message_id = proto_response.message_id();
    output.message = proto_response.message();
    const auto record = ToRpcRecord(proto_response.record());
    if (!record.has_value() || output.message_id == 0 ||
        record->message_id != output.message_id ||
        record->from_user_id != request.from_user_id ||
        record->client_message_id != request.client_message_id) {
        return PersistGroupMessageRpcCallResult::Failure(
            RpcErrorCode::kDataLoss,
            "MessageService returned invalid group-message persistence identity",
            true);
    }
    output.record = *record;
    if (output.Accepted()) {
        if (record->group_id != request.group_id ||
            record->message_type != request.message_type ||
            record->content != request.content) {
            return PersistGroupMessageRpcCallResult::Failure(
                RpcErrorCode::kDataLoss,
                "MessageService returned inconsistent accepted group-message record",
                true);
        }
    } else {
        const bool differs = record->group_id != request.group_id ||
                             record->message_type != request.message_type ||
                             record->content != request.content;
        if (!differs) {
            return PersistGroupMessageRpcCallResult::Failure(
                RpcErrorCode::kDataLoss,
                "MessageService returned false group-message idempotency conflict",
                true);
        }
    }
    return PersistGroupMessageRpcCallResult::Success(std::move(output));
}

RpcResult<GetGroupMessageDeliveryRpcResponse>
MessageRpcClient::GetGroupMessageDelivery(
    const GetGroupMessageDeliveryRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.message_id == 0 || request.recipient_user_id == 0)
        return RpcResult<GetGroupMessageDeliveryRpcResponse>::Failure(RpcErrorCode::kInvalidArgument, "invalid GetGroupMessageDelivery request");
    if (options.remaining_timeout <= std::chrono::milliseconds::zero())
        return RpcResult<GetGroupMessageDeliveryRpcResponse>::Failure(RpcErrorCode::kDeadlineExceeded, "GetGroupMessageDelivery RPC budget exhausted");
    if (!endpoint_provider_) return RpcResult<GetGroupMessageDeliveryRpcResponse>::Failure(RpcErrorCode::kUnavailable, "MessageService endpoint provider is not configured");
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint || endpoint->target.empty()) return RpcResult<GetGroupMessageDeliveryRpcResponse>::Failure(RpcErrorCode::kUnavailable, "MessageService endpoint is unavailable");
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) return RpcResult<GetGroupMessageDeliveryRpcResponse>::Failure(RpcErrorCode::kUnavailable, "MessageService gRPC stub could not be created");
    tinyimx::message::v1::GetGroupMessageDeliveryRequest in;
    FillMeta(options, in.mutable_meta()); in.set_message_id(request.message_id); in.set_recipient_user_id(request.recipient_user_id);
    tinyimx::message::v1::GetGroupMessageDeliveryResponse out;
    grpc::ClientContext context; context.set_deadline(std::chrono::system_clock::now()+options.remaining_timeout);
    const auto status = stub->GetGroupMessageDelivery(&context, in, &out);
    if (!status.ok()) { const auto mapped=MapGrpcStatus(status); return RpcResult<GetGroupMessageDeliveryRpcResponse>::Failure(mapped.code,mapped.message); }
    const auto work = ToRpcRecord(out.work());
    if (!work || work->delivery.message_id != request.message_id || work->delivery.recipient_user_id != request.recipient_user_id)
        return RpcResult<GetGroupMessageDeliveryRpcResponse>::Failure(RpcErrorCode::kDataLoss,"MessageService returned invalid group delivery");
    GetGroupMessageDeliveryRpcResponse response; response.work=*work;
    return RpcResult<GetGroupMessageDeliveryRpcResponse>::Success(std::move(response));
}

RpcResult<ClaimGroupMessageDeliveriesRpcResponse>
MessageRpcClient::ClaimGroupMessageDeliveries(
    const ClaimGroupMessageDeliveriesRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.lease_owner.empty() || request.lease_token.empty() || request.limit==0 || request.limit>256 || request.lease_ms<100 || request.lease_ms>60000)
        return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(RpcErrorCode::kInvalidArgument,"invalid ClaimGroupMessageDeliveries request");
    if (options.remaining_timeout <= std::chrono::milliseconds::zero())
        return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(RpcErrorCode::kDeadlineExceeded,"ClaimGroupMessageDeliveries RPC budget exhausted");
    if (!endpoint_provider_) return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(RpcErrorCode::kUnavailable,"MessageService endpoint provider is not configured");
    const auto endpoint=endpoint_provider_->Resolve(ServiceKind::kMessage);
    if(!endpoint||endpoint->target.empty()) return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(RpcErrorCode::kUnavailable,"MessageService endpoint is unavailable");
    auto stub=GetOrCreateStub(*endpoint); if(!stub) return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(RpcErrorCode::kUnavailable,"MessageService gRPC stub could not be created");
    tinyimx::message::v1::ClaimGroupMessageDeliveriesRequest in; FillMeta(options,in.mutable_meta());
    in.set_lease_owner(request.lease_owner); in.set_lease_token(request.lease_token); in.set_limit(request.limit); in.set_lease_ms(request.lease_ms); in.set_message_id(request.message_id);
    tinyimx::message::v1::ClaimGroupMessageDeliveriesResponse out; grpc::ClientContext context; context.set_deadline(std::chrono::system_clock::now()+options.remaining_timeout);
    const auto status=stub->ClaimGroupMessageDeliveries(&context,in,&out); if(!status.ok()){const auto mapped=MapGrpcStatus(status);return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(mapped.code,mapped.message);}
    ClaimGroupMessageDeliveriesRpcResponse response; response.work_items.reserve(static_cast<std::size_t>(out.work_items_size()));
    for(const auto& item:out.work_items()){const auto work=ToRpcRecord(item); if(!work || work->delivery.lease_token!=request.lease_token) return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(RpcErrorCode::kDataLoss,"MessageService returned invalid claimed group delivery"); response.work_items.push_back(*work);}
    return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Success(std::move(response));
}

RpcResult<ClaimGroupMessageDeliveriesRpcResponse>
MessageRpcClient::ClaimGroupMessageDeliveriesForRecipient(
    const ClaimGroupMessageDeliveriesForRecipientRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.recipient_user_id == 0 || request.lease_owner.empty() ||
        request.lease_token.empty() || request.lease_owner.size() > 128 ||
        request.lease_token.size() > 128 || request.limit == 0 ||
        request.limit > 256 || request.lease_ms < 100 ||
        request.lease_ms > 60000) {
        return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(
            RpcErrorCode::kInvalidArgument,
            "invalid ClaimGroupMessageDeliveriesForRecipient request");
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "ClaimGroupMessageDeliveriesForRecipient RPC budget exhausted");
    }
    if (!endpoint_provider_) {
        return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint provider is not configured");
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint || endpoint->target.empty()) {
        return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint is unavailable");
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService gRPC stub could not be created");
    }

    tinyimx::message::v1::ClaimGroupMessageDeliveriesForRecipientRequest in;
    FillMeta(options, in.mutable_meta());
    in.set_recipient_user_id(request.recipient_user_id);
    in.set_lease_owner(request.lease_owner);
    in.set_lease_token(request.lease_token);
    in.set_limit(request.limit);
    in.set_lease_ms(request.lease_ms);

    tinyimx::message::v1::ClaimGroupMessageDeliveriesResponse out;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout);
    const auto status = stub->ClaimGroupMessageDeliveriesForRecipient(
        &context, in, &out);
    if (!status.ok()) {
        const auto mapped = MapGrpcStatus(status);
        return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(
            mapped.code, mapped.message);
    }

    ClaimGroupMessageDeliveriesRpcResponse response;
    response.work_items.reserve(static_cast<std::size_t>(out.work_items_size()));
    std::uint64_t previous_message_id = 0;
    for (const auto& item : out.work_items()) {
        const auto work = ToRpcRecord(item);
        if (!work ||
            work->delivery.recipient_user_id != request.recipient_user_id ||
            work->delivery.delivery_state != GroupDeliveryRpcState::kDeferredOffline ||
            work->delivery.lease_owner != request.lease_owner ||
            work->delivery.lease_token != request.lease_token ||
            work->message.message_id == 0 ||
            work->message.message_id <= previous_message_id) {
            return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Failure(
                RpcErrorCode::kDataLoss,
                "MessageService returned invalid recipient replay claim");
        }
        previous_message_id = work->message.message_id;
        response.work_items.push_back(*work);
    }
    return RpcResult<ClaimGroupMessageDeliveriesRpcResponse>::Success(
        std::move(response));
}

MessageMutationRpcCallResult MessageRpcClient::CompleteGroupMessageDeliveryAttempt(
    const CompleteGroupMessageDeliveryAttemptRpcRequest& request,
    const RpcCallOptions& options
) const {
    if(request.message_id==0||request.recipient_user_id==0||request.lease_token.empty()) return MessageMutationRpcCallResult::Failure(RpcErrorCode::kInvalidArgument,"invalid CompleteGroupMessageDeliveryAttempt request",false);
    if(options.remaining_timeout<=std::chrono::milliseconds::zero()) return MessageMutationRpcCallResult::Failure(RpcErrorCode::kDeadlineExceeded,"CompleteGroupMessageDeliveryAttempt RPC budget exhausted",false);
    if(!endpoint_provider_) return MessageMutationRpcCallResult::Failure(RpcErrorCode::kUnavailable,"MessageService endpoint provider is not configured",false);
    const auto endpoint=endpoint_provider_->Resolve(ServiceKind::kMessage); if(!endpoint||endpoint->target.empty()) return MessageMutationRpcCallResult::Failure(RpcErrorCode::kUnavailable,"MessageService endpoint is unavailable",false);
    auto stub=GetOrCreateStub(*endpoint); if(!stub) return MessageMutationRpcCallResult::Failure(RpcErrorCode::kUnavailable,"MessageService gRPC stub could not be created",false);
    tinyimx::message::v1::CompleteGroupMessageDeliveryAttemptRequest in; FillMeta(options,in.mutable_meta()); in.set_message_id(request.message_id); in.set_recipient_user_id(request.recipient_user_id); in.set_lease_token(request.lease_token); in.set_gateway_id(request.gateway_id); in.set_retry_after_ms(request.retry_after_ms); in.set_error_code(request.error_code);
    switch(request.outcome){case GroupDeliveryAttemptRpcOutcome::kSubmitted:in.set_outcome(tinyimx::message::v1::GROUP_DELIVERY_ATTEMPT_OUTCOME_SUBMITTED);break;case GroupDeliveryAttemptRpcOutcome::kOffline:in.set_outcome(tinyimx::message::v1::GROUP_DELIVERY_ATTEMPT_OUTCOME_OFFLINE);break;case GroupDeliveryAttemptRpcOutcome::kRetryableFailure:in.set_outcome(tinyimx::message::v1::GROUP_DELIVERY_ATTEMPT_OUTCOME_RETRYABLE_FAILURE);break;}
    tinyimx::message::v1::MessageMutationResponse out; grpc::ClientContext context; context.set_deadline(std::chrono::system_clock::now()+options.remaining_timeout); const auto status=stub->CompleteGroupMessageDeliveryAttempt(&context,in,&out); if(!status.ok()){const auto mapped=MapGrpcStatus(status); return MessageMutationRpcCallResult::Failure(mapped.code,mapped.message,true);} MessageMutationRpcResponse response; response.affected_rows=out.affected_rows(); return MessageMutationRpcCallResult::Success(std::move(response));
}

MessageMutationRpcCallResult MessageRpcClient::ConfirmGroupMessageDelivery(
    const ConfirmGroupMessageDeliveryRpcRequest& request,
    const RpcCallOptions& options
) const {
    if(request.message_id==0||request.recipient_user_id==0) return MessageMutationRpcCallResult::Failure(RpcErrorCode::kInvalidArgument,"invalid ConfirmGroupMessageDelivery request",false);
    if(options.remaining_timeout<=std::chrono::milliseconds::zero()) return MessageMutationRpcCallResult::Failure(RpcErrorCode::kDeadlineExceeded,"ConfirmGroupMessageDelivery RPC budget exhausted",false);
    if(!endpoint_provider_) return MessageMutationRpcCallResult::Failure(RpcErrorCode::kUnavailable,"MessageService endpoint provider is not configured",false);
    const auto endpoint=endpoint_provider_->Resolve(ServiceKind::kMessage); if(!endpoint||endpoint->target.empty()) return MessageMutationRpcCallResult::Failure(RpcErrorCode::kUnavailable,"MessageService endpoint is unavailable",false);
    auto stub=GetOrCreateStub(*endpoint); if(!stub) return MessageMutationRpcCallResult::Failure(RpcErrorCode::kUnavailable,"MessageService gRPC stub could not be created",false);
    tinyimx::message::v1::ConfirmGroupMessageDeliveryRequest in; FillMeta(options,in.mutable_meta()); in.set_message_id(request.message_id); in.set_recipient_user_id(request.recipient_user_id);
    tinyimx::message::v1::MessageMutationResponse out; grpc::ClientContext context; context.set_deadline(std::chrono::system_clock::now()+options.remaining_timeout); const auto status=stub->ConfirmGroupMessageDelivery(&context,in,&out); if(!status.ok()){const auto mapped=MapGrpcStatus(status); return MessageMutationRpcCallResult::Failure(mapped.code,mapped.message,true);} MessageMutationRpcResponse response; response.affected_rows=out.affected_rows(); return MessageMutationRpcCallResult::Success(std::move(response));
}

RpcResult<GetPrivateMessageRpcResponse>
MessageRpcClient::GetPrivateMessage(
    const GetPrivateMessageRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.message_id == 0) {
        return RpcResult<GetPrivateMessageRpcResponse>::Failure(
            RpcErrorCode::kInvalidArgument,
            "invalid MessageService GetPrivateMessage request"
        );
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return RpcResult<GetPrivateMessageRpcResponse>::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "GetPrivateMessage remaining RPC budget is exhausted"
        );
    }
    if (!endpoint_provider_) {
        return RpcResult<GetPrivateMessageRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint provider is not configured"
        );
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return RpcResult<GetPrivateMessageRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint is unavailable"
        );
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return RpcResult<GetPrivateMessageRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService gRPC stub could not be created"
        );
    }

    tinyimx::message::v1::GetPrivateMessageRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_message_id(request.message_id);
    tinyimx::message::v1::GetPrivateMessageResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + options.remaining_timeout
    );
    const grpc::Status grpc_status = stub->GetPrivateMessage(
        &context, proto_request, &proto_response
    );
    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return RpcResult<GetPrivateMessageRpcResponse>::Failure(
            mapped.code, mapped.message
        );
    }

    auto record = ToRpcRecord(proto_response.record());
    if (!record.has_value() || record->message_id != request.message_id ||
        record->from_user_id == 0 || record->to_user_id == 0 ||
        record->message_type < 1 || record->message_type > 3) {
        return RpcResult<GetPrivateMessageRpcResponse>::Failure(
            RpcErrorCode::kDataLoss,
            "MessageService returned invalid private message record"
        );
    }

    GetPrivateMessageRpcResponse output;
    output.record = std::move(*record);
    return RpcResult<GetPrivateMessageRpcResponse>::Success(std::move(output));
}

RpcResult<ListHistoryRpcResponse>
MessageRpcClient::ListHistory(
    const ListHistoryRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.actor_user_id == 0 ||
        request.peer_user_id == 0 ||
        request.actor_user_id == request.peer_user_id ||
        request.limit == 0 ||
        request.limit > kMaxReadPageLimit) {
        return RpcResult<ListHistoryRpcResponse>::Failure(
            RpcErrorCode::kInvalidArgument,
            "invalid MessageService ListHistory request"
        );
    }

    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return RpcResult<ListHistoryRpcResponse>::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "ListHistory remaining RPC budget is exhausted"
        );
    }

    if (!endpoint_provider_) {
        return RpcResult<ListHistoryRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint provider is not configured"
        );
    }

    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return RpcResult<ListHistoryRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint is unavailable"
        );
    }

    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return RpcResult<ListHistoryRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService gRPC stub could not be created"
        );
    }

    tinyimx::message::v1::ListHistoryRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_actor_user_id(request.actor_user_id);
    proto_request.set_peer_user_id(request.peer_user_id);
    proto_request.set_before_message_id(request.before_message_id);
    proto_request.set_limit(request.limit);

    tinyimx::message::v1::ListHistoryResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + options.remaining_timeout
    );

    const grpc::Status grpc_status = stub->ListHistory(
        &context,
        proto_request,
        &proto_response
    );

    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return RpcResult<ListHistoryRpcResponse>::Failure(
            mapped.code,
            mapped.message
        );
    }

    if (proto_response.messages_size() >
        static_cast<int>(request.limit)) {
        return RpcResult<ListHistoryRpcResponse>::Failure(
            RpcErrorCode::kDataLoss,
            "MessageService returned more history rows than requested"
        );
    }

    ListHistoryRpcResponse output;
    output.has_more = proto_response.has_more();
    output.messages.reserve(
        static_cast<std::size_t>(proto_response.messages_size())
    );

    std::uint64_t previous_message_id = 0;
    for (const auto& proto_message : proto_response.messages()) {
        auto message = ToRpcRecord(proto_message);
        if (!message.has_value() ||
            !ValidMessageForDialog(
                *message,
                request.actor_user_id,
                request.peer_user_id
            ) ||
            (previous_message_id != 0 &&
             message->message_id <= previous_message_id)) {
            return RpcResult<ListHistoryRpcResponse>::Failure(
                RpcErrorCode::kDataLoss,
                "MessageService returned invalid history record ordering/identity"
            );
        }

        previous_message_id = message->message_id;
        output.messages.push_back(std::move(*message));
    }

    return RpcResult<ListHistoryRpcResponse>::Success(
        std::move(output)
    );
}

RpcResult<ListConversationsRpcResponse>
MessageRpcClient::ListConversations(
    const ListConversationsRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.actor_user_id == 0 ||
        request.limit == 0 ||
        request.limit > kMaxReadPageLimit) {
        return RpcResult<ListConversationsRpcResponse>::Failure(
            RpcErrorCode::kInvalidArgument,
            "invalid MessageService ListConversations request"
        );
    }

    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return RpcResult<ListConversationsRpcResponse>::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "ListConversations remaining RPC budget is exhausted"
        );
    }

    if (!endpoint_provider_) {
        return RpcResult<ListConversationsRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint provider is not configured"
        );
    }

    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return RpcResult<ListConversationsRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint is unavailable"
        );
    }

    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return RpcResult<ListConversationsRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService gRPC stub could not be created"
        );
    }

    tinyimx::message::v1::ListConversationsRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_actor_user_id(request.actor_user_id);
    proto_request.set_limit(request.limit);

    tinyimx::message::v1::ListConversationsResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + options.remaining_timeout
    );

    const grpc::Status grpc_status = stub->ListConversations(
        &context,
        proto_request,
        &proto_response
    );

    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return RpcResult<ListConversationsRpcResponse>::Failure(
            mapped.code,
            mapped.message
        );
    }

    if (proto_response.conversations_size() >
        static_cast<int>(request.limit)) {
        return RpcResult<ListConversationsRpcResponse>::Failure(
            RpcErrorCode::kDataLoss,
            "MessageService returned more conversations than requested"
        );
    }

    ListConversationsRpcResponse output;
    output.has_more = proto_response.has_more();
    output.conversations.reserve(
        static_cast<std::size_t>(proto_response.conversations_size())
    );

    std::uint64_t previous_last_message_id = 0;
    std::set<std::uint64_t> peers;

    for (const auto& proto_conversation : proto_response.conversations()) {
        auto conversation = ToRpcRecord(proto_conversation);
        if (!conversation.has_value() ||
            !ValidConversationForActor(*conversation, request.actor_user_id) ||
            (previous_last_message_id != 0 &&
             conversation->last_message_id >= previous_last_message_id) ||
            !peers.insert(conversation->peer_user_id).second) {
            return RpcResult<ListConversationsRpcResponse>::Failure(
                RpcErrorCode::kDataLoss,
                "MessageService returned invalid conversation ordering/identity"
            );
        }

        previous_last_message_id = conversation->last_message_id;
        output.conversations.push_back(std::move(*conversation));
    }

    return RpcResult<ListConversationsRpcResponse>::Success(
        std::move(output)
    );
}

RpcResult<CountPendingRpcResponse>
MessageRpcClient::CountPending(
    const CountPendingRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.to_user_id == 0) {
        return RpcResult<CountPendingRpcResponse>::Failure(
            RpcErrorCode::kInvalidArgument,
            "invalid MessageService CountPending request"
        );
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return RpcResult<CountPendingRpcResponse>::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "CountPending remaining RPC budget is exhausted"
        );
    }
    if (!endpoint_provider_) {
        return RpcResult<CountPendingRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint provider is not configured"
        );
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return RpcResult<CountPendingRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint is unavailable"
        );
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return RpcResult<CountPendingRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService gRPC stub could not be created"
        );
    }

    tinyimx::message::v1::CountPendingRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_to_user_id(request.to_user_id);
    tinyimx::message::v1::CountPendingResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + options.remaining_timeout
    );
    const grpc::Status grpc_status = stub->CountPending(
        &context, proto_request, &proto_response
    );
    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return RpcResult<CountPendingRpcResponse>::Failure(
            mapped.code, mapped.message
        );
    }

    CountPendingRpcResponse output;
    output.count = proto_response.count();
    return RpcResult<CountPendingRpcResponse>::Success(std::move(output));
}

RpcResult<ListPendingAfterRpcResponse>
MessageRpcClient::ListPendingAfter(
    const ListPendingAfterRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.to_user_id == 0 || request.limit == 0 ||
        request.limit > kMaxPendingPageLimit) {
        return RpcResult<ListPendingAfterRpcResponse>::Failure(
            RpcErrorCode::kInvalidArgument,
            "invalid MessageService ListPendingAfter request"
        );
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return RpcResult<ListPendingAfterRpcResponse>::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "ListPendingAfter remaining RPC budget is exhausted"
        );
    }
    if (!endpoint_provider_) {
        return RpcResult<ListPendingAfterRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint provider is not configured"
        );
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return RpcResult<ListPendingAfterRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint is unavailable"
        );
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return RpcResult<ListPendingAfterRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService gRPC stub could not be created"
        );
    }

    tinyimx::message::v1::ListPendingAfterRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_to_user_id(request.to_user_id);
    proto_request.set_after_message_id(request.after_message_id);
    proto_request.set_limit(request.limit);
    tinyimx::message::v1::ListPendingAfterResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + options.remaining_timeout
    );
    const grpc::Status grpc_status = stub->ListPendingAfter(
        &context, proto_request, &proto_response
    );
    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return RpcResult<ListPendingAfterRpcResponse>::Failure(
            mapped.code, mapped.message
        );
    }
    if (proto_response.messages_size() > static_cast<int>(request.limit)) {
        return RpcResult<ListPendingAfterRpcResponse>::Failure(
            RpcErrorCode::kDataLoss,
            "MessageService returned more pending rows than requested"
        );
    }

    ListPendingAfterRpcResponse output;
    output.has_more = proto_response.has_more();
    output.messages.reserve(
        static_cast<std::size_t>(proto_response.messages_size())
    );
    std::uint64_t previous_id = request.after_message_id;
    for (const auto& proto_message : proto_response.messages()) {
        auto message = ToRpcRecord(proto_message);
        if (!message.has_value() || message->message_id <= previous_id ||
            message->to_user_id != request.to_user_id ||
            message->delivery_state != MessageDeliveryState::kPending) {
            return RpcResult<ListPendingAfterRpcResponse>::Failure(
                RpcErrorCode::kDataLoss,
                "MessageService returned invalid pending page"
            );
        }
        previous_id = message->message_id;
        output.messages.push_back(std::move(*message));
    }
    return RpcResult<ListPendingAfterRpcResponse>::Success(std::move(output));
}

MessageMutationRpcCallResult
MessageRpcClient::ConfirmReceiver(
    const ConfirmReceiverRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.message_id == 0 || request.receiver_user_id == 0) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kInvalidArgument,
            "invalid MessageService ConfirmReceiver request",
            false
        );
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "ConfirmReceiver remaining RPC budget is exhausted",
            false
        );
    }
    if (!endpoint_provider_) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint provider is not configured",
            false
        );
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint is unavailable",
            false
        );
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService gRPC stub could not be created",
            false
        );
    }

    tinyimx::message::v1::ConfirmReceiverRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_message_id(request.message_id);
    proto_request.set_receiver_user_id(request.receiver_user_id);
    tinyimx::message::v1::MessageMutationResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + options.remaining_timeout
    );
    const grpc::Status grpc_status = stub->ConfirmReceiver(
        &context, proto_request, &proto_response
    );
    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return MessageMutationRpcCallResult::Failure(
            mapped.code, mapped.message, true
        );
    }
    MessageMutationRpcResponse output;
    output.affected_rows = proto_response.affected_rows();
    return MessageMutationRpcCallResult::Success(std::move(output));
}

MessageMutationRpcCallResult
MessageRpcClient::ConfirmReceiverBatch(
    const ConfirmReceiverBatchRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.receiver_user_id == 0 || request.message_ids.empty() ||
        request.message_ids.size() > kMaxConfirmBatchSize) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kInvalidArgument,
            "invalid MessageService ConfirmReceiverBatch request",
            false
        );
    }
    std::set<std::uint64_t> unique_ids;
    for (const auto message_id : request.message_ids) {
        if (message_id == 0 || !unique_ids.insert(message_id).second) {
            return MessageMutationRpcCallResult::Failure(
                RpcErrorCode::kInvalidArgument,
                "ConfirmReceiverBatch contains invalid/duplicate message id",
                false
            );
        }
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "ConfirmReceiverBatch remaining RPC budget is exhausted",
            false
        );
    }
    if (!endpoint_provider_) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint provider is not configured",
            false
        );
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint is unavailable",
            false
        );
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService gRPC stub could not be created",
            false
        );
    }

    tinyimx::message::v1::ConfirmReceiverBatchRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_receiver_user_id(request.receiver_user_id);
    for (const auto message_id : request.message_ids) {
        proto_request.add_message_ids(message_id);
    }
    tinyimx::message::v1::MessageMutationResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + options.remaining_timeout
    );
    const grpc::Status grpc_status = stub->ConfirmReceiverBatch(
        &context, proto_request, &proto_response
    );
    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return MessageMutationRpcCallResult::Failure(
            mapped.code, mapped.message, true
        );
    }
    MessageMutationRpcResponse output;
    output.affected_rows = proto_response.affected_rows();
    return MessageMutationRpcCallResult::Success(std::move(output));
}

MessageMutationRpcCallResult
MessageRpcClient::MarkDialogRead(
    const MarkDialogReadRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.reader_user_id == 0 || request.peer_user_id == 0 ||
        request.reader_user_id == request.peer_user_id) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kInvalidArgument,
            "invalid MessageService MarkDialogRead request",
            false
        );
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kDeadlineExceeded,
            "MarkDialogRead remaining RPC budget is exhausted",
            false
        );
    }
    if (!endpoint_provider_) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint provider is not configured",
            false
        );
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kMessage);
    if (!endpoint.has_value() || endpoint->target.empty()) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService endpoint is unavailable",
            false
        );
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        return MessageMutationRpcCallResult::Failure(
            RpcErrorCode::kUnavailable,
            "MessageService gRPC stub could not be created",
            false
        );
    }

    tinyimx::message::v1::MarkDialogReadRequest proto_request;
    FillMeta(options, proto_request.mutable_meta());
    proto_request.set_reader_user_id(request.reader_user_id);
    proto_request.set_peer_user_id(request.peer_user_id);
    tinyimx::message::v1::MessageMutationResponse proto_response;
    grpc::ClientContext context;
    context.set_deadline(
        std::chrono::system_clock::now() + options.remaining_timeout
    );
    const grpc::Status grpc_status = stub->MarkDialogRead(
        &context, proto_request, &proto_response
    );
    if (!grpc_status.ok()) {
        const RpcStatus mapped = MapGrpcStatus(grpc_status);
        return MessageMutationRpcCallResult::Failure(
            mapped.code, mapped.message, true
        );
    }
    MessageMutationRpcResponse output;
    output.affected_rows = proto_response.affected_rows();
    return MessageMutationRpcCallResult::Success(std::move(output));
}

std::size_t MessageRpcClient::CachedTargetCountForTest() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return stub_cache_.size();
}

std::uint64_t MessageRpcClient::StubCreationCountForTest() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return stub_creation_count_;
}

std::shared_ptr<MessageRpcClient::MessageStubInterface>
MessageRpcClient::GetOrCreateStub(
    const ServiceEndpoint& endpoint
) const {
    if (endpoint.target.empty()) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(cache_mutex_);
    const std::uint64_t use_sequence = ++cache_use_sequence_;

    const auto existing = stub_cache_.find(endpoint.target);
    if (existing != stub_cache_.end()) {
        existing->second.last_used = use_sequence;
        return existing->second.stub;
    }

    auto channel = grpc::CreateChannel(
        endpoint.target,
        grpc::InsecureChannelCredentials()
    );
    if (!channel) {
        return nullptr;
    }

    auto unique_stub = tinyimx::message::v1::MessageService::NewStub(channel);
    if (!unique_stub) {
        return nullptr;
    }

    std::shared_ptr<MessageStubInterface> new_stub(
        std::move(unique_stub)
    );

    if (stub_cache_.size() >= kMaxCachedTargets) {
        const auto victim = std::min_element(
            stub_cache_.begin(),
            stub_cache_.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.second.last_used < rhs.second.last_used;
            }
        );
        if (victim != stub_cache_.end()) {
            stub_cache_.erase(victim);
        }
    }

    CachedStubEntry entry;
    entry.channel = std::move(channel);
    entry.stub = new_stub;
    entry.last_used = use_sequence;
    stub_cache_.emplace(endpoint.target, std::move(entry));
    ++stub_creation_count_;
    return new_stub;
}

RpcStatus MessageRpcClient::MapGrpcStatus(
    const grpc::Status& status
) {
    if (status.ok()) {
        return RpcStatus::Ok();
    }

    RpcErrorCode code = RpcErrorCode::kUnknown;
    switch (status.error_code()) {
        case grpc::StatusCode::INVALID_ARGUMENT:
            code = RpcErrorCode::kInvalidArgument;
            break;
        case grpc::StatusCode::CANCELLED:
            code = RpcErrorCode::kCancelled;
            break;
        case grpc::StatusCode::DEADLINE_EXCEEDED:
            code = RpcErrorCode::kDeadlineExceeded;
            break;
        case grpc::StatusCode::NOT_FOUND:
            code = RpcErrorCode::kNotFound;
            break;
        case grpc::StatusCode::ALREADY_EXISTS:
            code = RpcErrorCode::kAlreadyExists;
            break;
        case grpc::StatusCode::PERMISSION_DENIED:
            code = RpcErrorCode::kPermissionDenied;
            break;
        case grpc::StatusCode::UNAUTHENTICATED:
            code = RpcErrorCode::kUnauthenticated;
            break;
        case grpc::StatusCode::RESOURCE_EXHAUSTED:
            code = RpcErrorCode::kResourceExhausted;
            break;
        case grpc::StatusCode::FAILED_PRECONDITION:
            code = RpcErrorCode::kFailedPrecondition;
            break;
        case grpc::StatusCode::ABORTED:
            code = RpcErrorCode::kAborted;
            break;
        case grpc::StatusCode::OUT_OF_RANGE:
            code = RpcErrorCode::kOutOfRange;
            break;
        case grpc::StatusCode::UNIMPLEMENTED:
            code = RpcErrorCode::kUnimplemented;
            break;
        case grpc::StatusCode::UNAVAILABLE:
            code = RpcErrorCode::kUnavailable;
            break;
        case grpc::StatusCode::INTERNAL:
            code = RpcErrorCode::kInternal;
            break;
        case grpc::StatusCode::DATA_LOSS:
            code = RpcErrorCode::kDataLoss;
            break;
        case grpc::StatusCode::OK:
            code = RpcErrorCode::kOk;
            break;
        default:
            code = RpcErrorCode::kUnknown;
            break;
    }

    return RpcStatus{code, status.error_message()};
}

}  // namespace tinyimx::rpc
