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

class OnlineStatusCache {
public:
    explicit OnlineStatusCache(RedisConnectionPool* pool,
                               std::string key_prefix = "tinyimx:online:");

    OnlineStatusCache(const OnlineStatusCache&) = delete;
    OnlineStatusCache& operator=(const OnlineStatusCache&) = delete;

    bool SetOnline(std::uint64_t user_id,
                   const std::string& gateway_id,
                   const std::string& connection_name,
                   int ttl_seconds);

    bool SetOffline(std::uint64_t user_id);

    bool RefreshOnline(std::uint64_t user_id,
                       int ttl_seconds);

    std::optional<OnlineStatusRecord> GetOnlineStatus(
        std::uint64_t user_id
    );

    bool IsOnline(std::uint64_t user_id);

    const std::string& LastError() const;

private:
    std::string BuildKey(std::uint64_t user_id) const;

    std::string Serialize(const OnlineStatusRecord& record) const;

    std::optional<OnlineStatusRecord> Deserialize(
        const std::string& value
    );

    void SetError(const std::string& error_message);

private:
    RedisConnectionPool* pool_{nullptr};
    std::string key_prefix_;
    std::string last_error_;
};

}  // namespace tinyimx