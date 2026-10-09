#include "services/cache/OnlineStatusCache.h"

#include "common/logging/LogMacros.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <utility>
#include <vector>

namespace tinyimx {
namespace {

using Json = nlohmann::json;

std::int64_t NowUnixSeconds() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()
        );

    return seconds.count();
}

constexpr const char* kSetOnlineIfMissingScript = R"lua(
    local stored = redis.call('SET', KEYS[1], ARGV[1], 'EX', ARGV[2], 'NX')
    if stored then return 1 end
    return 0
)lua";

constexpr const char* kSetOfflineIfMatchScript = R"lua(
    local value = redis.call('GET', KEYS[1])

    if not value then
        return 0
    end

    local decode_ok, record =
        pcall(cjson.decode, value)

    if not decode_ok or
    type(record) ~= 'table' then
        return 3
    end

    if record['gateway_id'] ~= ARGV[1] or
    record['connection_name'] ~= ARGV[2] then
        return 2
    end

    redis.call('DEL', KEYS[1])

    return 1
)lua";

constexpr const char* kRefreshOnlineIfMatchScript = R"lua(
    local value = redis.call('GET', KEYS[1])

    if not value then
        return 0
    end

    local decode_ok, record =
        pcall(cjson.decode, value)

    if not decode_ok or
    type(record) ~= 'table' then
        return 3
    end

    if record['gateway_id'] ~= ARGV[1] or
    record['connection_name'] ~= ARGV[2] then
        return 2
    end

    local ttl_seconds =
        tonumber(ARGV[3])

    if not ttl_seconds or
    ttl_seconds <= 0 then
        return 4
    end

    local expire_result =
        redis.call(
            'EXPIRE',
            KEYS[1],
            ttl_seconds
        )

    if expire_result ~= 1 then
        return 5
    end

    return 1
)lua";

const std::string& RefreshOnlineBatchScript() {
    // Build once from the original single-key script, so ownership and TTL
    // checks cannot drift between the two paths.
    static const std::string script = [] {
        std::string body{kRefreshOnlineIfMatchScript};
        const std::vector<std::pair<std::string, std::string>> replacements{
            {"KEYS[1]", "key"}, {"ARGV[1]", "gateway"},
            {"ARGV[2]", "connection"}, {"ARGV[3]", "ttl"}
        };
        for (const auto& replacement : replacements) {
            std::size_t position = 0;
            while ((position = body.find(replacement.first, position)) !=
                   std::string::npos) {
                body.replace(position, replacement.first.size(),
                             replacement.second);
                position += replacement.second.size();
            }
        }
        return "local function refresh_one(key,gateway,connection,ttl)\n" + body +
            "\nend\nlocal results={}\nfor i=1,#KEYS do\n"
            "local ok,result=pcall(refresh_one,KEYS[i],ARGV[3*i-2],"
            "ARGV[3*i-1],ARGV[3*i])\n"
            "if ok then results[i]=tostring(result) "
            "else results[i]='redis_error' end\nend\nreturn results";
    }();
    return script;
}

}  // namespace

OnlineStatusCache::OnlineStatusCache(RedisConnectionPool* pool,
                                     std::string key_prefix)
    : pool_(pool),
      key_prefix_(std::move(key_prefix)) {}

SetOnlineResult OnlineStatusCache::SetOnline(
    std::uint64_t user_id,
    const std::string& gateway_id,
    const std::string& connection_name,
    int ttl_seconds
) {
    SetOnlineResult result;

    if (user_id == 0) {
        result.status =
            SetOnlineStatus::
                kInvalidArgument;

        result.error_message =
            "online status set online "
            "failed: invalid user_id";

        return result;
    }

    if (gateway_id.empty()) {
        result.status =
            SetOnlineStatus::
                kInvalidArgument;

        result.error_message =
            "online status set online "
            "failed: gateway_id is empty";

        return result;
    }

    if (connection_name.empty()) {
        result.status =
            SetOnlineStatus::
                kInvalidArgument;

        result.error_message =
            "online status set online "
            "failed: connection_name is empty";

        return result;
    }

    if (ttl_seconds <= 0) {
        result.status =
            SetOnlineStatus::
                kInvalidArgument;

        result.error_message =
            "online status set online "
            "failed: invalid ttl_seconds";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            SetOnlineStatus::
                kRedisError;

        result.error_message =
            "online status set online "
            "failed: redis pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            SetOnlineStatus::
                kRedisError;

        result.error_message =
            "online status set online "
            "failed: acquire redis "
            "connection failed";

        return result;
    }

    OnlineStatusRecord record;
    record.user_id = user_id;
    record.gateway_id = gateway_id;
    record.connection_name =
        connection_name;
    record.login_time =
        NowUnixSeconds();

    const bool stored =
        connection->SetEx(
            BuildKey(user_id),
            Serialize(record),
            ttl_seconds
        );

    if (!stored) {
        result.status =
            SetOnlineStatus::
                kRedisError;

        result.error_message =
            connection->LastError();

        if (result.error_message.empty()) {
            result.error_message =
                "online status set online "
                "failed: redis SETEX failed";
        }

        return result;
    }

    result.status =
        SetOnlineStatus::kStored;

    LOG_INFO(
        "online status set"
        << ", user_id=" << user_id
        << ", gateway_id="
        << gateway_id
        << ", connection="
        << connection_name
        << ", ttl_seconds="
        << ttl_seconds
    );

    return result;
}

