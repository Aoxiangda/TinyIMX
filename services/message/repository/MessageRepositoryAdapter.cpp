#include "services/message/repository/MessageRepositoryAdapter.h"

#include "services/repository/MessageRepository.h"

#include <utility>

namespace tinyimx::message {
namespace {

MessageApplicationStatus MapStatus(
    tinyimx::MessageQueryStatus status
) {
    switch (status) {
        case tinyimx::MessageQueryStatus::kSucceeded:
            return MessageApplicationStatus::kSucceeded;
        case tinyimx::MessageQueryStatus::kInvalidArgument:
            return MessageApplicationStatus::kInvalidArgument;
        case tinyimx::MessageQueryStatus::kInvalidRecord:
            return MessageApplicationStatus::kInvalidRecord;
        case tinyimx::MessageQueryStatus::kStorageError:
            return MessageApplicationStatus::kStorageError;
    }
    return MessageApplicationStatus::kStorageError;
}

MessageApplicationStatus MapStatus(
    tinyimx::MessageMutationStatus status
) {
    switch (status) {
        case tinyimx::MessageMutationStatus::kSucceeded:
            return MessageApplicationStatus::kSucceeded;
        case tinyimx::MessageMutationStatus::kInvalidArgument:
            return MessageApplicationStatus::kInvalidArgument;
        case tinyimx::MessageMutationStatus::kStorageError:
            return MessageApplicationStatus::kStorageError;
    }
    return MessageApplicationStatus::kStorageError;
}

MessageDeliveryState MapDeliveryState(
    std::uint32_t status
) {
    switch (status) {
        case static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kPending):
            return MessageDeliveryState::kPending;
        case static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kReceiverConfirmed):
            return MessageDeliveryState::kReceiverConfirmed;
        case static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kRead):
            return MessageDeliveryState::kRead;
        case static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kFailed):
            return MessageDeliveryState::kFailed;
        default:
            return MessageDeliveryState::kFailed;
    }
}

bool ValidDeliveryStatus(std::uint32_t status) {
    return status <= static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kFailed);
}

MessageView ToView(tinyimx::PrivateMessageRecord record) {
    MessageView view;
    view.message_id = record.message_id;
    view.client_message_id = std::move(record.client_message_id);
    view.from_user_id = record.from_user_id;
    view.to_user_id = record.to_user_id;
    view.message_type = record.message_type;
    view.content = std::move(record.content);
    view.delivery_state = MapDeliveryState(record.delivery_status);
    view.created_at = std::move(record.created_at);
    view.receiver_confirmed_at = std::move(record.delivered_at);
    view.read_at = std::move(record.read_at);
    return view;
}

ConversationView ToView(tinyimx::ConversationRecord record) {
    ConversationView view;
    view.peer_user_id = record.peer_user_id;
    view.last_message_id = record.last_message_id;
    view.last_client_message_id = std::move(record.last_client_message_id);
    view.last_from_user_id = record.last_from_user_id;
    view.last_to_user_id = record.last_to_user_id;
    view.last_message_type = record.last_message_type;
    view.last_content = std::move(record.last_content);
    view.last_delivery_state = MapDeliveryState(record.last_delivery_status);
    view.last_created_at = std::move(record.last_created_at);
    view.last_receiver_confirmed_at = std::move(record.last_delivered_at);
    view.last_read_at = std::move(record.last_read_at);
    return view;
}

MessageRepositoryMutationResult ToMutation(
    tinyimx::UpdatePrivateMessagesResult result
) {
    MessageRepositoryMutationResult output;
    output.status = MapStatus(result.status);
    output.affected_rows = result.affected_rows;
    output.message = std::move(result.message);
    return output;
}

}  // namespace

MessageRepositoryAdapter::MessageRepositoryAdapter(
    tinyimx::MessageRepository* repository
)
    : repository_(repository) {
}

MessageRepositoryPersistResult
MessageRepositoryAdapter::PersistPrivateMessage(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& client_message_id,
    std::uint32_t message_type,
    const std::string& content
) {
    MessageRepositoryPersistResult output;

    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }

    if (message_type < static_cast<std::uint32_t>(tinyimx::PrivateMessageType::kText) ||
        message_type > static_cast<std::uint32_t>(tinyimx::PrivateMessageType::kFile)) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid private message type";
        return output;
    }

    auto result = repository_->SavePrivateMessageIdempotent(
        from_user_id,
        to_user_id,
        content,
        client_message_id,
        static_cast<tinyimx::PrivateMessageType>(message_type)
    );

    output.message_id = result.message_id;
    output.message = std::move(result.message);

    switch (result.status) {
        case tinyimx::IdempotentSavePrivateMessageStatus::kCreated:
            output.status = MessageApplicationStatus::kSucceeded;
            output.outcome = PersistPrivateMessageOutcome::kCreated;
            break;
        case tinyimx::IdempotentSavePrivateMessageStatus::kReused:
            output.status = MessageApplicationStatus::kSucceeded;
            output.outcome = PersistPrivateMessageOutcome::kReused;
            break;
        case tinyimx::IdempotentSavePrivateMessageStatus::kIdempotencyConflict:
            output.status = MessageApplicationStatus::kSucceeded;
            output.outcome = PersistPrivateMessageOutcome::kIdempotencyConflict;
            break;
        case tinyimx::IdempotentSavePrivateMessageStatus::kInvalidArgument:
            output.status = MessageApplicationStatus::kInvalidArgument;
            return output;
        case tinyimx::IdempotentSavePrivateMessageStatus::kInvalidRecord:
            output.status = MessageApplicationStatus::kInvalidRecord;
            return output;
        case tinyimx::IdempotentSavePrivateMessageStatus::kStorageError:
            output.status = MessageApplicationStatus::kStorageError;
            return output;
    }

    if (result.message_id == 0 ||
        result.record.message_id == 0 ||
        !ValidDeliveryStatus(result.record.delivery_status)) {
        output.status = MessageApplicationStatus::kInvalidRecord;
        output.message = "idempotent repository returned invalid message record";
        return output;
    }

    output.record = ToView(std::move(result.record));
    return output;
}

