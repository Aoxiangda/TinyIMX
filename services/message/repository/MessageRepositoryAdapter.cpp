#include "services/message/repository/MessageRepositoryAdapter.h"
#include "common/observability/StorageCallTrace.h"
#include "services/message/application/GroupDeliveryCompletionValidation.h"
#include "services/message/application/ReceiverConfirmationDecision.h"
#include "services/message/repository/PrivatePersistenceTrace.h"
#include "services/message/repository/PrivatePersistBatchTrace.h"
#include "services/message/repository/ReceiverConfirmationBatchPolicy.h"

#include "services/repository/MessageRepository.h"

#include "common/db/MySqlConnection.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/message/application/MessageEventFactory.h"
#include "services/outbox/OutboxRepository.h"

#include <utility>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>

namespace tinyimx::message {
namespace {

bool PrivateOutboxCommitPacketEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_PRIVATE_OUTBOX_COMMIT_PACKET_ENABLE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

bool PrivateNativeSqlBatchEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_PRIVATE_NATIVE_SQL_BATCH_ENABLE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

bool PrivatePersistSqlBatchEnabled() noexcept {
    static const bool enabled=[] {
        const char* v=std::getenv("TINYIMX_PRIVATE_PERSIST_SQL_BATCH_ENABLE");
        return v&&v[0]=='1'&&v[1]=='\0';
    }();
    return enabled;
}

bool PrivatePersistShardsEnabled() noexcept {
    static const bool enabled=[] {
        const char* v=std::getenv("TINYIMX_PRIVATE_PERSIST_SHARDS");
        return v&&v[0]=='4'&&v[1]=='\0';
    }();
    return enabled;
}

bool PrivateSenderCoalesceEnabled() noexcept {
    static const bool enabled = [] {
        const char* v = std::getenv("TINYIMX_PRIVATE_PERSIST_COALESCE_ENABLE");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return enabled;
}

bool PrivateReceiverConfirmCoalesceEnabled() noexcept {
    static const bool enabled = [] {
        const char* v = std::getenv("TINYIMX_PRIVATE_CONFIRM_COALESCE_ENABLE");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return enabled;
}

bool PrivateReceiverOwnedUpdateEnabled() noexcept {
    static const bool enabled = [] {
        const char* v = std::getenv("TINYIMX_PRIVATE_RECEIVER_OWNED_UPDATE_ENABLE");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return enabled;
}

bool PrivateBeginInsertReadBatchEnabled() noexcept {
    static const bool enabled = [] {
        const char* v = std::getenv("TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return enabled;
}

// Default-off candidate: the UNIQUE(from_user_id, client_message_id) index
// arbitrates every insert. Duplicate attempts still roll back and release the
// lease before the original full-record recovery read; no cached permission,
// identity shortcut, outbox omission or early durable ACK is introduced.
bool PrivateInsertFirstEnabled() noexcept {
    static const bool enabled = [] {
        const char* v = std::getenv("TINYIMX_PRIVATE_INSERT_FIRST_ENABLE");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return enabled;
}

MessageApplicationStatus MapStatus(
    tinyimx::MessageQueryStatus status
) {
    switch (status) {
        case tinyimx::MessageQueryStatus::kSucceeded:
            return MessageApplicationStatus::kSucceeded;
        case tinyimx::MessageQueryStatus::kInvalidArgument:
            return MessageApplicationStatus::kInvalidArgument;
        case tinyimx::MessageQueryStatus::kInvalidRecord:
            return MessageApplicationStatus::kInvalidRecord;
        case tinyimx::MessageQueryStatus::kStorageError:
            return MessageApplicationStatus::kStorageError;
    }
    return MessageApplicationStatus::kStorageError;
}

MessageApplicationStatus MapStatus(
    tinyimx::MessageMutationStatus status
) {
    switch (status) {
        case tinyimx::MessageMutationStatus::kSucceeded:
            return MessageApplicationStatus::kSucceeded;
        case tinyimx::MessageMutationStatus::kInvalidArgument:
            return MessageApplicationStatus::kInvalidArgument;
        case tinyimx::MessageMutationStatus::kStorageError:
            return MessageApplicationStatus::kStorageError;
    }
    return MessageApplicationStatus::kStorageError;
}

MessageDeliveryState MapDeliveryState(
    std::uint32_t status
) {
    switch (status) {
        case static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kPending):
            return MessageDeliveryState::kPending;
        case static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kReceiverConfirmed):
            return MessageDeliveryState::kReceiverConfirmed;
        case static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kRead):
            return MessageDeliveryState::kRead;
        case static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kFailed):
            return MessageDeliveryState::kFailed;
        default:
            return MessageDeliveryState::kFailed;
    }
}

bool ValidDeliveryStatus(std::uint32_t status) {
    return status <= static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kFailed);
}

MessageView ToView(tinyimx::PrivateMessageRecord record) {
    MessageView view;
    view.message_id = record.message_id;
    view.client_message_id = std::move(record.client_message_id);
    view.from_user_id = record.from_user_id;
    view.to_user_id = record.to_user_id;
    view.message_type = record.message_type;
    view.content = std::move(record.content);
    view.delivery_state = MapDeliveryState(record.delivery_status);
    view.created_at = std::move(record.created_at);
    view.receiver_confirmed_at = std::move(record.delivered_at);
    view.read_at = std::move(record.read_at);
    return view;
}



GroupMessageView ToView(tinyimx::GroupMessageRecord record) {
    GroupMessageView view;
    view.message_id = record.message_id;
    view.client_message_id = std::move(record.client_message_id);
    view.group_id = record.group_id;
    view.from_user_id = record.from_user_id;
    view.message_type = record.message_type;
    view.content = std::move(record.content);
    view.membership_epoch = record.membership_epoch;
    view.member_version = record.member_version;
    view.authorized_role = record.authorized_role;
    view.created_at = std::move(record.created_at);
    return view;
}

GroupDeliveryState MapGroupDeliveryState(tinyimx::GroupDeliveryStatus status) {
    switch (status) {
        case tinyimx::GroupDeliveryStatus::kPending: return GroupDeliveryState::kPending;
        case tinyimx::GroupDeliveryStatus::kDeferredOffline: return GroupDeliveryState::kDeferredOffline;
        case tinyimx::GroupDeliveryStatus::kDelivered: return GroupDeliveryState::kDelivered;
    }
    return GroupDeliveryState::kPending;
}

GroupDeliveryWorkItem ToView(tinyimx::GroupDeliveryWorkRecord record) {
    GroupDeliveryWorkItem view;
    view.message = ToView(std::move(record.message));
    view.delivery.message_id = record.delivery.message_id;
    view.delivery.group_id = record.delivery.group_id;
    view.delivery.recipient_user_id = record.delivery.recipient_user_id;
    view.delivery.delivery_state = MapGroupDeliveryState(record.delivery.delivery_status);
    view.delivery.attempt_count = record.delivery.attempt_count;
    view.delivery.last_gateway_id = std::move(record.delivery.last_gateway_id);
    view.delivery.lease_owner = std::move(record.delivery.lease_owner);
    view.delivery.lease_token = std::move(record.delivery.lease_token);
    view.delivery.lease_until = std::move(record.delivery.lease_until);
    view.delivery.next_retry_at = std::move(record.delivery.next_retry_at);
    view.delivery.last_error_code = std::move(record.delivery.last_error_code);
    view.delivery.created_at = std::move(record.delivery.created_at);
    view.delivery.updated_at = std::move(record.delivery.updated_at);
    view.delivery.delivered_at = std::move(record.delivery.delivered_at);
    return view;
}

ConversationView ToView(tinyimx::ConversationRecord record) {
    ConversationView view;
    view.peer_user_id = record.peer_user_id;
    view.last_message_id = record.last_message_id;
    view.last_client_message_id = std::move(record.last_client_message_id);
    view.last_from_user_id = record.last_from_user_id;
    view.last_to_user_id = record.last_to_user_id;
    view.last_message_type = record.last_message_type;
    view.last_content = std::move(record.last_content);
    view.last_delivery_state = MapDeliveryState(record.last_delivery_status);
    view.last_created_at = std::move(record.last_created_at);
    view.last_receiver_confirmed_at = std::move(record.last_delivered_at);
    view.last_read_at = std::move(record.last_read_at);
    return view;
}

MessageRepositoryMutationResult ToMutation(
    tinyimx::UpdatePrivateMessagesResult result
) {
    MessageRepositoryMutationResult output;
    output.status = MapStatus(result.status);
    output.affected_rows = result.affected_rows;
    output.message = std::move(result.message);
    return output;
}

}  // namespace

MessageRepositoryAdapter::MessageRepositoryAdapter(
    tinyimx::MessageRepository* repository,
    tinyimx::MySqlConnectionPool* pool,
    tinyimx::outbox::OutboxRepository* outbox_repository,
    std::string message_events_topic
)
    : receiver_confirmation_batch_({64,64,ReceiverConfirmationCollectWindow()}),
      repository_(repository),
      pool_(pool),
      outbox_(outbox_repository),
      message_events_topic_(std::move(message_events_topic)) {
    if (!MessageEventFactory::IsValidTopic(message_events_topic_)) {
        throw std::invalid_argument("invalid message event topic");
    }
}

void MessageRepositoryAdapter::SetTransactionalPreInsertHookForTest(
    TransactionalPreInsertHookForTest hook
) {
    pre_insert_hook_for_test_ = std::move(hook);
}

MessageRepositoryPersistResult
MessageRepositoryAdapter::PersistPrivateMessage(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& client_message_id,
    std::uint32_t message_type,
    const std::string& content
) {
    // The legacy pre-insert race barrier is intentionally lease-free. Never
    // serialize that test barrier behind a batch leader waiting on its peers.
    if (!PrivateSenderCoalesceEnabled() || pre_insert_hook_for_test_ ||
        !repository_ || !pool_ || !outbox_ || !from_user_id || !to_user_id ||
        from_user_id==to_user_id || client_message_id.empty() ||
        client_message_id.size()>64 || message_type<1 || message_type>3 ||
        content.empty() || content.size()>65535) {
        return PersistPrivateMessageDirect(from_user_id,to_user_id,client_message_id,message_type,content);
    }
    MessageRepositoryPersistResult failure;
    failure.status=MessageApplicationStatus::kStorageError;
    failure.message="private persistence batch unavailable or overloaded";
    auto& batch=PrivatePersistShardsEnabled()
        ?private_persist_lanes_[from_user_id%private_persist_lanes_.size()].batch:private_persist_batch_;
    return batch.Run(
        PrivatePersistInput{from_user_id,to_user_id,client_message_id,message_type,content,
            PrivatePersistenceTrace::EnvironmentEnabled()?PrivatePersistBatchTrace::Now():0},
        [this](const auto& inputs){return ExecutePrivatePersistBatch(inputs);},failure);
}

MessageRepositoryPersistResult
MessageRepositoryAdapter::PersistPrivateMessageDirect(
    std::uint64_t from_user_id, std::uint64_t to_user_id,
    const std::string& client_message_id, std::uint32_t message_type,
    const std::string& content
) {
    MessageRepositoryPersistResult output;
    PrivatePersistenceTrace trace(from_user_id,to_user_id,output);
    using Phase=PrivatePersistenceTrace::Phase;

    if (repository_ == nullptr || pool_ == nullptr || outbox_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message transactional persistence is unavailable";
        return output;
    }

    if (from_user_id == 0 || to_user_id == 0 || from_user_id == to_user_id ||
        client_message_id.empty() || client_message_id.size() > 64) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid private message identity";
        return output;
    }

