#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/UserRepository.h"

#include <iostream>
#include <string>

namespace {

bool ExpectLoginSuccess(tinyimx::UserRepository& repository,
                        const std::string& username,
                        const std::string& password,
                        std::uint64_t expected_user_id) {
    const auto result =
        repository.VerifyLogin(username, password);

    std::cout << "login username=" << username
              << ", status="
              << tinyimx::LoginVerifyStatusToString(result.status)
              << ", message=" << result.message
              << '\n';

    if (!result.Success()) {
        std::cerr << "expected login success, but failed\n";
        return false;
    }

    if (result.user->user_id != expected_user_id) {
        std::cerr << "user_id mismatch, expected="
                  << expected_user_id
                  << ", got=" << result.user->user_id
                  << '\n';
        return false;
    }

    return true;
}

bool ExpectLoginFailure(tinyimx::UserRepository& repository,
                        const std::string& username,
                        const std::string& password,
                        tinyimx::LoginVerifyStatus expected_status) {
    const auto result =
        repository.VerifyLogin(username, password);

    std::cout << "login username=" << username
              << ", status="
              << tinyimx::LoginVerifyStatusToString(result.status)
              << ", message=" << result.message
              << '\n';

    if (result.Success()) {
        std::cerr << "expected login failure, but succeeded\n";
        return false;
    }

    if (result.status != expected_status) {
        std::cerr << "login status mismatch, expected="
                  << tinyimx::LoginVerifyStatusToString(expected_status)
                  << ", got="
                  << tinyimx::LoginVerifyStatusToString(result.status)
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

    tinyimx::UserRepository user_repository(&mysql_pool);

    std::cout << "========== User Login Verify Demo ==========\n";

    bool ok = true;

    ok = ok &&
         ExpectLoginSuccess(
             user_repository,
             "user10001",
             "123456",
             10001);

    ok = ok &&
         ExpectLoginSuccess(
             user_repository,
             "user10002",
             "123456",
             10002);

    ok = ok &&
         ExpectLoginFailure(
             user_repository,
             "user10001",
             "wrong-password",
             tinyimx::LoginVerifyStatus::kWrongPassword);

    ok = ok &&
         ExpectLoginFailure(
             user_repository,
             "not_exists_user",
             "123456",
             tinyimx::LoginVerifyStatus::kUserNotFound);

    mysql_pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    if (!ok) {
        std::cerr << "user login verify demo failed\n";
        return 1;
    }

    std::cout << "User login verify demo finished\n";
    std::cout << "============================================\n";

    return 0;
}