#pragma once

#include "services/message/application/MessageRepositoryPort.h"
#include "common/runtime/BoundedCallerBatch.h"

#include <cstddef>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tinyimx {
class MessageRepository;
class MySqlConnectionPool;
class MySqlConnection;
}

namespace tinyimx::outbox {
class OutboxRepository;
}

namespace tinyimx::message {

class MessageRepositoryAdapter final
    : public MessageRepositoryPort {
public:
    [[nodiscard]] PendingRecipientsResult ListPendingRecipientsAfter(
        std::uint64_t after_user_id, std::size_t limit) override;
    GroupHistoryApplicationResult ListGroupHistory(std::uint64_t actor, std::uint64_t group, std::uint64_t before, std::size_t limit) override;
    MessageRepositoryAdapter(
        tinyimx::MessageRepository* repository,
        tinyimx::MySqlConnectionPool* pool,
        tinyimx::outbox::OutboxRepository* outbox_repository,
        std::string message_events_topic = "tinyimx-message-events"
    );

    [[nodiscard]] MessageRepositoryPersistResult PersistPrivateMessage(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& client_message_id,
        std::uint32_t message_type,
        const std::string& content
    ) override;

    [[nodiscard]] std::vector<MessageRepositoryPersistResult> PersistPrivateMessages(
        const std::vector<PrivateMessagePersistInput>& inputs) override;


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
    [[nodiscard]] MessageRepositoryGetResult FindPrivateMessageByClientMessageId(
        std::uint64_t from_user_id,
        const std::string& client_message_id
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

    [[nodiscard]] MessageRepositoryMutationResult CompleteGroupMessageDeliveryAttempts(
        const std::vector<GroupDeliveryAttemptCompletion>& attempts
    ) override;

    [[nodiscard]] std::optional<MessageRepositoryMutationResult>
    TryConfirmReceiverOwnedIfPending(
        std::uint64_t message_id, std::uint64_t receiver_user_id
    ) override;

    // Configure test hooks before starting concurrent calls; never set at runtime.
    void SetReceiverConfirmationBeforeCommitHookForTest(
        std::function<void(tinyimx::MySqlConnection*)> hook) {
        receiver_confirmation_before_commit_for_test_ = std::move(hook);
    }
    [[nodiscard]] std::size_t ReceiverConfirmationQueuedForTest() const {
        return receiver_confirmation_batch_.QueuedCount();
    }
    void SetPrivatePersistBeforeCommitHookForTest(
        std::function<void(tinyimx::MySqlConnection*)> hook) {
        private_persist_before_commit_for_test_ = std::move(hook);
    }
    [[nodiscard]] std::size_t PrivatePersistQueuedForTest() const {
        auto count=private_persist_batch_.QueuedCount();
        for(const auto& lane:private_persist_lanes_)count+=lane.batch.QueuedCount();
        return count;
    }

private:
    struct PrivatePersistInput {
        std::uint64_t from_user_id;
        std::uint64_t to_user_id;
        std::string client_message_id;
        std::uint32_t message_type;
        std::string content;
        std::int64_t queued_at_us;
    };
    MessageRepositoryPersistResult PersistPrivateMessageDirect(
        std::uint64_t from_user_id, std::uint64_t to_user_id,
        const std::string& client_message_id, std::uint32_t message_type,
        const std::string& content);
    std::vector<MessageRepositoryPersistResult> ExecutePrivatePersistBatch(
        const std::vector<PrivatePersistInput>& inputs);
    std::vector<MessageRepositoryPersistResult> ExecutePrivatePersistBatchChosen(
        const std::vector<PrivatePersistInput>& inputs,bool grouped);
    std::vector<MessageRepositoryPersistResult> ExecutePrivatePersistBatchSequential(
        const std::vector<PrivatePersistInput>& inputs);
    std::vector<MessageRepositoryPersistResult> ExecutePrivatePersistBatchPackets(
        const std::vector<PrivatePersistInput>& inputs);
    std::vector<MessageRepositoryPersistResult> RecoverPrivatePersistBatch(
        const std::vector<PrivatePersistInput>& inputs);
    tinyimx::BoundedCallerBatch<PrivatePersistInput, MessageRepositoryPersistResult>
        private_persist_batch_{{64, 64, std::chrono::microseconds(0)}};
    struct PrivatePersistLane {
        tinyimx::BoundedCallerBatch<PrivatePersistInput, MessageRepositoryPersistResult>
            batch{{64,64,std::chrono::microseconds(0)}};
    };
    // Opt-in four independent caller-led lanes; no extra worker threads.
    // Same sender (therefore same CID identity) always maps to the same lane.
    // Aggregate queue limit is256 in that mode, not the legacy64; retain this
    // explicit admission/transaction-concurrency tradeoff in experiment records.
    std::array<PrivatePersistLane,4> private_persist_lanes_;
    std::function<void(tinyimx::MySqlConnection*)> private_persist_before_commit_for_test_;
    struct ReceiverConfirmationInput {
        std::uint64_t message_id;
        std::uint64_t receiver_user_id;
    };
    std::optional<MessageRepositoryMutationResult> TryConfirmReceiverOwnedPendingDirect(
        std::uint64_t message_id, std::uint64_t receiver_user_id);
    std::vector<MessageRepositoryMutationResult> ExecuteReceiverConfirmationBatch(
        const std::vector<ReceiverConfirmationInput>& inputs);
    // Default0us drains already-arrived work. The explicit054candidate may
    // collect for2/5ms; the constructor selects it without a detached worker.
    tinyimx::BoundedCallerBatch<ReceiverConfirmationInput, MessageRepositoryMutationResult>
        receiver_confirmation_batch_{{64, 64, std::chrono::microseconds(0)}};
    std::function<void(tinyimx::MySqlConnection*)> receiver_confirmation_before_commit_for_test_;
    tinyimx::MessageRepository* repository_{nullptr};       // non-owning
    tinyimx::MySqlConnectionPool* pool_{nullptr};           // non-owning
    tinyimx::outbox::OutboxRepository* outbox_{nullptr};    // non-owning
    const std::string message_events_topic_;
    TransactionalPreInsertHookForTest pre_insert_hook_for_test_;
};

}  // namespace tinyimx::message
