#include "services/outbox/OutboxRepository.h"
#include <unordered_set>

#include "common/db/MySqlConnection.h"
#include "common/db/NativeSqlBatchPolicy.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/eventing/EventCodec.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace tinyimx::outbox {
namespace {

constexpr std::size_t kMaxEventIdLength = 191;
constexpr std::size_t kMaxEventTypeLength = 96;
constexpr std::size_t kMaxAggregateTypeLength = 64;
constexpr std::size_t kMaxAggregateIdLength = 128;
constexpr std::size_t kMaxProducerServiceLength = 64;
constexpr std::size_t kMaxTopicLength = 128;
constexpr std::size_t kMaxTagLength = 64;
constexpr std::size_t kMaxMessageKeyLength = 191;
constexpr std::size_t kMaxClaimTokenLength = 128;
constexpr std::size_t kMaxLastErrorLength = 1024;
constexpr std::size_t kMaxClaimBatch = 1024;
constexpr std::size_t kMaxCleanupBatch = 10000;
constexpr char kOutboxInsertPrefix[] =
    "INSERT INTO im_event_outbox (event_id, event_type, schema_version, "
    "aggregate_type, aggregate_id, producer_service, topic, tag, message_key, "
    "payload, status, attempt_count, next_attempt_at) VALUES ";

bool ClaimReadCommittedEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_OUTBOX_CLAIM_READ_COMMITTED_ENABLE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

bool ValidRoutingMetadata(
    const eventing::DomainEvent& event,
    const std::string& topic,
    const std::string& tag,
    const std::string& message_key
) {
    return event.event_id.size() <= kMaxEventIdLength &&
           event.event_type.size() <= kMaxEventTypeLength &&
           event.aggregate_type.size() <= kMaxAggregateTypeLength &&
           event.aggregate_id.size() <= kMaxAggregateIdLength &&
           event.producer_service.size() <= kMaxProducerServiceLength &&
           !topic.empty() && topic.size() <= kMaxTopicLength &&
           tag.size() <= kMaxTagLength &&
           !message_key.empty() &&
           message_key.size() <= kMaxMessageKeyLength;
}

std::string ClampError(const std::string& value) {
    if (value.size() <= kMaxLastErrorLength) {
        return value;
    }
    return value.substr(0, kMaxLastErrorLength);
}

bool ParseUnsigned64(const std::string& text, std::uint64_t* value) {
    if (value == nullptr || text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(text, &consumed, 10);
        if (consumed != text.size()) {
            return false;
        }
        *value = static_cast<std::uint64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseUnsigned32(const std::string& text, std::uint32_t* value) {
    std::uint64_t parsed = 0;
    if (!ParseUnsigned64(text, &parsed) ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    *value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool ParseRecord(
    const std::vector<std::string>& row,
    OutboxRecord* record,
    std::string* error_message
) {
    constexpr std::size_t kExpectedFields = 14;
    if (record == nullptr || row.size() != kExpectedFields) {
        if (error_message != nullptr) {
            *error_message = "outbox claim failed: unexpected result shape";
        }
        return false;
    }

    std::uint64_t outbox_id = 0;
    std::uint32_t schema_version = 0;
    std::uint32_t status = 0;
    std::uint32_t attempt_count = 0;

    if (!ParseUnsigned64(row[0], &outbox_id) || outbox_id == 0 ||
        !ParseUnsigned32(row[3], &schema_version) || schema_version == 0 ||
        !ParseUnsigned32(row[11], &status) || status > 3 ||
        !ParseUnsigned32(row[12], &attempt_count)) {
        if (error_message != nullptr) {
            *error_message = "outbox claim failed: invalid numeric result";
        }
        return false;
    }

    if (status != static_cast<std::uint32_t>(OutboxStatus::kPending) &&
        status != static_cast<std::uint32_t>(OutboxStatus::kRetry)) {
        if (error_message != nullptr) {
            *error_message = "outbox claim failed: selected non-routable status";
        }
        return false;
    }

    record->outbox_id = outbox_id;
    record->event_id = row[1];
    record->event_type = row[2];
    record->schema_version = schema_version;
    record->aggregate_type = row[4];
    record->aggregate_id = row[5];
    record->producer_service = row[6];
    record->topic = row[7];
    record->tag = row[8];
    record->message_key = row[9];
    record->payload = row[10];
    record->status = static_cast<OutboxStatus>(status);
    record->attempt_count = attempt_count;
    record->locked_by = row[13];
    return true;
}

std::string BuildIdList(const std::vector<OutboxRecord>& records) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (i != 0) {
            oss << ',';
        }
        oss << records[i].outbox_id;
    }
    return oss.str();
}

}  // namespace

const char* OutboxStatusToString(OutboxStatus status) noexcept {
    switch (status) {
        case OutboxStatus::kPending:
            return "pending";
        case OutboxStatus::kPublished:
            return "published";
        case OutboxStatus::kRetry:
            return "retry";
        case OutboxStatus::kQuarantined:
            return "quarantined";
    }
    return "unknown";
}

OutboxRepository::OutboxRepository(
    tinyimx::MySqlConnectionPool* pool
)
    : pool_(pool) {
}

void OutboxRepository::SetForceInsertFailureForTest(
    bool enabled
) noexcept {
    force_insert_failure_for_test_.store(enabled, std::memory_order_release);
}

bool OutboxRepository::PrepareInsertStatement(
    tinyimx::MySqlConnection* connection,const tinyimx::eventing::DomainEvent& event,
    const std::string& topic,const std::string& tag,const std::string& message_key,
    std::string* sql,std::string* error) const {
    if(!sql||!error)return false;
    if (pool_ == nullptr) {
        *error = "outbox insert failed: repository pool unavailable";
        return false;
    }

    if (connection == nullptr || !connection->IsConnected()) {
        *error = "outbox insert failed: connection unavailable";
        return false;
    }

    if (!connection->InTransaction()) {
        *error =
            "outbox insert failed: active MySQL transaction required";
        return false;
    }

    if (!event.Valid() ||
        !ValidRoutingMetadata(event, topic, tag, message_key)) {
        *error = "outbox insert failed: invalid event metadata";
        return false;
    }

    if (force_insert_failure_for_test_.load(std::memory_order_acquire)) {
        *error = "outbox insert failed: injected test failure";
        return false;
    }

    const auto encoded = eventing::EventCodec::Encode(event);
    if (!encoded.success) {
        *error = encoded.message;
        return false;
    }

    *sql = std::string(kOutboxInsertPrefix) + "('" +
        connection->EscapeString(event.event_id) + "', '" +
        connection->EscapeString(event.event_type) + "', " +
        std::to_string(event.schema_version) + ", '" +
        connection->EscapeString(event.aggregate_type) + "', '" +
        connection->EscapeString(event.aggregate_id) + "', '" +
        connection->EscapeString(event.producer_service) + "', '" +
        connection->EscapeString(topic) + "', '" +
        connection->EscapeString(tag) + "', '" +
        connection->EscapeString(message_key) + "', '" +
        connection->EscapeString(encoded.encoded) + "', 0, 0, NOW(3))";

    return true;
}

OutboxInsertResult OutboxRepository::InsertOnConnection(
    tinyimx::MySqlConnection* connection,
    const tinyimx::eventing::DomainEvent& event,
    const std::string& topic,
    const std::string& tag,
    const std::string& message_key
) {
    OutboxInsertResult result;
    std::string sql;
    if(!PrepareInsertStatement(connection,event,topic,tag,message_key,&sql,&result.message))return result;

    if (!connection->Execute(sql)) {
        result.message = connection->LastError();
        if (result.message.empty()) {
            result.message = "outbox insert failed: execute failed";
        }
        return result;
    }

    result.outbox_id = connection->LastInsertId();
    if (result.outbox_id == 0) {
        result.message = "outbox insert failed: invalid last insert id";
        return result;
    }

    result.success = true;
    result.message = "outbox event inserted";
    return result;
}

OutboxInsertCommitResult OutboxRepository::InsertAndCommitOnConnection(
    tinyimx::MySqlConnection* connection,
    const tinyimx::eventing::DomainEvent& event,
    const std::string& topic, const std::string& tag,
    const std::string& message_key) {
    OutboxInsertCommitResult result;
    std::string sql;
    if (!PrepareInsertStatement(connection,event,topic,tag,message_key,&sql,&result.message))
        return result;
    if (sql.size() > tinyimx::MySqlConnection::kInsertCommitPacketBytes - 7) {
        result.use_separate_commands = true;
        result.message = "event requires original separate INSERT and COMMIT";
        return result;
    }
    tinyimx::MySqlInsertCommitResult committed;
    const bool ok = connection->InsertAndCommit(sql,&committed);
    result.command_attempted = committed.command_attempted;
    if (!ok || !committed.commit_acknowledged || !committed.insert_acknowledged) {
        result.message = connection->LastError();
        if (result.message.empty()) result.message = "outbox INSERT/COMMIT not acknowledged";
        return result;
    }
    result.success = true;
    result.outbox_id = committed.insert_id;
    result.message = "message and outbox transaction committed";
    return result;
}

std::vector<OutboxInsertResult> OutboxRepository::InsertBatchOnConnection(
    tinyimx::MySqlConnection* connection,const std::vector<OutboxInsertSpec>& events) {
    std::vector<OutboxInsertResult> results(events.size());
    if(events.empty()||events.size()>64)return results;
    std::vector<tinyimx::MySqlTransactionStatement> statements;statements.reserve(events.size());
    for(std::size_t i=0;i<events.size();++i) {
        const auto& e=events[i];std::string sql;
        if(!PrepareInsertStatement(connection,e.event,e.topic,e.tag,e.message_key,&sql,&results[i].message))return results;
        statements.push_back({std::move(sql),false});
    }
    std::vector<tinyimx::MySqlTransactionStatementResult> replies;
    if(!connection->ExecuteTransactionStatements(statements,&replies)||replies.size()!=events.size())return results;
    for(const auto& reply:replies)if(reply.affected_rows!=1||!reply.insert_id)return results;
    for(std::size_t i=0;i<results.size();++i) {
        results[i].success=true;results[i].outbox_id=replies[i].insert_id;
        results[i].message="outbox event inserted in caller transaction";
    }
    return results;
}

std::vector<OutboxInsertResult> OutboxRepository::InsertBulkOnConnection(
    tinyimx::MySqlConnection* connection, const std::vector<OutboxInsertSpec>& events) {
    std::vector<OutboxInsertResult> results(events.size());
    if (events.empty() || events.size() > 64) return results;
    std::map<std::string,std::size_t> indexes;
    std::string sql = kOutboxInsertPrefix;
    std::string ids;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& e = events[i];
        std::string statement;
        if (!PrepareInsertStatement(connection, e.event, e.topic, e.tag,
                e.message_key, &statement, &results[i].message) ||
            !indexes.emplace(e.event.event_id, i).second) return results;
        if (statement.compare(0, sizeof(kOutboxInsertPrefix) - 1, kOutboxInsertPrefix) != 0)
            return results;
        if (i) { sql += ','; ids += ','; }
        sql += statement.substr(sizeof(kOutboxInsertPrefix) - 1);
        ids += "'" + connection->EscapeString(e.event.event_id) + "'";
    }
    if (sql.size() > 1024 * 1024) return InsertBatchOnConnection(connection, events);
    tinyimx::MySqlQueryResult rows;
    const std::string lookup = "SELECT outbox_id,event_id FROM im_event_outbox WHERE event_id IN (" + ids + ')';
    if (tinyimx::NativeSqlPacketEnabled()) {
        std::vector<tinyimx::MySqlTransactionStatementResult> replies;
        if (!connection->ExecuteTransactionStatementsBoundedRows(
                {{sql,false},{lookup,true}}, &replies,64) || replies.size() != 2 ||
            replies[0].affected_rows != events.size() || !replies[0].insert_id) return results;
        rows = std::move(replies[1].rows);
    } else {
        if (!connection->Execute(sql) || connection->AffectedRows() != events.size() ||
            !connection->Query(lookup, &rows)) return results;
    }
    if (rows.rows.size() != events.size()) return results;
    std::vector<std::uint64_t> found(events.size());
    for (const auto& row : rows.rows) {
        std::uint64_t id = 0;
        if (row.size() != 2 || !ParseUnsigned64(row[0], &id) || !id) return results;
        const auto it = indexes.find(row[1]);
        if (it == indexes.end() || found[it->second]) return results;
        found[it->second] = id;
    }
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (!found[i]) return std::vector<OutboxInsertResult>(events.size());
        results[i].success = true;
        results[i].outbox_id = found[i];
        results[i].message = "native Outbox multi-row insert and identity read completed";
    }
    return results;
}

