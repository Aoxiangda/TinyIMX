#pragma once

#include "services/message/application/MessageApplicationTypes.h"

namespace tinyimx::message {
// nullopt authorizes a pending->confirmed mutation, not a successful ACK.
// Keep the rejection/idempotence order shared by individual and batched calls.
inline std::optional<MessageRepositoryMutationResult> ReceiverConfirmationDecision(
    const MessageRepositoryGetResult& lookup, std::uint64_t receiver_user_id) {
    MessageRepositoryMutationResult result;
    if (!lookup.Succeeded()) {
        result.status = lookup.status;
        result.message = lookup.message;
    } else if (!lookup.found) {
        result.status = MessageApplicationStatus::kNotFound;
        result.message = "private message not found";
    } else if (lookup.record.to_user_id != receiver_user_id) {
        result.status = MessageApplicationStatus::kPermissionDenied;
        result.message = "receiver does not own private message";
    } else {
        switch (lookup.record.delivery_state) {
            case MessageDeliveryState::kPending: return std::nullopt;
            case MessageDeliveryState::kReceiverConfirmed:
            case MessageDeliveryState::kRead:
                result.status = MessageApplicationStatus::kSucceeded;
                result.message = "receiver confirmation already durable";
                break;
            case MessageDeliveryState::kFailed:
                result.status = MessageApplicationStatus::kFailedPrecondition;
                result.message = "failed message cannot be receiver-confirmed";
                break;
            default:
                result.status = MessageApplicationStatus::kInvalidRecord;
                result.message = "message repository returned invalid delivery status";
        }
    }
    return result;
}
} // namespace tinyimx::message
