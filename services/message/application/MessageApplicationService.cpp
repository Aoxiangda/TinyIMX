#include "services/message/application/MessageApplicationService.h"
#include "services/message/application/GroupDeliveryCompletionValidation.h"

#include <algorithm>
#include <cstddef>
#include <unordered_set>
#include <utility>

namespace tinyimx::message {
namespace {

constexpr std::uint32_t kMaxReadPageLimit = 50;
constexpr std::uint32_t kMaxPendingPageLimit = 100;
// Includes all variable fields and conservative protobuf framing allowance.
// Keep well below default gRPC receive size without changing the wire schema.
constexpr std::size_t kMaxPendingPageBytes = 2 * 1024 * 1024;
constexpr std::size_t kPendingRecordOverheadBytes = 256;
constexpr std::size_t kMaxConfirmBatchSize = 100;

}  // namespace

MessageApplicationService::MessageApplicationService(
    MessageRepositoryPort* repository
)
    : repository_(repository) {
}

ResolvePrivateMessageApplicationResult
MessageApplicationService::ResolvePrivateMessage(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& client_message_id,
    std::uint32_t message_type,
    const std::string& content
) {
    ResolvePrivateMessageApplicationResult output;
    if (from_user_id == 0 || to_user_id == 0 || from_user_id == to_user_id ||
        client_message_id.empty() || client_message_id.size() > 64 ||
        message_type < 1 || message_type > 3 || content.empty()) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid ResolvePrivateMessage application request";
        return output;
    }
    if (repository_ == nullptr) {
        output.message = "message repository port is unavailable";
        return output;
    }
    auto result = repository_->FindPrivateMessageByClientMessageId(
        from_user_id, client_message_id);
    if (!result.Succeeded()) {
        output.status = result.status;
        output.message = std::move(result.message);
        return output;
    }
    if (!result.found) {
        output.status = MessageApplicationStatus::kSucceeded;
        output.message = "private message not observed in this read";
        return output;
    }
    const auto& record = result.record;
    const auto state = static_cast<std::uint32_t>(record.delivery_state);
    if (record.message_id == 0 || record.from_user_id != from_user_id ||
        record.client_message_id != client_message_id || record.to_user_id == 0 ||
        record.to_user_id == from_user_id || record.message_type < 1 ||
        record.message_type > 3 || record.content.empty() || state > 3) {
        output.status = MessageApplicationStatus::kInvalidRecord;
        output.message = "identity lookup returned an invalid private message";
        return output;
    }
    output.status = MessageApplicationStatus::kSucceeded;
    if (record.to_user_id != to_user_id || record.message_type != message_type ||
        record.content != content) {
        output.outcome = ResolvePrivateMessageOutcome::kIdempotencyConflict;
        output.message = "client_message_id belongs to different immutable content";
        return output;
    }
    output.outcome = ResolvePrivateMessageOutcome::kMatchedDurable;
    output.record = std::move(result.record);
    output.message = "matching durable private message observed";
    return output;
}

PersistPrivateMessageApplicationResult
MessageApplicationService::PersistPrivateMessage(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& client_message_id,
    std::uint32_t message_type,
    const std::string& content
) {
    PersistPrivateMessageApplicationResult output;

    if (from_user_id == 0 ||
        to_user_id == 0 ||
        from_user_id == to_user_id ||
        client_message_id.empty() ||
        client_message_id.size() > 64 ||
        message_type < 1 ||
        message_type > 3 ||
        content.empty()) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid PersistPrivateMessage application request";
        return output;
    }

    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }

    auto repository_result = repository_->PersistPrivateMessage(
        from_user_id,
        to_user_id,
        client_message_id,
        message_type,
        content
    );

    output.status = repository_result.status;
    output.outcome = repository_result.outcome;
    output.message_id = repository_result.message_id;
    output.record = std::move(repository_result.record);
    output.message = std::move(repository_result.message);
    return output;
}



