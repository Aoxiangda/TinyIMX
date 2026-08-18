#pragma once

#include "common/cache/RedisConnectionPool.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tinyimx {

struct GatewayInstanceRecord {
    std::string gateway_id;
    std::string lease_token;
    std::string listen_host;
    std::uint16_t listen_port{0};
    std::int64_t started_at{0};
};

enum class RegisterGatewayStatus {
    kRegistered = 0,
    kRenewed,
    kConflict,
    kInvalidRecord,
    kInvalidArgument,
    kRedisError
};

struct RegisterGatewayResult {
    RegisterGatewayStatus status{RegisterGatewayStatus::kRedisError};

    std::string error_message;

    bool Succeeded() const noexcept {
        return status == RegisterGatewayStatus::kRegistered ||
               status == RegisterGatewayStatus::kRenewed;
    }

    bool NewlyRegistered() const noexcept {
        return status == RegisterGatewayStatus::kRegistered;
    }
};

enum class RefreshGatewayLeaseStatus {
    kRefreshed = 0,
    kNotFound,
    kMismatch,
    kInvalidRecord,
    kInvalidArgument,
    kRedisError
};

struct RefreshGatewayLeaseResult {
    RefreshGatewayLeaseStatus status{
        RefreshGatewayLeaseStatus::kRedisError
    };

    std::string error_message;

    bool Refreshed() const noexcept {
        return status ==
               RefreshGatewayLeaseStatus::kRefreshed;
    }

    bool Completed() const noexcept {
        return status ==
                   RefreshGatewayLeaseStatus::kRefreshed ||
               status ==
                   RefreshGatewayLeaseStatus::kNotFound ||
               status ==
                   RefreshGatewayLeaseStatus::kMismatch;
    }
};

enum class UnregisterGatewayStatus {
    kDeleted = 0,
    kNotFound,
    kMismatch,
    kInvalidRecord,
    kInvalidArgument,
    kRedisError
};

struct UnregisterGatewayResult {
    UnregisterGatewayStatus status{
        UnregisterGatewayStatus::kRedisError
    };

    std::string error_message;

    bool Deleted() const noexcept {
        return status ==
               UnregisterGatewayStatus::kDeleted;
    }

    bool Completed() const noexcept {
        return status ==
                   UnregisterGatewayStatus::kDeleted ||
               status ==
                   UnregisterGatewayStatus::kNotFound ||
               status ==
                   UnregisterGatewayStatus::kMismatch;
    }
};

enum class GetGatewayStatus {
    kFound = 0,
    kNotFound,
    kInvalidRecord,
    kInvalidArgument,
    kRedisError
};

struct GetGatewayResult {
    GetGatewayStatus status{
        GetGatewayStatus::kRedisError
    };

    std::optional<GatewayInstanceRecord> record;

    std::string error_message;

    bool Found() const noexcept {
        return status ==
                   GetGatewayStatus::kFound &&
               record.has_value();
    }
};

enum class ListGatewayStatus {
    kOk = 0,
    kInvalidArgument,
    kRedisError
};

struct ListGatewayResult {
    ListGatewayStatus status{
        ListGatewayStatus::kRedisError
    };

    std::vector<GatewayInstanceRecord>
        instances;

    std::string error_message;

    bool Succeeded() const noexcept {
        return status ==
            ListGatewayStatus::kOk;
    }
};

class GatewayRegistry {
public:
    explicit GatewayRegistry(
        RedisConnectionPool* pool,
        std::string key_prefix =
            "tinyimx:gateway:registry:"
    );

    GatewayRegistry(
        const GatewayRegistry&
    ) = delete;

    GatewayRegistry& operator=(
        const GatewayRegistry&
    ) = delete;

    RegisterGatewayResult Register(
        const GatewayInstanceRecord& record,
        int ttl_seconds
    );

    RefreshGatewayLeaseResult
    RefreshIfMatch(
        const std::string& gateway_id,
        const std::string& lease_token,
        int ttl_seconds
    );

    UnregisterGatewayResult
    UnregisterIfMatch(
        const std::string& gateway_id,
        const std::string& lease_token
    );

    GetGatewayResult Get(
        const std::string& gateway_id
    );

    ListGatewayResult ListActiveGateways();
private:
    std::string BuildKey(
        const std::string& gateway_id
    ) const;

    std::string BuildIndexKey() const;

    static std::string Serialize(
        const GatewayInstanceRecord& record
    );

    static bool Deserialize(
        const std::string& value,
        GatewayInstanceRecord* record,
        std::string* error_message
    );

private:
    RedisConnectionPool* pool_{nullptr};

    const std::string key_prefix_;
};

std::string RegisterGatewayStatusToString(
    RegisterGatewayStatus status
);

std::string RefreshGatewayLeaseStatusToString(
    RefreshGatewayLeaseStatus status
);

std::string UnregisterGatewayStatusToString(
    UnregisterGatewayStatus status
);

std::string GetGatewayStatusToString(
    GetGatewayStatus status
);

}  // namespace tinyimx