    if (message_type < static_cast<std::uint32_t>(tinyimx::PrivateMessageType::kText) ||
        message_type > static_cast<std::uint32_t>(tinyimx::PrivateMessageType::kFile)) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid private message type";
        return output;
    }

    const auto same_identity = [&](const tinyimx::PrivateMessageRecord& record) {
        return record.message_id != 0 &&
               record.client_message_id == client_message_id &&
               record.from_user_id == from_user_id &&
               record.to_user_id == to_user_id &&
               record.message_type == message_type &&
               record.content == content;
    };

    const auto map_query_failure = [&](const tinyimx::FindPrivateMessageResult& result,
                                       const std::string& phase) {
        switch (result.status) {
            case tinyimx::MessageQueryStatus::kInvalidArgument:
                output.status = MessageApplicationStatus::kInvalidArgument;
                break;
            case tinyimx::MessageQueryStatus::kInvalidRecord:
                output.status = MessageApplicationStatus::kInvalidRecord;
                break;
            case tinyimx::MessageQueryStatus::kStorageError:
            case tinyimx::MessageQueryStatus::kSucceeded:
            default:
                output.status = MessageApplicationStatus::kStorageError;
                break;
        }
        output.message = "transactional private message " + phase + " failed";
        if (!result.message.empty()) {
            output.message += ": " + result.message;
        }
    };

    const auto set_existing_result = [&](const tinyimx::PrivateMessageRecord& record) {
        output.message_id = record.message_id;
        output.record = ToView(record);
        output.status = MessageApplicationStatus::kSucceeded;
        if (same_identity(record)) {
            output.outcome = PersistPrivateMessageOutcome::kReused;
            output.message = "existing private message reused";
        } else {
            output.outcome = PersistPrivateMessageOutcome::kIdempotencyConflict;
            output.message =
                "client_message_id already belongs to a different private message";
        }
    };

