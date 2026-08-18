#pragma once

#include "common/config/ConfigTypes.h"

#include <hiredis/hiredis.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tinyimx {

class RedisConnection {
public:
    RedisConnection() = default;
    ~RedisConnection();

    RedisConnection(
        const RedisConnection&
    ) = delete;

    RedisConnection& operator=(
        const RedisConnection&
    ) = delete;

    bool Connect(
        const RedisConfig& config
    );

    void Close();

    bool IsConnected() const;

    bool Ping();

    bool Set(
        const std::string& key,
        const std::string& value
    );

    bool SetEx(
        const std::string& key,
        const std::string& value,
        int ttl_seconds
    );

    std::optional<std::string>
    Get(
        const std::string& key
    );

    bool Del(
        const std::string& key
    );

    std::optional<std::int64_t>
    Incr(
        const std::string& key
    );

    std::optional<std::int64_t>
    DecrBy(
        const std::string& key,
        std::int64_t delta
    );

    bool Expire(
        const std::string& key,
        int ttl_seconds
    );

    bool SAdd(
        const std::string& key,
        const std::string& member
    );

    bool SRem(
        const std::string& key,
        const std::string& member
    );

    std::optional<std::vector<std::string>>
    SMembers(
        const std::string& key
    );

    std::optional<std::int64_t>
    EvalInteger(
        const std::string& script,
        const std::vector<std::string>& keys,
        const std::vector<std::string>& arguments
    );

    const std::string&
    LastError() const;

private:
    redisReply* Command(
        const std::vector<std::string>& args
    );

    bool CheckStatusReply(
        redisReply* reply,
        const std::string& expected_status
    );

    void SetError(
        const std::string& error_message
    );

    void SetRedisError(
        const std::string& prefix
    );

private:
    redisContext* context_{nullptr};

    bool connected_{false};

    std::string last_error_;
};

}  // namespace tinyimx