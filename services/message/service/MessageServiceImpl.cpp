#include "services/message/service/MessageServiceImpl.h"

#include "services/message/application/MessageApplicationService.h"
#include "services/rpc/GroupRpcClient.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace tinyimx::message {
namespace {

grpc::Status MapApplicationFailure(
    MessageApplicationStatus status,
    const std::string& message
) {
    switch (status) {
        case MessageApplicationStatus::kInvalidArgument:
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, message);
        case MessageApplicationStatus::kNotFound:
            return grpc::Status(grpc::StatusCode::NOT_FOUND, message);
        case MessageApplicationStatus::kPermissionDenied:
            return grpc::Status(grpc::StatusCode::PERMISSION_DENIED, message);
        case MessageApplicationStatus::kFailedPrecondition:
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, message);
        case MessageApplicationStatus::kInvalidRecord:
            return grpc::Status(grpc::StatusCode::DATA_LOSS, message);
        case MessageApplicationStatus::kStorageError:
            return grpc::Status(grpc::StatusCode::UNAVAILABLE, message);
        case MessageApplicationStatus::kSucceeded:
            return grpc::Status::OK;
    }

    return grpc::Status(
        grpc::StatusCode::INTERNAL,
        "unknown MessageService application status"
    );
}

std::chrono::milliseconds ResolveFaultDelay(
    const char* environment_name
) {
    const char* value = std::getenv(environment_name);
    if (value == nullptr || value[0] == '\0') {
        return std::chrono::milliseconds::zero();
    }

    try {
        const long long parsed = std::stoll(value);
        if (parsed <= 0) {
            return std::chrono::milliseconds::zero();
        }
        constexpr long long kMaxDelayMs = 60000;
        return std::chrono::milliseconds(
            parsed > kMaxDelayMs ? kMaxDelayMs : parsed
        );
    } catch (...) {
        return std::chrono::milliseconds::zero();
    }
}

bool ApplyFaultDelay(
    grpc::ServerContext* context,
    const char* environment_name
) {
    const auto delay = ResolveFaultDelay(environment_name);
    if (delay <= std::chrono::milliseconds::zero()) {
        return true;
    }

    std::this_thread::sleep_for(delay);
    return context != nullptr && !context->IsCancelled();
}

tinyimx::message::v1::MessageDeliveryState ToProtoDeliveryState(
    MessageDeliveryState state
) {
    switch (state) {
        case MessageDeliveryState::kPending:
            return tinyimx::message::v1::MESSAGE_DELIVERY_STATE_PENDING;
        case MessageDeliveryState::kReceiverConfirmed:
            return tinyimx::message::v1::MESSAGE_DELIVERY_STATE_RECEIVER_CONFIRMED;
        case MessageDeliveryState::kRead:
            return tinyimx::message::v1::MESSAGE_DELIVERY_STATE_READ;
        case MessageDeliveryState::kFailed:
            return tinyimx::message::v1::MESSAGE_DELIVERY_STATE_FAILED;
    }
    return tinyimx::message::v1::MESSAGE_DELIVERY_STATE_UNSPECIFIED;
}

void FillMessage(
    const MessageView& source,
    tinyimx::message::v1::MessageRecord* target
) {
    if (target == nullptr) {
        return;
    }

    target->set_message_id(source.message_id);
    target->set_client_message_id(source.client_message_id);
    target->set_from_user_id(source.from_user_id);
    target->set_to_user_id(source.to_user_id);
    target->set_message_type(source.message_type);
    target->set_content(source.content);
    target->set_delivery_state(ToProtoDeliveryState(source.delivery_state));
    target->set_created_at(source.created_at);
    target->set_receiver_confirmed_at(source.receiver_confirmed_at);
    target->set_read_at(source.read_at);
}