    // Keep the healthy lease from precheck through the atomic write. A second
    // acquisition/Ping adds a round trip and another pool admission without
    // providing a stronger identity or durability check.
    auto connection = trace.Measure(Phase::Acquire,[&]{return pool_->Acquire();});
    if (!connection) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "transactional private message acquire failed";
        return output;
    }

    if (!PrivateInsertFirstEnabled()) {
        // Fast-path only. The UNIQUE(from_user_id, client_message_id) constraint
        // remains the final concurrent arbiter after a precheck miss.
        const auto existing = trace.Measure(Phase::Precheck,[&]{
            return repository_->FindPrivateMessageByClientMessageIdOnConnection(
                connection.operator->(),from_user_id,client_message_id);
        });

        if (!existing.Succeeded()) {
            map_query_failure(existing, "precheck");
            return output;
        }

        if (existing.Found()) {
            set_existing_result(existing.record);
            return output;
        }
    }

    if (pre_insert_hook_for_test_) {
        // The deterministic race hook may wait for more callers than pool
        // slots. Preserve its original lease-free barrier behavior, including
        // pool_size=1. Production has no hook and reuses the original lease.
        connection.Reset();
        pre_insert_hook_for_test_();
        connection = trace.Measure(Phase::Acquire,[&]{return pool_->Acquire();});
        if (!connection) {
            output.status = MessageApplicationStatus::kStorageError;
            output.message = "transactional private message acquire failed after test hook";
            return output;
        }
    }

    tinyimx::SavePrivateMessageResult save;
    tinyimx::FindPrivateMessageResult created;
    const bool batched = PrivateBeginInsertReadBatchEnabled();
    if (batched) {
        auto started = trace.Measure(Phase::BeginInsertRead, [&] {
            return repository_->BeginSavePrivateMessageOnConnection(
                connection.operator->(), from_user_id, to_user_id, content,
                tinyimx::DeliveryStatus::kPending,
                static_cast<tinyimx::PrivateMessageType>(message_type), client_message_id);
        });
        if (!started.began) {
            output.status = MessageApplicationStatus::kStorageError;
            output.message = "transactional private message begin failed: " + started.save.message;
            return output;
        }
        save = std::move(started.save);
        created = std::move(started.created);
    } else {
        if (!trace.Measure(Phase::Begin,[&]{return connection->BeginTransaction();})) {
            output.status = MessageApplicationStatus::kStorageError;
            output.message = "transactional private message begin failed: " +
                             connection->LastError();
            return output;
        }

        save = trace.Measure(Phase::Insert,[&]{return repository_->SavePrivateMessageOnConnection(
            connection.operator->(),
            from_user_id,
            to_user_id,
            content,
            tinyimx::DeliveryStatus::kPending,
            static_cast<tinyimx::PrivateMessageType>(message_type),
            client_message_id
        );});

    }

    if (!save.Succeeded()) {
        const std::string insert_error = save.message;
        if (connection->InTransaction()) {
            trace.Measure(Phase::Rollback,[&]{return connection->Rollback();});
        }
        // Release the transaction connection before the recovery read. This is
        // essential for pool_size=1 and avoids self-deadlock.
        connection.Reset();

        const auto recovered = trace.Measure(Phase::RecoveryRead,[&]{
            return repository_->FindPrivateMessageByClientMessageId(from_user_id,client_message_id);
        });

        if (!recovered.Succeeded()) {
            map_query_failure(recovered, "recovery read");
            if (!insert_error.empty()) {
                output.message += ", original_insert_error=" + insert_error;
            }
            return output;
        }

        if (recovered.Found()) {
            set_existing_result(recovered.record);
            return output;
        }

        output.status = MessageApplicationStatus::kStorageError;
        output.message = insert_error.empty()
            ? "transactional private message insert failed and no durable row was recovered"
            : insert_error;
        return output;
    }

    if (!batched) {
        created = trace.Measure(Phase::IdentityRead,[&]{return repository_->FindPrivateMessageByIdOnConnection(
            connection.operator->(),
            save.message_id
        );});

    }

    if (!created.Succeeded() || !created.Found() || !same_identity(created.record)) {
        if (connection->InTransaction()) {
            trace.Measure(Phase::Rollback,[&]{return connection->Rollback();});
        }
        if (!created.Succeeded()) {
            map_query_failure(created, "post-insert verification");
        } else {
            output.status = MessageApplicationStatus::kInvalidRecord;
            output.message =
                "transactional private message post-insert identity verification failed";
        }
        return output;
    }

    const MessageView created_view = ToView(created.record);
    const auto event_spec = MessageEventFactory::MessageCreated(created_view, message_events_topic_);
    bool committed_with_packet = false;
    // The legacy test seam is specifically *after* Outbox INSERT and before
    // COMMIT. Keep it on that original path; packet response-loss contracts use
    // a separate real-wire fixture rather than moving this seam silently.
    if (PrivateOutboxCommitPacketEnabled() && !private_persist_before_commit_for_test_) {
        const auto packet = trace.Measure(Phase::OutboxCommit,[&] {
            return outbox_->InsertAndCommitOnConnection(connection.operator->(),
                event_spec.event,event_spec.topic,event_spec.tag,event_spec.message_key);
        });
        if (!packet.use_separate_commands) {
            if (!packet.success) {
                if (connection->InTransaction())
                    trace.Measure(Phase::Rollback,[&]{return connection->Rollback();});
                // The combined command may already have committed even if its
                // first/second OK was lost. Release before a read-only lookup,
                // including pool_size=1; never replay the INSERT/COMMIT packet.
                connection.Reset();
                if (packet.command_attempted) {
                    const auto recovered = trace.Measure(Phase::RecoveryRead,[&] {
                        return repository_->FindPrivateMessageByClientMessageId(
                            from_user_id,client_message_id);
                    });
                    if (recovered.Succeeded() && recovered.Found()) {
                        set_existing_result(recovered.record);
                        output.message = same_identity(recovered.record)
                            ? "combined commit outcome recovered as existing private message"
                            : "combined commit outcome recovered as idempotency conflict";
                        return output;
                    }
                }
                output.status = MessageApplicationStatus::kStorageError;
                output.message = packet.command_attempted
                    ? "message outbox commit outcome unresolved: " + packet.message
                    : "message outbox insert failed: " + packet.message;
                return output;
            }
            committed_with_packet = true;
        }
    }
    if (!committed_with_packet) {
    const auto outbox_result = trace.Measure(Phase::OutboxInsert,[&]{return outbox_->InsertOnConnection(
        connection.operator->(),
        event_spec.event,
        event_spec.topic,
        event_spec.tag,
        event_spec.message_key
    );});

    if (!outbox_result.success) {
        if (connection->InTransaction()) {
            trace.Measure(Phase::Rollback,[&]{return connection->Rollback();});
        }
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message outbox insert failed: " + outbox_result.message;
        return output;
    }

    if (private_persist_before_commit_for_test_)
        private_persist_before_commit_for_test_(connection.operator->());
    if (!trace.Measure(Phase::Commit,[&]{return connection->Commit();})) {
        const std::string commit_error = connection->LastError();
        if (connection->InTransaction()) {
            trace.Measure(Phase::Rollback,[&]{return connection->Rollback();});
        }
        connection.Reset();

        // MySQL commit failures can be outcome-ambiguous. Recover by the stable
        // M12 idempotency key. If a matching durable row exists, the atomic
        // transaction also made its Outbox row durable.
        const auto recovered = trace.Measure(Phase::RecoveryRead,[&]{
            return repository_->FindPrivateMessageByClientMessageId(from_user_id,client_message_id);
        });
        if (recovered.Succeeded() && recovered.Found()) {
            set_existing_result(recovered.record);
            output.message = same_identity(recovered.record)
                ? "commit outcome recovered as existing private message"
                : "commit outcome recovered as idempotency conflict";
            return output;
        }

        output.status = MessageApplicationStatus::kStorageError;
        output.message = "transactional private message commit failed";
        if (!commit_error.empty()) {
            output.message += ": " + commit_error;
        }
        return output;
    }
    }

    output.status = MessageApplicationStatus::kSucceeded;
    output.outcome = PersistPrivateMessageOutcome::kCreated;
    output.message_id = created.record.message_id;
    output.record = created_view;
    output.message = "private message and outbox event committed";
    return output;
}

std::vector<MessageRepositoryPersistResult>
MessageRepositoryAdapter::RecoverPrivatePersistBatch(
    const std::vector<PrivatePersistInput>& inputs) {
    std::vector<MessageRepositoryPersistResult> results(inputs.size());
    // This is a read-only resolution of stable CID outcomes. Never replay a
    // possibly committed batch, nor insert while resolving uncertain COMMIT.
    for (std::size_t i=0;i<inputs.size();++i) {
        const auto& in=inputs[i];auto& result=results[i];
        const auto found=repository_->FindPrivateMessageByClientMessageId(
            in.from_user_id,in.client_message_id);
        result.status=found.Succeeded()?MessageApplicationStatus::kStorageError:MapStatus(found.status);
        result.message="private batch failed; no durable CID outcome found";
        if (!found.Succeeded()||!found.Found()) continue;
        result.status=MessageApplicationStatus::kSucceeded;
        result.message_id=found.record.message_id;result.record=ToView(found.record);
        const auto& r=found.record;
        const bool same=r.message_id&&r.from_user_id==in.from_user_id&&r.to_user_id==in.to_user_id&&
            r.client_message_id==in.client_message_id&&r.message_type==in.message_type&&r.content==in.content;
        result.outcome=same?PersistPrivateMessageOutcome::kReused:PersistPrivateMessageOutcome::kIdempotencyConflict;
        result.message=same?"private batch durable CID outcome recovered":"private batch CID conflict recovered";
    }
    return results;
}

std::vector<MessageRepositoryPersistResult>
MessageRepositoryAdapter::ExecutePrivatePersistBatch(
    const std::vector<PrivatePersistInput>& inputs) {
    return ExecutePrivatePersistBatchChosen(inputs,PrivatePersistSqlBatchEnabled());
}

std::vector<MessageRepositoryPersistResult>
MessageRepositoryAdapter::PersistPrivateMessages(const std::vector<PrivateMessagePersistInput>& inputs) {
    if(inputs.size()>64)return std::vector<MessageRepositoryPersistResult>(inputs.size());
    std::vector<PrivatePersistInput> batch;batch.reserve(inputs.size());
    for(const auto& in:inputs){
        if(!in.from_user_id||!in.to_user_id||in.from_user_id==in.to_user_id||
           in.client_message_id.empty()||in.client_message_id.size()>64||
           in.message_type<1||in.message_type>3||in.content.empty())
            return std::vector<MessageRepositoryPersistResult>(inputs.size());
        batch.push_back({in.from_user_id,in.to_user_id,in.client_message_id,in.message_type,in.content,0});
    }
    // Ready-queue admission and completion are outside the repository. Do not
    // enter the old caller-led coordinator or wait on another RPC thread.
    return ExecutePrivatePersistBatchChosen(batch,true);
}

