#pragma once

#include "services/message/application/MessageRepositoryPort.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tinyimx {
class MessageRepository;
class MySqlConnectionPool;
}

namespace tinyimx::outbox {
class OutboxRepository;
}

namespace tinyimx::message {

class MessageRepositoryAdapter final
    : public MessageRepositoryPort {
public:
    MessageRepositoryAdapter(
        tinyimx::MessageRepository* repository,
        tinyimx::MySqlConnectionPool* pool,
        tinyimx::outbox::OutboxRepository* outbox_repository
    );

    [[nodiscard]] MessageRepositoryPersistResult PersistPrivateMessage(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content
    ) override;


    [[nodiscard]] MessageRepositoryGroupGetResult FindGroupMessageByClientMessageId(
        std::uint64_t from_user_id,
        const std::string& client_message_id
    ) override;

    [[nodiscard]] MessageRepositoryGroupPersistResult PersistAuthorizedGroupMessage(
        std::uint64_t from_user_id,
        std::uint64_t group_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content,
        std::uint64_t membership_epoch,
        std::uint64_t member_version,
        std::uint32_t authorized_role
    );

    [[nodiscard]] MessageRepositoryGroupPersistResult PersistAuthorizedGroupMessage(
        std::uint64_t from_user_id,
        std::uint64_t group_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content,
        std::uint64_t membership_epoch,
        std::uint64_t member_version,
        std::uint32_t authorized_role,
        const std::vector<std::uint64_t>& recipient_user_ids
    ) override;


    [[nodiscard]] MessageRepositoryGroupDeliveryGetResult GetGroupMessageDelivery(
        std::uint64_t message_id,
        std::uint64_t recipient_user_id
    ) override;

    [[nodiscard]] MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveries(
        const std::string& lease_owner,
        const std::string& lease_token,
        std::size_t limit,
        std::uint32_t lease_ms,
        std::uint64_t message_id
    ) override;

    [[nodiscard]] MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveriesForRecipient(
        std::uint64_t recipient_user_id,
        const std::string& lease_owner,
        const std::string& lease_token,
        std::size_t limit,
        std::uint32_t lease_ms
    ) override;

    [[nodiscard]] MessageRepositoryMutationResult CompleteGroupMessageDeliveryAttempt(
        std::uint64_t message_id,
        std::uint64_t recipient_user_id,
        const std::string& lease_token,
        GroupDeliveryAttemptOutcome outcome,
        const std::string& gateway_id,
        std::uint32_t retry_after_ms,
        const std::string& error_code
    ) override;

    [[nodiscard]] MessageRepositoryMutationResult ConfirmGroupMessageDelivery(
        std::uint64_t message_id,
        std::uint64_t recipient_user_id
    ) override;
    [[nodiscard]] MessageRepositoryGetResult GetPrivateMessage(
        std::uint64_t message_id
    ) override;

    [[nodiscard]] MessageRepositoryHistoryResult ListHistory(
        std::uint64_t actor_user_id,
        std::uint64_t peer_user_id,
        std::uint64_t before_message_id,
        std::size_t limit
    ) override;

    [[nodiscard]] MessageRepositoryConversationResult ListConversations(
        std::uint64_t actor_user_id,
        std::size_t limit
    ) override;

    [[nodiscard]] MessageRepositoryCountResult CountPending(
        std::uint64_t to_user_id
    ) override;

    [[nodiscard]] MessageRepositoryPendingResult ListPendingAfter(
        std::uint64_t to_user_id,
        std::uint64_t after_message_id,
        std::size_t limit
    ) override;

    [[nodiscard]] MessageRepositoryMutationResult ConfirmReceiver(
        std::uint64_t message_id
    ) override;

    [[nodiscard]] MessageRepositoryMutationResult ConfirmReceiverBatch(
        const std::vector<std::uint64_t>& message_ids
    ) override;

    [[nodiscard]] MessageRepositoryMutationResult MarkDialogRead(
        std::uint64_t reader_user_id,
        std::uint64_t peer_user_id
    ) override;

    using TransactionalPreInsertHookForTest = std::function<void()>;

    // Deterministic concurrency seam. Configure before concurrent calls.
    void SetTransactionalPreInsertHookForTest(
        TransactionalPreInsertHookForTest hook
    );

private:
    tinyimx::MessageRepository* repository_{nullptr};       // non-owning
    tinyimx::MySqlConnectionPool* pool_{nullptr};           // non-owning
    tinyimx::outbox::OutboxRepository* outbox_{nullptr};    // non-owning
    TransactionalPreInsertHookForTest pre_insert_hook_for_test_;
};

}  // namespace tinyimx::message
