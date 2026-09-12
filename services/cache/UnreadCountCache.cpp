#include "services/cache/UnreadCountCache.h"

#include "common/logging/LogMacros.h"

#include <charconv>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>
#include <unordered_set>
#include <limits>

namespace {

/*
 * 返回值约定：
 *
 * >= 1：递增后的private未读数
 *   -1：private键保存的值非法
 *   -2：total键保存的值非法
 *   -3：private键继续递增会溢出
 *   -4：total键继续递增会溢出
 */
constexpr const char*
    kIncrementPrivateUnreadScript = R"lua(
local max_before_increment =
    '9223372036854775806'

local function validate_count(value)
    if not value then
        return 0
    end

    if value ~= '0' and
       not string.match(
           value,
           '^[1-9][0-9]*$'
       ) then
        return 1
    end

    if #value >
       #max_before_increment then
        return 2
    end

    if #value ==
           #max_before_increment and
       value >
           max_before_increment then
        return 2
    end

    return 0
end

local private_status =
    validate_count(
        redis.call(
            'GET',
            KEYS[1]
        )
    )

if private_status == 1 then
    return -1
end

if private_status == 2 then
    return -3
end

local total_status =
    validate_count(
        redis.call(
            'GET',
            KEYS[2]
        )
    )

if total_status == 1 then
    return -2
end

if total_status == 2 then
    return -4
end

local private_count =
    redis.call(
        'INCR',
        KEYS[1]
    )

redis.call(
    'INCR',
    KEYS[2]
)

return private_count
)lua";


/*
 * M14-C2 per-message unread projection gate.
 *
 * KEYS[1] = private unread counter
 * KEYS[2] = total unread counter
 * KEYS[3] = stable message projection marker
 * ARGV[1] = receiver:sender identity
 * ARGV[2] = "1" when this durable state contributes unread, else "0"
 *
 * return  1 = marker created (and counters incremented when requested)
 * return  0 = marker already existed with the same identity
 * return -1/-2 = invalid stored private/total count
 * return -3/-4 = private/total overflow
 * return -5 = marker identity conflict
 */
constexpr const char*
    kEnsurePrivateUnreadProjectionScript = R"lua(
local max_before_increment =
    '9223372036854775806'

local function validate_count(value)
    if not value then
        return 0
    end

    if value ~= '0' and
       not string.match(value, '^[1-9][0-9]*$') then
        return 1
    end

    if #value > #max_before_increment then
        return 2
    end

    if #value == #max_before_increment and
       value > max_before_increment then
        return 2
    end

    return 0
end

local marker = redis.call('GET', KEYS[3])
if marker then
    if marker ~= ARGV[1] then
        return -5
    end
    return 0
end

local private_status = validate_count(redis.call('GET', KEYS[1]))
if private_status == 1 then return -1 end
if private_status == 2 then return -3 end

local total_status = validate_count(redis.call('GET', KEYS[2]))
if total_status == 1 then return -2 end
if total_status == 2 then return -4 end

redis.call('SET', KEYS[3], ARGV[1])

if ARGV[2] == '1' then
    redis.call('INCR', KEYS[1])
    redis.call('INCR', KEYS[2])
end

return 1
)lua";

constexpr const char*
    kClearPrivateUnreadScript = R"lua(
local max_count =
    '9223372036854775807'

local function validate_count(value)
    if not value then
        return 0
    end

    if value ~= '0' and
       not string.match(
           value,
           '^[1-9][0-9]*$'
       ) then
        return 1
    end

    if #value >
       #max_count then
        return 1
    end

    if #value ==
           #max_count and
       value >
           max_count then
        return 1
    end

    return 0
end

local private_value =
    redis.call(
        'GET',
        KEYS[1]
    )

if not private_value then
    return -1
end

if validate_count(
       private_value
   ) ~= 0 then
    return -2
end

local total_value =
    redis.call(
        'GET',
        KEYS[2]
    )

if total_value and
   validate_count(
       total_value
   ) ~= 0 then
    return -3
end

local total_after_clear =
    redis.call(
        'DECRBY',
        KEYS[2],
        private_value
    )

redis.call(
    'DEL',
    KEYS[1]
)

if total_after_clear < 0 then
    redis.call(
        'SET',
        KEYS[2],
        '0'
    )

    return 0
end

return total_after_clear
)lua";


/*
 * M16-B exact projection write.
 * KEYS[1] private counter, KEYS[2] total counter.
 * ARGV[1] private durable count, ARGV[2] total durable count.
 */