SetOnlineResult OnlineStatusCache::SetOnlineIfMissing(
    std::uint64_t user_id,
    const std::string& gateway_id,
    const std::string& connection_name,
    int ttl_seconds
) {
    SetOnlineResult result;
    if (user_id == 0 || gateway_id.empty() || connection_name.empty() ||
        ttl_seconds <= 0) {
        result.status = SetOnlineStatus::kInvalidArgument;
        result.error_message = "missing online status restore: invalid argument";
        return result;
    }
    if (pool_ == nullptr) {
        result.error_message = "missing online status restore: redis pool is null";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        result.error_message = "missing online status restore: acquire redis failed";
        return result;
    }
    OnlineStatusRecord record;
    record.user_id = user_id;
    record.gateway_id = gateway_id;
    record.connection_name = connection_name;
    record.login_time = NowUnixSeconds();
    const auto stored = connection->EvalInteger(
        kSetOnlineIfMissingScript, {BuildKey(user_id)},
        {Serialize(record), std::to_string(ttl_seconds)}
    );
    if (stored && (*stored == 0 || *stored == 1)) {
        result.status = *stored == 1 ? SetOnlineStatus::kStored :
                                     SetOnlineStatus::kAlreadyExists;
        return result;
    }
    result.error_message = connection->LastError();
    if (result.error_message.empty())
        result.error_message = "missing online status restore: invalid Redis reply";
    return result;
}

SetOfflineIfMatchResult OnlineStatusCache::SetOfflineIfMatch(
    std::uint64_t user_id,
    const std::string& gateway_id,
    const std::string& connection_name
) {
    SetOfflineIfMatchResult result;

    if (user_id == 0) {
        result.status =
            SetOfflineIfMatchStatus::
                kInvalidArgument;

        result.error_message =
            "online status conditional "
            "offline failed: invalid user_id";

        return result;
    }

    if (gateway_id.empty()) {
        result.status =
            SetOfflineIfMatchStatus::
                kInvalidArgument;

        result.error_message =
            "online status conditional "
            "offline failed: "
            "gateway_id is empty";

        return result;
    }

    if (connection_name.empty()) {
        result.status =
            SetOfflineIfMatchStatus::
                kInvalidArgument;

        result.error_message =
            "online status conditional "
            "offline failed: "
            "connection_name is empty";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            SetOfflineIfMatchStatus::
                kRedisError;

        result.error_message =
            "online status conditional "
            "offline failed: "
            "redis pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            SetOfflineIfMatchStatus::
                kRedisError;

        result.error_message =
            "online status conditional "
            "offline failed: acquire "
            "redis connection failed";

        return result;
    }

    const auto script_result =
        connection->EvalInteger(
            kSetOfflineIfMatchScript,
            std::vector<std::string>{
                BuildKey(user_id)
            },
            std::vector<std::string>{
                gateway_id,
                connection_name
            }
        );

    if (!script_result.has_value()) {
        result.status =
            SetOfflineIfMatchStatus::
                kRedisError;

        result.error_message =
            connection->LastError();

        return result;
    }

    switch (script_result.value()) {
        case 0:
            result.status =
                SetOfflineIfMatchStatus::
                    kNotFound;

            return result;

        case 1:
            result.status =
                SetOfflineIfMatchStatus::
                    kDeleted;

            LOG_INFO(
                "online status conditionally "
                "removed"
                << ", user_id=" << user_id
                << ", gateway_id="
                << gateway_id
                << ", connection="
                << connection_name
            );

            return result;

        case 2:
            result.status =
                SetOfflineIfMatchStatus::
                    kMismatch;

            LOG_INFO(
                "online status conditional "
                "remove ignored: "
                "record mismatch"
                << ", user_id=" << user_id
                << ", gateway_id="
                << gateway_id
                << ", connection="
                << connection_name
            );

            return result;

        case 3:
            result.status =
                SetOfflineIfMatchStatus::
                    kInvalidRecord;

            result.error_message =
                "online status conditional "
                "offline failed: stored "
                "record is invalid";

            LOG_ERROR(
                result.error_message
                << ", user_id="
                << user_id
            );

            return result;

        default:
            result.status =
                SetOfflineIfMatchStatus::
                    kRedisError;

            result.error_message =
                "online status conditional "
                "offline failed: unexpected "
                "script result="
                + std::to_string(
                    script_result.value()
                );

            LOG_ERROR(
                result.error_message
                << ", user_id="
                << user_id
            );

            return result;
    }
}

