#pragma once

#include "services/message/application/MessageApplicationTypes.h"
#include "services/message/local/LocalEventSignal.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <unordered_set>
#include <vector>

namespace tinyimx::message::local {

struct LocalIdentityResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::string canonical_cid, message;
};

// Explicit experimental storage owner. It is not selected by existing service
// startup. The SQL/group adapter and original database remain independent.
struct LocalPrivateStoreOptions {
    std::string directory;
    std::string storage_identity;
    std::string message_events_topic;
    std::uint64_t first_message_id{0};
    std::vector<std::uint64_t> initial_user_ids;
    std::size_t block_cache_bytes{128 * 1024 * 1024};
    std::size_t max_pending_events{65536};
    // Bounded by the explicit identity registry. A comparison switch, not a
    // production default: avoid serializing unrelated users on hash collisions.
    bool exact_user_locking{false};
    // Independent experiment: the caller supplies already-ready requests.
    // No delay or queue is introduced here; success follows one synced write.
    bool persist_batch_enable{false};
    // Raw identity is only for isolated fixtures. A service owner supplies an
    // authoritative resolver for legacy collation and current actor existence.
    // to==0 means lookup-only: do not infer that an unknown actor is forbidden.
    std::string identity_policy{"fixture-raw-bytes-v1"};
    std::function<LocalIdentityResult(std::uint64_t,std::uint64_t,const std::string&)> resolve_identity;
    std::function<MessageRepositoryMutationResult(std::uint64_t,std::uint64_t)> before_new_message_commit;
    std::function<bool()> admit_events;
    std::shared_ptr<LocalEventSignal> event_signal;
};

struct PendingLocalEvent {
    std::string event_id, topic, tag, message_key, body;
};
struct LocalEventsResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::vector<PendingLocalEvent> events;
    std::string message;
    // Includes excluded keys, so a bounded page of held entries still advances.
    std::size_t examined_events{0};
    std::string last_examined_event_id;
    bool Succeeded() const noexcept { return status == MessageApplicationStatus::kSucceeded; }
};
struct FailedLocalEvent {PendingLocalEvent event;std::string reason,failed_at;};
struct LocalFailedEventsResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::vector<FailedLocalEvent> events;
    std::string message;
    bool Succeeded()const noexcept{return status==MessageApplicationStatus::kSucceeded;}
};
struct LocalActorIndexResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::vector<std::uint64_t> actors;
    bool has_messages{false};
    std::string message;
    bool Succeeded()const noexcept{return status==MessageApplicationStatus::kSucceeded;}
};
struct LocalUnreadResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::uint64_t private_unread{0}, total_unread{0};
    std::string message;
    bool Succeeded() const noexcept { return status == MessageApplicationStatus::kSucceeded; }
};
struct LocalUserUnreadResult {
    MessageApplicationStatus status{MessageApplicationStatus::kStorageError};
    std::vector<std::pair<std::uint64_t,std::uint64_t>> peer_counts;
    std::uint64_t total_unread{0};
    std::string message;
    bool Succeeded() const noexcept { return status == MessageApplicationStatus::kSucceeded; }
};
struct LocalStoreStats {
    std::uint64_t message_commits{0}, confirmation_commits{0}, read_commits{0};
    std::uint64_t id_reservations{0}, publication_batches{0}, engine_wal_syncs{0};
    std::uint64_t pending_events{0};
    bool poisoned{false}, closing{false};
    std::uint64_t event_scans_started{0}, empty_event_scans_skipped{0};
    std::uint64_t pending_index_queries{0};
    bool pending_event_index_active{false};
    std::uint64_t event_decode_attempts{0}, held_events_skipped{0};
};
struct LocalPhysicalWriteGroupStats {
    bool enabled{false};
    std::uint64_t calls{0},execute_calls{0},engine_write_calls{0},grouped_inputs{0},fallbacks{0};
    std::size_t largest_group{0},held_input_bytes{0},queued{0};
};

struct OwnedReceiverConfirmation {
    std::uint64_t message_id{0}, receiver_user_id{0};
};

class LocalPrivateStore final {
public:
    enum class CommitPoint { kBeforeLocks, kAfterLocks, kBeforeWrite, kAfterSyncBeforeReply,
        kBeforeConfirmationBatchWrite,kAfterConfirmationBatchSyncBeforeReply,
        kBeforePrivateBatchWrite,kAfterPrivateBatchSyncBeforeReply,
        kAfterPublicationSyncBeforeCheckpoint,
        kBeforeEngineWrite,kAfterEngineWriteBeforeWake };
    // Tests can stop/kill their own process at a real engine write boundary.
    // The two engine points carry key-operation count, not a message ID.
    using CommitObserver = std::function<void(CommitPoint, std::uint64_t)>;