OutboxClaimResult OutboxRepository::ClaimBatch(
    const std::string& claim_token,
    std::size_t limit,
    int lease_ms
) {
    OutboxClaimResult result;

    if (pool_ == nullptr) {
        result.message = "outbox claim failed: repository pool unavailable";
        return result;
    }
    if (claim_token.empty() || claim_token.size() > kMaxClaimTokenLength) {
        result.message = "outbox claim failed: invalid claim token";
        return result;
    }
    if (limit == 0 || limit > kMaxClaimBatch || lease_ms <= 0) {
        result.message = "outbox claim failed: invalid claim parameters";
        return result;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        result.message = "outbox claim failed: acquire MySQL connection failed";
        return result;
    }

    // Queue claiming needs a committed eligible row plus an exclusive record
    // lock, not a repeatable snapshot or a gap lock blocking new producers.
    // Scope the setting to this NEXT transaction; no session/global default,
    // message+Outbox atomic write, owner fence, lease or SKIP LOCKED changes.
    const bool read_committed = ClaimReadCommittedEnabled();
    if (read_committed &&
        !connection->Execute("SET TRANSACTION ISOLATION LEVEL READ COMMITTED")) {
        result.message = connection->LastError();
        connection->Close();
        return result;
    }
    if (!connection->BeginTransaction()) {
        result.message = connection->LastError();
        if (read_committed) connection->Close();
        return result;
    }

    const std::string select_sql =
        // Migration 014 keeps ready-status rows in global outbox-ID order,
        // avoiding a primary-key walk through published history. Due-time and
        // lease filters, row locking and oldest-ID ordering remain unchanged.
        "SELECT /*+ INDEX(im_event_outbox idx_im_event_outbox_claim_active) */ "
        "outbox_id, event_id, event_type, schema_version, "
        "aggregate_type, aggregate_id, producer_service, topic, tag, "
        "message_key, CAST(payload AS CHAR), status, attempt_count, "
        "COALESCE(locked_by, '') "
        "FROM im_event_outbox "
        "WHERE (status IN (0, 2)) = 1 "
        "AND next_attempt_at <= NOW(3) "
        "AND (locked_until IS NULL OR locked_until < NOW(3)) "
        "ORDER BY outbox_id "
        "LIMIT " + std::to_string(limit) +
        " FOR UPDATE SKIP LOCKED";

    tinyimx::MySqlQueryResult query_result;
    if (!connection->Query(select_sql, &query_result)) {
        result.message = connection->LastError();
        (void)connection->Rollback();
        return result;
    }

    result.records.reserve(query_result.rows.size());
    for (const auto& row : query_result.rows) {
        OutboxRecord record;
        if (!ParseRecord(row, &record, &result.message)) {
            (void)connection->Rollback();
            result.records.clear();
            return result;
        }
        result.records.push_back(std::move(record));
    }

    if (result.records.empty()) {
        if (!connection->Commit()) {
            result.message = connection->LastError();
            return result;
        }
        result.success = true;
        result.message = "outbox claim completed: no ready events";
        return result;
    }

    const std::int64_t lease_us =
        static_cast<std::int64_t>(lease_ms) * 1000;
    const std::string update_sql =
        "UPDATE im_event_outbox SET locked_by = '" +
        connection->EscapeString(claim_token) +
        "', locked_until = DATE_ADD(NOW(3), INTERVAL " +
        std::to_string(lease_us) +
        " MICROSECOND) WHERE outbox_id IN (" +
        BuildIdList(result.records) +
        ") AND status IN (0, 2)";

    if (!connection->Execute(update_sql) ||
        connection->AffectedRows() != result.records.size()) {
        result.message = connection->LastError();
        if (result.message.empty()) {
            result.message = "outbox claim failed: claim update lost rows";
        }
        (void)connection->Rollback();
        result.records.clear();
        return result;
    }

    if (!connection->Commit()) {
        result.message = connection->LastError();
        result.records.clear();
        return result;
    }

    for (auto& record : result.records) {
        record.locked_by = claim_token;
    }

    result.success = true;
    result.message = "outbox batch claimed";
    return result;
}

