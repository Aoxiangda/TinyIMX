#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/cache/UnreadCountCache.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string config_path = "config/gateway-a.local.json";
    if (argc >= 2) config_path = argv[1];

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path) || !config.Redis().enable) {
        std::cerr << "[FAIL] valid Redis config required: " << config.LastError() << '\n';
        return 1;
    }
    if (!tinyimx::Logger::Instance().Init(config.Logger())) return 1;

    tinyimx::RedisConnectionPool pool;
    if (!pool.Initialize(config.Redis())) return 1;
    tinyimx::UnreadCountCache cache(&pool, "tinyimx:m16b:test:unread:");

    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    const std::uint64_t receiver = 800000000ULL + static_cast<std::uint64_t>(nanos % 1000000);
    const std::uint64_t peer_a = receiver + 1;
    const std::uint64_t peer_b = receiver + 2;

    const auto set = cache.SetUnreadSnapshot(receiver, peer_a, 3, 5);
    if (!set.Succeeded()) {
        std::cerr << "[FAIL] exact unread snapshot: " << set.error_message << '\n';
        return 1;
    }
    const auto private_a = cache.GetPrivateUnread(receiver, peer_a);
    const auto total = cache.GetTotalUnread(receiver);
    if (!private_a.Completed() || private_a.count != 3 ||
        !total.Completed() || total.count != 5) {
        std::cerr << "[FAIL] exact snapshot Redis values mismatch\n";
        return 1;
    }
    std::cout << "[PASS] exact dialog+total snapshot applied atomically\n";

    const std::vector<std::pair<std::uint64_t, std::int64_t>> peers{
        {peer_a, 0},
        {peer_b, 2},
    };
    const auto rebuilt = cache.ReplaceUserUnreadProjection(receiver, peers, 2);
    if (!rebuilt.Succeeded()) {
        std::cerr << "[FAIL] rebuild: " << rebuilt.error_message << '\n';
        return 1;
    }
    const auto after_a = cache.GetPrivateUnread(receiver, peer_a);
    const auto after_b = cache.GetPrivateUnread(receiver, peer_b);
    const auto after_total = cache.GetTotalUnread(receiver);
    if (!after_a.Completed() || after_a.count != 0 ||
        !after_b.Completed() || after_b.count != 2 ||
        !after_total.Completed() || after_total.count != 2) {
        std::cerr << "[FAIL] rebuild did not replace legacy projection\n";
        return 1;
    }
    std::cout << "[PASS] user projection rebuild deletes zero peers and sets durable snapshot\n";

    const std::vector<std::pair<std::uint64_t, std::int64_t>> cleanup{
        {peer_a, 0}, {peer_b, 0}
    };
    (void)cache.ReplaceUserUnreadProjection(receiver, cleanup, 0);
    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();
    return 0;
}
