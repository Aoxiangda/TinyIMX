#include "services/message/repository/MessageRepositoryAdapter.h"

#include "services/repository/MessageRepository.h"

#include "common/db/MySqlConnection.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/message/application/MessageEventFactory.h"
#include "services/outbox/OutboxRepository.h"

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
    tinyimx::MessageRepository* repository,
    tinyimx::MySqlConnectionPool* pool,
    tinyimx::outbox::OutboxRepository* outbox_repository
)
    : repository_(repository),
      pool_(pool),
      outbox_(outbox_repository) {
}

void MessageRepositoryAdapter::SetTransactionalPreInsertHookForTest(
    TransactionalPreInsertHookForTest hook
) {
    pre_insert_hook_for_test_ = std::move(hook);
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

    if (repository_ == nullptr || pool_ == nullptr || outbox_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message transactional persistence is unavailable";
        return output;
    }

    if (from_user_id == 0 || to_user_id == 0 || from_user_id == to_user_id ||
        client_message_id.empty() || client_message_id.size() > 64) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid private message identity";
        return output;
    }

    if (message_type < static_cast<std::uint32_t>(tinyimx::PrivateMessageType::kText) ||
        message_type > static_cast<std::uint32_t>(tinyimx::PrivateMessageType::kFile)) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid private message type";
        return output;
    }

    const auto same_identity = [&](const tinyimx::PrivateMessageRecord& record) {
        return record.message_id != 0 &&
               record.client_message_id == client_message_id &&
               record.from_user_id == from_user_id &&
               record.to_user_id == to_user_id &&
               record.message_type == message_type &&
               record.content == content;
    };

    const auto map_query_failure = [&](const tinyimx::FindPrivateMessageResult& result,
                                       const std::string& phase) {
        switch (result.status) {
            case tinyimx::MessageQueryStatus::kInvalidArgument:
                output.status = MessageApplicationStatus::kInvalidArgument;
                break;
            case tinyimx::MessageQueryStatus::kInvalidRecord:
                output.status = MessageApplicationStatus::kInvalidRecord;
                break;
            case tinyimx::MessageQueryStatus::kStorageError:
            case tinyimx::MessageQueryStatus::kSucceeded:
            default:
                output.status = MessageApplicationStatus::kStorageError;
                break;
        }
        output.message = "transactional private message " + phase + " failed";
        if (!result.message.empty()) {
            output.message += ": " + result.message;
        }
    };

    const auto set_existing_result = [&](const tinyimx::PrivateMessageRecord& record) {
        output.message_id = record.message_id;
        output.record = ToView(record);
        output.status = MessageApplicationStatus::kSucceeded;
        if (same_identity(record)) {
            output.outcome = PersistPrivateMessageOutcome::kReused;
            output.message = "existing private message reused";
        } else {
            output.outcome = PersistPrivateMessageOutcome::kIdempotencyConflict;
            output.message =
                "client_message_id already belongs to a different private message";
        }
    };

    // Fast-path only. The UNIQUE(from_user_id, client_message_id) constraint
    // remains the final concurrent arbiter after a precheck miss.
    const auto existing = repository_->FindPrivateMessageByClientMessageId(
        from_user_id,
        client_message_id
    );

    if (!existing.Succeeded()) {
        map_query_failure(existing, "precheck");
        return output;
    }

    if (existing.Found()) {
        set_existing_result(existing.record);
        return output;
    }

    if (pre_insert_hook_for_test_) {
        pre_insert_hook_for_test_();
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "transactional private message acquire failed";
        return output;
    }

    if (!connection->BeginTransaction()) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "transactional private message begin failed: " +
                         connection->LastError();
        return output;
    }

    const auto save = repository_->SavePrivateMessageOnConnection(
        connection.operator->(),
        from_user_id,
        to_user_id,
        content,
        tinyimx::DeliveryStatus::kPending,
        static_cast<tinyimx::PrivateMessageType>(message_type),
        client_message_id
    );

    if (!save.Succeeded()) {
        const std::string insert_error = save.message;
        if (connection->InTransaction()) {
            connection->Rollback();
        }
        // Release the transaction connection before the recovery read. This is
        // essential for pool_size=1 and avoids self-deadlock.
        connection.Reset();

        const auto recovered = repository_->FindPrivateMessageByClientMessageId(
            from_user_id,
            client_message_id
        );

        if (!recovered.Succeeded()) {
            map_query_failure(recovered, "recovery read");
            if (!insert_error.empty()) {
                output.message += ", original_insert_error=" + insert_error;
            }
            return output;
        }

        if (recovered.Found()) {
            set_existing_result(recovered.record);
            return output;
        }

        output.status = MessageApplicationStatus::kStorageError;
        output.message = insert_error.empty()
            ? "transactional private message insert failed and no durable row was recovered"
            : insert_error;
        return output;
    }

    const auto created = repository_->FindPrivateMessageByIdOnConnection(
        connection.operator->(),
        save.message_id
    );

    if (!created.Succeeded() || !created.Found() || !same_identity(created.record)) {
        if (connection->InTransaction()) {
            connection->Rollback();
        }
        if (!created.Succeeded()) {
            map_query_failure(created, "post-insert verification");
        } else {
            output.status = MessageApplicationStatus::kInvalidRecord;
            output.message =
                "transactional private message post-insert identity verification failed";
        }
        return output;
    }

    const MessageView created_view = ToView(created.record);
    const auto event_spec = MessageEventFactory::MessageCreated(created_view);
    const auto outbox_result = outbox_->InsertOnConnection(
        connection.operator->(),
        event_spec.event,
        event_spec.topic,
        event_spec.tag,
        event_spec.message_key
    );

    if (!outbox_result.success) {
        if (connection->InTransaction()) {
            connection->Rollback();
        }
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message outbox insert failed: " + outbox_result.message;
        return output;
    }

    if (!connection->Commit()) {
        const std::string commit_error = connection->LastError();
        if (connection->InTransaction()) {
            connection->Rollback();
        }
        connection.Reset();

        // MySQL commit failures can be outcome-ambiguous. Recover by the stable
        // M12 idempotency key. If a matching durable row exists, the atomic
        // transaction also made its Outbox row durable.
        const auto recovered = repository_->FindPrivateMessageByClientMessageId(
            from_user_id,
            client_message_id
        );
        if (recovered.Succeeded() && recovered.Found()) {
            set_existing_result(recovered.record);
            output.message = same_identity(recovered.record)
                ? "commit outcome recovered as existing private message"
                : "commit outcome recovered as idempotency conflict";
            return output;
        }

        output.status = MessageApplicationStatus::kStorageError;
        output.message = "transactional private message commit failed";
        if (!commit_error.empty()) {
            output.message += ": " + commit_error;
        }
        return output;
    }

    output.status = MessageApplicationStatus::kSucceeded;
    output.outcome = PersistPrivateMessageOutcome::kCreated;
    output.message_id = created.record.message_id;
    output.record = created_view;
    output.message = "private message and outbox event committed";
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
    MessageRepositoryMutationResult output;

    if (repository_ == nullptr || pool_ == nullptr || outbox_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message transactional read mutation is unavailable";
        return output;
    }

    if (reader_user_id == 0 || peer_user_id == 0 ||
        reader_user_id == peer_user_id) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid dialog read identity";
        return output;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "transactional mark read acquire failed";
        return output;
    }

    if (!connection->BeginTransaction()) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "transactional mark read begin failed: " +
                         connection->LastError();
        return output;
    }

    const auto mutation = repository_->MarkReadByDialogOnConnection(
        connection.operator->(),
        reader_user_id,
        peer_user_id
    );

    if (!mutation.Succeeded()) {
        if (connection->InTransaction()) {
            connection->Rollback();
        }
        output.status = MapStatus(mutation.status);
        output.affected_rows = mutation.affected_rows;
        output.message = std::move(mutation.message);
        return output;
    }

    if (mutation.affected_rows > 0) {
        const auto event_spec = MessageEventFactory::DialogReadAdvanced(
            reader_user_id,
            peer_user_id,
            mutation.affected_rows
        );
        const auto outbox_result = outbox_->InsertOnConnection(
            connection.operator->(),
            event_spec.event,
            event_spec.topic,
            event_spec.tag,
            event_spec.message_key
        );

        if (!outbox_result.success) {
            if (connection->InTransaction()) {
                connection->Rollback();
            }
            output.status = MessageApplicationStatus::kStorageError;
            output.message = "dialog read outbox insert failed: " +
                             outbox_result.message;
            return output;
        }
    }

    if (!connection->Commit()) {
        const std::string commit_error = connection->LastError();
        if (connection->InTransaction()) {
            connection->Rollback();
        }
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "transactional mark read commit failed";
        if (!commit_error.empty()) {
            output.message += ": " + commit_error;
        }
        return output;
    }

    output.status = MessageApplicationStatus::kSucceeded;
    output.affected_rows = mutation.affected_rows;
    output.message = mutation.affected_rows == 0
        ? "dialog already read; no outbox event created"
        : "dialog read state and outbox event committed";
    return output;
}

}  // namespace tinyimx::message