OutboxMutationResult OutboxRepository::ExecuteOwnedMutation(
    std::uint64_t outbox_id,
    const std::string& claim_token,
    const std::string& set_clause,
    const std::string& action
) {
    OutboxMutationResult result;
    if (pool_ == nullptr || outbox_id == 0 || claim_token.empty() ||
        claim_token.size() > kMaxClaimTokenLength) {
        result.message = action + " failed: invalid repository or ownership input";
        return result;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        result.message = action + " failed: acquire MySQL connection failed";
        return result;
    }

    const std::string sql =
        "UPDATE im_event_outbox SET " + set_clause +
        " WHERE outbox_id = " + std::to_string(outbox_id) +
        " AND locked_by = '" + connection->EscapeString(claim_token) +
        "' AND status IN (0, 2)";

    if (!connection->Execute(sql)) {
        result.message = connection->LastError();
        return result;
    }

    result.affected_rows = connection->AffectedRows();
    if (result.affected_rows != 1) {
        result.ownership_lost = true;
        result.message = action + " rejected: claim ownership lost";
        return result;
    }

    result.success = true;
    result.message = action + " succeeded";
    return result;
}

OutboxMutationResult OutboxRepository::RenewClaim(
    std::uint64_t outbox_id,
    const std::string& claim_token,
    int lease_ms
) {
    if (lease_ms <= 0) {
        OutboxMutationResult result;
        result.message = "outbox renew failed: invalid lease";
        return result;
    }
    const std::int64_t lease_us =
        static_cast<std::int64_t>(lease_ms) * 1000;
    return ExecuteOwnedMutation(
        outbox_id,
        claim_token,
        "locked_until = DATE_ADD(NOW(3), INTERVAL " +
            std::to_string(lease_us) + " MICROSECOND)",
        "outbox renew"
    );
}