std::vector<MessageRepositoryPersistResult>
MessageRepositoryAdapter::ExecutePrivatePersistBatchChosen(
    const std::vector<PrivatePersistInput>& inputs,bool grouped) {
    if(grouped&&inputs.size()>1) {
        std::set<std::pair<std::uint64_t,std::string>> unique;
        bool eligible=true;
        // The sequential path preserves same-batch duplicate/case/collation
        // decisions. Grouped writes require distinct canonical ASCII CIDs;
        // stored DB records still use original SQL collation and exact checks.
        for(const auto& in:inputs) {
            const bool canonical=std::all_of(in.client_message_id.begin(),in.client_message_id.end(),
                [](unsigned char c){return(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-';});
            eligible=eligible&&canonical&&unique.emplace(in.from_user_id,in.client_message_id).second;
        }
        if(eligible)return ExecutePrivatePersistBatchPackets(inputs);
    }
    return ExecutePrivatePersistBatchSequential(inputs);
}

std::vector<MessageRepositoryPersistResult>
MessageRepositoryAdapter::ExecutePrivatePersistBatchPackets(
    const std::vector<PrivatePersistInput>& inputs) {
    std::vector<MessageRepositoryPersistResult> results(inputs.size());
    PrivatePersistBatchTrace trace;
    std::size_t created_count=0;
    std::uint64_t grouped_commands=0;
    const auto finish=[&](){
        trace.SetGroupedQueryCommands(grouped_commands);
        trace.Emit(inputs,results,created_count);return std::move(results);
    };
    if(inputs.size()<2||inputs.size()>64||!repository_||!pool_||!outbox_)return finish();
    auto connection=trace.Measure(PrivatePersistBatchTrace::Acquire,[&]{return pool_->Acquire();});
    if(!connection||!trace.Measure(PrivatePersistBatchTrace::Begin,[&]{return connection->BeginTransaction();}))return finish();
    const auto grouped=[&](auto phase,auto&& operation){
        const auto before=connection->GroupedStatementCommandAttempts();
        auto output=trace.Measure(phase,std::forward<decltype(operation)>(operation));
        grouped_commands+=connection->GroupedStatementCommandAttempts()-before;
        return output;
    };
    const auto recover=[&](){
        if(connection->InTransaction()&&!trace.Measure(PrivatePersistBatchTrace::Rollback,[&]{return connection->Rollback();}))connection->Close();
        connection.Reset();
        results=trace.Measure(PrivatePersistBatchTrace::Recovery,[&]{return RecoverPrivatePersistBatch(inputs);});
        return finish();
    };
    const auto same=[](const auto& in,const auto& record){
        return record.message_id&&record.from_user_id==in.from_user_id&&record.to_user_id==in.to_user_id&&
            record.client_message_id==in.client_message_id&&record.message_type==in.message_type&&record.content==in.content;
    };
    std::vector<std::pair<std::uint64_t,std::string>> identities;
    identities.reserve(inputs.size());
    for(const auto& in:inputs)identities.emplace_back(in.from_user_id,in.client_message_id);
    auto found=grouped(PrivatePersistBatchTrace::Lookup,[&]{
        if (PrivateNativeSqlBatchEnabled())
            return repository_->FindPrivateMessageCidsBulkOnConnection(connection.operator->(),identities);
        return repository_->FindPrivateMessageCidsOnConnection(connection.operator->(),identities);
    });
    if(found.size()!=inputs.size())return recover();
    std::vector<tinyimx::PrivateMessageRecord> pending;
    std::vector<std::size_t> indexes;
    for(std::size_t i=0;i<inputs.size();++i){
        if(!found[i].Succeeded())return recover();
        const auto& in=inputs[i];auto& result=results[i];
        if(found[i].Found()){
            result.status=MessageApplicationStatus::kSucceeded;
            result.outcome=same(in,found[i].record)?PersistPrivateMessageOutcome::kReused:PersistPrivateMessageOutcome::kIdempotencyConflict;
            result.message_id=found[i].record.message_id;result.record=ToView(std::move(found[i].record));
            result.message="private grouped batch existing CID classified";
        }else{
            tinyimx::PrivateMessageRecord record;
            record.from_user_id=in.from_user_id;record.to_user_id=in.to_user_id;
            record.client_message_id=in.client_message_id;record.message_type=in.message_type;
            record.content=in.content;record.delivery_status=static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kPending);
            pending.push_back(std::move(record));indexes.push_back(i);
        }
    }
    if(pending.empty()){
        if(!trace.Measure(PrivatePersistBatchTrace::Rollback,[&]{return connection->Rollback();}))connection->Close();
        return finish();
    }
    auto verified=grouped(PrivatePersistBatchTrace::InsertVerify,[&]{
        if (PrivateNativeSqlBatchEnabled())
            return repository_->SaveAndFindPrivateMessagesBulkOnConnection(connection.operator->(),pending);
        return repository_->SaveAndFindPrivateMessagesOnConnection(connection.operator->(),pending);
    });
    if(!verified.Succeeded()||verified.records.size()!=pending.size())return recover();
    std::vector<tinyimx::outbox::OutboxInsertSpec> events;events.reserve(pending.size());
    for(std::size_t j=0;j<indexes.size();++j){
        const auto i=indexes[j];if(!same(inputs[i],verified.records[j]))return recover();
        results[i].record=ToView(verified.records[j]);
        auto event=MessageEventFactory::MessageCreated(results[i].record, message_events_topic_);
        events.push_back({std::move(event.event),std::move(event.topic),std::move(event.tag),std::move(event.message_key)});
    }
    const auto inserted=grouped(PrivatePersistBatchTrace::Outbox,[&]{
        if (PrivateNativeSqlBatchEnabled())
            return outbox_->InsertBulkOnConnection(connection.operator->(),events);
        return outbox_->InsertBatchOnConnection(connection.operator->(),events);
    });
    if(inserted.size()!=events.size()||!std::all_of(inserted.begin(),inserted.end(),[](const auto& item){return item.success;}))return recover();
    created_count=indexes.size();
    for(const auto i:indexes){
        auto& result=results[i];result.status=MessageApplicationStatus::kSucceeded;
        result.outcome=PersistPrivateMessageOutcome::kCreated;result.message_id=result.record.message_id;
        result.message="private message and outbox grouped batch committed";
    }
    if(private_persist_before_commit_for_test_)private_persist_before_commit_for_test_(connection.operator->());
    if(!trace.Measure(PrivatePersistBatchTrace::Commit,[&]{return connection->Commit();})){
        connection->Close();connection.Reset();
        results=trace.Measure(PrivatePersistBatchTrace::Recovery,[&]{return RecoverPrivatePersistBatch(inputs);});
    }
    // Full identity verification and every event insert precede COMMIT. Grouping
    // SQL commands does not weaken the durable ACK or replay failed writes.
    return finish();
}

std::vector<MessageRepositoryPersistResult>
MessageRepositoryAdapter::ExecutePrivatePersistBatchSequential(
    const std::vector<PrivatePersistInput>& inputs) {
    std::vector<MessageRepositoryPersistResult> results(inputs.size());
    PrivatePersistBatchTrace trace;
    std::size_t created_count=0;
    const auto finish=[&](){trace.Emit(inputs,results,created_count);return std::move(results);};
    if (inputs.empty()||inputs.size()>64||!repository_||!pool_||!outbox_)return finish();
    if (inputs.size()==1) {
        const auto& in=inputs.front();
        results.front()=trace.Measure(PrivatePersistBatchTrace::Direct,[&]{
            return PersistPrivateMessageDirect(in.from_user_id,in.to_user_id,in.client_message_id,in.message_type,in.content);
        });
        created_count=results.front().Completed()&&results.front().outcome==PersistPrivateMessageOutcome::kCreated?1:0;
        return finish();
    }
    auto connection=trace.Measure(PrivatePersistBatchTrace::Acquire,[&]{return pool_->Acquire();});
    if (!connection||!trace.Measure(PrivatePersistBatchTrace::Begin,[&]{return connection->BeginTransaction();}))return finish();
    const auto recover=[&](){
        if (connection->InTransaction()&&!trace.Measure(PrivatePersistBatchTrace::Rollback,[&]{return connection->Rollback();}))connection->Close();
        connection.Reset();
        results=trace.Measure(PrivatePersistBatchTrace::Recovery,[&]{return RecoverPrivatePersistBatch(inputs);});
        return finish();
    };
    for (std::size_t i=0;i<inputs.size();++i) {
        const auto& in=inputs[i];auto& result=results[i];
        const auto same=[&](const auto& r){return r.message_id&&r.from_user_id==in.from_user_id&&r.to_user_id==in.to_user_id&&
            r.client_message_id==in.client_message_id&&r.message_type==in.message_type&&r.content==in.content;};
        // Own writes remain visible inside RR, so same-batch duplicate CIDs
        // reuse the first row and never emit a second Outbox event.
        auto found=trace.Measure(PrivatePersistBatchTrace::Lookup,[&]{
            return repository_->FindPrivateMessageByClientMessageIdOnConnection(
                connection.operator->(),in.from_user_id,in.client_message_id);
        });
        if (!found.Succeeded())return recover();
        if (found.Found()) {
            result.status=MessageApplicationStatus::kSucceeded;
            result.outcome=same(found.record)?PersistPrivateMessageOutcome::kReused:PersistPrivateMessageOutcome::kIdempotencyConflict;
            result.message_id=found.record.message_id;result.record=ToView(std::move(found.record));
            result.message="private batch existing CID classified";
            continue;
        }
        const auto saved=trace.Measure(PrivatePersistBatchTrace::Insert,[&]{
            return repository_->SavePrivateMessageOnConnection(connection.operator->(),
                in.from_user_id,in.to_user_id,in.content,tinyimx::DeliveryStatus::kPending,
                static_cast<tinyimx::PrivateMessageType>(in.message_type),in.client_message_id);
        });
        if (!saved.Succeeded())return recover();
        auto verified=trace.Measure(PrivatePersistBatchTrace::Verify,[&]{
            return repository_->FindPrivateMessageByIdOnConnection(connection.operator->(),saved.message_id);
        });
        if (!verified.Succeeded()||!verified.Found()||!same(verified.record))return recover();
        result.record=ToView(verified.record);
        const auto event=MessageEventFactory::MessageCreated(result.record, message_events_topic_);
        const auto inserted=trace.Measure(PrivatePersistBatchTrace::Outbox,[&]{
            return outbox_->InsertOnConnection(connection.operator->(),event.event,event.topic,event.tag,event.message_key);
        });
        if (!inserted.success)return recover();
        result.status=MessageApplicationStatus::kSucceeded;result.outcome=PersistPrivateMessageOutcome::kCreated;
        result.message_id=verified.record.message_id;result.message="private message and outbox batch committed";
        ++created_count;
    }
    if (!created_count) {
        if (!trace.Measure(PrivatePersistBatchTrace::Rollback,[&]{return connection->Rollback();}))connection->Close();
        return finish();
    }
    if (private_persist_before_commit_for_test_)
        private_persist_before_commit_for_test_(connection.operator->());
    if (!trace.Measure(PrivatePersistBatchTrace::Commit,[&]{return connection->Commit();})) {
        // Closing releases any still-active transaction. Resolve only by reads.
        connection->Close();connection.Reset();
        results=trace.Measure(PrivatePersistBatchTrace::Recovery,[&]{return RecoverPrivatePersistBatch(inputs);});
    }
    // No caller receives these internally prepared Created results until the
    // one transaction has committed successfully, or durable CID was resolved.
    return finish();
}



MessageRepositoryGroupGetResult
MessageRepositoryAdapter::FindGroupMessageByClientMessageId(
    std::uint64_t from_user_id,
    const std::string& client_message_id
) {
    MessageRepositoryGroupGetResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }
    const auto result = repository_->FindGroupMessageByClientMessageId(
        from_user_id, client_message_id);
    output.status = MapStatus(result.status);
    output.found = result.found;
    if (result.found) output.record = ToView(result.record);
    output.message = result.message;
    return output;
}

MessageRepositoryGroupPersistResult
MessageRepositoryAdapter::PersistAuthorizedGroupMessage(
    std::uint64_t from_user_id,
    std::uint64_t group_id,
    const std::string& client_message_id,
    std::uint32_t message_type,
    const std::string& content,
    std::uint64_t membership_epoch,
    std::uint64_t member_version,
    std::uint32_t authorized_role
) {
    return PersistAuthorizedGroupMessage(
        from_user_id, group_id, client_message_id, message_type, content,
        membership_epoch, member_version, authorized_role, {});
}

MessageRepositoryGroupPersistResult
MessageRepositoryAdapter::PersistAuthorizedGroupMessage(
    std::uint64_t from_user_id,
    std::uint64_t group_id,
    const std::string& client_message_id,
    std::uint32_t message_type,
    const std::string& content,
    std::uint64_t membership_epoch,
    std::uint64_t member_version,
    std::uint32_t authorized_role,
    const std::vector<std::uint64_t>& recipient_user_ids
) {
    MessageRepositoryGroupPersistResult output;
    if (repository_ == nullptr || pool_ == nullptr || outbox_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "group message transactional persistence is unavailable";
        return output;
    }
    if (from_user_id == 0 || group_id == 0 || client_message_id.empty() ||
        client_message_id.size() > 64 || message_type < 1 || message_type > 3 ||
        content.empty() || membership_epoch == 0 || member_version == 0 ||
        authorized_role < 1 || authorized_role > 3) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid authorized group message";
        return output;
    }
    if (recipient_user_ids.size() > 5000) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "group recipient snapshot exceeds supported size";
        return output;
    }
    std::uint64_t previous_recipient = 0;
    for (const auto recipient : recipient_user_ids) {
        if (recipient == 0 || recipient == from_user_id || recipient <= previous_recipient) {
            output.status = MessageApplicationStatus::kInvalidArgument;
            output.message = "group recipient snapshot must be sorted unique and exclude sender";
            return output;
        }
        previous_recipient = recipient;
    }

    const auto same_identity = [&](const tinyimx::GroupMessageRecord& record) {
        return record.message_id != 0 &&
               record.client_message_id == client_message_id &&
               record.from_user_id == from_user_id &&
               record.group_id == group_id &&
               record.message_type == message_type &&
               record.content == content;
    };

    const auto set_existing = [&](const tinyimx::GroupMessageRecord& record) {
        output.status = MessageApplicationStatus::kSucceeded;
        output.message_id = record.message_id;
        output.record = ToView(record);
        if (same_identity(record)) {
            output.outcome = PersistGroupMessageOutcome::kReused;
            output.message = "existing group message reused";
        } else {
            output.outcome = PersistGroupMessageOutcome::kIdempotencyConflict;
            output.message = "client_message_id already belongs to a different group message";
        }
    };

    const auto precheck = repository_->FindGroupMessageByClientMessageId(
        from_user_id, client_message_id);
    if (!precheck.Succeeded()) {
        output.status = MapStatus(precheck.status);
        output.message = "group message precheck failed: " + precheck.message;
        return output;
    }
    if (precheck.Found()) {
        set_existing(precheck.record);
        return output;
    }

    if (pre_insert_hook_for_test_) pre_insert_hook_for_test_();

    auto connection = pool_->Acquire();
    if (!connection) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "group message acquire failed";
        return output;
    }
    if (!connection->BeginTransaction()) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "group message begin failed: " + connection->LastError();
        return output;
    }

    const auto saved = repository_->SaveGroupMessageOnConnection(
        connection.operator->(), group_id, from_user_id, client_message_id,
        message_type, content, membership_epoch, member_version, authorized_role);
    if (!saved.Succeeded()) {
        const std::string insert_error = saved.message;
        if (connection->InTransaction()) connection->Rollback();
        connection.Reset();
        const auto recovered = repository_->FindGroupMessageByClientMessageId(
            from_user_id, client_message_id);
        if (recovered.Succeeded() && recovered.Found()) {
            set_existing(recovered.record);
            return output;
        }
        output.status = MessageApplicationStatus::kStorageError;
        output.message = insert_error.empty() ? "group message insert failed" : insert_error;
        return output;
    }

    const auto created = repository_->FindGroupMessageByIdOnConnection(
        connection.operator->(), saved.message_id);
    if (!created.Succeeded() || !created.Found() || !same_identity(created.record) ||
        created.record.membership_epoch != membership_epoch ||
        created.record.member_version != member_version ||
        created.record.authorized_role != authorized_role) {
        if (connection->InTransaction()) connection->Rollback();
        output.status = created.Succeeded()
            ? MessageApplicationStatus::kInvalidRecord
            : MapStatus(created.status);
        output.message = "group message post-insert verification failed";
        return output;
    }

    const GroupMessageView created_view = ToView(created.record);
    const auto delivery_insert = repository_->InsertGroupMessageDeliveriesOnConnection(
        connection.operator->(), created.record.message_id, group_id, recipient_user_ids);
    if (!delivery_insert.Succeeded() ||
        delivery_insert.affected_rows != recipient_user_ids.size()) {
        if (connection->InTransaction()) connection->Rollback();
        output.status = delivery_insert.Succeeded()
            ? MessageApplicationStatus::kInvalidRecord
            : MapStatus(delivery_insert.status);
        output.message = delivery_insert.Succeeded()
            ? "group delivery recipient count mismatch"
            : "group delivery insert failed: " + delivery_insert.message;
        return output;
    }

    const auto event_spec = MessageEventFactory::GroupMessageCreated(created_view, message_events_topic_);
    const auto outbox_result = outbox_->InsertOnConnection(
        connection.operator->(), event_spec.event, event_spec.topic,
        event_spec.tag, event_spec.message_key);
    if (!outbox_result.success) {
        if (connection->InTransaction()) connection->Rollback();
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "group message outbox insert failed: " + outbox_result.message;
        return output;
    }

    if (!connection->Commit()) {
        const std::string commit_error = connection->LastError();
        if (connection->InTransaction()) connection->Rollback();
        connection.Reset();
        const auto recovered = repository_->FindGroupMessageByClientMessageId(
            from_user_id, client_message_id);
        if (recovered.Succeeded() && recovered.Found()) {
            set_existing(recovered.record);
            output.message = same_identity(recovered.record)
                ? "commit outcome recovered as existing group message"
                : "commit outcome recovered as group-message idempotency conflict";
            return output;
        }
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "group message commit failed";
        if (!commit_error.empty()) output.message += ": " + commit_error;
        return output;
    }

    output.status = MessageApplicationStatus::kSucceeded;
    output.outcome = PersistGroupMessageOutcome::kCreated;
    output.message_id = created.record.message_id;
    output.record = created_view;
    output.message = "group message, recipient snapshot, and outbox event committed";
    return output;
}