MessageRepositoryGetResult
MessageRepositoryAdapter::GetPrivateMessage(
    std::uint64_t message_id
) {
    MessageRepositoryGetResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }

    auto result = repository_->FindPrivateMessageById(message_id);
    output.status = MapStatus(result.status);
    output.found = result.found;
    output.message = std::move(result.message);
    if (!result.Succeeded() || !result.found) {
        return output;
    }
    if (!ValidDeliveryStatus(result.record.delivery_status)) {
        output.status = MessageApplicationStatus::kInvalidRecord;
        output.found = false;
        output.message = "message repository returned invalid delivery status";
        return output;
    }
    output.record = ToView(std::move(result.record));
    return output;
}

MessageRepositoryHistoryResult
MessageRepositoryAdapter::ListHistory(
    std::uint64_t actor_user_id,
    std::uint64_t peer_user_id,
    std::uint64_t before_message_id,
    std::size_t limit
) {
    MessageRepositoryHistoryResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }

    auto result = repository_->ListDialogMessages(
        actor_user_id,
        peer_user_id,
        before_message_id,
        limit
    );
    output.status = MapStatus(result.status);
    output.message = std::move(result.message);
    if (!result.Succeeded()) {
        return output;
    }

    output.records.reserve(result.records.size());
    for (auto& record : result.records) {
        if (!ValidDeliveryStatus(record.delivery_status)) {
            output.status = MessageApplicationStatus::kInvalidRecord;
            output.message = "message repository returned invalid delivery status";
            output.records.clear();
            return output;
        }
        output.records.push_back(ToView(std::move(record)));
    }
    return output;
}

MessageRepositoryConversationResult
MessageRepositoryAdapter::ListConversations(
    std::uint64_t actor_user_id,
    std::size_t limit
) {
    MessageRepositoryConversationResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }

    auto result = repository_->ListConversations(actor_user_id, limit);
    output.status = MapStatus(result.status);
    output.message = std::move(result.message);
    if (!result.Succeeded()) {
        return output;
    }

    output.records.reserve(result.records.size());
    for (auto& record : result.records) {
        if (!ValidDeliveryStatus(record.last_delivery_status)) {
            output.status = MessageApplicationStatus::kInvalidRecord;
            output.message = "message repository returned invalid conversation state";
            output.records.clear();
            return output;
        }
        output.records.push_back(ToView(std::move(record)));
    }
    return output;
}

MessageRepositoryCountResult
MessageRepositoryAdapter::CountPending(
    std::uint64_t to_user_id
) {
    MessageRepositoryCountResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }

    auto result = repository_->CountPendingMessages(to_user_id);
    output.status = MapStatus(result.status);
    output.count = result.count;
    output.message = std::move(result.message);
    return output;
}

MessageRepositoryPendingResult
MessageRepositoryAdapter::ListPendingAfter(
    std::uint64_t to_user_id,
    std::uint64_t after_message_id,
    std::size_t limit
) {
    MessageRepositoryPendingResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }

    auto result = repository_->ListPendingMessagesAfter(
        to_user_id,
        after_message_id,
        limit
    );
    output.status = MapStatus(result.status);
    output.message = std::move(result.message);
    if (!result.Succeeded()) {
        return output;
    }

    output.records.reserve(result.records.size());
    for (auto& record : result.records) {
        if (!ValidDeliveryStatus(record.delivery_status) ||
            record.delivery_status != static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kPending)) {
            output.status = MessageApplicationStatus::kInvalidRecord;
            output.message = "message repository returned non-pending record in pending page";
            output.records.clear();
            return output;
        }
        output.records.push_back(ToView(std::move(record)));
    }
    return output;
}

MessageRepositoryMutationResult
MessageRepositoryAdapter::ConfirmReceiver(
    std::uint64_t message_id
) {
    if (repository_ == nullptr) {
        MessageRepositoryMutationResult output;
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }
    return ToMutation(repository_->MarkReceiverConfirmed(message_id));
}

MessageRepositoryMutationResult
MessageRepositoryAdapter::ConfirmReceiverBatch(
    const std::vector<std::uint64_t>& message_ids
) {
    if (repository_ == nullptr) {
        MessageRepositoryMutationResult output;
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }
    return ToMutation(repository_->MarkReceiverConfirmedBatch(message_ids));
}

MessageRepositoryMutationResult
MessageRepositoryAdapter::MarkDialogRead(
    std::uint64_t reader_user_id,
    std::uint64_t peer_user_id
) {
    if (repository_ == nullptr) {
        MessageRepositoryMutationResult output;
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }
    return ToMutation(repository_->MarkReadByDialog(reader_user_id, peer_user_id));
}

}  // namespace tinyimx::message
