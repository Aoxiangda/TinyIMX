#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "common/security/PasswordHasher.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

bool UpdateUserPassword(tinyimx::MySqlConnection* connection,
                        tinyimx::PasswordHasher* hasher,
                        std::uint64_t user_id,
                        const std::string& password) {
    if (connection == nullptr || hasher == nullptr) {
        std::cerr << "invalid connection or hasher\n";
        return false;
    }

    const std::string salt = hasher->GenerateSaltHex();
    const std::string hash = hasher->HashPassword(password, salt);

    const std::string escaped_salt =
        connection->EscapeString(salt);

    const std::string escaped_hash =
        connection->EscapeString(hash);

    const std::string sql =
        "UPDATE im_users "
        "SET password_salt = '" + escaped_salt + "', "
        "password_hash = '" + escaped_hash + "' "
        "WHERE user_id = " + std::to_string(user_id);

    if (!connection->Execute(sql)) {
        std::cerr << "update user password failed"
                  << ", user_id=" << user_id
                  << ", error=" << connection->LastError()
                  << '\n';
        return false;
    }

    if (connection->AffectedRows() == 0) {
        std::cerr << "user not found or password unchanged"
                  << ", user_id=" << user_id << '\n';
        return false;
    }

    std::cout << "password updated"
              << ", user_id=" << user_id
              << ", salt=" << salt
              << ", hash=" << hash
              << '\n';

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

    if (!config.MySql().enable) {
        std::cerr << "mysql is disabled in config\n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::MySqlConnectionPool mysql_pool;

    if (!mysql_pool.Initialize(config.MySql())) {
        std::cerr << "mysql pool init failed\n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    auto connection = mysql_pool.Acquire();

    if (!connection) {
        std::cerr << "acquire mysql connection failed\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::PasswordHasher hasher;

    std::cout << "========== User Password Seed Demo ==========\n";

    bool ok = true;

    ok = ok &&
         UpdateUserPassword(
             connection.operator->(),
             &hasher,
             10001,
             "123456");

    ok = ok &&
         UpdateUserPassword(
             connection.operator->(),
             &hasher,
             10002,
             "123456");

    mysql_pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    if (!ok) {
        std::cerr << "user password seed demo failed\n";
        return 1;
    }

    std::cout << "User password seed demo finished\n";
    std::cout << "============================================\n";

    return 0;
}