void FillConversation(
    const ConversationView& source,
    tinyimx::message::v1::ConversationRecord* target
) {
    if (target == nullptr) {
        return;
    }

    target->set_peer_user_id(source.peer_user_id);
    target->set_last_message_id(source.last_message_id);
    target->set_last_client_message_id(source.last_client_message_id);
    target->set_last_from_user_id(source.last_from_user_id);
    target->set_last_to_user_id(source.last_to_user_id);
    target->set_last_message_type(source.last_message_type);
    target->set_last_content(source.last_content);
    target->set_last_delivery_state(ToProtoDeliveryState(source.last_delivery_state));
    target->set_last_created_at(source.last_created_at);
    target->set_last_receiver_confirmed_at(source.last_receiver_confirmed_at);
    target->set_last_read_at(source.last_read_at);
}

bool ValidateRpcArguments(
    grpc::ServerContext* context,
    const void* request,
    void* response
) {
    return context != nullptr && request != nullptr && response != nullptr;
}



void FillGroupMessage(
    const GroupMessageView& source,
    tinyimx::message::v1::GroupMessageRecord* target
) {
    if (target == nullptr) return;
    target->set_message_id(source.message_id);
    target->set_client_message_id(source.client_message_id);
    target->set_group_id(source.group_id);
    target->set_from_user_id(source.from_user_id);
    target->set_message_type(source.message_type);
    target->set_content(source.content);
    target->set_membership_epoch(source.membership_epoch);
    target->set_member_version(source.member_version);
    target->set_authorized_role(source.authorized_role);
    target->set_created_at(source.created_at);
}

tinyimx::message::v1::GroupMessageDeliveryState ToProtoGroupDeliveryState(
    GroupDeliveryState state
) {
    switch (state) {
        case GroupDeliveryState::kPending:
            return tinyimx::message::v1::GROUP_MESSAGE_DELIVERY_STATE_PENDING;
        case GroupDeliveryState::kDeferredOffline:
            return tinyimx::message::v1::GROUP_MESSAGE_DELIVERY_STATE_DEFERRED_OFFLINE;
        case GroupDeliveryState::kDelivered:
            return tinyimx::message::v1::GROUP_MESSAGE_DELIVERY_STATE_DELIVERED;
    }
    return tinyimx::message::v1::GROUP_MESSAGE_DELIVERY_STATE_UNSPECIFIED;
}

void FillGroupDelivery(
    const GroupDeliveryView& source,
    tinyimx::message::v1::GroupMessageDeliveryRecord* target
) {
    if (target == nullptr) return;
    target->set_message_id(source.message_id);
    target->set_group_id(source.group_id);
    target->set_recipient_user_id(source.recipient_user_id);
    target->set_delivery_state(ToProtoGroupDeliveryState(source.delivery_state));
    target->set_attempt_count(source.attempt_count);
    target->set_last_gateway_id(source.last_gateway_id);
    target->set_lease_owner(source.lease_owner);
    target->set_lease_token(source.lease_token);
    target->set_lease_until(source.lease_until);
    target->set_next_retry_at(source.next_retry_at);
    target->set_last_error_code(source.last_error_code);
    target->set_created_at(source.created_at);
    target->set_updated_at(source.updated_at);
    target->set_delivered_at(source.delivered_at);
}

void FillGroupDeliveryWork(
    const GroupDeliveryWorkItem& source,
    tinyimx::message::v1::GroupDeliveryWorkItem* target
) {
    if (target == nullptr) return;
    FillGroupMessage(source.message, target->mutable_message());
    FillGroupDelivery(source.delivery, target->mutable_delivery());
}

grpc::Status MapRpcFailure(const tinyimx::rpc::RpcStatus& status) {
    using tinyimx::rpc::RpcErrorCode;
    switch (status.code) {
        case RpcErrorCode::kInvalidArgument:
            return {grpc::StatusCode::INVALID_ARGUMENT, status.message};
        case RpcErrorCode::kCancelled:
            return {grpc::StatusCode::CANCELLED, status.message};
        case RpcErrorCode::kDeadlineExceeded:
            return {grpc::StatusCode::DEADLINE_EXCEEDED, status.message};
        case RpcErrorCode::kNotFound:
            return {grpc::StatusCode::NOT_FOUND, status.message};
        case RpcErrorCode::kPermissionDenied:
            return {grpc::StatusCode::PERMISSION_DENIED, status.message};
        case RpcErrorCode::kFailedPrecondition:
            return {grpc::StatusCode::FAILED_PRECONDITION, status.message};
        case RpcErrorCode::kResourceExhausted:
            return {grpc::StatusCode::RESOURCE_EXHAUSTED, status.message};
        case RpcErrorCode::kUnavailable:
            return {grpc::StatusCode::UNAVAILABLE, status.message};
        case RpcErrorCode::kDataLoss:
            return {grpc::StatusCode::DATA_LOSS, status.message};
        case RpcErrorCode::kInternal:
            return {grpc::StatusCode::INTERNAL, status.message};
        case RpcErrorCode::kOk:
            return grpc::Status::OK;
        default:
            return {grpc::StatusCode::UNKNOWN, status.message};
    }
}

