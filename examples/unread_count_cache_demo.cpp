#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/cache/UnreadCountCache.h"

#include <iostream>
#include <string>

namespace {

bool ExpectCount(const std::string& name,
                 const std::optional<std::int64_t>& value,
                 std::int64_t expected) {
    if (!value.has_value()) {
        std::cerr << name << " failed\n";
        return false;
    }

    std::cout << name << " = " << value.value() << '\n';

    if (value.value() != expected) {
        std::cerr << name << " expected "
                  << expected << ", got "
                  << value.value() << '\n';
        return false;
    }

    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;

    if (!config.LoadFromFile(config_path)) {
        std::cerr << "load config failed: "
                  << config.LastError() << '\n';
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "logger init failed\n";
        return 1;
    }

    if (!config.Redis().enable) {
        std::cerr << "redis is disabled in config\n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::RedisConnectionPool redis_pool;

    if (!redis_pool.Initialize(config.Redis())) {
        std::cerr << "redis pool init failed\n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const std::uint64_t receiver = 10002;
    const std::uint64_t sender_a = 10001;
    const std::uint64_t sender_b = 10003;

    {
        auto connection = redis_pool.Acquire();
        if (!connection) {
            std::cerr << "acquire redis connection failed\n";
            redis_pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        connection->Del("tinyimx:demo:unread:private:10002:10001");
        connection->Del("tinyimx:demo:unread:private:10002:10003");
        connection->Del("tinyimx:demo:unread:total:10002");
    }

    tinyimx::UnreadCountCache unread_cache(
        &redis_pool,
        "tinyimx:demo:unread:"
    );

    std::cout << "========== Unread Count Cache Demo ==========\n";

    if (!ExpectCount(
            "count_after_a_1",
            unread_cache.IncrementPrivateUnread(receiver, sender_a),
            1)) {
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!ExpectCount(
            "count_after_a_2",
            unread_cache.IncrementPrivateUnread(receiver, sender_a),
            2)) {
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!ExpectCount(
            "count_after_b_1",
            unread_cache.IncrementPrivateUnread(receiver, sender_b),
            1)) {
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!ExpectCount(
            "private_unread_from_a",
            unread_cache.GetPrivateUnread(receiver, sender_a),
            2)) {
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!ExpectCount(
            "private_unread_from_b",
            unread_cache.GetPrivateUnread(receiver, sender_b),
            1)) {
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!ExpectCount(
            "total_unread",
            unread_cache.GetTotalUnread(receiver),
            3)) {
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!unread_cache.ClearPrivateUnread(receiver, sender_a)) {
        std::cerr << "clear private unread failed: "
                  << unread_cache.LastError() << '\n';
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!ExpectCount(
            "private_unread_from_a_after_clear",
            unread_cache.GetPrivateUnread(receiver, sender_a),
            0)) {
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!ExpectCount(
            "total_unread_after_clear_a",
            unread_cache.GetTotalUnread(receiver),
            1)) {
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!unread_cache.ClearPrivateUnread(receiver, sender_b)) {
        std::cerr << "clear private unread b failed: "
                  << unread_cache.LastError() << '\n';
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!ExpectCount(
            "total_unread_after_clear_all",
            unread_cache.GetTotalUnread(receiver),
            0)) {
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    redis_pool.Shutdown();

    std::cout << "Unread count cache demo finished\n";
    std::cout << "============================================\n";

    tinyimx::Logger::Instance().Shutdown();

    return 0;
}