RefreshOnlineIfMatchResult
OnlineStatusCache::RefreshOnlineIfMatch(
    std::uint64_t user_id,
    const std::string& gateway_id,
    const std::string& connection_name,
    int ttl_seconds
) {
    RefreshOnlineIfMatchResult result;

    if (user_id == 0) {
        result.status =
            RefreshOnlineIfMatchStatus::
                kInvalidArgument;

        result.error_message =
            "online status conditional "
            "refresh failed: invalid user_id";

        return result;
    }

    if (gateway_id.empty()) {
        result.status =
            RefreshOnlineIfMatchStatus::
                kInvalidArgument;

        result.error_message =
            "online status conditional "
            "refresh failed: "
            "gateway_id is empty";

        return result;
    }

    if (connection_name.empty()) {
        result.status =
            RefreshOnlineIfMatchStatus::
                kInvalidArgument;

        result.error_message =
            "online status conditional "
            "refresh failed: "
            "connection_name is empty";

        return result;
    }

    if (ttl_seconds <= 0) {
        result.status =
            RefreshOnlineIfMatchStatus::
                kInvalidArgument;

        result.error_message =
            "online status conditional "
            "refresh failed: "
            "invalid ttl_seconds";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            RefreshOnlineIfMatchStatus::
                kRedisError;

        result.error_message =
            "online status conditional "
            "refresh failed: "
            "redis pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            RefreshOnlineIfMatchStatus::
                kRedisError;

        result.error_message =
            "online status conditional "
            "refresh failed: acquire "
            "redis connection failed";

        return result;
    }

    const auto script_result =
        connection->EvalInteger(
            kRefreshOnlineIfMatchScript,
            std::vector<std::string>{
                BuildKey(user_id)
            },
            std::vector<std::string>{
                gateway_id,
                connection_name,
                std::to_string(ttl_seconds)
            }
        );

    if (!script_result.has_value()) {
        result.status =
            RefreshOnlineIfMatchStatus::
                kRedisError;

        result.error_message =
            connection->LastError();

        if (result.error_message.empty()) {
            result.error_message =
                "online status conditional "
                "refresh failed: "
                "redis EVAL failed";
        }

        return result;
    }

    switch (script_result.value()) {
        case 0:
            result.status =
                RefreshOnlineIfMatchStatus::
                    kNotFound;

            return result;

        case 1:
            result.status =
                RefreshOnlineIfMatchStatus::
                    kRefreshed;

            LOG_INFO(
                "online status conditionally "
                "refreshed"
                << ", user_id="
                << user_id
                << ", gateway_id="
                << gateway_id
                << ", connection="
                << connection_name
                << ", ttl_seconds="
                << ttl_seconds
            );

            return result;

        case 2:
            result.status =
                RefreshOnlineIfMatchStatus::
                    kMismatch;

            LOG_INFO(
                "online status conditional "
                "refresh ignored: "
                "record mismatch"
                << ", user_id="
                << user_id
                << ", gateway_id="
                << gateway_id
                << ", connection="
                << connection_name
            );

            return result;

        case 3:
            result.status =
                RefreshOnlineIfMatchStatus::
                    kInvalidRecord;

            result.error_message =
                "online status conditional "
                "refresh failed: stored "
                "record is invalid";

            return result;

        case 4:
            result.status =
                RefreshOnlineIfMatchStatus::
                    kInvalidArgument;

            result.error_message =
                "online status conditional "
                "refresh failed: script "
                "rejected ttl_seconds";

            return result;

        case 5:
            result.status =
                RefreshOnlineIfMatchStatus::
                    kRedisError;

            result.error_message =
                "online status conditional "
                "refresh failed: EXPIRE "
                "returned unexpected result";

            return result;

        default:
            result.status =
                RefreshOnlineIfMatchStatus::
                    kRedisError;

            result.error_message =
                "online status conditional "
                "refresh failed: unexpected "
                "script result=" +
                std::to_string(
                    script_result.value()
                );

            return result;
    }
}