tinyimx::rpc::RpcCallOptions BuildGroupAuthorizationOptions(
    grpc::ServerContext* context,
    const tinyimx::common::v1::RequestMeta& meta
) {
    tinyimx::rpc::RpcCallOptions options;
    options.request_id = meta.request_id();
    options.trace_id = meta.trace_id();
    options.caller_service = "message-service";
    options.caller_instance = meta.caller_instance();
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        context->deadline() - std::chrono::system_clock::now());
    if (remaining <= std::chrono::milliseconds::zero()) {
        remaining = std::chrono::milliseconds{1};
    }
    options.remaining_timeout = remaining;
    return options;
}

}  // namespace

MessageServiceImpl::MessageServiceImpl(
    MessageApplicationService* application_service,
    tinyimx::rpc::GroupRpcClient* group_rpc_client
)
    : application_service_(application_service),
      group_rpc_client_(group_rpc_client) {
}

grpc::Status MessageServiceImpl::PersistPrivateMessage(
    grpc::ServerContext* context,
    const tinyimx::message::v1::PersistPrivateMessageRequest* request,
    tinyimx::message::v1::PersistPrivateMessageResponse* response
) {
    if (!ValidateRpcArguments(context, request, response)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "invalid PersistPrivateMessage RPC arguments");
    }
    if (application_service_ == nullptr) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "MessageService application service is unavailable");
    }
    if (!ApplyFaultDelay(
            context,
            "TINYIMX_FAULT_MESSAGE_PERSIST_PRE_REPOSITORY_DELAY_MS")) {
        return grpc::Status(grpc::StatusCode::CANCELLED,
                            "PersistPrivateMessage RPC cancelled before repository");
    }

    auto result = application_service_->PersistPrivateMessage(
        request->from_user_id(),
        request->to_user_id(),
        request->client_message_id(),
        request->message_type(),
        request->content()
    );
    if (!result.Completed()) {
        return MapApplicationFailure(result.status, result.message);
    }

    switch (result.outcome) {
        case PersistPrivateMessageOutcome::kCreated:
            response->set_result(
                tinyimx::message::v1::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED);
            break;
        case PersistPrivateMessageOutcome::kReused:
            response->set_result(
                tinyimx::message::v1::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED);
            break;
        case PersistPrivateMessageOutcome::kIdempotencyConflict:
            response->set_result(
                tinyimx::message::v1::PERSIST_PRIVATE_MESSAGE_RESULT_IDEMPOTENCY_CONFLICT);
            break;
    }

    response->set_message_id(result.message_id);
    response->set_message(result.message);
    FillMessage(result.record, response->mutable_record());

    if (result.Accepted() &&
        !ApplyFaultDelay(
            context,
            "TINYIMX_FAULT_MESSAGE_PERSIST_POST_COMMIT_DELAY_MS")) {
        return grpc::Status(grpc::StatusCode::CANCELLED,
                            "PersistPrivateMessage RPC cancelled after durable result");
    }
    return grpc::Status::OK;
}