MessageRepositoryGroupDeliveryGetResult
MessageRepositoryAdapter::GetGroupMessageDelivery(
    std::uint64_t message_id,
    std::uint64_t recipient_user_id
) {
    MessageRepositoryGroupDeliveryGetResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }
    auto result = repository_->FindGroupMessageDelivery(message_id, recipient_user_id);
    output.status = MapStatus(result.status);
    output.found = result.found;
    output.message = std::move(result.message);
    if (result.found) output.record = ToView(std::move(result.record));
    return output;
}

MessageRepositoryGroupDeliveryListResult
MessageRepositoryAdapter::ClaimGroupMessageDeliveries(
    const std::string& lease_owner,
    const std::string& lease_token,
    std::size_t limit,
    std::uint32_t lease_ms,
    std::uint64_t message_id
) {
    MessageRepositoryGroupDeliveryListResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }
    auto result = repository_->ClaimGroupMessageDeliveries(
        lease_owner, lease_token, limit, lease_ms, message_id);
    output.status = MapStatus(result.status);
    output.message = std::move(result.message);
    if (!result.Succeeded()) return output;
    output.records.reserve(result.records.size());
    for (auto& item : result.records) output.records.push_back(ToView(std::move(item)));
    return output;
}

MessageRepositoryGroupDeliveryListResult
MessageRepositoryAdapter::ClaimGroupMessageDeliveriesForRecipient(
    std::uint64_t recipient_user_id,
    const std::string& lease_owner,
    const std::string& lease_token,
    std::size_t limit,
    std::uint32_t lease_ms
) {
    MessageRepositoryGroupDeliveryListResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }
    auto result = repository_->ClaimGroupMessageDeliveriesForRecipient(
        recipient_user_id, lease_owner, lease_token, limit, lease_ms);
    output.status = MapStatus(result.status);
    output.message = std::move(result.message);
    if (!result.Succeeded()) return output;
    output.records.reserve(result.records.size());
    for (auto& item : result.records) {
        output.records.push_back(ToView(std::move(item)));
    }
    return output;
}

