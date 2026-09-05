#pragma once

#include "services/message/application/MessageApplicationTypes.h"
#include "services/message/application/MessageRepositoryPort.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx::message {

class MessageApplicationService final {
public:
    explicit MessageApplicationService(
        MessageRepositoryPort* repository
    );

    MessageApplicationService(const MessageApplicationService&) = delete;
    MessageApplicationService& operator=(const MessageApplicationService&) = delete;

    [[nodiscard]] PersistPrivateMessageApplicationResult PersistPrivateMessage(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content
    );

    [[nodiscard]] GetPrivateMessageApplicationResult GetPrivateMessage(
        std::uint64_t message_id
    );

    [[nodiscard]] ListHistoryApplicationResult ListHistory(
        std::uint64_t actor_user_id,
        std::uint64_t peer_user_id,
        std::uint64_t before_message_id,
        std::uint32_t limit
    );

    [[nodiscard]] ListConversationsApplicationResult ListConversations(
        std::uint64_t actor_user_id,
        std::uint32_t limit
    );

    [[nodiscard]] CountPendingApplicationResult CountPending(
        std::uint64_t to_user_id
    );

    [[nodiscard]] ListPendingAfterApplicationResult ListPendingAfter(
        std::uint64_t to_user_id,
        std::uint64_t after_message_id,
        std::uint32_t limit
    );

    [[nodiscard]] MessageMutationApplicationResult ConfirmReceiver(
        std::uint64_t message_id,
        std::uint64_t receiver_user_id
    );

    [[nodiscard]] MessageMutationApplicationResult ConfirmReceiverBatch(
        std::uint64_t receiver_user_id,
        const std::vector<std::uint64_t>& message_ids
    );

    [[nodiscard]] MessageMutationApplicationResult MarkDialogRead(
        std::uint64_t reader_user_id,
        std::uint64_t peer_user_id
    );

private:
    MessageRepositoryPort* repository_{nullptr};  // non-owning
};

}  // namespace tinyimx::message
