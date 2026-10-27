#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/cache/UnreadCountCache.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
void Require(bool value, const char* error) {
    if (!value) throw std::runtime_error(error);
}
}

// Explicit newly-owned test-cache identity is required. This target is not in
// default ctest; neither FLUSHDB nor existing application keys are touched.
int main(int argc, char** argv) {
    if (argc != 3 || std::string(argv[2]) != "--owned-perf1231-test-cache") return 2;
    tinyimx::Config config;
    if (!config.LoadFromFile(argv[1]) || config.Redis().host != "codex-perf1231-test-redis" ||
        config.Redis().db != 0) return 2;
    if (!tinyimx::Logger::Instance().Init(config.Logger())) return 2;
    tinyimx::RedisConnectionPool pool;
    if (!pool.Initialize(config.Redis())) return 2;
    try {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::string prefix = "owned:pair-contract:" + std::to_string(nonce) + ":";
        tinyimx::UnreadCountCache cache(&pool, prefix);
        tinyimx::UnreadCountCache no_pool(nullptr, prefix);
        using Status = tinyimx::GetUnreadCountStatus;
        for (const auto pair : {std::pair{0ULL, 2ULL}, {1ULL, 0ULL}, {1ULL, 1ULL}}) {
            const auto value = cache.GetPrivateAndTotalUnread(pair.first, pair.second);
            Require(value.private_unread.status == Status::kInvalidArgument &&
                    value.total_unread.status == Status::kInvalidArgument, "invalid identity accepted");
        }
        const auto null_pool = no_pool.GetPrivateAndTotalUnread(1, 2);
        Require(null_pool.private_unread.status == Status::kRedisError &&
                null_pool.total_unread.status == Status::kRedisError, "null pool accepted");
        auto missing = cache.GetPrivateAndTotalUnread(1, 2);
        Require(missing.private_unread.NotFound() && missing.total_unread.NotFound(), "missing keys changed meaning");

        // Real wire replies must preserve every int64 bit, including >2^53.
        const auto maximum = std::numeric_limits<std::int64_t>::max();
        Require(cache.SetUnreadSnapshot(1, 2, maximum - 1, maximum).Succeeded(), "set int64 snapshot failed");
        auto full = cache.GetPrivateAndTotalUnread(1, 2);
        Require(full.private_unread.Found() && full.private_unread.count == maximum - 1 &&
                full.total_unread.Found() && full.total_unread.count == maximum, "int64 snapshot rounded");
        const auto private_key = prefix + "private:1:2";
        const auto total_key = prefix + "total:1";
        // Resolve the exact storage keys via a real known-value check before
        // injecting corruption; a naming error must fail this test.
        {
            auto connection = pool.Acquire(); Require(bool(connection), "raw connection unavailable");
            Require(connection->Get(private_key) == std::to_string(maximum - 1) &&
                    connection->Get(total_key) == std::to_string(maximum), "storage key fixture differs");
            Require(connection->Set(private_key, "9223372036854775808"), "overflow fixture failed");
        }
        auto corrupt = cache.GetPrivateAndTotalUnread(1, 2);
        Require(corrupt.private_unread.status == Status::kInvalidValue &&
                corrupt.total_unread.Found() && corrupt.total_unread.count == maximum, "corruption hid healthy total");
        {
            auto connection = pool.Acquire(); Require(bool(connection), "raw connection unavailable");
            Require(connection->Del(private_key) && connection->SAdd(private_key, "wrong-type"), "wrong type fixture failed");
        }
        auto wrong_type = cache.GetPrivateAndTotalUnread(1, 2);
        Require(wrong_type.private_unread.status == Status::kRedisError &&
                wrong_type.total_unread.Found(), "WRONGTYPE converted to missing or hid total");
        Require(cache.SetUnreadSnapshot(1, 2, 0, 9).Succeeded(), "mixed missing fixture failed");
        auto mixed = cache.GetPrivateAndTotalUnread(1, 2);
        Require(mixed.private_unread.NotFound() && mixed.total_unread.count == 9, "mixed missing snapshot differs");
        Require(cache.SetUnreadSnapshot(1, 2, 2, 7).Succeeded(), "initial concurrent snapshot failed");
        std::atomic<bool> failed{false};
        std::thread writer([&] {
            for (int i = 0; i < 1000; ++i) {
                const auto x = i % 2 ? 2 : 11;
                const auto y = i % 2 ? 7 : 19;
                if (!cache.SetUnreadSnapshot(1, 2, x, y).Succeeded()) failed.store(true);
            }
        });
        for (int i = 0; i < 1000; ++i) {
            const auto value = cache.GetPrivateAndTotalUnread(1, 2);
            const auto x = value.private_unread.count, y = value.total_unread.count;
            if (!value.private_unread.Found() || !value.total_unread.Found() ||
                !((x == 2 && y == 7) || (x == 11 && y == 19))) failed.store(true);
        }
        writer.join();
        Require(!failed.load(), "atomic reader returned a torn pair under concurrent snapshot writer");
        // Independent scalar calls are the value/status oracle when quiescent.
        const auto last = cache.GetPrivateAndTotalUnread(1, 2);
        Require(last.private_unread.status == cache.GetPrivateUnread(1, 2).status &&
                last.private_unread.count == cache.GetPrivateUnread(1, 2).count &&
                last.total_unread.status == cache.GetTotalUnread(1).status &&
                last.total_unread.count == cache.GetTotalUnread(1).count, "scalar parity failed");
        pool.Shutdown(); tinyimx::Logger::Instance().Shutdown();
        std::cout << "[PASS] unread pair real Redis: missing, int64, corruption, WRONGTYPE, scalar parity and1000 concurrent snapshots\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        pool.Shutdown(); tinyimx::Logger::Instance().Shutdown(); return 1;
    }
}
