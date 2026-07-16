#pragma once

#include "common/cache/RedisConnectionPool.h"

#include <cstdint>
#include <optional>
#include <string>

namespace tinyimx {

class UnreadCountCache {
public:
    explicit UnreadCountCache(
        RedisConnectionPool* pool,
        std::string key_prefix = "tinyimx:unread:"
    );

    UnreadCountCache(const UnreadCountCache&) = delete;
    UnreadCountCache& operator=(const UnreadCountCache&) = delete;

    std::optional<std::int64_t> IncrementPrivateUnread(
        std::uint64_t receiver_user_id,
        std::uint64_t sender_user_id
    );

    std::optional<std::int64_t> GetPrivateUnread(
        std::uint64_t receiver_user_id,
        std::uint64_t sender_user_id
    );

    std::optional<std::int64_t> GetTotalUnread(
        std::uint64_t receiver_user_id
    );

    bool ClearPrivateUnread(
        std::uint64_t receiver_user_id,
        std::uint64_t sender_user_id
    );

    const std::string& LastError() const;

private:
    bool ValidateUserId(std::uint64_t user_id,
                        const std::string& action);

    bool ValidateUserPair(std::uint64_t receiver_user_id,
                          std::uint64_t sender_user_id,
                          const std::string& action);

    std::string BuildPrivateKey(std::uint64_t receiver_user_id,
                                std::uint64_t sender_user_id) const;

    std::string BuildTotalKey(std::uint64_t receiver_user_id) const;

    std::optional<std::int64_t> ParseCount(
        const std::optional<std::string>& value,
        const std::string& action
    );

    void SetError(const std::string& error_message);

private:
    RedisConnectionPool* pool_{nullptr};
    std::string key_prefix_;
    std::string last_error_;
};

}  // namespace tinyimx