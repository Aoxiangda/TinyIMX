#pragma once
#include "services/message/application/MessageRepositoryPort.h"
namespace tinyimx::tests {
using namespace tinyimx::message;
// Unused methods fail with their original default failure result.
class ReadyBatchNoopRepository:public MessageRepositoryPort {
public:
    MessageRepositoryPersistResult PersistPrivateMessage(std::uint64_t from_user_id, std::uint64_t to_user_id, const std::string& client_message_id, std::uint32_t message_type, const std::string& content)override{return {};}
    MessageRepositoryGroupGetResult FindGroupMessageByClientMessageId(std::uint64_t from_user_id, const std::string& client_message_id)override{return {};}
    MessageRepositoryGroupPersistResult PersistAuthorizedGroupMessage(std::uint64_t from_user_id, std::uint64_t group_id, const std::string& client_message_id, std::uint32_t message_type, const std::string& content, std::uint64_t membership_epoch, std::uint64_t member_version, std::uint32_t authorized_role, const std::vector<std::uint64_t>& recipient_user_ids)override{return {};}
    MessageRepositoryGroupDeliveryGetResult GetGroupMessageDelivery(std::uint64_t message_id, std::uint64_t recipient_user_id)override{return {};}
    MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveries(const std::string& lease_owner, const std::string& lease_token, std::size_t limit, std::uint32_t lease_ms, std::uint64_t message_id)override{return {};}
    MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveriesForRecipient(std::uint64_t recipient_user_id, const std::string& lease_owner, const std::string& lease_token, std::size_t limit, std::uint32_t lease_ms)override{return {};}
    MessageRepositoryMutationResult CompleteGroupMessageDeliveryAttempt(std::uint64_t message_id, std::uint64_t recipient_user_id, const std::string& lease_token, GroupDeliveryAttemptOutcome outcome, const std::string& gateway_id, std::uint32_t retry_after_ms, const std::string& error_code)override{return {};}
    MessageRepositoryMutationResult ConfirmGroupMessageDelivery(std::uint64_t message_id, std::uint64_t recipient_user_id)override{return {};}
    MessageRepositoryGetResult GetPrivateMessage(std::uint64_t message_id)override{return {};}
    MessageRepositoryHistoryResult ListHistory(std::uint64_t actor_user_id, std::uint64_t peer_user_id, std::uint64_t before_message_id, std::size_t limit)override{return {};}
    MessageRepositoryConversationResult ListConversations(std::uint64_t actor_user_id, std::size_t limit)override{return {};}
    MessageRepositoryCountResult CountPending(std::uint64_t to_user_id)override{return {};}
    MessageRepositoryPendingResult ListPendingAfter(std::uint64_t to_user_id, std::uint64_t after_message_id, std::size_t limit)override{return {};}
    MessageRepositoryMutationResult ConfirmReceiver(std::uint64_t message_id)override{return {};}
    MessageRepositoryMutationResult ConfirmReceiverBatch(const std::vector<std::uint64_t>& message_ids)override{return {};}
    MessageRepositoryMutationResult MarkDialogRead(std::uint64_t reader_user_id, std::uint64_t peer_user_id)override{return {};}
};
}