grpc::Status MessageServiceImpl::PersistGroupMessage(
    grpc::ServerContext* context,
    const tinyimx::message::v1::PersistGroupMessageRequest* request,
    tinyimx::message::v1::PersistGroupMessageResponse* response
) {
    if (context == nullptr || request == nullptr || response == nullptr) {
        return {grpc::StatusCode::INVALID_ARGUMENT,
                "invalid PersistGroupMessage RPC arguments"};
    }
    if (application_service_ == nullptr) {
        return {grpc::StatusCode::UNAVAILABLE,
                "MessageService application service is unavailable"};
    }
    if (!ApplyFaultDelay(
            context,
            "TINYIMX_FAULT_GROUP_MESSAGE_PERSIST_PRE_REPOSITORY_DELAY_MS")) {
        return {grpc::StatusCode::CANCELLED,
                "PersistGroupMessage RPC cancelled before durable lookup"};
    }

    const auto existing = application_service_->FindGroupMessageByClientMessageId(
        request->from_user_id(), request->client_message_id());
    if (!existing.Succeeded()) {
        return MapApplicationFailure(existing.status, existing.message);
    }

    auto fill_existing = [&](const GroupMessageView& record) {
        const bool same =
            record.group_id == request->group_id() &&
            record.from_user_id == request->from_user_id() &&
            record.client_message_id == request->client_message_id() &&
            record.message_type == request->message_type() &&
            record.content == request->content();
        response->set_result(same
            ? tinyimx::message::v1::PERSIST_GROUP_MESSAGE_RESULT_REUSED
            : tinyimx::message::v1::PERSIST_GROUP_MESSAGE_RESULT_IDEMPOTENCY_CONFLICT);
        response->set_message_id(record.message_id);
        FillGroupMessage(record, response->mutable_record());
        response->set_message(same
            ? "existing group message reused"
            : "client_message_id already belongs to a different group message");
    };

    // Durable truth wins over current authorization. This recovers the
    // commit-success/response-loss case even if membership changed later.
    if (existing.Found()) {
        fill_existing(existing.record);
        return grpc::Status::OK;
    }

    if (group_rpc_client_ == nullptr) {
        return {grpc::StatusCode::UNAVAILABLE,
                "GroupService authorization client is unavailable"};
    }
    tinyimx::rpc::PrepareGroupMessageSendRpcRequest auth_request;
    auth_request.actor_user_id = request->from_user_id();
    auth_request.group_id = request->group_id();
    const auto auth = group_rpc_client_->PrepareGroupMessageSend(
        auth_request, BuildGroupAuthorizationOptions(context, request->meta()));
    if (!auth.ok()) {
        return MapRpcFailure(auth.status);
    }
    if (!auth.value->allowed) {
        const bool inactive = auth.value->message.find("not active") != std::string::npos;
        return {inactive ? grpc::StatusCode::FAILED_PRECONDITION
                         : grpc::StatusCode::PERMISSION_DENIED,
                auth.value->message.empty()
                    ? "group send permission denied"
                    : auth.value->message};
    }

    auto result = application_service_->PersistAuthorizedGroupMessage(
        request->from_user_id(),
        request->group_id(),
        request->client_message_id(),
        request->message_type(),
        request->content(),
        auth.value->membership_epoch,
        auth.value->member_version,
        static_cast<std::uint32_t>(auth.value->role),
        auth.value->recipient_user_ids);
    if (!result.Completed()) {
        return MapApplicationFailure(result.status, result.message);
    }
    switch (result.outcome) {
        case PersistGroupMessageOutcome::kCreated:
            response->set_result(
                tinyimx::message::v1::PERSIST_GROUP_MESSAGE_RESULT_CREATED);
            break;
        case PersistGroupMessageOutcome::kReused:
            response->set_result(
                tinyimx::message::v1::PERSIST_GROUP_MESSAGE_RESULT_REUSED);
            break;
        case PersistGroupMessageOutcome::kIdempotencyConflict:
            response->set_result(
                tinyimx::message::v1::PERSIST_GROUP_MESSAGE_RESULT_IDEMPOTENCY_CONFLICT);
            break;
    }
    response->set_message_id(result.message_id);
    FillGroupMessage(result.record, response->mutable_record());
    response->set_message(result.message);

    if (result.Accepted() && !ApplyFaultDelay(
            context,
            "TINYIMX_FAULT_GROUP_MESSAGE_PERSIST_POST_COMMIT_DELAY_MS")) {
        return {grpc::StatusCode::CANCELLED,
                "PersistGroupMessage RPC cancelled after durable result"};
    }
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::GetGroupMessageDelivery(
    grpc::ServerContext* context,
    const tinyimx::message::v1::GetGroupMessageDeliveryRequest* request,
    tinyimx::message::v1::GetGroupMessageDeliveryResponse* response
) {
    if (!ValidateRpcArguments(context, request, response))
        return {grpc::StatusCode::INVALID_ARGUMENT, "invalid GetGroupMessageDelivery RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE, "MessageService application service is unavailable"};
    const auto result = application_service_->GetGroupMessageDelivery(
        request->message_id(), request->recipient_user_id());
    if (!result.Succeeded()) return MapApplicationFailure(result.status, result.message);
    if (!result.found) return {grpc::StatusCode::NOT_FOUND, result.message};
    FillGroupDeliveryWork(result.record, response->mutable_work());
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::ClaimGroupMessageDeliveries(
    grpc::ServerContext* context,
    const tinyimx::message::v1::ClaimGroupMessageDeliveriesRequest* request,
    tinyimx::message::v1::ClaimGroupMessageDeliveriesResponse* response
) {
    if (!ValidateRpcArguments(context, request, response))
        return {grpc::StatusCode::INVALID_ARGUMENT, "invalid ClaimGroupMessageDeliveries RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE, "MessageService application service is unavailable"};
    const auto result = application_service_->ClaimGroupMessageDeliveries(
        request->lease_owner(), request->lease_token(), request->limit(),
        request->lease_ms(), request->message_id());
    if (!result.Succeeded()) return MapApplicationFailure(result.status, result.message);
    for (const auto& item : result.records) FillGroupDeliveryWork(item, response->add_work_items());
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::ClaimGroupMessageDeliveriesForRecipient(
    grpc::ServerContext* context,
    const tinyimx::message::v1::ClaimGroupMessageDeliveriesForRecipientRequest* request,
    tinyimx::message::v1::ClaimGroupMessageDeliveriesResponse* response
) {
    if (!ValidateRpcArguments(context, request, response)) {
        return {grpc::StatusCode::INVALID_ARGUMENT,
                "invalid ClaimGroupMessageDeliveriesForRecipient RPC arguments"};
    }
    if (!application_service_) {
        return {grpc::StatusCode::UNAVAILABLE,
                "MessageService application service is unavailable"};
    }
    const auto result =
        application_service_->ClaimGroupMessageDeliveriesForRecipient(
            request->recipient_user_id(),
            request->lease_owner(),
            request->lease_token(),
            request->limit(),
            request->lease_ms());
    if (!result.Succeeded()) {
        return MapApplicationFailure(result.status, result.message);
    }
    for (const auto& item : result.records) {
        FillGroupDeliveryWork(item, response->add_work_items());
    }
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::CompleteGroupMessageDeliveryAttempt(
    grpc::ServerContext* context,
    const tinyimx::message::v1::CompleteGroupMessageDeliveryAttemptRequest* request,
    tinyimx::message::v1::MessageMutationResponse* response
) {
    if (!ValidateRpcArguments(context, request, response))
        return {grpc::StatusCode::INVALID_ARGUMENT, "invalid CompleteGroupMessageDeliveryAttempt RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE, "MessageService application service is unavailable"};
    GroupDeliveryAttemptOutcome outcome;
    switch (request->outcome()) {
        case tinyimx::message::v1::GROUP_DELIVERY_ATTEMPT_OUTCOME_SUBMITTED:
            outcome = GroupDeliveryAttemptOutcome::kSubmitted; break;
        case tinyimx::message::v1::GROUP_DELIVERY_ATTEMPT_OUTCOME_OFFLINE:
            outcome = GroupDeliveryAttemptOutcome::kOffline; break;
        case tinyimx::message::v1::GROUP_DELIVERY_ATTEMPT_OUTCOME_RETRYABLE_FAILURE:
            outcome = GroupDeliveryAttemptOutcome::kRetryableFailure; break;
        default:
            return {grpc::StatusCode::INVALID_ARGUMENT, "invalid group delivery attempt outcome"};
    }
    const auto result = application_service_->CompleteGroupMessageDeliveryAttempt(
        request->message_id(), request->recipient_user_id(), request->lease_token(), outcome,
        request->gateway_id(), request->retry_after_ms(), request->error_code());
    if (!result.Succeeded()) return MapApplicationFailure(result.status, result.message);
    response->set_affected_rows(result.affected_rows);
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::ConfirmGroupMessageDelivery(
    grpc::ServerContext* context,
    const tinyimx::message::v1::ConfirmGroupMessageDeliveryRequest* request,
    tinyimx::message::v1::MessageMutationResponse* response
) {
    if (!ValidateRpcArguments(context, request, response))
        return {grpc::StatusCode::INVALID_ARGUMENT, "invalid ConfirmGroupMessageDelivery RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE, "MessageService application service is unavailable"};
    const auto result = application_service_->ConfirmGroupMessageDelivery(
        request->message_id(), request->recipient_user_id());
    if (!result.Succeeded()) return MapApplicationFailure(result.status, result.message);
    response->set_affected_rows(result.affected_rows);
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::GetPrivateMessage(
    grpc::ServerContext* context,
    const tinyimx::message::v1::GetPrivateMessageRequest* request,
    tinyimx::message::v1::GetPrivateMessageResponse* response
) {
    if (!ValidateRpcArguments(context, request, response)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "invalid GetPrivateMessage RPC arguments");
    }
    if (application_service_ == nullptr) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "MessageService application service is unavailable");
    }

    auto result = application_service_->GetPrivateMessage(request->message_id());
    if (!result.Succeeded()) {
        return MapApplicationFailure(result.status, result.message);
    }
    FillMessage(result.record, response->mutable_record());
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::ListHistory(
    grpc::ServerContext* context,
    const tinyimx::message::v1::ListHistoryRequest* request,
    tinyimx::message::v1::ListHistoryResponse* response
) {
    if (!ValidateRpcArguments(context, request, response)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "invalid ListHistory RPC arguments");
    }
    if (application_service_ == nullptr) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "MessageService application service is unavailable");
    }
    if (!ApplyFaultDelay(context, "TINYIMX_FAULT_MESSAGE_HISTORY_DELAY_MS")) {
        return grpc::Status(grpc::StatusCode::CANCELLED,
                            "ListHistory RPC cancelled during injected delay");
    }

    auto result = application_service_->ListHistory(
        request->actor_user_id(),
        request->peer_user_id(),
        request->before_message_id(),
        request->limit()
    );
    if (!result.Succeeded()) {
        return MapApplicationFailure(result.status, result.message);
    }

    response->set_has_more(result.has_more);
    for (const auto& message : result.messages) {
        FillMessage(message, response->add_messages());
    }
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::ListConversations(
    grpc::ServerContext* context,
    const tinyimx::message::v1::ListConversationsRequest* request,
    tinyimx::message::v1::ListConversationsResponse* response
) {
    if (!ValidateRpcArguments(context, request, response)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "invalid ListConversations RPC arguments");
    }
    if (application_service_ == nullptr) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "MessageService application service is unavailable");
    }
    if (!ApplyFaultDelay(context, "TINYIMX_FAULT_MESSAGE_CONVERSATION_DELAY_MS")) {
        return grpc::Status(grpc::StatusCode::CANCELLED,
                            "ListConversations RPC cancelled during injected delay");
    }

    auto result = application_service_->ListConversations(
        request->actor_user_id(), request->limit());
    if (!result.Succeeded()) {
        return MapApplicationFailure(result.status, result.message);
    }

    response->set_has_more(result.has_more);
    for (const auto& conversation : result.conversations) {
        FillConversation(conversation, response->add_conversations());
    }
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::CountPending(
    grpc::ServerContext* context,
    const tinyimx::message::v1::CountPendingRequest* request,
    tinyimx::message::v1::CountPendingResponse* response
) {
    if (!ValidateRpcArguments(context, request, response)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "invalid CountPending RPC arguments");
    }
    if (application_service_ == nullptr) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "MessageService application service is unavailable");
    }

    auto result = application_service_->CountPending(request->to_user_id());
    if (!result.Succeeded()) {
        return MapApplicationFailure(result.status, result.message);
    }
    response->set_count(result.count);
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::ListPendingAfter(
    grpc::ServerContext* context,
    const tinyimx::message::v1::ListPendingAfterRequest* request,
    tinyimx::message::v1::ListPendingAfterResponse* response
) {
    if (!ValidateRpcArguments(context, request, response)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "invalid ListPendingAfter RPC arguments");
    }
    if (application_service_ == nullptr) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "MessageService application service is unavailable");
    }

    auto result = application_service_->ListPendingAfter(
        request->to_user_id(),
        request->after_message_id(),
        request->limit()
    );
    if (!result.Succeeded()) {
        return MapApplicationFailure(result.status, result.message);
    }

    response->set_has_more(result.has_more);
    for (const auto& message : result.messages) {
        FillMessage(message, response->add_messages());
    }
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::ConfirmReceiver(
    grpc::ServerContext* context,
    const tinyimx::message::v1::ConfirmReceiverRequest* request,
    tinyimx::message::v1::MessageMutationResponse* response
) {
    if (!ValidateRpcArguments(context, request, response)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "invalid ConfirmReceiver RPC arguments");
    }
    if (application_service_ == nullptr) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "MessageService application service is unavailable");
    }

    auto result = application_service_->ConfirmReceiver(
        request->message_id(), request->receiver_user_id());
    if (!result.Succeeded()) {
        return MapApplicationFailure(result.status, result.message);
    }
    response->set_affected_rows(result.affected_rows);

    // The mutation is already durable when the application method returns.
    // This seam deterministically exercises COMMIT-success/response-loss.
    if (!ApplyFaultDelay(
            context,
            "TINYIMX_FAULT_MESSAGE_CONFIRM_POST_COMMIT_DELAY_MS")) {
        return grpc::Status(grpc::StatusCode::CANCELLED,
                            "ConfirmReceiver RPC cancelled after durable result");
    }
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::ConfirmReceiverBatch(
    grpc::ServerContext* context,
    const tinyimx::message::v1::ConfirmReceiverBatchRequest* request,
    tinyimx::message::v1::MessageMutationResponse* response
) {
    if (!ValidateRpcArguments(context, request, response)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "invalid ConfirmReceiverBatch RPC arguments");
    }
    if (application_service_ == nullptr) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "MessageService application service is unavailable");
    }

    std::vector<std::uint64_t> message_ids;
    message_ids.reserve(static_cast<std::size_t>(request->message_ids_size()));
    for (const auto message_id : request->message_ids()) {
        message_ids.push_back(message_id);
    }

    auto result = application_service_->ConfirmReceiverBatch(
        request->receiver_user_id(), message_ids);
    if (!result.Succeeded()) {
        return MapApplicationFailure(result.status, result.message);
    }
    response->set_affected_rows(result.affected_rows);

    if (!ApplyFaultDelay(
            context,
            "TINYIMX_FAULT_MESSAGE_CONFIRM_POST_COMMIT_DELAY_MS")) {
        return grpc::Status(grpc::StatusCode::CANCELLED,
                            "ConfirmReceiverBatch RPC cancelled after durable result");
    }
    return grpc::Status::OK;
}

grpc::Status MessageServiceImpl::MarkDialogRead(
    grpc::ServerContext* context,
    const tinyimx::message::v1::MarkDialogReadRequest* request,
    tinyimx::message::v1::MessageMutationResponse* response
) {
    if (!ValidateRpcArguments(context, request, response)) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "invalid MarkDialogRead RPC arguments");
    }
    if (application_service_ == nullptr) {
        return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                            "MessageService application service is unavailable");
    }

    auto result = application_service_->MarkDialogRead(
        request->reader_user_id(), request->peer_user_id());
    if (!result.Succeeded()) {
        return MapApplicationFailure(result.status, result.message);
    }
    response->set_affected_rows(result.affected_rows);

    if (!ApplyFaultDelay(
            context,
            "TINYIMX_FAULT_MESSAGE_MARK_READ_POST_COMMIT_DELAY_MS")) {
        return grpc::Status(grpc::StatusCode::CANCELLED,
                            "MarkDialogRead RPC cancelled after durable result");
    }
    return grpc::Status::OK;
}

}  // namespace tinyimx::message
