#pragma once

#include "common/cache/RedisConnectionPool.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tinyimx {

enum class GetUnreadCountStatus {
    kFound = 0,
    kNotFound,
    kInvalidValue,
    kInvalidArgument,
    kRedisError
};

struct GetUnreadCountResult {
    GetUnreadCountStatus status{
        GetUnreadCountStatus::kRedisError
    };

    std::int64_t count{0};
    std::string error_message;

    bool Found() const noexcept {
        return status ==
               GetUnreadCountStatus::kFound;
    }

    bool NotFound() const noexcept {
        return status ==
               GetUnreadCountStatus::kNotFound;
    }

    bool Completed() const noexcept {
        return status ==
                   GetUnreadCountStatus::kFound ||
               status ==
                   GetUnreadCountStatus::kNotFound;
    }
};

enum class IncrementUnreadStatus {
    kIncremented = 0,
    kInvalidArgument,
    kInvalidValue,
    kRedisError
};

struct IncrementUnreadResult {
    IncrementUnreadStatus status{
        IncrementUnreadStatus::kRedisError
    };

    std::int64_t private_count{0};
    std::string error_message;

    bool Succeeded() const noexcept {
        return status ==
               IncrementUnreadStatus::
                   kIncremented;
    }
};


enum class EnsureUnreadProjectionStatus {
    kApplied = 0,
    kAlreadyApplied,
    kIdentityConflict,
    kInvalidArgument,
    kInvalidValue,
    kRedisError
};

struct EnsureUnreadProjectionResult {
    EnsureUnreadProjectionStatus status{
        EnsureUnreadProjectionStatus::kRedisError
    };

    bool incremented{false};
    std::string error_message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == EnsureUnreadProjectionStatus::kApplied ||
               status == EnsureUnreadProjectionStatus::kAlreadyApplied;
    }

    [[nodiscard]] bool Applied() const noexcept {
        return status == EnsureUnreadProjectionStatus::kApplied;
    }
};

enum class ClearUnreadStatus {
    kCleared = 0,
    kNotFound,
    kInvalidArgument,
    kInvalidValue,
    kRedisError
};

struct ClearUnreadResult {
    ClearUnreadStatus status{
        ClearUnreadStatus::kRedisError
    };

    /*
     * kCleared时：
     * 表示完成本次清除后的总未读数。
     *
     * kNotFound时：
     * 该字段不代表Redis中的真实总未读数，
     * 调用方如需总数，应再执行GetTotalUnread()。
     */
    std::int64_t total_count{0};

    std::string error_message;

    bool Cleared() const noexcept {
        return status ==
               ClearUnreadStatus::kCleared;
    }

    bool NotFound() const noexcept {
        return status ==
               ClearUnreadStatus::kNotFound;
    }

    bool Completed() const noexcept {
        return status ==
                   ClearUnreadStatus::kCleared ||
               status ==
                   ClearUnreadStatus::kNotFound;
    }
};


enum class SetUnreadSnapshotStatus {
    kApplied = 0,
    kInvalidArgument,
    kRedisError
};

struct SetUnreadSnapshotResult {
    SetUnreadSnapshotStatus status{SetUnreadSnapshotStatus::kRedisError};
    std::string error_message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == SetUnreadSnapshotStatus::kApplied;
    }
};

enum class ReplaceUnreadProjectionStatus {
    kApplied = 0,
    kInvalidArgument,
    kTooManyPeers,
    kRedisError
};

struct ReplaceUnreadProjectionResult {
    ReplaceUnreadProjectionStatus status{ReplaceUnreadProjectionStatus::kRedisError};
    std::string error_message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == ReplaceUnreadProjectionStatus::kApplied;
    }
};

class UnreadCountCache {
public:
    explicit UnreadCountCache(
        RedisConnectionPool* pool,
        std::string key_prefix =
            "tinyimx:unread:"
    );

    UnreadCountCache(
        const UnreadCountCache&
    ) = delete;

    UnreadCountCache& operator=(
        const UnreadCountCache&
    ) = delete;

    IncrementUnreadResult
    IncrementPrivateUnread(
        std::uint64_t receiver_user_id,
        std::uint64_t sender_user_id
    );


    EnsureUnreadProjectionResult
    EnsurePrivateUnreadProjection(
        std::uint64_t message_id,
        std::uint64_t receiver_user_id,
        std::uint64_t sender_user_id,
        bool should_count_as_unread
    );

    GetUnreadCountResult
    GetPrivateUnread(
        std::uint64_t receiver_user_id,
        std::uint64_t sender_user_id
    );

    GetUnreadCountResult
    GetTotalUnread(
        std::uint64_t receiver_user_id
    );

    ClearUnreadResult
    ClearPrivateUnread(
        std::uint64_t receiver_user_id,
        std::uint64_t sender_user_id
    );

    // M16-B exact, idempotent projection writer. Both counters are replaced
    // from durable MySQL truth in one Redis script.
    SetUnreadSnapshotResult SetUnreadSnapshot(
        std::uint64_t receiver_user_id,
        std::uint64_t sender_user_id,
        std::int64_t private_unread,
        std::int64_t total_unread
    );

    // Recovery/cutover primitive. peer_counts must include all historical
    // peers for the receiver, including zero counts, so stale legacy keys
    // are deleted without a blocking Redis KEYS/SCAN operation.
    ReplaceUnreadProjectionResult ReplaceUserUnreadProjection(
        std::uint64_t receiver_user_id,
        const std::vector<std::pair<std::uint64_t, std::int64_t>>& peer_counts,
        std::int64_t total_unread
    );

private:
    std::string BuildPrivateKey(
        std::uint64_t receiver_user_id,
        std::uint64_t sender_user_id
    ) const;

    std::string BuildTotalKey(
        std::uint64_t receiver_user_id
    ) const;


    std::string BuildProjectionKey(
        std::uint64_t message_id
    ) const;

    GetUnreadCountResult
    ReadCount(
        const std::string& key,
        const std::string& action
    );

private:
    RedisConnectionPool* pool_{nullptr};

    std::string key_prefix_;
};

std::string
GetUnreadCountStatusToString(
    GetUnreadCountStatus status
);

std::string
IncrementUnreadStatusToString(
    IncrementUnreadStatus status
);


std::string
EnsureUnreadProjectionStatusToString(
    EnsureUnreadProjectionStatus status
);

std::string
ClearUnreadStatusToString(
    ClearUnreadStatus status
);

std::string SetUnreadSnapshotStatusToString(
    SetUnreadSnapshotStatus status
);

std::string ReplaceUnreadProjectionStatusToString(
    ReplaceUnreadProjectionStatus status
);

}  // namespace tinyimx