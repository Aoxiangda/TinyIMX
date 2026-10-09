#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "common/logging/LogMacros.h"

#include <iostream>
#include <string>

namespace {

void PrintQueryResult(const tinyimx::MySqlQueryResult& result) {
    for (const auto& field : result.fields) {
        std::cout << field << '\t';
    }

    std::cout << '\n';

    for (const auto& row : result.rows) {
        for (const auto& value : row) {
            std::cout << value << '\t';
        }

        std::cout << '\n';
    }
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

    std::cout << "========== MySQL Pool Demo ==========\n";
    std::cout << "pool_size = " << pool.Size() << '\n';
    std::cout << "available = " << pool.AvailableCount() << '\n';

    {
        auto connection = pool.Acquire();

        if (!connection) {
            std::cerr << "acquire mysql connection failed\n";
            pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        tinyimx::MySqlQueryResult result;

        const std::string sql =
            "SELECT user_id, username, nickname, status "
            "FROM im_users ORDER BY user_id";

        if (!connection->Query(sql, &result)) {
            std::cerr << "query failed: "
                      << connection->LastError() << '\n';
            pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        PrintQueryResult(result);

        std::cout << "row_count = "
                  << result.RowCount() << '\n';
    }

    std::cout << "available_after_release = "
              << pool.AvailableCount() << '\n';

    pool.Shutdown();

    std::cout << "MySQL pool demo finished\n";
    std::cout << "=====================================\n";

    tinyimx::Logger::Instance().Shutdown();

    return 0;
}