MessageRepositoryGroupGetResult
MessageApplicationService::FindGroupMessageByClientMessageId(
    std::uint64_t from_user_id,
    const std::string& client_message_id
) {
    MessageRepositoryGroupGetResult output;
    if (from_user_id == 0 || client_message_id.empty() || client_message_id.size() > 64) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid group message idempotency lookup";
        return output;
    }
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }
    return repository_->FindGroupMessageByClientMessageId(
        from_user_id, client_message_id);
}

PersistGroupMessageApplicationResult
MessageApplicationService::PersistAuthorizedGroupMessage(
    std::uint64_t from_user_id,
    std::uint64_t group_id,
    const std::string& client_message_id,
    std::uint32_t message_type,
    const std::string& content,
    std::uint64_t membership_epoch,
    std::uint64_t member_version,
    std::uint32_t authorized_role
) {
    return PersistAuthorizedGroupMessage(
        from_user_id, group_id, client_message_id, message_type, content,
        membership_epoch, member_version, authorized_role, {});
}

PersistGroupMessageApplicationResult
MessageApplicationService::PersistAuthorizedGroupMessage(
    std::uint64_t from_user_id,
    std::uint64_t group_id,
    const std::string& client_message_id,
    std::uint32_t message_type,
    const std::string& content,
    std::uint64_t membership_epoch,
    std::uint64_t member_version,
    std::uint32_t authorized_role,
    const std::vector<std::uint64_t>& recipient_user_ids
) {
    PersistGroupMessageApplicationResult output;
    if (from_user_id == 0 || group_id == 0 || client_message_id.empty() ||
        client_message_id.size() > 64 || message_type < 1 || message_type > 3 ||
        content.empty() || membership_epoch == 0 || member_version == 0 ||
        authorized_role < 1 || authorized_role > 3) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid authorized group message application request";
        return output;
    }
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }
    auto result = repository_->PersistAuthorizedGroupMessage(
        from_user_id, group_id, client_message_id, message_type, content,
        membership_epoch, member_version, authorized_role, recipient_user_ids);
    output.status = result.status;
    output.outcome = result.outcome;
    output.message_id = result.message_id;
    output.record = std::move(result.record);
    output.message = std::move(result.message);
    return output;
}

MessageRepositoryGroupDeliveryGetResult
MessageApplicationService::GetGroupMessageDelivery(
    std::uint64_t message_id,
    std::uint64_t recipient_user_id
) {
    MessageRepositoryGroupDeliveryGetResult output;
    if (message_id == 0 || recipient_user_id == 0) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid GetGroupMessageDelivery application request";
        return output;
    }
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }
    return repository_->GetGroupMessageDelivery(message_id, recipient_user_id);
}

MessageRepositoryGroupDeliveryListResult
MessageApplicationService::ClaimGroupMessageDeliveries(
    const std::string& lease_owner,
    const std::string& lease_token,
    std::size_t limit,
    std::uint32_t lease_ms,
    std::uint64_t message_id
) {
    MessageRepositoryGroupDeliveryListResult output;
    if (lease_owner.empty() || lease_token.empty() || limit == 0 || limit > 256 ||
        lease_ms < 100 || lease_ms > 60000) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid ClaimGroupMessageDeliveries application request";
        return output;
    }
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }
    return repository_->ClaimGroupMessageDeliveries(lease_owner, lease_token, limit, lease_ms, message_id);
}

MessageRepositoryGroupDeliveryListResult
MessageApplicationService::ClaimGroupMessageDeliveriesForRecipient(
    std::uint64_t recipient_user_id,
    const std::string& lease_owner,
    const std::string& lease_token,
    std::size_t limit,
    std::uint32_t lease_ms
) {
    MessageRepositoryGroupDeliveryListResult output;
    if (recipient_user_id == 0 || lease_owner.empty() || lease_token.empty() ||
        lease_owner.size() > 128 || lease_token.size() > 128 ||
        limit == 0 || limit > 256 || lease_ms < 100 || lease_ms > 60000) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message =
            "invalid ClaimGroupMessageDeliveriesForRecipient application request";
        return output;
    }
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }
    return repository_->ClaimGroupMessageDeliveriesForRecipient(
        recipient_user_id, lease_owner, lease_token, limit, lease_ms);
}

