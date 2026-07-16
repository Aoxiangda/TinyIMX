#include "common/config/Config.h"
#include "common/db/MySqlConnection.h"
#include "common/logging/Logger.h"
#include "services/repository/MessageRepository.h"
#include "services/repository/UserRepository.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
    void PrintUser(const tinyimx::UserRecord& user) {
        std::cout << "user:"
                  << "user_id = " << user.user_id
                  << "username = "<< user.username
                  << "nickname = " << user.nickname
                  << "avatar_url = " << user.avatar_url
                  << "status = " << user.status
                  << "\n";
    }

    void PrintMessage(const tinyimx::PrivateMessageRecord& message) {
        std::cout << "message: "
                  << "message_id = " << message.message_id
                  << "from = " << message.from_user_id
                  << "to = " << message.to_user_id
                  << "status = " << message.delivery_status
                  << "content = " << message.content
                  << "\n";
    }
} // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;

    if (!config.LoadFromFile(config_path)) {
        std::cerr << "Load config failed: "
                  << config.LastError() << "\n";
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "Logger init failed\n";
        return 1;
    }

    tinyimx::MySqlConnectionPool pool;

    if (!pool.Initialize(config.MySql())) {
        std::cerr << "Mysql Pool init failed \n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }


    tinyimx::UserRepository user_repository(&pool);
    tinyimx::MessageRepository message_repository(&pool);

    std::cout << "========== Repository Demo ==========\n";

    const auto user_a = user_repository.FindById(10001);
    const auto user_b = user_repository.FindById(10002);

    if (!user_a || !user_b) {
        std::cerr << "demo users not found\n";
        pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    PrintUser(*user_a);
    PrintUser(*user_b);

    if (!user_repository.UpdateLastLogin(10001)) {
        std::cerr << "update last login failed: "
                  << user_repository.LastError() << '\n';
        pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const std::string content =
        R"({"from":10001,"to":10002,"text":"repository demo message"})";

    const std::uint64_t message_id =
        message_repository.SavePrivateMessage(
            10001,
            10002,
            content,
            tinyimx::DeliveryStatus::kPending
        );

    if (message_id == 0) {
        std::cerr << "save private message failed: "
                  << message_repository.LastError() << '\n';
        pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    std::cout << "saved_message_id = "
              << message_id << '\n';

    const auto pending_messages =
        message_repository.ListPendingMessages(10002, 10);

    std::cout << "pending_count = "
              << pending_messages.size() << '\n';

    for (const auto& message : pending_messages) {
        PrintMessage(message);
    }

    if (!message_repository.MarkDelivered(message_id)) {
        std::cerr << "mark delivered failed: "
                  << message_repository.LastError() << '\n';
        pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto pending_after_mark =
        message_repository.ListPendingMessages(10002, 10);

    std::cout << "pending_count_after_mark = "
              << pending_after_mark.size() << '\n';

    pool.Shutdown();

    std::cout << "Repository demo finished\n";
    std::cout << "=====================================\n";

    tinyimx::Logger::Instance().Shutdown();

    return 0;
}