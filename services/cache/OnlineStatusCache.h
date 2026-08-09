#pragma once

#include "common/cache/RedisConnectionPool.h"

#include <cstdint>
#include <optional>
#include <string>

namespace tinyimx {

struct OnlineStatusRecord {
    std::uint64_t user_id{0};
    std::string gateway_id;
    std::string connection_name;
    std::int64_t login_time{0};
};

enum class GetOnlineStatusStatus {
    kFound = 0,
    kNotFound,
    kInvalidRecord,
    kInvalidArgument,
    kRedisError
};

struct GetOnlineStatusResult {
    GetOnlineStatusStatus status{
        GetOnlineStatusStatus::kRedisError
    };

    std::optional<OnlineStatusRecord> record;

    std::string error_message;

    bool Found() const noexcept {
        return status ==
               GetOnlineStatusStatus::kFound &&
               record.has_value();
    }

    bool NotFound() const noexcept {
        return status ==
               GetOnlineStatusStatus::kNotFound;
    }

    bool Completed() const noexcept {
        return status ==
                   GetOnlineStatusStatus::kFound ||
               status ==
                   GetOnlineStatusStatus::kNotFound;
    }
};

enum class SetOnlineStatus {
    kStored = 0,
    kInvalidArgument,
    kRedisError
};

struct SetOnlineResult {
    SetOnlineStatus status{
        SetOnlineStatus::kRedisError
    };

    std::string error_message;

    bool Succeeded() const noexcept {
        return status ==
               SetOnlineStatus::kStored;
    }
};

enum class RefreshOnlineIfMatchStatus {
    kRefreshed = 0,
    kNotFound,
    kMismatch,
    kInvalidRecord,
    kInvalidArgument,
    kRedisError
};

struct RefreshOnlineIfMatchResult {
    RefreshOnlineIfMatchStatus status{
        RefreshOnlineIfMatchStatus::kRedisError
    };

    std::string error_message;

    bool Refreshed() const noexcept {
        return status ==
               RefreshOnlineIfMatchStatus::
                   kRefreshed;
    }
};

enum class SetOfflineIfMatchStatus {
    kDeleted = 0,
    kNotFound,
    kMismatch,
    kInvalidRecord,
    kInvalidArgument,
    kRedisError
};


struct SetOfflineIfMatchResult {
    SetOfflineIfMatchStatus status{
        SetOfflineIfMatchStatus::kRedisError
    };

    std::string error_message;

    bool Deleted() const noexcept {
        return status ==
               SetOfflineIfMatchStatus::kDeleted;
    }

    bool Completed() const noexcept {
        return status ==
                   SetOfflineIfMatchStatus::kDeleted ||
               status ==
                   SetOfflineIfMatchStatus::kNotFound ||
               status ==
                   SetOfflineIfMatchStatus::kMismatch;
    }
};

class OnlineStatusCache {
public:
    explicit OnlineStatusCache(
        RedisConnectionPool* pool,
        std::string key_prefix =
            "tinyimx:online:"
    );

    OnlineStatusCache(
        const OnlineStatusCache&
    ) = delete;

    OnlineStatusCache& operator=(
        const OnlineStatusCache&
    ) = delete;

    SetOnlineResult SetOnline(
        std::uint64_t user_id,
        const std::string& gateway_id,
        const std::string& connection_name,
        int ttl_seconds
    );

    SetOfflineIfMatchResult
    SetOfflineIfMatch(
        std::uint64_t user_id,
        const std::string& gateway_id,
        const std::string& connection_name
    );

    RefreshOnlineIfMatchResult RefreshOnlineIfMatch(
        std::uint64_t user_id,
        const std::string& gateway_id,
        const std::string& connection_name,
        int ttl_seconds
    );

    GetOnlineStatusResult GetOnlineStatus(
        std::uint64_t user_id
    );

private:
    std::string BuildKey(
        std::uint64_t user_id
    ) const;

    std::string Serialize(
        const OnlineStatusRecord& record
    ) const;

    static GetOnlineStatusResult
        Deserialize(
            std::uint64_t expected_user_id,
            const std::string& value
    );

private:
    RedisConnectionPool* pool_{nullptr};

    std::string key_prefix_;
};

std::string GetOnlineStatusStatusToString(
    GetOnlineStatusStatus status
);

std::string SetOfflineIfMatchStatusToString(
    SetOfflineIfMatchStatus status
);

std::string RefreshOnlineIfMatchStatusToString(
    RefreshOnlineIfMatchStatus status
);

std::string SetOnlineStatusToString(
    SetOnlineStatus status
);

}  // namespace tinyimx