MessageMutationApplicationResult
MessageApplicationService::CompleteGroupMessageDeliveryAttempt(
    std::uint64_t message_id,
    std::uint64_t recipient_user_id,
    const std::string& lease_token,
    GroupDeliveryAttemptOutcome outcome,
    const std::string& gateway_id,
    std::uint32_t retry_after_ms,
    const std::string& error_code
) {
    MessageMutationApplicationResult output;
    if (message_id == 0 || recipient_user_id == 0 || lease_token.empty()) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid CompleteGroupMessageDeliveryAttempt application request";
        return output;
    }
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }
    auto result = repository_->CompleteGroupMessageDeliveryAttempt(
        message_id, recipient_user_id, lease_token, outcome, gateway_id, retry_after_ms, error_code);
    output.status = result.status;
    output.affected_rows = result.affected_rows;
    output.message = std::move(result.message);
    return output;
}

MessageMutationApplicationResult
MessageApplicationService::CompleteGroupMessageDeliveryAttempts(
    const std::vector<GroupDeliveryAttemptCompletion>& attempts
) {
    MessageMutationApplicationResult output;
    if (!ValidGroupDeliveryAttemptCompletions(attempts)) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid CompleteGroupMessageDeliveryAttempts application request";
        return output;
    }
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }
    auto result = repository_->CompleteGroupMessageDeliveryAttempts(attempts);
    output.status = result.status;
    output.affected_rows = result.affected_rows;
    output.message = std::move(result.message);
    return output;
}

MessageMutationApplicationResult
MessageApplicationService::ConfirmGroupMessageDelivery(
    std::uint64_t message_id,
    std::uint64_t recipient_user_id
) {
    MessageMutationApplicationResult output;
    if (message_id == 0 || recipient_user_id == 0) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid ConfirmGroupMessageDelivery application request";
        return output;
    }
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }
    auto result = repository_->ConfirmGroupMessageDelivery(message_id, recipient_user_id);
    output.status = result.status;
    output.affected_rows = result.affected_rows;
    output.message = std::move(result.message);
    return output;
}

GetPrivateMessageApplicationResult
MessageApplicationService::GetPrivateMessage(
    std::uint64_t message_id
) {
    GetPrivateMessageApplicationResult output;

    if (message_id == 0) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid GetPrivateMessage application request";
        return output;
    }

    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }

    auto result = repository_->GetPrivateMessage(message_id);
    if (!result.Succeeded()) {
        output.status = result.status;
        output.message = std::move(result.message);
        return output;
    }

    if (!result.found) {
        output.status = MessageApplicationStatus::kNotFound;
        output.message = "private message not found";
        return output;
    }

    if (result.record.message_id != message_id ||
        result.record.from_user_id == 0 ||
        result.record.to_user_id == 0 ||
        result.record.message_type < 1 ||
        result.record.message_type > 3) {
        output.status = MessageApplicationStatus::kInvalidRecord;
        output.message = "message repository returned invalid private message";
        return output;
    }

    output.status = MessageApplicationStatus::kSucceeded;
    output.record = std::move(result.record);
    output.message = "private message queried";
    return output;
}

