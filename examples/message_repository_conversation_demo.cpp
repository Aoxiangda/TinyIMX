#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/MessageRepository.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

void PrintConversations(
    const std::vector<tinyimx::ConversationRecord>& conversations
) {
    for (const auto& conversation : conversations) {
        std::cout << "conversation:"
                  << " peer=" << conversation.peer_user_id
                  << " last_message_id=" << conversation.last_message_id
                  << " last_from=" << conversation.last_from_user_id
                  << " last_to=" << conversation.last_to_user_id
                  << " last_status=" << conversation.last_delivery_status
                  << " last_created_at=" << conversation.last_created_at
                  << " last_content=" << conversation.last_content
                  << '\n';
    }
}

bool ContainsConversation(
    const std::vector<tinyimx::ConversationRecord>& conversations,
    std::uint64_t peer_user_id,
    std::uint64_t last_message_id
) {
    for (const auto& conversation : conversations) {
        if (conversation.peer_user_id == peer_user_id &&
            conversation.last_message_id == last_message_id) {
            return true;
        }
    }

    return false;
}

bool IsDescendingByLastMessageId(
    const std::vector<tinyimx::ConversationRecord>& conversations
) {
    std::uint64_t previous_message_id = 0;

    for (const auto& conversation : conversations) {
        if (conversation.last_message_id == 0) {
            return false;
        }

        if (previous_message_id != 0 &&
            conversation.last_message_id >= previous_message_id) {
            return false;
        }

        previous_message_id = conversation.last_message_id;
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

    tinyimx::MessageRepository message_repository(&mysql_pool);

    std::cout << "========== Message Repository Conversation Demo ==========\n";

    const std::string content =
        R"({"from":10001,"to":10002,"text":"conversation demo message"})";

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

    const auto conversations =
        message_repository.ListConversations(
            10001,
            20
        );

    std::cout << "conversation count="
              << conversations.size() << '\n';

    PrintConversations(conversations);

    if (conversations.empty()) {
        std::cerr << "conversation list should not be empty\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!IsDescendingByLastMessageId(conversations)) {
        std::cerr << "conversations should be ordered by last_message_id desc\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!ContainsConversation(conversations, 10002, message_id)) {
        std::cerr << "conversation list should contain peer=10002"
                  << " and last_message_id=" << message_id << '\n';
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto peer_conversations =
        message_repository.ListConversations(
            10002,
            20
        );

    std::cout << "peer conversation count="
              << peer_conversations.size() << '\n';

    PrintConversations(peer_conversations);

    if (!ContainsConversation(peer_conversations, 10001, message_id)) {
        std::cerr << "peer conversation list should contain peer=10001"
                  << " and last_message_id=" << message_id << '\n';
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto invalid_conversations =
        message_repository.ListConversations(
            0,
            20
        );

    if (!invalid_conversations.empty()) {
        std::cerr << "invalid user conversation list should be empty\n";
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    mysql_pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    std::cout << "message repository conversation demo finished\n";
    std::cout << "=========================================================\n";

    return 0;
}