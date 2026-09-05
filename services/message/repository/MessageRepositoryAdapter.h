#pragma once

#include "services/message/application/MessageRepositoryPort.h"

namespace tinyimx {
class MessageRepository;
}

namespace tinyimx::message {

class MessageRepositoryAdapter final
    : public MessageRepositoryPort {
public:
    explicit MessageRepositoryAdapter(
        tinyimx::MessageRepository* repository
    );

    [[nodiscard]] MessageRepositoryPersistResult PersistPrivateMessage(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content
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

private:
    tinyimx::MessageRepository* repository_{nullptr};  // non-owning
};

}  // namespace tinyimx::message