ListHistoryApplicationResult
MessageApplicationService::ListHistory(
    std::uint64_t actor_user_id,
    std::uint64_t peer_user_id,
    std::uint64_t before_message_id,
    std::uint32_t limit
) {
    ListHistoryApplicationResult output;

    if (actor_user_id == 0 ||
        peer_user_id == 0 ||
        actor_user_id == peer_user_id ||
        limit == 0 ||
        limit > kMaxReadPageLimit) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid ListHistory application request";
        return output;
    }

    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }

    const std::size_t query_limit =
        static_cast<std::size_t>(limit) + 1U;

    auto repository_result = repository_->ListHistory(
        actor_user_id,
        peer_user_id,
        before_message_id,
        query_limit
    );

    if (!repository_result.Succeeded()) {
        output.status = repository_result.status;
        output.message = std::move(repository_result.message);
        return output;
    }

    output.messages = std::move(repository_result.records);
    if (output.messages.size() > limit) {
        output.has_more = true;
        output.messages.erase(output.messages.begin());
    }

    output.status = MessageApplicationStatus::kSucceeded;
    output.message = "message history queried";
    return output;
}

ListConversationsApplicationResult
MessageApplicationService::ListConversations(
    std::uint64_t actor_user_id,
    std::uint32_t limit
) {
    ListConversationsApplicationResult output;

    if (actor_user_id == 0 ||
        limit == 0 ||
        limit > kMaxReadPageLimit) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid ListConversations application request";
        return output;
    }

    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }

    const std::size_t query_limit =
        static_cast<std::size_t>(limit) + 1U;

    auto repository_result = repository_->ListConversations(
        actor_user_id,
        query_limit
    );

    if (!repository_result.Succeeded()) {
        output.status = repository_result.status;
        output.message = std::move(repository_result.message);
        return output;
    }

    output.conversations = std::move(repository_result.records);
    if (output.conversations.size() > limit) {
        output.has_more = true;
        output.conversations.resize(limit);
    }

    output.status = MessageApplicationStatus::kSucceeded;
    output.message = "message conversations queried";
    return output;
}

CountPendingApplicationResult
MessageApplicationService::CountPending(
    std::uint64_t to_user_id
) {
    CountPendingApplicationResult output;

    if (to_user_id == 0) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid CountPending application request";
        return output;
    }

    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }

    auto result = repository_->CountPending(to_user_id);
    output.status = result.status;
    output.count = result.count;
    output.message = std::move(result.message);
    return output;
}

ListPendingAfterApplicationResult
MessageApplicationService::ListPendingAfter(
    std::uint64_t to_user_id,
    std::uint64_t after_message_id,
    std::uint32_t limit
) {
    ListPendingAfterApplicationResult output;

    if (to_user_id == 0 ||
        limit == 0 ||
        limit > kMaxPendingPageLimit) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid ListPendingAfter application request";
        return output;
    }

    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }

    auto result = repository_->ListPendingAfter(
        to_user_id,
        after_message_id,
        static_cast<std::size_t>(limit)
    );

    if (!result.Succeeded()) {
        output.status = result.status;
        output.message = std::move(result.message);
        return output;
    }

    if (result.records.size() > limit) {
        output.status = MessageApplicationStatus::kInvalidRecord;
        output.message = "message repository exceeded pending page count";
        return output;
    }
    std::uint64_t previous_id = after_message_id;
    for (const auto& message : result.records) {
        if (message.message_id == 0 ||
            message.message_id <= previous_id ||
            message.to_user_id != to_user_id ||
            message.delivery_state != MessageDeliveryState::kPending) {
            output.status = MessageApplicationStatus::kInvalidRecord;
            output.message = "message repository returned invalid pending page";
            return output;
        }
        previous_id = message.message_id;
    }

    const auto record_count = result.records.size();
    std::size_t page_bytes = 0, take = 0;
    for (const auto& message : result.records) {
        std::size_t record_bytes = kPendingRecordOverheadBytes;
        bool record_fits = true;
        for (const auto* field : {&message.content, &message.client_message_id,
                                  &message.created_at, &message.receiver_confirmed_at, &message.read_at}) {
            if (field->size() > kMaxPendingPageBytes - record_bytes) {
                record_fits = false;
                break;
            }
            record_bytes += field->size();
        }
        if (!record_fits || record_bytes > kMaxPendingPageBytes - page_bytes) {
            if (take == 0) {
                output.status = MessageApplicationStatus::kInvalidRecord;
                output.message = "pending message exceeds response byte budget";
                return output;
            }
            break;
        }
        page_bytes += record_bytes;
        ++take;
    }
    output.messages = std::move(result.records);
    output.messages.resize(take);
    // Byte-limited short pages continue after the last returned M; omitted
    // rows remain in SQL. A count-full page retains the conservative hint.
    output.has_more = take < record_count || record_count == limit;
    output.status = MessageApplicationStatus::kSucceeded;
    output.message = "pending messages queried";
    return output;
}

