#include "services/cache/UnreadCountCache.h"

#include "common/logging/LogMacros.h"

#include <stdexcept>
#include <utility>

namespace tinyimx {

UnreadCountCache::UnreadCountCache(
    RedisConnectionPool* pool,
    std::string key_prefix
)
    : pool_(pool),
      key_prefix_(std::move(key_prefix)) {}

std::optional<std::int64_t>
UnreadCountCache::IncrementPrivateUnread(
    std::uint64_t receiver_user_id,
    std::uint64_t sender_user_id
) {
    const std::string action = "increment private unread";

    if (!ValidateUserPair(receiver_user_id, sender_user_id, action)) {
        return std::nullopt;
    }

    if (pool_ == nullptr) {
        SetError("unread count " + action + " failed: redis pool is null");
        return std::nullopt;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("unread count " + action +
                 " failed: acquire redis connection failed");
        return std::nullopt;
    }

    const std::string private_key =
        BuildPrivateKey(receiver_user_id, sender_user_id);

    const std::string total_key =
        BuildTotalKey(receiver_user_id);

    const auto private_count = connection->Incr(private_key);
    if (!private_count.has_value()) {
        SetError(connection->LastError());
        return std::nullopt;
    }

    const auto total_count = connection->Incr(total_key);
    if (!total_count.has_value()) {
        SetError(connection->LastError());
        return std::nullopt;
    }

    LOG_INFO("unread count incremented"
             << ", receiver=" << receiver_user_id
             << ", sender=" << sender_user_id
             << ", private_count=" << private_count.value()
             << ", total_count=" << total_count.value());

    last_error_.clear();
    return private_count;
}

std::optional<std::int64_t>UnreadCountCache::GetPrivateUnread(
    std::uint64_t receiver_user_id,
    std::uint64_t sender_user_id
) {
    const std::string action = "get private unread";

    if (!ValidateUserPair(receiver_user_id, sender_user_id, action)) {
        return std::nullopt;
    }

    if (pool_ == nullptr) {
        SetError("unread count " + action + " failed: redis pool is null");
        return std::nullopt;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("unread count " + action +
                 " failed: acquire redis connection failed");
        return std::nullopt;
    }

    const auto value = connection->Get(
        BuildPrivateKey(receiver_user_id, sender_user_id)
    );

    return ParseCount(value, action);
}

std::optional<std::int64_t>
UnreadCountCache::GetTotalUnread(
    std::uint64_t receiver_user_id
) {
    const std::string action = "get total unread";

    if (!ValidateUserId(receiver_user_id, action)) {
        return std::nullopt;
    }

    if (pool_ == nullptr) {
        SetError("unread count " + action + " failed: redis pool is null");
        return std::nullopt;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("unread count " + action +
                 " failed: acquire redis connection failed");
        return std::nullopt;
    }

    const auto value = connection->Get(
        BuildTotalKey(receiver_user_id)
    );

    return ParseCount(value, action);
}

bool UnreadCountCache::ClearPrivateUnread(
    std::uint64_t receiver_user_id,
    std::uint64_t sender_user_id
) {
    const std::string action = "clear private unread";

    if (!ValidateUserPair(receiver_user_id, sender_user_id, action)) {
        return false;
    }

    if (pool_ == nullptr) {
        SetError("unread count " + action + " failed: redis pool is null");
        return false;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("unread count " + action +
                 " failed: acquire redis connection failed");
        return false;
    }

    const std::string private_key =
        BuildPrivateKey(receiver_user_id, sender_user_id);

    const std::string total_key =
        BuildTotalKey(receiver_user_id);

    const auto private_value = connection->Get(private_key);
    const auto private_count = ParseCount(private_value, action);

    if (!private_count.has_value()) {
        return false;
    }

    if (private_count.value() <= 0) {
        connection->Del(private_key);
        last_error_.clear();
        return true;
    }

    if (!connection->Del(private_key)) {
        SetError(connection->LastError());
        return false;
    }

    const auto total_after_decr =
        connection->DecrBy(total_key, private_count.value());

    if (!total_after_decr.has_value()) {
        SetError(connection->LastError());
        return false;
    }

    if (total_after_decr.value() < 0) {
        connection->Set(total_key, "0");
    }

    LOG_INFO("unread count cleared"
             << ", receiver=" << receiver_user_id
             << ", sender=" << sender_user_id
             << ", cleared_count=" << private_count.value()
             << ", total_after_decr=" << total_after_decr.value());

    last_error_.clear();
    return true;
}

const std::string& UnreadCountCache::LastError() const {
    return last_error_;
}

bool UnreadCountCache::ValidateUserId(
    std::uint64_t user_id,
    const std::string& action
) {
    if (user_id == 0) {
        SetError("unread count " + action +
                 " failed: invalid user_id");
        return false;
    }

    return true;
}

bool UnreadCountCache::ValidateUserPair(
    std::uint64_t receiver_user_id,
    std::uint64_t sender_user_id,
    const std::string& action
) {
    if (receiver_user_id == 0 || sender_user_id == 0) {
        SetError("unread count " + action +
                 " failed: invalid user id");
        return false;
    }

    if (receiver_user_id == sender_user_id) {
        SetError("unread count " + action +
                 " failed: receiver equals sender");
        return false;
    }

    return true;
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

std::optional<std::int64_t> UnreadCountCache::ParseCount(
    const std::optional<std::string>& value,
    const std::string& action
) {
    if (!value.has_value()) {
        last_error_.clear();
        return 0;
    }

    try {
        const auto count = std::stoll(value.value());

        if (count < 0) {
            SetError("unread count " + action +
                     " failed: negative count");
            return std::nullopt;
        }

        last_error_.clear();
        return static_cast<std::int64_t>(count);
    } catch (const std::exception& e) {
        SetError("unread count " + action +
                 " failed: parse count error: " + e.what());
        return std::nullopt;
    }
}

void UnreadCountCache::SetError(
    const std::string& error_message
) {
    last_error_ = error_message;
    LOG_ERROR(error_message);
}

}  // namespace tinyimx