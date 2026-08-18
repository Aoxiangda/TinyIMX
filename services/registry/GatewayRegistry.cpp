#include "services/registry/GatewayRegistry.h"

#include "common/logging/LogMacros.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>
#include <vector>

namespace tinyimx {
namespace {

using Json = nlohmann::json;

constexpr const char*
    kRegisterGatewayScript = R"lua(
local value = redis.call('GET', KEYS[1])

if not value then
    redis.call(
        'SET',
        KEYS[1],
        ARGV[1],
        'EX',
        ARGV[2]
    )

    redis.call(
        'SADD',
        KEYS[2],
        ARGV[4]
    )

    return 1
end

local decode_ok, record =
    pcall(cjson.decode, value)

if not decode_ok or
   type(record) ~= 'table' or
   type(record['gateway_id']) ~= 'string' or
   type(record['lease_token']) ~= 'string' then
    return 4
end

if record['gateway_id'] ~= ARGV[4] then
    return 4
end

if record['lease_token'] ~= ARGV[3] then
    return 3
end

redis.call(
    'SET',
    KEYS[1],
    ARGV[1],
    'EX',
    ARGV[2]
)

redis.call(
    'SADD',
    KEYS[2],
    ARGV[4]
)

return 2
)lua";

constexpr const char*
    kRefreshGatewayLeaseScript = R"lua(
local value = redis.call('GET', KEYS[1])

if not value then
    return 0
end

local decode_ok, record =
    pcall(cjson.decode, value)

if not decode_ok or
   type(record) ~= 'table' or
   type(record['gateway_id']) ~= 'string' or
   type(record['lease_token']) ~= 'string' then
    return 3
end

if record['gateway_id'] ~= ARGV[1] then
    return 3
end

if record['lease_token'] ~= ARGV[2] then
    return 2
end

local expire_result =
    redis.call(
        'EXPIRE',
        KEYS[1],
        ARGV[3]
    )

if expire_result == 1 then
    redis.call(
        'SADD',
        KEYS[2],
        ARGV[1]
    )

    return 1
end

return 0
)lua";

constexpr const char*
    kUnregisterGatewayScript = R"lua(
local value = redis.call('GET', KEYS[1])

if not value then
    redis.call(
        'SREM',
        KEYS[2],
        ARGV[1]
    )

    return 0
end

local decode_ok, record =
    pcall(cjson.decode, value)

if not decode_ok or
   type(record) ~= 'table' or
   type(record['gateway_id']) ~= 'string' or
   type(record['lease_token']) ~= 'string' then
    return 3
end

if record['gateway_id'] ~= ARGV[1] then
    return 3
end

if record['lease_token'] ~= ARGV[2] then
    return 2
end

redis.call(
    'DEL',
    KEYS[1]
)

redis.call(
    'SREM',
    KEYS[2],
    ARGV[1]
)

return 1

)lua";

bool IsValidGatewayRecord(
    const GatewayInstanceRecord& record
) {
    return
        !record.gateway_id.empty() &&
        !record.lease_token.empty() &&
        !record.listen_host.empty() &&
        record.listen_port != 0 &&
        record.started_at > 0;
}

}  // namespace

GatewayRegistry::GatewayRegistry(
    RedisConnectionPool* pool,
    std::string key_prefix
)
    : pool_(pool),
      key_prefix_(
          std::move(key_prefix)
      ) {}

