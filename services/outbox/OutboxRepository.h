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

    // Deterministic test seam. Configure before concurrent use.
    void SetForceInsertFailureForTest(bool enabled) noexcept;

private:
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