OutboxMutationResult OutboxRepository::MarkPublished(
    std::uint64_t outbox_id,
    const std::string& claim_token
) {
    return ExecuteOwnedMutation(
        outbox_id,
        claim_token,
        "status = 1, attempt_count = attempt_count + 1, "
        "published_at = NOW(3), next_attempt_at = NOW(3), "
        "last_error = '', locked_by = NULL, locked_until = NULL",
        "outbox mark published"
    );
}

OutboxBatchMutationResult OutboxRepository::MarkPublishedBatch(
    const std::vector<OutboxRecord>& records
) {
    OutboxBatchMutationResult result;
    if (pool_ == nullptr || records.empty() || records.size() > 64) {
        result.message = "outbox batch publish failed: invalid batch";
        return result;
    }
    std::unordered_set<std::uint64_t> identities;
    for (const auto& record : records) {
        if (record.outbox_id == 0 || record.locked_by.empty() ||
            record.locked_by.size() > kMaxClaimTokenLength ||
            !identities.insert(record.outbox_id).second) {
            result.message = "outbox batch publish failed: invalid or duplicate identity";
            return result;
        }
    }
    if (records.size() == 1) {
        const auto one = MarkPublished(records.front().outbox_id, records.front().locked_by);
        result.success = one.success || one.ownership_lost;
        result.affected_rows = one.affected_rows;
        result.ownership_lost_rows = one.ownership_lost ? 1 : 0;
        result.message = one.message;
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        result.message = "outbox batch publish failed: acquire MySQL connection failed";
        return result;
    }
    std::string identities_sql;
    for (const auto& record : records) {
        if (!identities_sql.empty()) identities_sql += " OR ";
        identities_sql += "(outbox_id = " + std::to_string(record.outbox_id) + " AND locked_by = '" +
                          connection->EscapeString(record.locked_by) + "')";
    }
    // One autocommit statement. Every row retains its own (ID, lease-token)
    // fence, including mixed owners; a stale owner never updates a replacement.
    // A partial match is a successful statement plus explicit ownership losses.
    // Transport/commit ambiguity remains retryable through the durable leases.
    const std::string sql =
        "UPDATE /*+ INDEX(im_event_outbox PRIMARY) */ im_event_outbox SET status = 1, attempt_count = attempt_count + 1, "
        "published_at = NOW(3), next_attempt_at = NOW(3), last_error = '', "
        "locked_by = NULL, locked_until = NULL WHERE (" +
        identities_sql + ") AND status IN (0, 2)";
    if (!connection->Execute(sql)) {
        result.message = connection->LastError();
        return result;
    }
    result.affected_rows = connection->AffectedRows();
    if (result.affected_rows > records.size()) {
        result.message = "outbox batch publish failed: unexpected affected row count";
        return result;
    }
    result.ownership_lost_rows = records.size() - result.affected_rows;
    result.success = true;
    result.message = "outbox batch publish completed";
    return result;
}