RegisterGatewayResult
GatewayRegistry::Register(
    const GatewayInstanceRecord& record,
    int ttl_seconds
) {
    RegisterGatewayResult result;

    if (pool_ == nullptr) {
        result.status =
            RegisterGatewayStatus::
                kInvalidArgument;

        result.error_message =
            "gateway registry register failed: "
            "redis pool is null";

        return result;
    }

    if (!IsValidGatewayRecord(record)) {
        result.status =
            RegisterGatewayStatus::
                kInvalidArgument;

        result.error_message =
            "gateway registry register failed: "
            "invalid gateway record";

        return result;
    }

    if (ttl_seconds <= 0) {
        result.status =
            RegisterGatewayStatus::
                kInvalidArgument;

        result.error_message =
            "gateway registry register failed: "
            "ttl_seconds must be positive";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            RegisterGatewayStatus::
                kRedisError;

        result.error_message =
            "gateway registry register failed: "
            "acquire redis connection failed";

        return result;
    }

    const auto script_result =
        connection->EvalInteger(
            kRegisterGatewayScript,
            {
                BuildKey(
                    record.gateway_id
                ),
                BuildIndexKey()
            },
            {
                Serialize(record),
                std::to_string(
                    ttl_seconds
                ),
                record.lease_token,
                record.gateway_id
            }
        );

        if (!connection->SAdd(
            BuildIndexKey(),
            record.gateway_id
        )) {
        result.status =
            RegisterGatewayStatus::kRedisError;

        result.error_message =
            connection->LastError();

        connection->Del(
            BuildKey(record.gateway_id)
        );

        return result;
        }
    if (!script_result.has_value()) {
        result.status =
            RegisterGatewayStatus::
                kRedisError;

        result.error_message =
            connection->LastError();

        return result;
    }

    switch (script_result.value()) {
        case 1:
            result.status =
                RegisterGatewayStatus::
                    kRegistered;

            LOG_INFO(
                "gateway registry registered"
                << ", gateway_id="
                << record.gateway_id
                << ", ttl_seconds="
                << ttl_seconds
            );

            return result;

        case 2:
            result.status =
                RegisterGatewayStatus::
                    kRenewed;

            LOG_INFO(
                "gateway registry existing lease renewed"
                << ", gateway_id="
                << record.gateway_id
                << ", ttl_seconds="
                << ttl_seconds
            );

            return result;

        case 3:
            result.status =
                RegisterGatewayStatus::
                    kConflict;

            LOG_WARN(
                "gateway registry register rejected: "
                "gateway_id owned by another lease"
                << ", gateway_id="
                << record.gateway_id
            );

            return result;

        case 4:
            result.status =
                RegisterGatewayStatus::
                    kInvalidRecord;

            result.error_message =
                "gateway registry register failed: "
                "stored gateway record is invalid";

            LOG_ERROR(
                result.error_message
                << ", gateway_id="
                << record.gateway_id
            );

            return result;

        default:
            result.status =
                RegisterGatewayStatus::
                    kRedisError;

            result.error_message =
                "gateway registry register failed: "
                "unexpected script result="
                + std::to_string(
                    script_result.value()
                );

            LOG_ERROR(
                result.error_message
            );

            return result;
    }
}

RefreshGatewayLeaseResult
GatewayRegistry::RefreshIfMatch(
    const std::string& gateway_id,
    const std::string& lease_token,
    int ttl_seconds
) {
    RefreshGatewayLeaseResult result;

    if (pool_ == nullptr ||
        gateway_id.empty() ||
        lease_token.empty() ||
        ttl_seconds <= 0) {
        result.status =
            RefreshGatewayLeaseStatus::
                kInvalidArgument;

        result.error_message =
            "gateway registry refresh failed: "
            "invalid argument";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            RefreshGatewayLeaseStatus::
                kRedisError;

        result.error_message =
            "gateway registry refresh failed: "
            "acquire redis connection failed";

        return result;
    }

    const auto script_result =
        connection->EvalInteger(
            kRefreshGatewayLeaseScript,
            {
                BuildKey(gateway_id),
                BuildIndexKey()
            },
            {
                gateway_id,
                lease_token,
                std::to_string(
                    ttl_seconds
                )
            }
        );

    if (!script_result.has_value()) {
        result.status =
            RefreshGatewayLeaseStatus::
                kRedisError;

        result.error_message =
            connection->LastError();

        return result;
    }

    switch (script_result.value()) {
        case 1:
            result.status =
                RefreshGatewayLeaseStatus::
                    kRefreshed;

            return result;

        case 0:
            result.status =
                RefreshGatewayLeaseStatus::
                    kNotFound;

            return result;

        case 2:
            result.status =
                RefreshGatewayLeaseStatus::
                    kMismatch;

            LOG_WARN(
                "gateway registry refresh ignored: "
                "lease mismatch"
                << ", gateway_id="
                << gateway_id
            );

            return result;

        case 3:
            result.status =
                RefreshGatewayLeaseStatus::
                    kInvalidRecord;

            result.error_message =
                "gateway registry refresh failed: "
                "stored gateway record is invalid";

            LOG_ERROR(
                result.error_message
                << ", gateway_id="
                << gateway_id
            );

            return result;

        default:
            result.status =
                RefreshGatewayLeaseStatus::
                    kRedisError;

            result.error_message =
                "gateway registry refresh failed: "
                "unexpected script result="
                + std::to_string(
                    script_result.value()
                );

            LOG_ERROR(
                result.error_message
            );

            return result;
    }
}

