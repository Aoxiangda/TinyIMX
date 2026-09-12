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

protected:
    OutboxStore() = default;
};

}  // namespace tinyimx::outbox