constexpr const char* kSetUnreadSnapshotScript = R"lua(
local function valid(v)
    if not v then return false end
    if v == '0' then return true end
    return string.match(v, '^[1-9][0-9]*$') ~= nil
end

if #KEYS ~= 2 or #ARGV ~= 2 then return -1 end
if not valid(ARGV[1]) or not valid(ARGV[2]) then return -2 end

if ARGV[1] == '0' then
    redis.call('DEL', KEYS[1])
else
    redis.call('SET', KEYS[1], ARGV[1])
end

if ARGV[2] == '0' then
    redis.call('DEL', KEYS[2])
else
    redis.call('SET', KEYS[2], ARGV[2])
end

return 1
)lua";

/*
 * M16-B cutover/recovery write. KEYS[1] is total, remaining keys are
 * every historical peer key returned by durable MySQL truth. Zero-count
 * peer keys are deleted, so the operation also removes stale legacy values.
 */
constexpr const char* kReplaceUserUnreadProjectionScript = R"lua(
local function valid(v)
    if not v then return false end
    if v == '0' then return true end
    return string.match(v, '^[1-9][0-9]*$') ~= nil
end

if #KEYS ~= #ARGV or #KEYS < 1 then return -1 end
for i = 1, #ARGV do
    if not valid(ARGV[i]) then return -2 end
end

if ARGV[1] == '0' then
    redis.call('DEL', KEYS[1])
else
    redis.call('SET', KEYS[1], ARGV[1])
end

for i = 2, #KEYS do
    if ARGV[i] == '0' then
        redis.call('DEL', KEYS[i])
    else
        redis.call('SET', KEYS[i], ARGV[i])
    end
end
return 1
)lua";

}  // namespace

namespace tinyimx {

UnreadCountCache::UnreadCountCache(
    RedisConnectionPool* pool,
    std::string key_prefix
)
    : pool_(pool),
      key_prefix_(std::move(key_prefix)) {}