UnregisterGatewayResult GatewayRegistry::UnregisterIfMatch(
    const std::string& gateway_id,
    const std::string& lease_token
) {
    UnregisterGatewayResult result;

    if (pool_ == nullptr ||
        gateway_id.empty() ||
        lease_token.empty()) {
        result.status =
            UnregisterGatewayStatus::
                kInvalidArgument;

        result.error_message =
            "gateway registry unregister failed: "
            "invalid argument";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            UnregisterGatewayStatus::
                kRedisError;

        result.error_message =
            "gateway registry unregister failed: "
            "acquire redis connection failed";

        return result;
    }

    const auto script_result =
        connection->EvalInteger(
            kUnregisterGatewayScript,
            {
                BuildKey(gateway_id),
                BuildIndexKey()
            },
            {
                gateway_id,
                lease_token
            }
        );

    if (!script_result.has_value()) {
        result.status =
            UnregisterGatewayStatus::
                kRedisError;

        result.error_message =
            connection->LastError();

        return result;
    }
    connection->SRem(
        BuildIndexKey(),
        gateway_id
    );
    switch (script_result.value()) {
        case 1:
            result.status =
                UnregisterGatewayStatus::
                    kDeleted;

            LOG_INFO(
                "gateway registry lease removed"
                << ", gateway_id="
                << gateway_id
            );

            return result;

        case 0:
            result.status =
                UnregisterGatewayStatus::
                    kNotFound;

            return result;

        case 2:
            result.status =
                UnregisterGatewayStatus::
                    kMismatch;

            LOG_WARN(
                "gateway registry unregister ignored: "
                "lease mismatch"
                << ", gateway_id="
                << gateway_id
            );

            return result;

        case 3:
            result.status =
                UnregisterGatewayStatus::
                    kInvalidRecord;

            result.error_message =
                "gateway registry unregister failed: "
                "stored gateway record is invalid";

            LOG_ERROR(
                result.error_message
                << ", gateway_id="
                << gateway_id
            );

            return result;

        default:
            result.status =
                UnregisterGatewayStatus::
                    kRedisError;

            result.error_message =
                "gateway registry unregister failed: "
                "unexpected script result="
                + std::to_string(
                    script_result.value()
                );

            LOG_ERROR(
                result.error_message
            );

            return result;
    }
}