std::vector<RefreshOnlineIfMatchResult>
OnlineStatusCache::RefreshOnlineIfMatchBatch(
    const std::vector<OnlineStatusRefreshRequest>& requests
) {
    std::vector<RefreshOnlineIfMatchResult> results(requests.size());
    if (requests.empty()) return results;
    if (requests.size() > kMaxRefreshBatchSize) {
        for (auto& result : results) {
            result.status = RefreshOnlineIfMatchStatus::kInvalidArgument;
            result.error_message = "online refresh batch exceeds maximum size16";
        }
        return results;
    }

    std::vector<std::size_t> indices;
    std::vector<std::string> keys;
    std::vector<std::string> arguments;
    indices.reserve(requests.size());
    keys.reserve(requests.size());
    arguments.reserve(requests.size() * 3);
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto& request = requests[i];
        const char* invalid = nullptr;
        if (request.user_id == 0) invalid = "invalid user_id";
        else if (request.gateway_id.empty()) invalid = "gateway_id is empty";
        else if (request.connection_name.empty()) invalid = "connection_name is empty";
        else if (request.ttl_seconds <= 0) invalid = "invalid ttl_seconds";
        if (invalid) {
            results[i].status = RefreshOnlineIfMatchStatus::kInvalidArgument;
            results[i].error_message =
                std::string("online status conditional refresh failed: ") + invalid;
            continue;
        }
        indices.push_back(i);
        keys.push_back(BuildKey(request.user_id));
        arguments.insert(arguments.end(), {request.gateway_id,
            request.connection_name, std::to_string(request.ttl_seconds)});
    }
    if (indices.empty()) return results;

    const auto fail_valid = [&results, &indices](const std::string& reason) {
        for (const auto index : indices) {
            results[index].status = RefreshOnlineIfMatchStatus::kRedisError;
            results[index].error_message = reason;
        }
    };
    if (pool_ == nullptr) {
        fail_valid("online status conditional refresh failed: redis pool is null");
        return results;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        fail_valid("online status conditional refresh failed: acquire redis connection failed");
        return results;
    }
    const auto response = connection->EvalStringArray(
        RefreshOnlineBatchScript(), keys, arguments);
    if (!response || response->size() != indices.size()) {
        fail_valid(connection->LastError().empty()
            ? "online status conditional refresh failed: invalid batch EVAL reply"
            : connection->LastError());
        return results;
    }
    for (std::size_t item = 0; item < indices.size(); ++item) {
        auto& result = results[indices[item]];
        const auto& code = response->at(item);
        if (code == "0") result.status = RefreshOnlineIfMatchStatus::kNotFound;
        else if (code == "1") result.status = RefreshOnlineIfMatchStatus::kRefreshed;
        else if (code == "2") result.status = RefreshOnlineIfMatchStatus::kMismatch;
        else if (code == "3") {
            result.status = RefreshOnlineIfMatchStatus::kInvalidRecord;
            result.error_message = "online status conditional refresh failed: stored record is invalid";
        } else if (code == "4") {
            result.status = RefreshOnlineIfMatchStatus::kInvalidArgument;
            result.error_message = "online status conditional refresh failed: script rejected ttl_seconds";
        } else {
            result.status = RefreshOnlineIfMatchStatus::kRedisError;
            result.error_message = code == "5"
                ? "online status conditional refresh failed: EXPIRE returned unexpected result"
                : "online status conditional refresh failed: per-key Redis script error";
        }
    }
    return results;
}

