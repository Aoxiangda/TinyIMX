#pragma once

#include "services/eventing/DomainEvent.h"
#include "services/outbox/OutboxStore.h"
#include "services/outbox/OutboxTypes.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace tinyimx {
class MySqlConnection;
class MySqlConnectionPool;
}

namespace tinyimx::outbox {

struct OutboxInsertSpec {
    tinyimx::eventing::DomainEvent event;
    std::string topic;
    std::string tag;
    std::string message_key;
};

class OutboxRepository final : public OutboxStore {
public:
    explicit OutboxRepository(
        tinyimx::MySqlConnectionPool* pool
    );

    OutboxRepository(const OutboxRepository&) = delete;
    OutboxRepository& operator=(const OutboxRepository&) = delete;

    [[nodiscard]] OutboxInsertResult InsertOnConnection(
        tinyimx::MySqlConnection* connection,
        const tinyimx::eventing::DomainEvent& event,
        const std::string& topic,
        const std::string& tag,
        const std::string& message_key
    );

    [[nodiscard]] OutboxClaimResult ClaimBatch(
        const std::string& claim_token,
        std::size_t limit,
        int lease_ms
    ) override;

    // Caller has validated the saved message. Commits both message and event;
    // success is only returned after the COMMIT reply. Larger valid events
    // request the original separate-command path before any wire attempt.
    [[nodiscard]] OutboxInsertCommitResult InsertAndCommitOnConnection(
        tinyimx::MySqlConnection* connection,
        const tinyimx::eventing::DomainEvent& event,
        const std::string& topic, const std::string& tag,
        const std::string& message_key);

    // Each event retains the original codec/routing checks and its own INSERT
    // result. Only COM_QUERY round trips are grouped; caller owns COMMIT.
    std::vector<OutboxInsertResult> InsertBatchOnConnection(
        tinyimx::MySqlConnection* connection,
        const std::vector<OutboxInsertSpec>& events);

    // One multi-row INSERT + one identity read; never assumes contiguous IDs.
    std::vector<OutboxInsertResult> InsertBulkOnConnection(
        tinyimx::MySqlConnection* connection,
        const std::vector<OutboxInsertSpec>& events);

    [[nodiscard]] OutboxMutationResult RenewClaim(
        std::uint64_t outbox_id,
        const std::string& claim_token,
        int lease_ms
    ) override;

    [[nodiscard]] OutboxMutationResult MarkPublished(
        std::uint64_t outbox_id,
        const std::string& claim_token
    ) override;

    [[nodiscard]] OutboxMutationResult MarkRetry(
        std::uint64_t outbox_id,
        const std::string& claim_token,
        int retry_after_ms,
        const std::string& error_message
    ) override;

    [[nodiscard]] OutboxMutationResult MarkQuarantined(
        std::uint64_t outbox_id,
        const std::string& claim_token,
        const std::string& error_message,
        bool publish_attempted
    ) override;

    [[nodiscard]] OutboxMutationResult ReleaseClaim(
        std::uint64_t outbox_id,
        const std::string& claim_token
    ) override;

    [[nodiscard]] OutboxCleanupResult CleanupPublished(
        int retention_hours,
        std::size_t limit
    ) override;

    [[nodiscard]] OutboxBatchMutationResult MarkPublishedBatch(
        const std::vector<OutboxRecord>& records
    ) override;

    // Deterministic test seam. Configure before concurrent use.
    void SetForceInsertFailureForTest(bool enabled) noexcept;

private:
    bool PrepareInsertStatement(
        tinyimx::MySqlConnection* connection,
        const tinyimx::eventing::DomainEvent& event,
        const std::string& topic,const std::string& tag,
        const std::string& message_key,std::string* sql,std::string* error) const;
    [[nodiscard]] OutboxMutationResult ExecuteOwnedMutation(
        std::uint64_t outbox_id,
        const std::string& claim_token,
        const std::string& set_clause,
        const std::string& action
    );

private:
    tinyimx::MySqlConnectionPool* pool_{nullptr};  // non-owning
    std::atomic<bool> force_insert_failure_for_test_{false};
};

}  // namespace tinyimx::outbox