GetGatewayResult
GatewayRegistry::Get(
    const std::string& gateway_id
) {
    GetGatewayResult result;

    if (pool_ == nullptr ||
        gateway_id.empty()) {
        result.status =
            GetGatewayStatus::
                kInvalidArgument;

        result.error_message =
            "gateway registry get failed: "
            "invalid argument";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            GetGatewayStatus::
                kRedisError;

        result.error_message =
            "gateway registry get failed: "
            "acquire redis connection failed";

        return result;
    }

    const auto value =
        connection->Get(
            BuildKey(gateway_id)
        );

    if (!value.has_value()) {
        if (!connection->
                LastError().empty()) {
            result.status =
                GetGatewayStatus::
                    kRedisError;

            result.error_message =
                connection->LastError();

            return result;
        }

        result.status =
            GetGatewayStatus::
                kNotFound;

        return result;
    }

    GatewayInstanceRecord record;
    std::string deserialize_error;

    if (!Deserialize(
            value.value(),
            &record,
            &deserialize_error
        )) {
        result.status =
            GetGatewayStatus::
                kInvalidRecord;

        result.error_message =
            std::move(
                deserialize_error
            );

        return result;
    }

    if (record.gateway_id !=
        gateway_id) {
        result.status =
            GetGatewayStatus::
                kInvalidRecord;

        result.error_message =
            "gateway registry get failed: "
            "stored gateway_id does not "
            "match redis key";

        return result;
    }

    result.status =
        GetGatewayStatus::
            kFound;

    result.record =
        std::move(record);

    return result;
}

ListGatewayResult GatewayRegistry::ListActiveGateways() {
    ListGatewayResult result;

    if (pool_ == nullptr) {
        result.status =
            ListGatewayStatus::
                kInvalidArgument;

        result.error_message =
            "gateway registry list failed: "
            "redis pool is null";

        return result;
    }

    std::optional<
        std::vector<std::string>
    > gateway_ids;

    {
        auto connection =
            pool_->Acquire();

        if (!connection) {
            result.status =
                ListGatewayStatus::
                    kRedisError;

            result.error_message =
                "gateway registry list failed: "
                "acquire redis connection failed";

            return result;
        }

        gateway_ids =
            connection->SMembers(
                BuildIndexKey()
            );

        if (!gateway_ids.has_value()) {
            result.status =
                ListGatewayStatus::
                    kRedisError;

            result.error_message =
                connection->LastError();

            return result;
        }
    }

    for (
        const auto& gateway_id :
        gateway_ids.value()
    ) {
        const auto get_result =
            Get(gateway_id);

        if (get_result.Found() &&
            get_result.record.has_value()) {
            result.instances.push_back(
                get_result.record.value()
            );

            continue;
        }

        if (
            get_result.status ==
                GetGatewayStatus::kRedisError
        ) {
            result.status =
                ListGatewayStatus::
                    kRedisError;

            result.error_message =
                get_result.error_message;

            return result;
        }

        /*
         * Index 中存在，
         * 但真正的 Lease 已不存在，
         * 说明它是 kill -9 / TTL 过期
         * 留下的 stale member。
         */
        auto connection =
            pool_->Acquire();

        if (!connection) {
            LOG_WARN(
                "gateway registry stale index "
                "cleanup skipped"
                << ", gateway_id="
                << gateway_id
            );

            continue;
        }

        if (!connection->SRem(
                BuildIndexKey(),
                gateway_id
            )) {
            LOG_WARN(
                "gateway registry stale index "
                "cleanup failed"
                << ", gateway_id="
                << gateway_id
                << ", error="
                << connection->LastError()
            );
        }
    }

    std::sort(
        result.instances.begin(),
        result.instances.end(),
        [](
            const GatewayInstanceRecord& lhs,
            const GatewayInstanceRecord& rhs
        ) {
            return lhs.gateway_id <
                   rhs.gateway_id;
        }
    );

    result.status =
        ListGatewayStatus::kOk;

    return result;
}

std::string
GatewayRegistry::BuildKey(
    const std::string& gateway_id
) const {
    return key_prefix_ +
           gateway_id;
}

std::string
GatewayRegistry::BuildIndexKey() const {
    return
        key_prefix_ +
        "__index__";
}

std::string
GatewayRegistry::Serialize(
    const GatewayInstanceRecord& record
) {
    Json body;

    body["gateway_id"] =
        record.gateway_id;

    body["lease_token"] =
        record.lease_token;

    body["listen_host"] =
        record.listen_host;

    body["listen_port"] =
        record.listen_port;

    body["started_at"] =
        record.started_at;

    return body.dump();
}

