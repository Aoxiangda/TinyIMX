#pragma once

#include "services/message/application/MessageApplicationTypes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tinyimx::message {

class MessageRepositoryPort {
public:
    [[nodiscard]] virtual PendingRecipientsResult ListPendingRecipientsAfter(
        std::uint64_t, std::size_t) {
        PendingRecipientsResult out;
        out.message = "pending recipient discovery is unavailable";
        return out;
    }
    virtual ~MessageRepositoryPort() = default;

    // Compatibility implementations fail explicitly; an unsupported lookup
    // must never be interpreted as proof that the original write is absent.
    [[nodiscard]] virtual MessageRepositoryGetResult FindPrivateMessageByClientMessageId(
        std::uint64_t,
        const std::string&
    ) {
        MessageRepositoryGetResult result;
        result.message = "private message identity lookup is unavailable";
        return result;
    }

    MessageRepositoryPort() = default;
    MessageRepositoryPort(const MessageRepositoryPort&) = delete;
    MessageRepositoryPort& operator=(const MessageRepositoryPort&) = delete;

    [[nodiscard]] virtual MessageRepositoryPersistResult PersistPrivateMessage(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content
    ) = 0;


    [[nodiscard]] virtual MessageRepositoryGroupGetResult FindGroupMessageByClientMessageId(
        std::uint64_t from_user_id,
        const std::string& client_message_id
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryGroupPersistResult PersistAuthorizedGroupMessage(
        std::uint64_t from_user_id,
        std::uint64_t group_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content,
        std::uint64_t membership_epoch,
        std::uint64_t member_version,
        std::uint32_t authorized_role,
        const std::vector<std::uint64_t>& recipient_user_ids
    ) = 0;


    [[nodiscard]] virtual MessageRepositoryGroupDeliveryGetResult GetGroupMessageDelivery(
        std::uint64_t message_id,
        std::uint64_t recipient_user_id
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveries(
        const std::string& lease_owner,
        const std::string& lease_token,
        std::size_t limit,
        std::uint32_t lease_ms,
        std::uint64_t message_id
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveriesForRecipient(
        std::uint64_t recipient_user_id,
        const std::string& lease_owner,
        const std::string& lease_token,
        std::size_t limit,
        std::uint32_t lease_ms
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryMutationResult CompleteGroupMessageDeliveryAttempt(
        std::uint64_t message_id,
        std::uint64_t recipient_user_id,
        const std::string& lease_token,
        GroupDeliveryAttemptOutcome outcome,
        const std::string& gateway_id,
        std::uint32_t retry_after_ms,
        const std::string& error_code
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryMutationResult ConfirmGroupMessageDelivery(
        std::uint64_t message_id,
        std::uint64_t recipient_user_id
    ) = 0;
    [[nodiscard]] virtual MessageRepositoryGetResult GetPrivateMessage(
        std::uint64_t message_id
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryHistoryResult ListHistory(
        std::uint64_t actor_user_id,
        std::uint64_t peer_user_id,
        std::uint64_t before_message_id,
        std::size_t limit
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryConversationResult ListConversations(
        std::uint64_t actor_user_id,
        std::size_t limit
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryCountResult CountPending(
        std::uint64_t to_user_id
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryPendingResult ListPendingAfter(
        std::uint64_t to_user_id,
        std::uint64_t after_message_id,
        std::size_t limit
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryMutationResult ConfirmReceiver(
        std::uint64_t message_id
    ) = 0;

    // Adapters may perform this validated read/mutation on one healthy lease.
    // The compatibility path retains ownership and monotonic-state checks.
    [[nodiscard]] virtual MessageRepositoryMutationResult ConfirmReceiverForRecipient(
        std::uint64_t message_id,
        std::uint64_t receiver_user_id
    ) {
        if (message_id == 0 || receiver_user_id == 0) {
            MessageRepositoryMutationResult out;
            out.status = MessageApplicationStatus::kInvalidArgument;
            out.message = "invalid ConfirmReceiver application request";
            return out;
        }
        auto terminal = ReceiverConfirmationTerminalResult(
            GetPrivateMessage(message_id), receiver_user_id);
        if (terminal) return std::move(*terminal);
        return ConfirmReceiver(message_id);
    }

    [[nodiscard]] virtual MessageRepositoryMutationResult ConfirmReceiverBatch(
        const std::vector<std::uint64_t>& message_ids
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryMutationResult MarkDialogRead(
        std::uint64_t reader_user_id,
        std::uint64_t peer_user_id
    ) = 0;

protected:
    [[nodiscard]] static std::optional<MessageRepositoryMutationResult>
    ReceiverConfirmationTerminalResult(
        MessageRepositoryGetResult lookup,
        std::uint64_t receiver_user_id
    ) {
        MessageRepositoryMutationResult out;
        if (!lookup.Succeeded()) {
            out.status = lookup.status;
            out.message = std::move(lookup.message);
            return out;
        }
        if (!lookup.found) {
            out.status = MessageApplicationStatus::kNotFound;
            out.message = "private message not found";
            return out;
        }
        if (lookup.record.to_user_id != receiver_user_id) {
            out.status = MessageApplicationStatus::kPermissionDenied;
            out.message = "receiver does not own private message";
            return out;
        }
        switch (lookup.record.delivery_state) {
            case MessageDeliveryState::kPending:
                return std::nullopt;
            case MessageDeliveryState::kReceiverConfirmed:
            case MessageDeliveryState::kRead:
                out.status = MessageApplicationStatus::kSucceeded;
                out.message = "receiver confirmation already durable";
                return out;
            case MessageDeliveryState::kFailed:
                out.status = MessageApplicationStatus::kFailedPrecondition;
                out.message = "failed message cannot be receiver-confirmed";
                return out;
        }
        out.status = MessageApplicationStatus::kInvalidRecord;
        out.message = "message repository returned invalid delivery status";
        return out;
    }
};

}  // namespace tinyimx::message
