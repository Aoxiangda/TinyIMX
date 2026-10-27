#pragma once

#include "services/outbox/OutboxTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace tinyimx::outbox {

class OutboxStore {
public:
    virtual ~OutboxStore() = default;

    OutboxStore(const OutboxStore&) = delete;
    OutboxStore& operator=(const OutboxStore&) = delete;

    [[nodiscard]] virtual OutboxClaimResult ClaimBatch(
        const std::string& claim_token,
        std::size_t limit,
        int lease_ms
    ) = 0;

    [[nodiscard]] virtual OutboxMutationResult RenewClaim(
        std::uint64_t outbox_id,
        const std::string& claim_token,
        int lease_ms
    ) = 0;

    [[nodiscard]] virtual OutboxMutationResult MarkPublished(
        std::uint64_t outbox_id,
        const std::string& claim_token
    ) = 0;

    [[nodiscard]] virtual OutboxMutationResult MarkRetry(
        std::uint64_t outbox_id,
        const std::string& claim_token,
        int retry_after_ms,
        const std::string& error_message
    ) = 0;

    [[nodiscard]] virtual OutboxMutationResult MarkQuarantined(
        std::uint64_t outbox_id,
        const std::string& claim_token,
        const std::string& error_message,
        bool publish_attempted
    ) = 0;

    [[nodiscard]] virtual OutboxMutationResult ReleaseClaim(
        std::uint64_t outbox_id,
        const std::string& claim_token
    ) = 0;

    [[nodiscard]] virtual OutboxCleanupResult CleanupPublished(
        int retention_hours,
        std::size_t limit
    ) = 0;

    // Compatibility fallback preserves each store's existing owner fence.
    // Persistent stores may override with one atomic, bounded update.
    [[nodiscard]] virtual OutboxBatchMutationResult MarkPublishedBatch(
        const std::vector<OutboxRecord>& records
    ) {
        OutboxBatchMutationResult result;
        if (records.empty() || records.size() > 64) {
            result.message = "outbox batch publish failed: invalid batch";
            return result;
        }
        result.success = true;
        for (const auto& record : records) {
            const auto one = MarkPublished(record.outbox_id, record.locked_by);
            if (one.success) result.affected_rows += one.affected_rows;
            else if (one.ownership_lost) ++result.ownership_lost_rows;
            else { result.success = false; result.message = one.message; }
        }
        return result;
    }

protected:
    OutboxStore() = default;
};

}  // namespace tinyimx::outbox