bool GatewayRegistry::Deserialize(
    const std::string& value,
    GatewayInstanceRecord* record,
    std::string* error_message
) {
    if (record == nullptr ||
        error_message == nullptr) {
        return false;
    }

    try {
        const Json body =
            Json::parse(value);

        GatewayInstanceRecord parsed;

        parsed.gateway_id =
            body.at("gateway_id").
                get<std::string>();

        parsed.lease_token =
            body.at("lease_token").
                get<std::string>();

        parsed.listen_host =
            body.at("listen_host").
                get<std::string>();

        parsed.listen_port =
            body.at("listen_port").
                get<std::uint16_t>();

        parsed.started_at =
            body.at("started_at").
                get<std::int64_t>();

        if (!IsValidGatewayRecord(
                parsed
            )) {
            *error_message =
                "gateway registry deserialize "
                "failed: invalid record values";

            return false;
        }

        *record =
            std::move(parsed);

        error_message->clear();

        return true;
    } catch (
        const std::exception& e
    ) {
        *error_message =
            std::string(
                "gateway registry deserialize "
                "failed: "
            ) +
            e.what();

        return false;
    }
}

std::string
RegisterGatewayStatusToString(
    RegisterGatewayStatus status
) {
    switch (status) {
        case RegisterGatewayStatus::
            kRegistered:
            return "registered";

        case RegisterGatewayStatus::
            kRenewed:
            return "renewed";

        case RegisterGatewayStatus::
            kConflict:
            return "conflict";

        case RegisterGatewayStatus::
            kInvalidRecord:
            return "invalid_record";

        case RegisterGatewayStatus::
            kInvalidArgument:
            return "invalid_argument";

        case RegisterGatewayStatus::
            kRedisError:
            return "redis_error";

        default:
            return "unknown";
    }
}

std::string
RefreshGatewayLeaseStatusToString(
    RefreshGatewayLeaseStatus status
) {
    switch (status) {
        case RefreshGatewayLeaseStatus::
            kRefreshed:
            return "refreshed";

        case RefreshGatewayLeaseStatus::
            kNotFound:
            return "not_found";

        case RefreshGatewayLeaseStatus::
            kMismatch:
            return "mismatch";

        case RefreshGatewayLeaseStatus::
            kInvalidRecord:
            return "invalid_record";

        case RefreshGatewayLeaseStatus::
            kInvalidArgument:
            return "invalid_argument";

        case RefreshGatewayLeaseStatus::
            kRedisError:
            return "redis_error";

        default:
            return "unknown";
    }
}

std::string
UnregisterGatewayStatusToString(
    UnregisterGatewayStatus status
) {
    switch (status) {
        case UnregisterGatewayStatus::
            kDeleted:
            return "deleted";

        case UnregisterGatewayStatus::
            kNotFound:
            return "not_found";

        case UnregisterGatewayStatus::
            kMismatch:
            return "mismatch";

        case UnregisterGatewayStatus::
            kInvalidRecord:
            return "invalid_record";

        case UnregisterGatewayStatus::
            kInvalidArgument:
            return "invalid_argument";

        case UnregisterGatewayStatus::
            kRedisError:
            return "redis_error";

        default:
            return "unknown";
    }
}

std::string GetGatewayStatusToString(
    GetGatewayStatus status
) {
    switch (status) {
        case GetGatewayStatus::
            kFound:
            return "found";

        case GetGatewayStatus::
            kNotFound:
            return "not_found";

        case GetGatewayStatus::
            kInvalidRecord:
            return "invalid_record";

        case GetGatewayStatus::
            kInvalidArgument:
            return "invalid_argument";

        case GetGatewayStatus::
            kRedisError:
            return "redis_error";

        default:
            return "unknown";
    }
}

}  // namespace tinyimx