MessageRepositoryMutationResult
MessageRepositoryAdapter::CompleteGroupMessageDeliveryAttempt(
    std::uint64_t message_id,
    std::uint64_t recipient_user_id,
    const std::string& lease_token,
    GroupDeliveryAttemptOutcome outcome,
    const std::string& gateway_id,
    std::uint32_t retry_after_ms,
    const std::string& error_code
) {
    tinyimx::GroupDeliveryStatus next = tinyimx::GroupDeliveryStatus::kPending;
    switch (outcome) {
        case GroupDeliveryAttemptOutcome::kSubmitted:
        case GroupDeliveryAttemptOutcome::kRetryableFailure:
            next = tinyimx::GroupDeliveryStatus::kPending;
            break;
        case GroupDeliveryAttemptOutcome::kOffline:
            next = tinyimx::GroupDeliveryStatus::kDeferredOffline;
            break;
    }
    const auto result = repository_->CompleteGroupMessageDeliveryAttempt(
        message_id, recipient_user_id, lease_token, next, gateway_id, retry_after_ms, error_code);
    MessageRepositoryMutationResult output;
    output.status = MapStatus(result.status);
    output.affected_rows = result.affected_rows;
    output.message = result.message;
    return output;
}

MessageRepositoryMutationResult
MessageRepositoryAdapter::CompleteGroupMessageDeliveryAttempts(
    const std::vector<GroupDeliveryAttemptCompletion>& attempts
) {
    MessageRepositoryMutationResult output;
    if (!ValidGroupDeliveryAttemptCompletions(attempts)) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid group delivery batch completion";
        return output;
    }
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }
    std::vector<tinyimx::GroupDeliveryAttemptCompletion> completions;
    completions.reserve(attempts.size());
    for (const auto& item : attempts) {
        tinyimx::GroupDeliveryAttemptCompletion completion;
        completion.message_id = item.message_id;
        completion.recipient_user_id = item.recipient_user_id;
        completion.lease_token = item.lease_token;
        completion.next_status = item.outcome == GroupDeliveryAttemptOutcome::kOffline
            ? tinyimx::GroupDeliveryStatus::kDeferredOffline
            : tinyimx::GroupDeliveryStatus::kPending;
        completion.gateway_id = item.gateway_id;
        completion.retry_after_ms = item.retry_after_ms;
        completion.error_code = item.error_code;
        completions.push_back(std::move(completion));
    }
    auto result = repository_->CompleteGroupMessageDeliveryAttempts(completions);
    output.status = MapStatus(result.status);
    output.affected_rows = result.affected_rows;
    output.message = std::move(result.message);
    return output;
}

MessageRepositoryMutationResult
MessageRepositoryAdapter::ConfirmGroupMessageDelivery(
    std::uint64_t message_id,
    std::uint64_t recipient_user_id
) {
    const auto result = repository_->ConfirmGroupMessageDelivery(message_id, recipient_user_id);
    MessageRepositoryMutationResult output;
    output.status = MapStatus(result.status);
    output.affected_rows = result.affected_rows;
    output.message = result.message;
    return output;
}

MessageRepositoryGetResult
MessageRepositoryAdapter::FindPrivateMessageByClientMessageId(
    std::uint64_t from_user_id,
    const std::string& client_message_id
) {
    MessageRepositoryGetResult output;
    if (repository_ == nullptr) {
        output.message = "message repository is unavailable";
        return output;
    }
    auto result = repository_->FindPrivateMessageByClientMessageId(
        from_user_id, client_message_id);
    output.status = MapStatus(result.status);
    output.found = result.found;
    output.message = std::move(result.message);
    if (!result.Succeeded() || !result.found) return output;
    if (!ValidDeliveryStatus(result.record.delivery_status)) {
        output.status = MessageApplicationStatus::kInvalidRecord;
        output.found = false;
        output.message = "message repository returned invalid delivery status";
        return output;
    }
    output.record = ToView(std::move(result.record));
    return output;
}