OutboxMutationResult OutboxRepository::MarkRetry(
    std::uint64_t outbox_id,
    const std::string& claim_token,
    int retry_after_ms,
    const std::string& error_message
) {
    OutboxMutationResult result;
    if (retry_after_ms < 0 || pool_ == nullptr || outbox_id == 0 ||
        claim_token.empty() || claim_token.size() > kMaxClaimTokenLength) {
        result.message = "outbox mark retry failed: invalid input";
        return result;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        result.message = "outbox mark retry failed: acquire MySQL connection failed";
        return result;
    }

    const std::int64_t retry_us =
        static_cast<std::int64_t>(retry_after_ms) * 1000;
    const std::string sql =
        "UPDATE im_event_outbox SET status = 2, "
        "attempt_count = attempt_count + 1, "
        "next_attempt_at = DATE_ADD(NOW(3), INTERVAL " +
        std::to_string(retry_us) +
        " MICROSECOND), last_error = '" +
        connection->EscapeString(ClampError(error_message)) +
        "', locked_by = NULL, locked_until = NULL "
        "WHERE outbox_id = " + std::to_string(outbox_id) +
        " AND locked_by = '" + connection->EscapeString(claim_token) +
        "' AND status IN (0, 2)";

    if (!connection->Execute(sql)) {
        result.message = connection->LastError();
        return result;
    }

    result.affected_rows = connection->AffectedRows();
    if (result.affected_rows != 1) {
        result.ownership_lost = true;
        result.message = "outbox mark retry rejected: claim ownership lost";
        return result;
    }

    result.success = true;
    result.message = "outbox marked retry";
    return result;
}

