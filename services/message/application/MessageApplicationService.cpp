#include "services/message/application/MessageApplicationService.h"

#include <algorithm>
#include <cstddef>
#include <unordered_set>
#include <utility>

namespace tinyimx::message {
namespace {

constexpr std::uint32_t kMaxReadPageLimit = 50;
constexpr std::uint32_t kMaxPendingPageLimit = 100;
constexpr std::size_t kMaxConfirmBatchSize = 100;

}  // namespace

MessageApplicationService::MessageApplicationService(
    MessageRepositoryPort* repository
)
    : repository_(repository) {
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

    output.messages = std::move(result.records);
    // MessageRepository intentionally caps pending pages at 100, so C3 uses
    // keyset pagination and a conservative full-page hint instead of a 101st
    // sentinel row. A full page causes one harmless follow-up query.
    output.has_more = output.messages.size() == limit;
    output.status = MessageApplicationStatus::kSucceeded;
    output.message = "pending messages queried";
    return output;
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

}  // namespace tinyimx::message
