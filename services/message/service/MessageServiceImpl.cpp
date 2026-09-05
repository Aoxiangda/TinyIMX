#include "services/message/service/MessageServiceImpl.h"

#include "services/message/application/MessageApplicationService.h"

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

}  // namespace

MessageServiceImpl::MessageServiceImpl(
    MessageApplicationService* application_service
)
    : application_service_(application_service) {
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