MessageRepositoryGetResult
MessageRepositoryAdapter::GetPrivateMessage(
    std::uint64_t message_id
) {
    MessageRepositoryGetResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }

    auto result = repository_->FindPrivateMessageById(message_id);
    output.status = MapStatus(result.status);
    output.found = result.found;
    output.message = std::move(result.message);
    if (!result.Succeeded() || !result.found) {
        return output;
    }
    if (!ValidDeliveryStatus(result.record.delivery_status)) {
        output.status = MessageApplicationStatus::kInvalidRecord;
        output.found = false;
        output.message = "message repository returned invalid delivery status";
        return output;
    }
    output.record = ToView(std::move(result.record));
    return output;
}

MessageRepositoryHistoryResult
MessageRepositoryAdapter::ListHistory(
    std::uint64_t actor_user_id,
    std::uint64_t peer_user_id,
    std::uint64_t before_message_id,
    std::size_t limit
) {
    MessageRepositoryHistoryResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }

    auto result = repository_->ListDialogMessages(
        actor_user_id,
        peer_user_id,
        before_message_id,
        limit
    );
    output.status = MapStatus(result.status);
    output.message = std::move(result.message);
    if (!result.Succeeded()) {
        return output;
    }

    output.records.reserve(result.records.size());
    for (auto& record : result.records) {
        if (!ValidDeliveryStatus(record.delivery_status)) {
            output.status = MessageApplicationStatus::kInvalidRecord;
            output.message = "message repository returned invalid delivery status";
            output.records.clear();
            return output;
        }
        output.records.push_back(ToView(std::move(record)));
    }
    return output;
}

MessageRepositoryConversationResult
MessageRepositoryAdapter::ListConversations(
    std::uint64_t actor_user_id,
    std::size_t limit
) {
    MessageRepositoryConversationResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }

    auto result = repository_->ListConversations(actor_user_id, limit);
    output.status = MapStatus(result.status);
    output.message = std::move(result.message);
    if (!result.Succeeded()) {
        return output;
    }

    output.records.reserve(result.records.size());
    for (auto& record : result.records) {
        if (!ValidDeliveryStatus(record.last_delivery_status)) {
            output.status = MessageApplicationStatus::kInvalidRecord;
            output.message = "message repository returned invalid conversation state";
            output.records.clear();
            return output;
        }
        output.records.push_back(ToView(std::move(record)));
    }
    return output;
}

MessageRepositoryCountResult
MessageRepositoryAdapter::CountPending(
    std::uint64_t to_user_id
) {
    MessageRepositoryCountResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }

    auto result = repository_->CountPendingMessages(to_user_id);
    output.status = MapStatus(result.status);
    output.count = result.count;
    output.message = std::move(result.message);
    return output;
}

MessageRepositoryPendingResult
MessageRepositoryAdapter::ListPendingAfter(
    std::uint64_t to_user_id,
    std::uint64_t after_message_id,
    std::size_t limit
) {
    MessageRepositoryPendingResult output;
    if (repository_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }

    auto result = repository_->ListPendingMessagesAfter(
        to_user_id,
        after_message_id,
        limit
    );
    output.status = MapStatus(result.status);
    output.message = std::move(result.message);
    if (!result.Succeeded()) {
        return output;
    }

    output.records.reserve(result.records.size());
    for (auto& record : result.records) {
        if (!ValidDeliveryStatus(record.delivery_status) ||
            record.delivery_status != static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kPending)) {
            output.status = MessageApplicationStatus::kInvalidRecord;
            output.message = "message repository returned non-pending record in pending page";
            output.records.clear();
            return output;
        }
        output.records.push_back(ToView(std::move(record)));
    }
    return output;
}

PendingRecipientsResult MessageRepositoryAdapter::ListPendingRecipientsAfter(
    std::uint64_t after_user_id, std::size_t limit
) {
    PendingRecipientsResult output;
    if (!repository_) {
        output.message = "message repository is unavailable";
        return output;
    }
    auto result = repository_->ListPendingRecipientsAfter(after_user_id, limit);
    output.status = MapStatus(result.status);
    output.message = std::move(result.message);
    if (!result.Succeeded()) return output;
    output.recipient_user_ids = std::move(result.recipient_user_ids);
    output.has_more = result.has_more;
    return output;
}

MessageRepositoryMutationResult
MessageRepositoryAdapter::ConfirmReceiver(
    std::uint64_t message_id
) {
    if (repository_ == nullptr) {
        MessageRepositoryMutationResult output;
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }
    return ToMutation(repository_->MarkReceiverConfirmed(message_id));
}

std::optional<MessageRepositoryMutationResult>
MessageRepositoryAdapter::TryConfirmReceiverOwnedIfPending(
    std::uint64_t message_id, std::uint64_t receiver_user_id
) {
    if (PrivateReceiverConfirmCoalesceEnabled()) {
        MessageRepositoryMutationResult failure;
        failure.status = MessageApplicationStatus::kStorageError;
        failure.message = "receiver confirmation batch unavailable or overloaded";
        if (!message_id || !receiver_user_id) {
            failure.status = MessageApplicationStatus::kInvalidArgument;
            return failure;
        }
        return receiver_confirmation_batch_.Run(
            ReceiverConfirmationInput{message_id, receiver_user_id},
            [this](const auto& inputs) { return ExecuteReceiverConfirmationBatch(inputs); },
            failure);
    }
    if (!PrivateReceiverOwnedUpdateEnabled()) return std::nullopt;
    return TryConfirmReceiverOwnedPendingDirect(message_id, receiver_user_id);
}

std::optional<MessageRepositoryMutationResult>
MessageRepositoryAdapter::TryConfirmReceiverOwnedPendingDirect(
    std::uint64_t message_id, std::uint64_t receiver_user_id
) {
    MessageRepositoryMutationResult output;
    diagnostics::StorageCallTrace storage_trace(2,message_id);
    if (message_id == 0 || receiver_user_id == 0) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid owned receiver confirmation identity";
        return output;
    }
    if (!repository_ || !pool_) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "owned receiver confirmation storage unavailable";
        return output;
    }
    auto connection = storage_trace.Measure(diagnostics::StorageCallTrace::Acquire,[&]{return pool_->Acquire();});
    if (!connection) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "owned receiver confirmation acquire failed";
        return output;
    }
    // Same validity predicates as BuildMessagesFromResult for the pending row.
    // The PK, owner and status predicates are checked by the same durable UPDATE;
    // read/failed/foreign/invalid/missing rows fall back to the original classifier.
    const std::string sql =
        "UPDATE im_private_messages SET delivery_status=1, delivered_at=NOW() "
        "WHERE message_id=" + std::to_string(message_id) +
        " AND to_user_id=" + std::to_string(receiver_user_id) +
        " AND delivery_status=0 AND from_user_id>0 "
        "AND message_type BETWEEN 1 AND 3 AND created_at IS NOT NULL";
    if (!storage_trace.Measure(diagnostics::StorageCallTrace::Sql,[&]{return connection->Execute(sql);})) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "owned receiver confirmation failed: " + connection->LastError();
        return output;
    }
    const auto affected = connection->AffectedRows();
    if (affected == 0) return std::nullopt;
    if (affected != 1) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "owned receiver confirmation unexpected affected rows";
        return output;
    }
    output.status = MessageApplicationStatus::kSucceeded;
    output.affected_rows = 1;
    output.message = "private message receiver confirmation durable";
    return output;
}