PendingRecipientsResult MessageApplicationService::ListPendingRecipientsAfter(
    std::uint64_t after_user_id, std::uint32_t limit
) {
    PendingRecipientsResult output;
    if (limit == 0 || limit > 256) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid pending recipient page limit";
        return output;
    }
    if (!repository_) {
        output.message = "message repository port is unavailable";
        return output;
    }
    auto result = repository_->ListPendingRecipientsAfter(after_user_id, limit);
    if (!result.Succeeded()) {
        output.status = result.status;
        output.message = std::move(result.message);
        return output;
    }
    if (result.recipient_user_ids.size() > limit ||
        (result.has_more && result.recipient_user_ids.size() != limit)) {
        output.status = MessageApplicationStatus::kInvalidRecord;
        output.message = "invalid pending recipient page count or continuation";
        return output;
    }
    std::uint64_t previous = after_user_id;
    for (const auto id : result.recipient_user_ids) {
        if (id == 0 || id <= previous) {
            output.status = MessageApplicationStatus::kInvalidRecord;
            output.message = "invalid pending recipient page ordering";
            return output;
        }
        previous = id;
    }
    return result;
}

MessageMutationApplicationResult
MessageApplicationService::ConfirmReceiver(
    std::uint64_t message_id,
    std::uint64_t receiver_user_id
) {
    MessageMutationApplicationResult output;

    if (message_id == 0 || receiver_user_id == 0) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid ConfirmReceiver application request";
        return output;
    }

    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }

    auto lookup = repository_->GetPrivateMessage(message_id);
    if (!lookup.Succeeded()) {
        output.status = lookup.status;
        output.message = std::move(lookup.message);
        return output;
    }
    if (!lookup.found) {
        output.status = MessageApplicationStatus::kNotFound;
        output.message = "private message not found";
        return output;
    }
    if (lookup.record.to_user_id != receiver_user_id) {
        output.status = MessageApplicationStatus::kPermissionDenied;
        output.message = "receiver does not own private message";
        return output;
    }

    switch (lookup.record.delivery_state) {
        case MessageDeliveryState::kPending:
            break;
        case MessageDeliveryState::kReceiverConfirmed:
        case MessageDeliveryState::kRead:
            output.status = MessageApplicationStatus::kSucceeded;
            output.affected_rows = 0;
            output.message = "receiver confirmation already durable";
            return output;
        case MessageDeliveryState::kFailed:
            output.status = MessageApplicationStatus::kFailedPrecondition;
            output.message = "failed message cannot be receiver-confirmed";
            return output;
        default:
            output.status = MessageApplicationStatus::kInvalidRecord;
            output.message = "message repository returned invalid delivery status";
            return output;
    }

    auto mutation = repository_->ConfirmReceiver(message_id);
    output.status = mutation.status;
    output.affected_rows = mutation.affected_rows;
    output.message = std::move(mutation.message);
    return output;
}

