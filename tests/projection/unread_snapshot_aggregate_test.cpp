#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/projection/unread/UnreadProjectionReader.h"

#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {
void Expect(bool condition, const char* label) {
    if (!condition) throw std::runtime_error(label);
    std::cout << "[PASS] " << label << '\n';
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 64;
    try {
        tinyimx::Config config;
        if (!config.LoadFromFile(argv[1]) || !config.MySql().enable) return 65;
        auto mysql = config.MySql();
        mysql.pool_size = 1; // Every lease uses the same private temp-table session.
        tinyimx::MySqlConnectionPool pool;
        Expect(pool.Initialize(mysql), "Owned MySQL connection initialized");
        {
            auto connection = pool.Acquire();
            Expect(static_cast<bool>(connection), "Fixture lease acquired");
            // TEMPORARY shadows this name only in this connection. Real table
            // rows, indexes and other service connections are never modified.
            Expect(connection->Execute(
                "CREATE TEMPORARY TABLE im_private_messages ("
                "from_user_id BIGINT UNSIGNED NOT NULL,"
                "to_user_id BIGINT UNSIGNED NOT NULL,"
                "delivery_status TINYINT UNSIGNED NOT NULL) ENGINE=InnoDB"),
                "Connection-local shadow created");
            Expect(connection->Execute(
                "INSERT INTO im_private_messages VALUES "
                "(1,10,0),(1,10,1),(1,10,2),(1,10,3),"
                "(2,10,0),(2,10,1),(3,10,2),(3,10,3),(1,11,0)"),
                "Only private temp fixture inserted");
        }
        tinyimx::projection::unread::UnreadProjectionReader reader(&pool);
        auto dialog = reader.LoadDialogSnapshot(10, 1);
        Expect(dialog.success && dialog.value.private_unread == 2 &&
               dialog.value.total_unread == 4, "Mixed states and distinct peers counted");
        dialog = reader.LoadDialogSnapshot(10, 3);
        Expect(dialog.success && dialog.value.private_unread == 0 &&
               dialog.value.total_unread == 4, "Read and failed peer yields zero, total remains");
        dialog = reader.LoadDialogSnapshot(12, 1);
        Expect(dialog.success && dialog.value.private_unread == 0 &&
               dialog.value.total_unread == 0, "Empty receiver returns two numeric zeros");
        dialog = reader.LoadDialogSnapshot(11, 1);
        Expect(dialog.success && dialog.value.private_unread == 1 &&
               dialog.value.total_unread == 1, "Recipient scopes do not leak counts");
        const auto user = reader.LoadUserSnapshot(10);
        const std::map<std::uint64_t, std::int64_t> counts(
            user.value.peer_counts.begin(), user.value.peer_counts.end());
        Expect(user.success && user.value.total_unread == 4 &&
               counts == std::map<std::uint64_t,std::int64_t>{{1,2},{2,2},{3,0}},
               "Grouped snapshot retains zero-unread historical peer");
        const auto empty = reader.LoadUserSnapshot(12);
        Expect(empty.success && empty.value.total_unread == 0 &&
               empty.value.peer_counts.empty(), "Empty user snapshot is consistent");
        Expect(!reader.LoadDialogSnapshot(0, 1).success &&
               !reader.LoadDialogSnapshot(10, 0).success &&
               !reader.LoadDialogSnapshot(10, 10).success &&
               !reader.LoadUserSnapshot(0).success, "Invalid identities rejected");
        {
            auto connection = pool.Acquire();
            Expect(connection && !connection->InTransaction(),
                   "Read leaves pooled connection without a transaction");
        }
        pool.Shutdown(); // Closing this owned connection drops only its temp table.
        Expect(!reader.LoadDialogSnapshot(10, 1).success &&
               !reader.LoadUserSnapshot(10).success, "Unavailable pool is not empty success");
        tinyimx::projection::unread::UnreadProjectionReader absent(nullptr);
        Expect(!absent.LoadDialogSnapshot(10, 1).success,
               "Missing repository connection rejected");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