GetOnlineStatusResult OnlineStatusCache::GetOnlineStatus(
    std::uint64_t user_id
) {
    GetOnlineStatusResult result;

    if (user_id == 0) {
        result.status =
            GetOnlineStatusStatus::
                kInvalidArgument;

        result.error_message =
            "online status get failed: "
            "invalid user_id";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            GetOnlineStatusStatus::
                kRedisError;

        result.error_message =
            "online status get failed: "
            "redis pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            GetOnlineStatusStatus::
                kRedisError;

        result.error_message =
            "online status get failed: "
            "acquire redis connection failed";

        return result;
    }

    const auto value =
        connection->Get(
            BuildKey(user_id)
        );

    if (!value.has_value()) {
        /*
         * RedisConnection::Get()的nullopt有两种含义：
         *
         * 1.Redis返回NIL，LastError为空；
         * 2.Redis命令失败，LastError非空。
         */
        if (!connection->
                LastError().empty()) {
            result.status =
                GetOnlineStatusStatus::
                    kRedisError;

            result.error_message =
                connection->LastError();

            return result;
        }

        result.status =
            GetOnlineStatusStatus::
                kNotFound;

        return result;
    }

    return Deserialize(
        user_id,
        value.value()
    );
}

std::vector<GetOnlineStatusResult> OnlineStatusCache::GetOnlineStatusBatch(
    const std::vector<std::uint64_t>& user_ids
) {
    std::vector<GetOnlineStatusResult> results(user_ids.size());
    if (user_ids.size() > kMaxGetBatchSize) {
        for (auto& result : results) {
            result.status = GetOnlineStatusStatus::kInvalidArgument;
            result.error_message = "online status batch failed: batch exceeds 256 users";
        }
        return results;
    }
    std::vector<std::string> keys;
    std::vector<std::size_t> positions;
    keys.reserve(user_ids.size());
    positions.reserve(user_ids.size());
    for (std::size_t i = 0; i < user_ids.size(); ++i) {
        if (user_ids[i] == 0) {
            results[i].status = GetOnlineStatusStatus::kInvalidArgument;
            results[i].error_message = "online status get failed: invalid user_id";
        } else {
            keys.push_back(BuildKey(user_ids[i]));
            positions.push_back(i);
        }
    }
    if (keys.empty()) return results;
    const auto fail_valid = [&](const std::string& error) {
        for (const auto i : positions) {
            results[i].status = GetOnlineStatusStatus::kRedisError;
            results[i].error_message = error;
        }
    };
    if (pool_ == nullptr) {
        fail_valid("online status get failed: redis pool is null");
        return results;
    }
    auto connection = pool_->Acquire(); // Preserve the original healthy PING.
    if (!connection) {
        fail_valid("online status get failed: acquire redis connection failed");
        return results;
    }
    // Read-only and bounded for the configured standalone Redis. Raw JSON
    // bytes go to the original C++ Deserialize; Lua never rounds uint64 IDs.
    static constexpr const char* script = R"lua(
        local output = {}
        for i = 1, #KEYS do
            local value = redis.pcall('GET', KEYS[i])
            if type(value) == 'table' and value.err then
                output[#output + 1] = 'error'
                output[#output + 1] = ''
            elseif value == false then
                output[#output + 1] = 'missing'
                output[#output + 1] = ''
            else
                output[#output + 1] = 'value'
                output[#output + 1] = value
            end
        end
        return output
    )lua";
    const auto values = connection->EvalStringArray(script, keys, {});
    if (!values) {
        fail_valid(connection->LastError());
        return results;
    }
    if (values->size() != positions.size() * 2) {
        fail_valid("online status batch failed: unexpected result cardinality");
        return results;
    }
    // Validate all tags before associating any value with a user.
    for (std::size_t j = 0; j < positions.size(); ++j) {
        const auto& tag = (*values)[j * 2];
        if (tag != "value" && tag != "missing" && tag != "error") {
            fail_valid("online status batch failed: invalid result tag");
            return results;
        }
    }
    for (std::size_t j = 0; j < positions.size(); ++j) {
        auto& result = results[positions[j]];
        const auto& tag = (*values)[j * 2];
        if (tag == "missing") {
            result.status = GetOnlineStatusStatus::kNotFound;
        } else if (tag == "error") {
            result.status = GetOnlineStatusStatus::kRedisError;
            result.error_message = "redis get failed: invalid reply type";
        } else {
            result = Deserialize(user_ids[positions[j]], (*values)[j * 2 + 1]);
        }
    }
    return results;
}

std::string OnlineStatusCache::BuildKey(std::uint64_t user_id) const {
    return key_prefix_ + std::to_string(user_id);
}

std::string OnlineStatusCache::Serialize(
    const OnlineStatusRecord& record
) const {
    Json body;
    body["user_id"] = record.user_id;
    body["gateway_id"] = record.gateway_id;
    body["connection_name"] = record.connection_name;
    body["login_time"] = record.login_time;

    return body.dump();
}

GetOnlineStatusResult OnlineStatusCache::Deserialize(
    std::uint64_t expected_user_id,
    const std::string& value
) {
    GetOnlineStatusResult result;

    try {
        const Json body =
            Json::parse(value);

        OnlineStatusRecord record;

        record.user_id =
            body.at("user_id").
                get<std::uint64_t>();

        record.gateway_id =
            body.at("gateway_id").
                get<std::string>();

        record.connection_name =
            body.at("connection_name").
                get<std::string>();

        record.login_time =
            body.at("login_time").
                get<std::int64_t>();

        if (record.user_id !=
            expected_user_id) {
            result.status =
                GetOnlineStatusStatus::
                    kInvalidRecord;

            result.error_message =
                "online status record "
                "user_id mismatch"
                ", expected=" +
                std::to_string(
                    expected_user_id
                ) +
                ", actual=" +
                std::to_string(
                    record.user_id
                );

            return result;
        }

        if (record.gateway_id.empty()) {
            result.status =
                GetOnlineStatusStatus::
                    kInvalidRecord;

            result.error_message =
                "online status record "
                "gateway_id is empty";

            return result;
        }

        if (record.connection_name.empty()) {
            result.status =
                GetOnlineStatusStatus::
                    kInvalidRecord;

            result.error_message =
                "online status record "
                "connection_name is empty";

            return result;
        }

        if (record.login_time <= 0) {
            result.status =
                GetOnlineStatusStatus::
                    kInvalidRecord;

            result.error_message =
                "online status record "
                "login_time is invalid";

            return result;
        }

        result.status =
            GetOnlineStatusStatus::
                kFound;

        result.record =
            std::move(record);

        return result;
    } catch (const std::exception& e) {
        result.status =
            GetOnlineStatusStatus::
                kInvalidRecord;

        result.error_message =
            std::string(
                "online status "
                "deserialize failed: "
            ) +
            e.what();

        return result;
    }
}

std::string GetOnlineStatusStatusToString(
    GetOnlineStatusStatus status
) {
    switch (status) {
        case GetOnlineStatusStatus::
            kFound:
            return "found";

        case GetOnlineStatusStatus::
            kNotFound:
            return "not_found";

        case GetOnlineStatusStatus::
            kInvalidRecord:
            return "invalid_record";

        case GetOnlineStatusStatus::
            kInvalidArgument:
            return "invalid_argument";

        case GetOnlineStatusStatus::
            kRedisError:
            return "redis_error";

        default:
            return "unknown";
    }
}

std::string SetOnlineStatusToString(
    SetOnlineStatus status
) {
    switch (status) {
        case SetOnlineStatus::kStored:
            return "stored";

        case SetOnlineStatus::
            kInvalidArgument:
            return "invalid_argument";

        case SetOnlineStatus::
            kRedisError:
            return "redis_error";

        case SetOnlineStatus::kAlreadyExists:
            return "already_exists";

        default:
            return "unknown";
    }
}

std::string
RefreshOnlineIfMatchStatusToString(
    RefreshOnlineIfMatchStatus status
) {
    switch (status) {
        case RefreshOnlineIfMatchStatus::
            kRefreshed:
            return "refreshed";

        case RefreshOnlineIfMatchStatus::
            kNotFound:
            return "not_found";

        case RefreshOnlineIfMatchStatus::
            kMismatch:
            return "mismatch";

        case RefreshOnlineIfMatchStatus::
            kInvalidRecord:
            return "invalid_record";

        case RefreshOnlineIfMatchStatus::
            kInvalidArgument:
            return "invalid_argument";

        case RefreshOnlineIfMatchStatus::
            kRedisError:
            return "redis_error";

        default:
            return "unknown";
    }
}

std::string SetOfflineIfMatchStatusToString(
    SetOfflineIfMatchStatus status
) {
    switch (status) {
        case SetOfflineIfMatchStatus::
            kDeleted:
            return "deleted";

        case SetOfflineIfMatchStatus::
            kNotFound:
            return "not_found";

        case SetOfflineIfMatchStatus::
            kMismatch:
            return "mismatch";

        case SetOfflineIfMatchStatus::
            kInvalidRecord:
            return "invalid_record";

        case SetOfflineIfMatchStatus::
            kInvalidArgument:
            return "invalid_argument";

        case SetOfflineIfMatchStatus::
            kRedisError:
            return "redis_error";

        default:
            return "unknown";
    }
}

}  // namespace tinyimx