    explicit LocalPrivateStore(LocalPrivateStoreOptions options,
                               CommitObserver observer_for_test = {});
    ~LocalPrivateStore();
    LocalPrivateStore(const LocalPrivateStore&) = delete;
    LocalPrivateStore& operator=(const LocalPrivateStore&) = delete;

    MessageRepositoryPersistResult PersistPrivateMessage(
        std::uint64_t from, std::uint64_t to, const std::string& cid,
        std::uint32_t type, const std::string& content);
    // Bounded input order and per-item results, including CID conflicts. With
    // the option disabled this retains the original sequential implementation.
    std::vector<MessageRepositoryPersistResult> PersistPrivateMessages(
        const std::vector<PrivateMessagePersistInput>& inputs);
    MessageRepositoryGetResult GetPrivateMessage(std::uint64_t mid);
    MessageRepositoryGetResult FindPrivateMessageByClientMessageId(
        std::uint64_t from, const std::string& cid);
    MessageRepositoryHistoryResult ListHistory(
        std::uint64_t actor, std::uint64_t peer, std::uint64_t before, std::size_t limit);
    MessageRepositoryConversationResult ListConversations(std::uint64_t actor, std::size_t limit);
    MessageRepositoryCountResult CountPending(std::uint64_t receiver);
    MessageRepositoryPendingResult ListPendingAfter(
        std::uint64_t receiver, std::uint64_t after, std::size_t limit);
    PendingRecipientsResult ListPendingRecipientsAfter(std::uint64_t after, std::size_t limit);
    MessageRepositoryMutationResult ConfirmReceiver(std::uint64_t mid, std::uint64_t receiver);
    // Independent caller results, unlike the all-or-nothing public MID batch.
    // Duplicate requests share one transition; success waits for the synced WAL.
    std::vector<MessageRepositoryMutationResult> ConfirmReceiverRequests(
        const std::vector<OwnedReceiverConfirmation>& inputs);
    MessageRepositoryMutationResult ConfirmReceiverBatch(const std::vector<std::uint64_t>& mids);
    MessageRepositoryMutationResult MarkDialogRead(std::uint64_t reader, std::uint64_t peer);
    LocalUnreadResult ReadUnreadSnapshot(std::uint64_t receiver, std::uint64_t peer);
    LocalUserUnreadResult ReadUserUnreadSnapshot(std::uint64_t receiver);
    LocalEventsResult ListPendingEvents(std::size_t limit);
    LocalEventsResult ListPendingEventsAfter(const std::string& after_event_id,std::size_t limit);
    // Caller keeps this bounded ownership set stable for the synchronous call.
    // Exclusion only avoids rereading work already held by that same relay;
    // it never changes durable state, publication progress or restart contents.
    LocalEventsResult ListPendingEventsAfterExcluding(const std::string& after_event_id,
        std::size_t limit,const std::unordered_set<std::string>& held);
    LocalFailedEventsResult ListFailedEvents(std::size_t limit);
    MessageRepositoryMutationResult MarkEventFailed(const std::string& event_id,const std::string& reason);
    MessageRepositoryMutationResult RequeueFailedEvents(const std::vector<std::string>& event_ids);
    LocalActorIndexResult ListMessageActors();
    std::shared_ptr<LocalEventSignal> EventSignal() const;
    std::string MessageEventsTopic() const;

    // Only after broker success. The WAL stays enabled; unsynced progress may
    // replay after a crash, which permits duplicates, never lost publication.
    // Message/read/confirmation commits always use sync=true. Shutdown syncs
    // progress. Integration must additionally schedule bounded periodic WAL sync.
    MessageRepositoryMutationResult MarkEventsPublished(const std::vector<std::string>& ids);
    MessageRepositoryMutationResult SyncPublicationProgress();
    // Opt-in relay optimization: the first call syncs recovered progress;
    // later calls skip only when no publication marker completed since the
    // last successful sync. affected_rows is 1 for a sync, 0 for a clean skip.
    // Shutdown still calls the unconditional method above.
    MessageRepositoryMutationResult SyncPublicationProgressIfDirty();
    LocalStoreStats Stats() const;
    LocalPhysicalWriteGroupStats PhysicalWriteGroupStats() const;
    bool Healthy() const noexcept;
    std::uint64_t MessageCommitBatches() const noexcept;
    // Opt-in experiment. Same event ID always shares a lock for publication,
    // quarantine and requeue. This does not change commit/durability semantics.
    bool EventProgressShardsEnabled() const noexcept;
    bool Shutdown() noexcept;

private:
    friend class LocalEventRelay;
    LocalEventsResult ListPendingEventPage(const std::string& after_event_id,
        std::size_t limit,const std::unordered_set<std::string>* held);
    bool AcquireRelayOwnership()noexcept;
    void ReleaseRelayOwnership()noexcept;
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace tinyimx::message::local
