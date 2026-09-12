#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx::outbox {

enum class OutboxStatus : std::uint32_t {
    kPending = 0,
    kPublished = 1,
    kRetry = 2,
    kQuarantined = 3,
};

struct OutboxRecord {
    std::uint64_t outbox_id{0};
    std::string event_id;
    std::string event_type;
    std::uint32_t schema_version{0};
    std::string aggregate_type;
    std::string aggregate_id;
    std::string producer_service;
    std::string topic;
    std::string tag;
    std::string message_key;
    std::string payload;
    OutboxStatus status{OutboxStatus::kPending};
    std::uint32_t attempt_count{0};
    std::string locked_by;
};

struct OutboxInsertResult {
    bool success{false};
    std::uint64_t outbox_id{0};
    std::string message;
};

struct OutboxClaimResult {
    bool success{false};
    std::vector<OutboxRecord> records;
    std::string message;
};

struct OutboxMutationResult {
    bool success{false};
    bool ownership_lost{false};
    std::uint64_t affected_rows{0};
    std::string message;
};

struct OutboxCleanupResult {
    bool success{false};
    std::uint64_t deleted_rows{0};
    std::string message;
};

const char* OutboxStatusToString(OutboxStatus status) noexcept;

}  // namespace tinyimx::outbox