      IncrementUnreadResult
UnreadCountCache::IncrementPrivateUnread(
    std::uint64_t receiver_user_id,
    std::uint64_t sender_user_id
) {
    IncrementUnreadResult result;

    if (receiver_user_id == 0 ||
        sender_user_id == 0) {
        result.status =
            IncrementUnreadStatus::
                kInvalidArgument;

        result.error_message =
            "unread count increment private "
            "failed: invalid user id";

        return result;
    }

    if (receiver_user_id ==
        sender_user_id) {
        result.status =
            IncrementUnreadStatus::
                kInvalidArgument;

        result.error_message =
            "unread count increment private "
            "failed: receiver equals sender";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            IncrementUnreadStatus::
                kRedisError;

        result.error_message =
            "unread count increment private "
            "failed: redis pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            IncrementUnreadStatus::
                kRedisError;

        result.error_message =
            "unread count increment private "
            "failed: acquire redis "
            "connection failed";

        return result;
    }

    const auto script_result =
        connection->EvalInteger(
            kIncrementPrivateUnreadScript,
            std::vector<std::string>{
                BuildPrivateKey(
                    receiver_user_id,
                    sender_user_id
                ),
                BuildTotalKey(
                    receiver_user_id
                )
            },
            std::vector<std::string>{}
        );

    if (!script_result.has_value()) {
        result.status =
            IncrementUnreadStatus::
                kRedisError;

        result.error_message =
            connection->LastError();

        if (result.error_message.empty()) {
            result.error_message =
                "unread count increment private "
                "failed: redis EVAL failed";
        }

        return result;
    }

    const std::int64_t script_code =
        script_result.value();

    if (script_code >= 1) {
        result.status =
            IncrementUnreadStatus::
                kIncremented;

        result.private_count =
            script_code;

        LOG_INFO(
            "unread count atomically "
            "incremented"
            << ", receiver="
            << receiver_user_id
            << ", sender="
            << sender_user_id
            << ", private_count="
            << result.private_count
        );

        return result;
    }

    result.status =
        IncrementUnreadStatus::
            kInvalidValue;

    switch (script_code) {
        case -1:
            result.error_message =
                "unread count increment private "
                "failed: stored private count "
                "is invalid";
            return result;

        case -2:
            result.error_message =
                "unread count increment private "
                "failed: stored total count "
                "is invalid";
            return result;

        case -3:
            result.error_message =
                "unread count increment private "
                "failed: private count "
                "would overflow";
            return result;

        case -4:
            result.error_message =
                "unread count increment private "
                "failed: total count "
                "would overflow";
            return result;

        default:
            result.status =
                IncrementUnreadStatus::
                    kRedisError;

            result.error_message =
                "unread count increment private "
                "failed: unexpected script "
                "result=" +
                std::to_string(
                    script_code
                );

            return result;
    }
}


EnsureUnreadProjectionResult
UnreadCountCache::EnsurePrivateUnreadProjection(
    std::uint64_t message_id,
    std::uint64_t receiver_user_id,
    std::uint64_t sender_user_id,
    bool should_count_as_unread
) {
    EnsureUnreadProjectionResult result;

    if (message_id == 0 ||
        receiver_user_id == 0 ||
        sender_user_id == 0 ||
        receiver_user_id == sender_user_id) {
        result.status = EnsureUnreadProjectionStatus::kInvalidArgument;
        result.error_message =
            "unread projection failed: invalid message/user identity";
        return result;
    }

    if (pool_ == nullptr) {
        result.status = EnsureUnreadProjectionStatus::kRedisError;
        result.error_message = "unread projection failed: redis pool is null";
        return result;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = EnsureUnreadProjectionStatus::kRedisError;
        result.error_message =
            "unread projection failed: acquire redis connection failed";
        return result;
    }

    const std::string identity =
        std::to_string(receiver_user_id) + ":" +
        std::to_string(sender_user_id);

    const auto script_result = connection->EvalInteger(
        kEnsurePrivateUnreadProjectionScript,
        std::vector<std::string>{
            BuildPrivateKey(receiver_user_id, sender_user_id),
            BuildTotalKey(receiver_user_id),
            BuildProjectionKey(message_id)
        },
        std::vector<std::string>{
            identity,
            should_count_as_unread ? "1" : "0"
        }
    );

    if (!script_result.has_value()) {
        result.status = EnsureUnreadProjectionStatus::kRedisError;
        result.error_message = connection->LastError();
        if (result.error_message.empty()) {
            result.error_message = "unread projection failed: redis EVAL failed";
        }
        return result;
    }

    switch (*script_result) {
        case 1:
            result.status = EnsureUnreadProjectionStatus::kApplied;
            result.incremented = should_count_as_unread;
            return result;
        case 0:
            result.status = EnsureUnreadProjectionStatus::kAlreadyApplied;
            result.incremented = false;
            return result;
        case -5:
            result.status = EnsureUnreadProjectionStatus::kIdentityConflict;
            result.error_message =
                "unread projection failed: message marker identity conflict";
            return result;
        case -1:
            result.status = EnsureUnreadProjectionStatus::kInvalidValue;
            result.error_message =
                "unread projection failed: stored private count is invalid";
            return result;
        case -2:
            result.status = EnsureUnreadProjectionStatus::kInvalidValue;
            result.error_message =
                "unread projection failed: stored total count is invalid";
            return result;
        case -3:
            result.status = EnsureUnreadProjectionStatus::kInvalidValue;
            result.error_message =
                "unread projection failed: private count would overflow";
            return result;
        case -4:
            result.status = EnsureUnreadProjectionStatus::kInvalidValue;
            result.error_message =
                "unread projection failed: total count would overflow";
            return result;
        default:
            result.status = EnsureUnreadProjectionStatus::kRedisError;
            result.error_message =
                "unread projection failed: unexpected redis script code";
            return result;
    }
}

GetUnreadCountResult
UnreadCountCache::GetPrivateUnread(
    std::uint64_t receiver_user_id,
    std::uint64_t sender_user_id
) {
    GetUnreadCountResult result;

    if (receiver_user_id == 0 ||
        sender_user_id == 0) {
        result.status =
            GetUnreadCountStatus::
                kInvalidArgument;

        result.error_message =
            "unread count get private "
            "failed: invalid user id";

        return result;
    }

    if (receiver_user_id ==
        sender_user_id) {
        result.status =
            GetUnreadCountStatus::
                kInvalidArgument;

        result.error_message =
            "unread count get private "
            "failed: receiver equals sender";

        return result;
    }

    return ReadCount(
        BuildPrivateKey(
            receiver_user_id,
            sender_user_id
        ),
        "get private unread"
    );
}


GetUnreadCountResult UnreadCountCache::GetTotalUnread(
    std::uint64_t receiver_user_id
) {
    GetUnreadCountResult result;

    if (receiver_user_id == 0) {
        result.status =
            GetUnreadCountStatus::
                kInvalidArgument;

        result.error_message =
            "unread count get total "
            "failed: invalid user_id";

        return result;
    }

    return ReadCount(
        BuildTotalKey(
            receiver_user_id
        ),
        "get total unread"
    );
}


std::string UnreadCountCache::BuildProjectionKey(
    std::uint64_t message_id
) const {
    return key_prefix_ + "projection:message:" + std::to_string(message_id);
}

GetUnreadCountResult
UnreadCountCache::ReadCount(
    const std::string& key,
    const std::string& action
) {
    GetUnreadCountResult result;

    if (pool_ == nullptr) {
        result.status =
            GetUnreadCountStatus::
                kRedisError;

        result.error_message =
            "unread count " +
            action +
            " failed: redis pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            GetUnreadCountStatus::
                kRedisError;

        result.error_message =
            "unread count " +
            action +
            " failed: acquire redis "
            "connection failed";

        return result;
    }

    const auto value =
        connection->Get(key);

    if (!value.has_value()) {
        if (!connection->
                LastError().empty()) {
            result.status =
                GetUnreadCountStatus::
                    kRedisError;

            result.error_message =
                connection->LastError();

            return result;
        }

        result.status =
            GetUnreadCountStatus::
                kNotFound;

        result.count = 0;

        return result;
    }

    const std::string& text =
        value.value();

    std::int64_t parsed_count = 0;

    const char* begin =
        text.data();

    const char* end =
        text.data() + text.size();

    const auto parse_result =
        std::from_chars(
            begin,
            end,
            parsed_count
        );

    if (parse_result.ec !=
            std::errc{} ||
        parse_result.ptr != end) {
        result.status =
            GetUnreadCountStatus::
                kInvalidValue;

        result.error_message =
            "unread count " +
            action +
            " failed: stored value "
            "is not a valid integer";

        return result;
    }

    if (parsed_count < 0) {
        result.status =
            GetUnreadCountStatus::
                kInvalidValue;

        result.error_message =
            "unread count " +
            action +
            " failed: stored count "
            "is negative";

        return result;
    }

    result.status =
        GetUnreadCountStatus::kFound;

    result.count =
        parsed_count;

    return result;
}

ClearUnreadResult
UnreadCountCache::ClearPrivateUnread(
    std::uint64_t receiver_user_id,
    std::uint64_t sender_user_id
) {
    ClearUnreadResult result;

    if (receiver_user_id == 0 ||
        sender_user_id == 0) {
        result.status =
            ClearUnreadStatus::
                kInvalidArgument;

        result.error_message =
            "unread count clear private "
            "failed: invalid user id";

        return result;
    }

    if (receiver_user_id ==
        sender_user_id) {
        result.status =
            ClearUnreadStatus::
                kInvalidArgument;

        result.error_message =
            "unread count clear private "
            "failed: receiver equals sender";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            ClearUnreadStatus::
                kRedisError;

        result.error_message =
            "unread count clear private "
            "failed: redis pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            ClearUnreadStatus::
                kRedisError;

        result.error_message =
            "unread count clear private "
            "failed: acquire redis "
            "connection failed";

        return result;
    }

    const auto script_result =
        connection->EvalInteger(
            kClearPrivateUnreadScript,
            std::vector<std::string>{
                BuildPrivateKey(
                    receiver_user_id,
                    sender_user_id
                ),
                BuildTotalKey(
                    receiver_user_id
                )
            },
            std::vector<std::string>{}
        );

    if (!script_result.has_value()) {
        result.status =
            ClearUnreadStatus::
                kRedisError;

        result.error_message =
            connection->LastError();

        if (result.error_message.empty()) {
            result.error_message =
                "unread count clear private "
                "failed: redis EVAL failed";
        }

        return result;
    }

    const std::int64_t script_code =
        script_result.value();

    if (script_code >= 0) {
        result.status =
            ClearUnreadStatus::kCleared;

        result.total_count =
            script_code;

        LOG_INFO(
            "unread count atomically cleared"
            << ", receiver="
            << receiver_user_id
            << ", sender="
            << sender_user_id
            << ", total_count="
            << result.total_count
        );

        return result;
    }

    switch (script_code) {
        case -1:
            result.status =
                ClearUnreadStatus::
                    kNotFound;

            return result;

        case -2:
            result.status =
                ClearUnreadStatus::
                    kInvalidValue;

            result.error_message =
                "unread count clear private "
                "failed: stored private "
                "count is invalid";

            return result;

        case -3:
            result.status =
                ClearUnreadStatus::
                    kInvalidValue;

            result.error_message =
                "unread count clear private "
                "failed: stored total "
                "count is invalid";

            return result;

        default:
            result.status =
                ClearUnreadStatus::
                    kRedisError;

            result.error_message =
                "unread count clear private "
                "failed: unexpected script "
                "result=" +
                std::to_string(
                    script_code
                );

            return result;
    }
}


SetUnreadSnapshotResult UnreadCountCache::SetUnreadSnapshot(
    std::uint64_t receiver_user_id,
    std::uint64_t sender_user_id,
    std::int64_t private_unread,
    std::int64_t total_unread
) {
    SetUnreadSnapshotResult result;
    if (receiver_user_id == 0 || sender_user_id == 0 ||
        receiver_user_id == sender_user_id ||
        private_unread < 0 || total_unread < 0 ||
        private_unread > total_unread) {
        result.status = SetUnreadSnapshotStatus::kInvalidArgument;
        result.error_message = "unread snapshot rejected invalid input";
        return result;
    }
    if (pool_ == nullptr) {
        result.status = SetUnreadSnapshotStatus::kRedisError;
        result.error_message = "unread snapshot failed: redis pool is null";
        return result;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = SetUnreadSnapshotStatus::kRedisError;
        result.error_message = "unread snapshot failed: acquire redis connection";
        return result;
    }

    const auto script_result = connection->EvalInteger(
        kSetUnreadSnapshotScript,
        std::vector<std::string>{
            BuildPrivateKey(receiver_user_id, sender_user_id),
            BuildTotalKey(receiver_user_id),
        },
        std::vector<std::string>{
            std::to_string(private_unread),
            std::to_string(total_unread),
        }
    );
    if (!script_result.has_value() || script_result.value() != 1) {
        result.status = SetUnreadSnapshotStatus::kRedisError;
        result.error_message = connection->LastError();
        if (result.error_message.empty()) {
            result.error_message = "unread snapshot Redis EVAL failed";
        }
        return result;
    }

    result.status = SetUnreadSnapshotStatus::kApplied;
    return result;
}

ReplaceUnreadProjectionResult UnreadCountCache::ReplaceUserUnreadProjection(
    std::uint64_t receiver_user_id,
    const std::vector<std::pair<std::uint64_t, std::int64_t>>& peer_counts,
    std::int64_t total_unread
) {
    constexpr std::size_t kMaxPeersPerAtomicRebuild = 2048;
    ReplaceUnreadProjectionResult result;

    if (receiver_user_id == 0 || total_unread < 0) {
        result.status = ReplaceUnreadProjectionStatus::kInvalidArgument;
        result.error_message = "unread projection rebuild rejected invalid receiver/count";
        return result;
    }
    if (peer_counts.size() > kMaxPeersPerAtomicRebuild) {
        result.status = ReplaceUnreadProjectionStatus::kTooManyPeers;
        result.error_message = "unread projection rebuild exceeds 2048-peer atomic bound";
        return result;
    }

    std::unordered_set<std::uint64_t> seen;
    std::int64_t sum = 0;
    for (const auto& [peer_user_id, count] : peer_counts) {
        if (peer_user_id == 0 || peer_user_id == receiver_user_id ||
            count < 0 || !seen.insert(peer_user_id).second ||
            count > std::numeric_limits<std::int64_t>::max() - sum) {
            result.status = ReplaceUnreadProjectionStatus::kInvalidArgument;
            result.error_message = "unread projection rebuild contains invalid peer/count";
            return result;
        }
        sum += count;
    }
    if (sum != total_unread) {
        result.status = ReplaceUnreadProjectionStatus::kInvalidArgument;
        result.error_message = "unread projection rebuild total does not equal peer sum";
        return result;
    }
    if (pool_ == nullptr) {
        result.status = ReplaceUnreadProjectionStatus::kRedisError;
        result.error_message = "unread projection rebuild failed: redis pool is null";
        return result;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = ReplaceUnreadProjectionStatus::kRedisError;
        result.error_message = "unread projection rebuild failed: acquire redis connection";
        return result;
    }

    std::vector<std::string> keys;
    std::vector<std::string> args;
    keys.reserve(peer_counts.size() + 1);
    args.reserve(peer_counts.size() + 1);
    keys.push_back(BuildTotalKey(receiver_user_id));
    args.push_back(std::to_string(total_unread));
    for (const auto& [peer_user_id, count] : peer_counts) {
        keys.push_back(BuildPrivateKey(receiver_user_id, peer_user_id));
        args.push_back(std::to_string(count));
    }

    const auto script_result = connection->EvalInteger(
        kReplaceUserUnreadProjectionScript,
        keys,
        args
    );
    if (!script_result.has_value() || script_result.value() != 1) {
        result.status = ReplaceUnreadProjectionStatus::kRedisError;
        result.error_message = connection->LastError();
        if (result.error_message.empty()) {
            result.error_message = "unread projection rebuild Redis EVAL failed";
        }
        return result;
    }

    result.status = ReplaceUnreadProjectionStatus::kApplied;
    return result;
}

std::string UnreadCountCache::BuildPrivateKey(
    std::uint64_t receiver_user_id,
    std::uint64_t sender_user_id
) const {
    return key_prefix_ +
           "private:" +
           std::to_string(receiver_user_id) +
           ":" +
           std::to_string(sender_user_id);
}

std::string UnreadCountCache::BuildTotalKey(
    std::uint64_t receiver_user_id
) const {
    return key_prefix_ +
           "total:" +
           std::to_string(receiver_user_id);
}

std::string IncrementUnreadStatusToString(
    IncrementUnreadStatus status
) {
    switch (status) {
        case IncrementUnreadStatus::
            kIncremented:
            return "incremented";

        case IncrementUnreadStatus::
            kInvalidArgument:
            return "invalid_argument";

        case IncrementUnreadStatus::
            kInvalidValue:
            return "invalid_value";

        case IncrementUnreadStatus::
            kRedisError:
            return "redis_error";

        default:
            return "unknown";
    }
}

std::string GetUnreadCountStatusToString(
    GetUnreadCountStatus status
) {
    switch (status) {
        case GetUnreadCountStatus::
            kFound:
            return "found";

        case GetUnreadCountStatus::
            kNotFound:
            return "not_found";

        case GetUnreadCountStatus::
            kInvalidValue:
            return "invalid_value";

        case GetUnreadCountStatus::
            kInvalidArgument:
            return "invalid_argument";

        case GetUnreadCountStatus::
            kRedisError:
            return "redis_error";

        default:
            return "unknown";
    }
}

std::string EnsureUnreadProjectionStatusToString(
    EnsureUnreadProjectionStatus status
) {
    switch (status) {
        case EnsureUnreadProjectionStatus::kApplied:
            return "applied";
        case EnsureUnreadProjectionStatus::kAlreadyApplied:
            return "already_applied";
        case EnsureUnreadProjectionStatus::kIdentityConflict:
            return "identity_conflict";
        case EnsureUnreadProjectionStatus::kInvalidArgument:
            return "invalid_argument";
        case EnsureUnreadProjectionStatus::kInvalidValue:
            return "invalid_value";
        case EnsureUnreadProjectionStatus::kRedisError:
            return "redis_error";
    }
    return "unknown";
}

std::string ClearUnreadStatusToString(
    ClearUnreadStatus status
) {
    switch (status) {
        case ClearUnreadStatus::
            kCleared:
            return "cleared";

        case ClearUnreadStatus::
            kNotFound:
            return "not_found";

        case ClearUnreadStatus::
            kInvalidArgument:
            return "invalid_argument";

        case ClearUnreadStatus::
            kInvalidValue:
            return "invalid_value";

        case ClearUnreadStatus::
            kRedisError:
            return "redis_error";

        default:
            return "unknown";
    }
}


std::string SetUnreadSnapshotStatusToString(
    SetUnreadSnapshotStatus status
) {
    switch (status) {
        case SetUnreadSnapshotStatus::kApplied:
            return "applied";
        case SetUnreadSnapshotStatus::kInvalidArgument:
            return "invalid_argument";
        case SetUnreadSnapshotStatus::kRedisError:
            return "redis_error";
    }
    return "unknown";
}

std::string ReplaceUnreadProjectionStatusToString(
    ReplaceUnreadProjectionStatus status
) {
    switch (status) {
        case ReplaceUnreadProjectionStatus::kApplied:
            return "applied";
        case ReplaceUnreadProjectionStatus::kInvalidArgument:
            return "invalid_argument";
        case ReplaceUnreadProjectionStatus::kTooManyPeers:
            return "too_many_peers";
        case ReplaceUnreadProjectionStatus::kRedisError:
            return "redis_error";
    }
    return "unknown";
}


}  // namespace tinyimx