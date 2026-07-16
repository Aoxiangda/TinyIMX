#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/FriendRepository.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

bool ExpectPermission(
    tinyimx::FriendRepository& repository,
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    tinyimx::ChatPermissionStatus expected_status
) {
    const auto result =
        repository.CheckPrivateChatPermission(
            from_user_id,
            to_user_id
        );

    std::cout << "check permission:"
              << " from=" << from_user_id
              << " to=" << to_user_id
              << " status="
              << tinyimx::ChatPermissionStatusToString(result.status)
              << " message=" << result.message
              << '\n';

    if (result.status != expected_status) {
        std::cerr << "permission status mismatch, expected="
                  << tinyimx::ChatPermissionStatusToString(
                         expected_status
                     )
                  << ", got="
                  << tinyimx::ChatPermissionStatusToString(
                         result.status
                     )
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

    tinyimx::FriendRepository friend_repository(&mysql_pool);

    std::cout << "========== Friend Repository Demo ==========\n";

    bool ok = true;

    ok = ok &&
         ExpectPermission(
             friend_repository,
             10001,
             10002,
             tinyimx::ChatPermissionStatus::kAllowed
         );

    ok = ok &&
         ExpectPermission(
             friend_repository,
             10002,
             10001,
             tinyimx::ChatPermissionStatus::kAllowed
         );

    ok = ok &&
         ExpectPermission(
             friend_repository,
             10001,
             10004,
             tinyimx::ChatPermissionStatus::kNotFriend
         );

    ok = ok &&
         ExpectPermission(
             friend_repository,
             10001,
             10003,
             tinyimx::ChatPermissionStatus::kBlockedBySelf
         );

    ok = ok &&
         ExpectPermission(
             friend_repository,
             10003,
             10001,
             tinyimx::ChatPermissionStatus::kBlockedByPeer
         );

    ok = ok &&
         ExpectPermission(
             friend_repository,
             0,
             10001,
             tinyimx::ChatPermissionStatus::kInvalidArgument
         );

    mysql_pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    if (!ok) {
        std::cerr << "friend repository demo failed\n";
        return 1;
    }

    std::cout << "Friend repository demo finished\n";
    std::cout << "============================================\n";

    return 0;
}