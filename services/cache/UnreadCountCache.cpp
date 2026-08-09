#include "services/cache/UnreadCountCache.h"

#include "common/logging/LogMacros.h"

#include <charconv>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

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


}  // namespace tinyimx