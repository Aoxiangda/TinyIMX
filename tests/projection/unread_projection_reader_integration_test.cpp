#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/projection/unread/UnreadProjectionReader.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {
bool QueryCount(
    tinyimx::MySqlConnectionPool* pool,
    const std::string& sql,
    std::int64_t* count
) {
    auto connection = pool->Acquire();
    if (!connection) return false;
    tinyimx::MySqlQueryResult result;
    if (!connection->Query(sql, &result) || result.rows.size() != 1 ||
        result.rows.front().size() != 1) return false;
    try {
        *count = std::stoll(result.rows.front().front());
        return true;
    } catch (...) {
        return false;
    }
}
}

int main(int argc, char** argv) {
    std::string config_path = "config/gateway-a.local.json";
    if (argc >= 2) config_path = argv[1];

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path) || !config.MySql().enable) {
        std::cerr << "[FAIL] valid MySQL config required: " << config.LastError() << '\n';
        return 1;
    }
    if (!tinyimx::Logger::Instance().Init(config.Logger())) return 1;

    tinyimx::MySqlConnectionPool pool;
    if (!pool.Initialize(config.MySql())) return 1;
    tinyimx::projection::unread::UnreadProjectionReader reader(&pool);

    constexpr std::uint64_t receiver = 10002;
    constexpr std::uint64_t peer = 10001;
    const auto dialog = reader.LoadDialogSnapshot(receiver, peer);
    if (!dialog.success) {
        std::cerr << "[FAIL] dialog snapshot: " << dialog.message << '\n';
        return 1;
    }

    std::int64_t expected_private = 0;
    std::int64_t expected_total = 0;
    if (!QueryCount(
            &pool,
            "SELECT COUNT(*) FROM im_private_messages WHERE to_user_id=10002 "
            "AND from_user_id=10001 AND delivery_status IN (0,1)",
            &expected_private) ||
        !QueryCount(
            &pool,
            "SELECT COUNT(*) FROM im_private_messages WHERE to_user_id=10002 "
            "AND delivery_status IN (0,1)",
            &expected_total)) {
        std::cerr << "[FAIL] direct authoritative count query\n";
        return 1;
    }

    if (dialog.value.private_unread != expected_private ||
        dialog.value.total_unread != expected_total) {
        std::cerr << "[FAIL] dialog snapshot differs from direct MySQL truth\n";
        return 1;
    }
    std::cout << "[PASS] dialog unread snapshot matches MySQL authoritative truth\n";

    const auto user = reader.LoadUserSnapshot(receiver);
    if (!user.success || user.value.total_unread != expected_total) {
        std::cerr << "[FAIL] user snapshot: " << user.message << '\n';
        return 1;
    }
    std::int64_t sum = 0;
    for (const auto& [unused_peer, count] : user.value.peer_counts) {
        (void)unused_peer;
        sum += count;
    }
    if (sum != user.value.total_unread) {
        std::cerr << "[FAIL] user projection peer sum inconsistent\n";
        return 1;
    }
    std::cout << "[PASS] user rebuild snapshot includes all peers and consistent total\n";

    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();
    return 0;
}