OutboxMutationResult OutboxRepository::MarkQuarantined(
    std::uint64_t outbox_id,
    const std::string& claim_token,
    const std::string& error_message,
    bool publish_attempted
) {
    OutboxMutationResult result;
    if (pool_ == nullptr || outbox_id == 0 || claim_token.empty() ||
        claim_token.size() > kMaxClaimTokenLength) {
        result.message = "outbox quarantine failed: invalid input";
        return result;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        result.message = "outbox quarantine failed: acquire MySQL connection failed";
        return result;
    }

    const std::string increment = publish_attempted
        ? "attempt_count = attempt_count + 1, "
        : "";
    const std::string sql =
        "UPDATE im_event_outbox SET status = 3, " + increment +
        "last_error = '" +
        connection->EscapeString(ClampError(error_message)) +
        "', locked_by = NULL, locked_until = NULL "
        "WHERE outbox_id = " + std::to_string(outbox_id) +
        " AND locked_by = '" + connection->EscapeString(claim_token) +
        "' AND status IN (0, 2)";

    if (!connection->Execute(sql)) {
        result.message = connection->LastError();
        return result;
    }

    result.affected_rows = connection->AffectedRows();
    if (result.affected_rows != 1) {
        result.ownership_lost = true;
        result.message = "outbox quarantine rejected: claim ownership lost";
        return result;
    }

    result.success = true;
    result.message = "outbox quarantined";
    return result;
}

OutboxMutationResult OutboxRepository::ReleaseClaim(
    std::uint64_t outbox_id,
    const std::string& claim_token
) {
    return ExecuteOwnedMutation(
        outbox_id,
        claim_token,
        "locked_by = NULL, locked_until = NULL",
        "outbox release claim"
    );
}

OutboxCleanupResult OutboxRepository::CleanupPublished(
    int retention_hours,
    std::size_t limit
) {
    OutboxCleanupResult result;
    if (pool_ == nullptr || retention_hours <= 0 ||
        limit == 0 || limit > kMaxCleanupBatch) {
        result.message = "outbox cleanup failed: invalid input";
        return result;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        result.message = "outbox cleanup failed: acquire MySQL connection failed";
        return result;
    }

    const std::string sql =
        // Match the retention index and stop after this batch. Sorting by the
        // primary ID can scan/sort the published backlog while holding locks
        // that delay new message+Outbox transactions. Migration 011 supplies
        // this index; MySQL 8.0.20+ supports the INDEX optimizer hint.
        "DELETE /*+ INDEX(im_event_outbox idx_im_event_outbox_cleanup) */ "
        "FROM im_event_outbox WHERE status = 1 "
        "AND published_at IS NOT NULL "
        "AND published_at < DATE_SUB(NOW(3), INTERVAL " +
        std::to_string(retention_hours) +
        " HOUR) ORDER BY published_at, outbox_id LIMIT " + std::to_string(limit);

    if (!connection->Execute(sql)) {
        result.message = connection->LastError();
        return result;
    }

    result.success = true;
    result.deleted_rows = connection->AffectedRows();
    result.message = "outbox published rows cleaned";
    return result;
}

}  // namespace tinyimx::outbox
