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
