#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/MessageRepository.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool ContainsMessageId(
    const std::vector<tinyimx::PrivateMessageRecord>& messages,
    std::uint64_t message_id
) {
    for (const auto& message : messages) {
        if (message.message_id == message_id) {
            return true;
        }
    }

    return false;
}

void PrintMessages(
    const std::vector<tinyimx::PrivateMessageRecord>& messages
) {
    for (const auto& message : messages) {
        std::cout << "message:"
                  << " id=" << message.message_id
                  << " from=" << message.from_user_id
                  << " to=" << message.to_user_id
                  << " status=" << message.delivery_status
                  << " created_at=" << message.created_at
                  << " content=" << message.content
                  << '\n';
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

    tinyimx::MySqlConnectionPool mysql_pool;

    if (!mysql_pool.Initialize(config.MySql())) {
        std::cerr << "mysql pool init failed\n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::MessageRepository message_repository(&mysql_pool);

    std::cout << "========== Message Repository History Demo ==========\n";

    const std::string content =
        R"({"from":10001,"to":10002,"text":"history demo message"})";

    const std::uint64_t message_id =
        message_repository.SavePrivateMessage(
            10001,
            10002,
            content,
            tinyimx::DeliveryStatus::kDelivered
        );

    if (message_id == 0) {
        std::cerr << "save private message failed: "
                  << message_repository.LastError() << '\n';
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    std::cout << "saved demo message_id=" << message_id << '\n';

    const auto latest_messages =
        message_repository.ListDialogMessages(
            10001,
            10002,
            0,
            20
        );

    std::cout << "latest dialog messages count="
              << latest_messages.size() << '\n';

    PrintMessages(latest_messages);

    if (!ContainsMessageId(latest_messages, message_id)) {
        std::cerr << "latest messages should contain saved message_id="
                  << message_id << '\n';
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }
    /*
        const auto older_messages =
            message_repository.ListDialogMessages(
                10001,
                10002,
                message_id,
                20
            );

        std::cout << "older dialog messages before message_id="
                << message_id
                << ", count=" << older_messages.size() << '\n';

        PrintMessages(older_messages);

        if (ContainsMessageId(older_messages, message_id)) {
            std::cerr << "older messages should not contain boundary message_id="
                    << message_id << '\n';
            mysql_pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }
    */

    const std::uint64_t oldest_message_id =
        latest_messages.empty()
            ? 0
            : latest_messages.front().message_id;

    if (oldest_message_id == 0) {
        std::cerr << "latest messages should not be empty\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto older_messages =
        message_repository.ListDialogMessages(
            10001,
            10002,
            oldest_message_id,
            20
        );

    std::cout << "older dialog messages before oldest_message_id="
            << oldest_message_id
            << ", count=" << older_messages.size() << '\n';

    PrintMessages(older_messages);

    if (ContainsMessageId(older_messages, oldest_message_id)) {
        std::cerr << "older messages should not contain boundary oldest_message_id="
                << oldest_message_id << '\n';
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    for (const auto& message : older_messages) {
        if (message.message_id >= oldest_message_id) {
            std::cerr << "older messages should all be less than oldest_message_id="
                    << oldest_message_id
                    << ", got message_id=" << message.message_id
                    << '\n';
            mysql_pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }
    }

    const auto invalid_messages =
        message_repository.ListDialogMessages(
            10001,
            10001,
            0,
            20
        );

    if (!invalid_messages.empty()) {
        std::cerr << "invalid self dialog should return empty messages\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    mysql_pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    std::cout << "message repository history demo finished\n";
    std::cout << "=====================================================\n";

    return 0;
}