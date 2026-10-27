#pragma once
#include "common/runtime/BoundedCallerBatch.h"
#include "services/message/application/MessageRepositoryPort.h"
#include "services/message/local/LocalPrivateStore.h"
#include <stdexcept>

namespace tinyimx::message::local {
// Private data has one explicit owner; never fall back to empty SQL history.
// Group calls keep the existing repository, permission snapshots and leases.
class LocalMessageRepositoryAdapter final : public MessageRepositoryPort {
public:
    LocalMessageRepositoryAdapter(LocalPrivateStore* store,MessageRepositoryPort* groups,
        bool receiver_confirmation_batch_enable=false):store_(store),groups_(groups),
        receiver_confirmation_batch_enable_(receiver_confirmation_batch_enable) {
        if(!store_||!groups_)throw std::invalid_argument("local repository composition requires both owners");
    }
    MessageRepositoryPersistResult PersistPrivateMessage(std::uint64_t from,std::uint64_t to,const std::string& cid,std::uint32_t type,const std::string& content)override{
        return store_->PersistPrivateMessage(from,to,cid,type,content);
    }
    std::vector<MessageRepositoryPersistResult> PersistPrivateMessages(
        const std::vector<PrivateMessagePersistInput>& inputs)override{
        return store_->PersistPrivateMessages(inputs);
    }
    MessageRepositoryGetResult FindPrivateMessageByClientMessageId(std::uint64_t from,const std::string& cid)override{
        return store_->FindPrivateMessageByClientMessageId(from,cid);
    }
    MessageRepositoryGetResult GetPrivateMessage(std::uint64_t mid)override{return store_->GetPrivateMessage(mid);}
    MessageRepositoryHistoryResult ListHistory(std::uint64_t actor,std::uint64_t peer,std::uint64_t before,std::size_t limit)override{
        return store_->ListHistory(actor,peer,before,limit);
    }
    MessageRepositoryConversationResult ListConversations(std::uint64_t actor,std::size_t limit)override{return store_->ListConversations(actor,limit);}
    MessageRepositoryCountResult CountPending(std::uint64_t actor)override{return store_->CountPending(actor);}
    MessageRepositoryPendingResult ListPendingAfter(std::uint64_t actor,std::uint64_t after,std::size_t limit)override{return store_->ListPendingAfter(actor,after,limit);}
    PendingRecipientsResult ListPendingRecipientsAfter(std::uint64_t after,std::size_t limit)override{return store_->ListPendingRecipientsAfter(after,limit);}
    MessageRepositoryMutationResult ConfirmReceiver(std::uint64_t mid)override{
        const auto found=store_->GetPrivateMessage(mid);
        if(!found.Found()){MessageRepositoryMutationResult out;out.status=found.Succeeded()?MessageApplicationStatus::kNotFound:found.status;out.message=found.message;return out;}
        return store_->ConfirmReceiver(mid,found.record.to_user_id);
    }
    MessageRepositoryMutationResult ConfirmReceiverBatch(const std::vector<std::uint64_t>& mids)override{return store_->ConfirmReceiverBatch(mids);}
    std::optional<MessageRepositoryMutationResult> TryConfirmReceiverOwnedIfPending(std::uint64_t mid,std::uint64_t actor)override{
        if(!receiver_confirmation_batch_enable_)return store_->ConfirmReceiver(mid,actor);
        MessageRepositoryMutationResult failure;
        failure.status=MessageApplicationStatus::kStorageError;
        failure.message="native receiver confirmation batch unavailable or overloaded";
        try {
            return receiver_confirmation_batch_.Run(OwnedReceiverConfirmation{mid,actor},
                [this](const auto& inputs){return store_->ConfirmReceiverRequests(inputs);},failure);
        }catch(const std::exception&){return failure;}
    }
    std::size_t ReceiverConfirmationQueuedForTest()const{return receiver_confirmation_batch_.QueuedCount();}
    MessageRepositoryMutationResult MarkDialogRead(std::uint64_t reader,std::uint64_t peer)override{return store_->MarkDialogRead(reader,peer);}

    MessageRepositoryGroupGetResult FindGroupMessageByClientMessageId(std::uint64_t from,const std::string& cid)override{
        return groups_->FindGroupMessageByClientMessageId(from,cid);
    }
    MessageRepositoryGroupPersistResult PersistAuthorizedGroupMessage(std::uint64_t from,std::uint64_t group,const std::string& cid,std::uint32_t type,
        const std::string& content,std::uint64_t epoch,std::uint64_t version,std::uint32_t role,const std::vector<std::uint64_t>& recipients)override{
        return groups_->PersistAuthorizedGroupMessage(from,group,cid,type,content,epoch,version,role,recipients);
    }
    GroupHistoryApplicationResult ListGroupHistory(std::uint64_t actor,std::uint64_t group,std::uint64_t before,std::size_t limit)override{
        return groups_->ListGroupHistory(actor,group,before,limit);
    }
    MessageRepositoryGroupDeliveryGetResult GetGroupMessageDelivery(std::uint64_t mid,std::uint64_t recipient)override{return groups_->GetGroupMessageDelivery(mid,recipient);}
    MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveries(const std::string& owner,const std::string& token,std::size_t limit,std::uint32_t lease,std::uint64_t mid)override{
        return groups_->ClaimGroupMessageDeliveries(owner,token,limit,lease,mid);
    }
    MessageRepositoryGroupDeliveryListResult ClaimGroupMessageDeliveriesForRecipient(std::uint64_t recipient,const std::string& owner,const std::string& token,std::size_t limit,std::uint32_t lease)override{
        return groups_->ClaimGroupMessageDeliveriesForRecipient(recipient,owner,token,limit,lease);
    }
    MessageRepositoryMutationResult CompleteGroupMessageDeliveryAttempt(std::uint64_t mid,std::uint64_t recipient,const std::string& token,
        GroupDeliveryAttemptOutcome outcome,const std::string& gateway,std::uint32_t retry,const std::string& error)override{
        return groups_->CompleteGroupMessageDeliveryAttempt(mid,recipient,token,outcome,gateway,retry,error);
    }
    MessageRepositoryMutationResult CompleteGroupMessageDeliveryAttempts(const std::vector<GroupDeliveryAttemptCompletion>& attempts)override{
        return groups_->CompleteGroupMessageDeliveryAttempts(attempts);
    }
    MessageRepositoryMutationResult ConfirmGroupMessageDelivery(std::uint64_t mid,std::uint64_t recipient)override{return groups_->ConfirmGroupMessageDelivery(mid,recipient);}
private:
    LocalPrivateStore* store_;
    MessageRepositoryPort* groups_;
    const bool receiver_confirmation_batch_enable_;
    // Caller-led, bounded and ready-only: no timer, new thread or early ACK.
    tinyimx::BoundedCallerBatch<OwnedReceiverConfirmation,MessageRepositoryMutationResult>
        receiver_confirmation_batch_{{64,128,std::chrono::microseconds(0)}};
};
}
