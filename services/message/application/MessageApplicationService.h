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


    [[nodiscard]] MessageRepositoryGroupGetResult FindGroupMessageByClientMessageId(
        std::uint64_t from_user_id,
        const std::string& client_message_id
    );

    // M17-B1 compatibility overload retained for focused regression tests.
    [[nodiscard]] PersistGroupMessageApplicationResult PersistAuthorizedGroupMessage(
        std::uint64_t from_user_id,
        std::uint64_t group_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content,
        std::uint64_t membership_epoch,
        std::uint64_t member_version,
        std::uint32_t authorized_role
    );

    [[nodiscard]] PersistGroupMessageApplicationResult PersistAuthorizedGroupMessage(
        std::uint64_t from_user_id,
        std::uint64_t group_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content,
        std::uint64_t membership_epoch,
        std::uint64_t member_version,
        std::uint32_t authorized_role,
        const std::vector<std::uint64_t>& recipient_user_ids
    );


    [[nodiscard]] MessageRepositoryGroupDeliveryGetResult GetGroupMessageDelivery(
        std::uint64_t message_id,
        std::uint64_t recipient_user_id
    );

    [[nodiscard]] MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveries(
        const std::string& lease_owner,
        const std::string& lease_token,
        std::size_t limit,
        std::uint32_t lease_ms,
        std::uint64_t message_id
    );

    [[nodiscard]] MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveriesForRecipient(
        std::uint64_t recipient_user_id,
        const std::string& lease_owner,
        const std::string& lease_token,
        std::size_t limit,
        std::uint32_t lease_ms
    );

    [[nodiscard]] MessageMutationApplicationResult CompleteGroupMessageDeliveryAttempt(
        std::uint64_t message_id,
        std::uint64_t recipient_user_id,
        const std::string& lease_token,
        GroupDeliveryAttemptOutcome outcome,
        const std::string& gateway_id,
        std::uint32_t retry_after_ms,
        const std::string& error_code
    );

    [[nodiscard]] MessageMutationApplicationResult ConfirmGroupMessageDelivery(
        std::uint64_t message_id,
        std::uint64_t recipient_user_id
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
