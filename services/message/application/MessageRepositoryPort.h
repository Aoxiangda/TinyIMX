#pragma once

#include "services/message/application/MessageApplicationTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx::message {

class MessageRepositoryPort {
public:
    virtual ~MessageRepositoryPort() = default;

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

    [[nodiscard]] virtual MessageRepositoryMutationResult ConfirmReceiverBatch(
        const std::vector<std::uint64_t>& message_ids
    ) = 0;

    [[nodiscard]] virtual MessageRepositoryMutationResult MarkDialogRead(
        std::uint64_t reader_user_id,
        std::uint64_t peer_user_id
    ) = 0;
};

}  // namespace tinyimx::message