std::vector<MessageRepositoryMutationResult>
MessageRepositoryAdapter::ExecuteReceiverConfirmationBatch(
    const std::vector<ReceiverConfirmationInput>& inputs) {
    MessageRepositoryMutationResult failure;
    failure.status = MessageApplicationStatus::kStorageError;
    failure.message = "receiver confirmation batch transaction failed";
    std::vector<MessageRepositoryMutationResult> results(inputs.size(), failure);
    if (inputs.empty() || inputs.size() > 64 || !repository_ || !pool_) return results;
    // Keep the low-contention single-call path one atomic autocommit UPDATE.
    // Tests requesting a commit hook deliberately take the explicit transaction.
    if (inputs.size() == 1 && !receiver_confirmation_before_commit_for_test_) {
        const auto& in = inputs.front();
        if (auto direct = TryConfirmReceiverOwnedPendingDirect(in.message_id, in.receiver_user_id)) {
            results.front() = std::move(*direct);
        } else {
            auto lookup = GetPrivateMessage(in.message_id);
            auto decision = ReceiverConfirmationDecision(lookup, in.receiver_user_id);
            results.front() = decision ? std::move(*decision) : ConfirmReceiver(in.message_id);
        }
        return results;
    }
    std::set<std::uint64_t> ids;
    for (const auto& in : inputs) {
        if (!in.message_id || !in.receiver_user_id) return results;
        ids.insert(in.message_id);
    }
    std::string id_list;
    for (auto id : ids) {
        if (!id_list.empty()) id_list += ',';
        id_list += std::to_string(id);
    }
    auto connection = pool_->Acquire();
    if (!connection || !connection->BeginTransaction()) return results;
    tinyimx::MySqlQueryResult rows;
    // Numeric columns are schema-typed. The validity expression is exactly the
    // original parser's predicates; invalid records fail before owner checking.
    // Lock PKs in ascending order, including owner/state until durable commit.
    if (!connection->Query(
            "SELECT message_id,to_user_id,delivery_status,"
            "(message_id>0 AND from_user_id>0 AND to_user_id>0 "
            "AND message_type BETWEEN 1 AND 3 AND delivery_status BETWEEN 0 AND 3 "
            "AND created_at IS NOT NULL) FROM im_private_messages WHERE message_id IN (" +
            id_list + ") ORDER BY message_id FOR UPDATE", &rows)) return results;
    std::map<std::uint64_t, MessageRepositoryGetResult> lookups;
    for (const auto& row : rows.rows) {
        if (row.size() != 4) return results;
        const auto id = std::stoull(row[0]);
        if (!ids.contains(id) || lookups.contains(id)) return results;
        MessageRepositoryGetResult lookup;
        lookup.status = row[3] == "1" ? MessageApplicationStatus::kSucceeded
                                       : MessageApplicationStatus::kInvalidRecord;
        lookup.found = true;
        lookup.record.message_id = id;
        lookup.record.to_user_id = std::stoull(row[1]);
        lookup.record.delivery_state = static_cast<MessageDeliveryState>(std::stoul(row[2]));
        lookup.message = row[3] == "1" ? "private message found" : "invalid private message record";
        lookups.emplace(id, std::move(lookup));
    }
    std::set<std::uint64_t> pending;
    std::vector<std::size_t> mutations;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const auto& in = inputs[i];
        auto found = lookups.find(in.message_id);
        MessageRepositoryGetResult missing;
        missing.status = MessageApplicationStatus::kSucceeded;
        const auto& lookup = found == lookups.end() ? missing : found->second;
        if (auto decision = ReceiverConfirmationDecision(lookup, in.receiver_user_id)) {
            results[i] = std::move(*decision);
        } else {
            mutations.push_back(i);
            // Several callers for the same MID observe one state transition.
            results[i].affected_rows = pending.insert(in.message_id).second ? 1 : 0;
        }
    }
    if (pending.empty()) {
        if (!connection->Rollback()) connection->Close();
        return results;
    }
    std::string pending_list;
    for (auto id : pending) {
        if (!pending_list.empty()) pending_list += ',';
        pending_list += std::to_string(id);
    }
    // Every included owner was checked under FOR UPDATE in this transaction.
    // Read/failed/foreign/malformed rows are absent from this update set.
    if (!connection->Execute(
            "UPDATE im_private_messages SET delivery_status=1,delivered_at=NOW() "
            "WHERE delivery_status=0 AND message_id IN (" + pending_list + ")") ||
        connection->AffectedRows() != pending.size()) {
        for (auto i : mutations) results[i] = failure;
        return results;
    }
    if (receiver_confirmation_before_commit_for_test_)
        receiver_confirmation_before_commit_for_test_(connection.operator->());
    if (!connection->Commit()) {
        // A lost COMMIT response is uncertain. Fail closed without replay and
        // close the connection so transaction state cannot escape the lease.
        connection->Close();
        for (auto i : mutations) results[i] = failure;
        return results;
    }
    for (auto i : mutations) {
        results[i].status = MessageApplicationStatus::kSucceeded;
        results[i].message = "private message receiver confirmation durable";
    }
    return results;
}

MessageRepositoryMutationResult
MessageRepositoryAdapter::ConfirmReceiverBatch(
    const std::vector<std::uint64_t>& message_ids
) {
    if (repository_ == nullptr) {
        MessageRepositoryMutationResult output;
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message repository is unavailable";
        return output;
    }
    return ToMutation(repository_->MarkReceiverConfirmedBatch(message_ids));
}

MessageRepositoryMutationResult
MessageRepositoryAdapter::MarkDialogRead(
    std::uint64_t reader_user_id,
    std::uint64_t peer_user_id
) {
    MessageRepositoryMutationResult output;

    if (repository_ == nullptr || pool_ == nullptr || outbox_ == nullptr) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "message transactional read mutation is unavailable";
        return output;
    }

    if (reader_user_id == 0 || peer_user_id == 0 ||
        reader_user_id == peer_user_id) {
        output.status = MessageApplicationStatus::kInvalidArgument;
        output.message = "invalid dialog read identity";
        return output;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "transactional mark read acquire failed";
        return output;
    }

    if (!connection->BeginTransaction()) {
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "transactional mark read begin failed: " +
                         connection->LastError();
        return output;
    }

    const auto mutation = repository_->MarkReadByDialogOnConnection(
        connection.operator->(),
        reader_user_id,
        peer_user_id
    );

    if (!mutation.Succeeded()) {
        if (connection->InTransaction()) {
            connection->Rollback();
        }
        output.status = MapStatus(mutation.status);
        output.affected_rows = mutation.affected_rows;
        output.message = std::move(mutation.message);
        return output;
    }

    if (mutation.affected_rows > 0) {
        const auto event_spec = MessageEventFactory::DialogReadAdvanced(
            reader_user_id,
            peer_user_id,
            mutation.affected_rows,
            message_events_topic_
        );
        const auto outbox_result = outbox_->InsertOnConnection(
            connection.operator->(),
            event_spec.event,
            event_spec.topic,
            event_spec.tag,
            event_spec.message_key
        );

        if (!outbox_result.success) {
            if (connection->InTransaction()) {
                connection->Rollback();
            }
            output.status = MessageApplicationStatus::kStorageError;
            output.message = "dialog read outbox insert failed: " +
                             outbox_result.message;
            return output;
        }
    }

    if (!connection->Commit()) {
        const std::string commit_error = connection->LastError();
        if (connection->InTransaction()) {
            connection->Rollback();
        }
        output.status = MessageApplicationStatus::kStorageError;
        output.message = "transactional mark read commit failed";
        if (!commit_error.empty()) {
            output.message += ": " + commit_error;
        }
        return output;
    }

    output.status = MessageApplicationStatus::kSucceeded;
    output.affected_rows = mutation.affected_rows;
    output.message = mutation.affected_rows == 0
        ? "dialog already read; no outbox event created"
        : "dialog read state and outbox event committed";
    return output;
}

GroupHistoryApplicationResult MessageRepositoryAdapter::ListGroupHistory(std::uint64_t actor, std::uint64_t group, std::uint64_t before, std::size_t limit) {
    GroupHistoryApplicationResult out;if(!repository_){out.message="group history repository unavailable";return out;}
    auto result=repository_->ListGroupHistory(actor,group,before,limit);out.status=MapStatus(result.status);out.message=std::move(result.message);
    if(result.Succeeded())for(auto &record:result.records)out.messages.push_back(ToView(std::move(record)));
    return out;
}
}  // namespace tinyimx::message