MessageMutationApplicationResult
MessageApplicationService::ConfirmReceiverBatch(
    std::uint64_t receiver_user_id,
    const std::vector<std::uint64_t>& message_ids
) {
    MessageMutationApplicationResult output;

    if (receiver_user_id == 0 ||
        message_ids.empty() ||
        message_ids.size() > kMaxConfirmBatchSize) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid ConfirmReceiverBatch application request";
        return output;
    }

    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }

    std::unordered_set<std::uint64_t> seen;
    std::vector<std::uint64_t> pending_ids;
    pending_ids.reserve(message_ids.size());

    // Validate the whole bounded page before mutating. This avoids a partial
    // authorization result when a replay-repair batch contains one foreign M.
    for (const auto message_id : message_ids) {
        if (message_id == 0 || !seen.insert(message_id).second) {
            output.status = MessageApplicationStatus::kInvalidArgument;
            output.message = "ConfirmReceiverBatch contains invalid/duplicate message id";
            return output;
        }

        auto lookup = repository_->GetPrivateMessage(message_id);
        if (!lookup.Succeeded()) {
            output.status = lookup.status;
            output.message = std::move(lookup.message);
            return output;
        }
        if (!lookup.found) {
            output.status = MessageApplicationStatus::kNotFound;
            output.message = "private message not found";
            return output;
        }
        if (lookup.record.to_user_id != receiver_user_id) {
            output.status = MessageApplicationStatus::kPermissionDenied;
            output.message = "receiver does not own every private message in batch";
            return output;
        }

        switch (lookup.record.delivery_state) {
            case MessageDeliveryState::kPending:
                pending_ids.push_back(message_id);
                break;
            case MessageDeliveryState::kReceiverConfirmed:
            case MessageDeliveryState::kRead:
                break;
            case MessageDeliveryState::kFailed:
                output.status = MessageApplicationStatus::kFailedPrecondition;
                output.message = "failed message cannot be receiver-confirmed";
                return output;
        }
    }

    if (pending_ids.empty()) {
        output.status = MessageApplicationStatus::kSucceeded;
        output.affected_rows = 0;
        output.message = "receiver confirmation batch already durable";
        return output;
    }

    auto mutation = repository_->ConfirmReceiverBatch(pending_ids);
    output.status = mutation.status;
    output.affected_rows = mutation.affected_rows;
    output.message = std::move(mutation.message);
    return output;
}

MessageMutationApplicationResult
MessageApplicationService::MarkDialogRead(
    std::uint64_t reader_user_id,
    std::uint64_t peer_user_id
) {
    MessageMutationApplicationResult output;

    if (reader_user_id == 0 ||
        peer_user_id == 0 ||
        reader_user_id == peer_user_id) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid MarkDialogRead application request";
        return output;
    }

    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository port is unavailable";
        return output;
    }

    auto mutation = repository_->MarkDialogRead(reader_user_id, peer_user_id);
    output.status = mutation.status;
    output.affected_rows = mutation.affected_rows;
    output.message = std::move(mutation.message);
    return output;
}

GroupHistoryApplicationResult MessageApplicationService::ListGroupHistory(std::uint64_t actor, std::uint64_t group, std::uint64_t before, std::size_t limit) {
    GroupHistoryApplicationResult out;
    if(actor==0 || group==0 || limit==0 || limit>100){out.status=MessageApplicationStatus::kInvalidArgument;out.message="invalid group history arguments";return out;}
    if(!repository_){out.message="group history unavailable";return out;}
    out=repository_->ListGroupHistory(actor,group,before,limit+1);if(!out.Succeeded())return out;
    std::uint64_t previous=before;
    for(const auto &record:out.messages){if(record.group_id!=group || record.message_id==0 || (previous && record.message_id>=previous) || record.from_user_id==0 || record.content.empty()){
        out.status=MessageApplicationStatus::kInvalidRecord;out.messages.clear();out.message="group history identity/order mismatch";return out;}
        previous=record.message_id;
    }
    out.has_more=out.messages.size()>limit;if(out.has_more)out.messages.resize(limit);return out;
}
}  // namespace tinyimx::message
