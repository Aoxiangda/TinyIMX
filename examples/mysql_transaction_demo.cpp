#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

bool QueryDemoRowCount(
    tinyimx::MySqlConnection* connection,
    std::uint64_t* count
) {
    if (connection == nullptr || count == nullptr) {
        return false;
    }

    tinyimx::MySqlQueryResult result;

    if (!connection->Query(
            "SELECT COUNT(*) "
            "FROM tinyimx_transaction_demo",
            &result
        )) {
        std::cerr
            << "query demo row count failed: "
            << connection->LastError()
            << '\n';

        return false;
    }

    if (result.rows.size() != 1 ||
        result.rows.front().size() != 1) {
        std::cerr
            << "unexpected count query result\n";

        return false;
    }

    try {
        *count = static_cast<std::uint64_t>(
            std::stoull(result.rows.front().front())
        );
    } catch (const std::exception& e) {
        std::cerr
            << "parse row count failed: "
            << e.what()
            << '\n';

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
        std::cerr
            << "load config failed: "
            << config.LastError()
            << '\n';

        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(
            config.Logger()
        )) {
        std::cerr << "logger init failed\n";
        return 1;
    }

    if (!config.MySql().enable) {
        std::cerr << "mysql is disabled in config\n";

        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::MySqlConnectionPool pool;

    if (!pool.Initialize(config.MySql())) {
        std::cerr << "mysql pool init failed\n";

        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    std::cout
        << "========== MySQL Transaction Demo ==========\n";

    bool ok = true;

    {
        auto connection = pool.Acquire();

        if (!connection) {
            std::cerr
                << "acquire mysql connection failed\n";

            ok = false;
        }

        if (ok &&
            !connection->Execute(
                "DROP TEMPORARY TABLE IF EXISTS "
                "tinyimx_transaction_demo"
            )) {
            std::cerr
                << "drop temporary table failed: "
                << connection->LastError()
                << '\n';

            ok = false;
        }

        if (ok &&
            !connection->Execute(
                "CREATE TEMPORARY TABLE "
                "tinyimx_transaction_demo ("
                "id BIGINT UNSIGNED NOT NULL PRIMARY KEY, "
                "note VARCHAR(64) NOT NULL"
                ") ENGINE=InnoDB"
            )) {
            std::cerr
                << "create temporary table failed: "
                << connection->LastError()
                << '\n';

            ok = false;
        }

        // 测试一：Rollback后数据必须消失。
        if (ok &&
            !connection->BeginTransaction()) {
            std::cerr
                << "begin rollback transaction failed: "
                << connection->LastError()
                << '\n';

            ok = false;
        }

        if (ok && !connection->InTransaction()) {
            std::cerr
                << "transaction should be active "
                << "after BeginTransaction\n";

            ok = false;
        }

        if (ok &&
            !connection->Execute(
                "INSERT INTO tinyimx_transaction_demo "
                "(id, note) VALUES "
                "(1, 'rollback_case')"
            )) {
            std::cerr
                << "insert rollback row failed: "
                << connection->LastError()
                << '\n';

            ok = false;
        }

        if (ok && !connection->Rollback()) {
            std::cerr
                << "rollback failed: "
                << connection->LastError()
                << '\n';

            ok = false;
        }

        if (ok && connection->InTransaction()) {
            std::cerr
                << "transaction should be inactive "
                << "after Rollback\n";

            ok = false;
        }

        std::uint64_t rollback_count = 0;

        if (ok &&
            !QueryDemoRowCount(
                connection.operator->(),
                &rollback_count
            )) {
            ok = false;
        }

        if (ok) {
            std::cout
                << "rollback_count="
                << rollback_count
                << '\n';

            if (rollback_count != 0) {
                std::cerr
                    << "rollback validation failed, "
                    << "expected 0 rows\n";

                ok = false;
            }
        }

        // 测试二：Commit后数据必须保留。
        if (ok &&
            !connection->BeginTransaction()) {
            std::cerr
                << "begin commit transaction failed: "
                << connection->LastError()
                << '\n';

            ok = false;
        }

        if (ok &&
            !connection->Execute(
                "INSERT INTO tinyimx_transaction_demo "
                "(id, note) VALUES "
                "(2, 'commit_case')"
            )) {
            std::cerr
                << "insert commit row failed: "
                << connection->LastError()
                << '\n';

            ok = false;
        }

        if (ok && !connection->Commit()) {
            std::cerr
                << "commit failed: "
                << connection->LastError()
                << '\n';

            ok = false;
        }

        if (ok && connection->InTransaction()) {
            std::cerr
                << "transaction should be inactive "
                << "after Commit\n";

            ok = false;
        }

        std::uint64_t commit_count = 0;

        if (ok &&
            !QueryDemoRowCount(
                connection.operator->(),
                &commit_count
            )) {
            ok = false;
        }

        if (ok) {
            std::cout
                << "commit_count="
                << commit_count
                << '\n';

            if (commit_count != 1) {
                std::cerr
                    << "commit validation failed, "
                    << "expected 1 row\n";

                ok = false;
            }
        }

        if (connection &&
            connection->InTransaction()) {
            connection->Rollback();
        }

        if (connection) {
            connection->Execute(
                "DROP TEMPORARY TABLE IF EXISTS "
                "tinyimx_transaction_demo"
            );
        }
    }

    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    if (!ok) {
        std::cerr
            << "mysql transaction validation failed\n";

        return 1;
    }

    std::cout
        << "mysql transaction validation passed\n"
        << "============================================\n";

    return 0;
}