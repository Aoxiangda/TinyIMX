#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/FriendRepository.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

void PrintFriends(
    const std::vector<tinyimx::FriendRecord>& friends
) {
    for (const auto& friend_record : friends) {
        std::cout << "friend:"
                  << " friend_user_id=" << friend_record.friend_user_id
                  << " username=" << friend_record.username
                  << " nickname=" << friend_record.nickname
                  << " avatar_url=" << friend_record.avatar_url
                  << " user_status=" << friend_record.user_status
                  << " relation_status="
                  << friend_record.relation_status
                  << " relation_created_at="
                  << friend_record.relation_created_at
                  << " relation_updated_at="
                  << friend_record.relation_updated_at
                  << '\n';
    }
}

bool ContainsFriend(
    const std::vector<tinyimx::FriendRecord>& friends,
    std::uint64_t friend_user_id
) {
    for (const auto& friend_record : friends) {
        if (friend_record.friend_user_id == friend_user_id) {
            return true;
        }
    }

    return false;
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

    std::cout << "========== Friend Repository List Demo ==========\n";

    const auto user_friends =
        friend_repository.ListFriends(
            10001,
            20
        );

    std::cout << "user10001 friend count="
              << user_friends.size() << '\n';

    PrintFriends(user_friends);

    if (user_friends.empty()) {
        std::cerr << "user10001 friend list should not be empty\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!ContainsFriend(user_friends, 10002)) {
        std::cerr << "user10001 friend list should contain user10002\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (ContainsFriend(user_friends, 10003)) {
        std::cerr << "blocked user10003 should not appear in friend list\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (ContainsFriend(user_friends, 10004)) {
        std::cerr << "non-friend user10004 should not appear in friend list\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto peer_friends =
        friend_repository.ListFriends(
            10002,
            20
        );

    std::cout << "user10002 friend count="
              << peer_friends.size() << '\n';

    PrintFriends(peer_friends);

    if (!ContainsFriend(peer_friends, 10001)) {
        std::cerr << "user10002 friend list should contain user10001\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto invalid_friends =
        friend_repository.ListFriends(
            0,
            20
        );

    if (!invalid_friends.empty()) {
        std::cerr << "invalid user friend list should be empty\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    mysql_pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    std::cout << "friend repository list demo finished\n";
    std::cout << "=================================================\n";

    return 0;
}