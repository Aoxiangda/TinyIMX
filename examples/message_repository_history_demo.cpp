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
        std::cout
            << "message:"
            << " id=" << message.message_id
            << " from=" << message.from_user_id
            << " to=" << message.to_user_id
            << " status=" << message.delivery_status
            << " created_at=" << message.created_at
            << " content=" << message.content
            << '\n';
    }
}

bool ExpectPrivateMessageQueryStatus(
    const std::string& name,
    const tinyimx::ListPrivateMessagesResult& result,
    tinyimx::MessageQueryStatus expected_status
) {
    std::cout
        << name
        << "_status="
        << tinyimx::MessageQueryStatusToString(
               result.status
           )
        << " record_count="
        << result.records.size()
        << '\n';

    if (result.status != expected_status) {
        std::cerr
            << name
            << " expected status="
            << tinyimx::MessageQueryStatusToString(
                   expected_status
               )
            << ", actual status="
            << tinyimx::MessageQueryStatusToString(
                   result.status
               )
            << ", message="
            << result.message
            << '\n';

        return false;
    }

    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path =
        "config/gateway.json";

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
        std::cerr
            << "logger init failed\n";

        return 1;
    }

    if (!config.MySql().enable) {
        std::cerr
            << "mysql is disabled in config\n";

        tinyimx::Logger::Instance().Shutdown();

        return 1;
    }

    tinyimx::MySqlConnectionPool mysql_pool;

    if (!mysql_pool.Initialize(
            config.MySql()
        )) {
        std::cerr
            << "mysql pool init failed\n";

        tinyimx::Logger::Instance().Shutdown();

        return 1;
    }

    tinyimx::MessageRepository
        message_repository(
            &mysql_pool
        );

    std::cout
        << "========== Message Repository "
           "History Demo ==========\n";

    const std::string content =
        R"({"from":10001,"to":10002,"text":"history demo message"})";

    const auto save_result =
        message_repository.
            SavePrivateMessage(
                10001,
                10002,
                content,
                tinyimx::
                    DeliveryStatus::kDelivered
            );

    std::cout
        << "save_message_status="
        << tinyimx::
            MessageMutationStatusToString(
                save_result.status
            )
        << ", message_id="
        << save_result.message_id
        << '\n';

    if (!save_result.Succeeded() ||
        save_result.message_id == 0) {
        std::cerr
            << "save private message failed: "
            << save_result.message
            << '\n';

        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const std::uint64_t message_id =
        save_result.message_id;

    std::cout
        << "saved demo message_id="
        << message_id
        << '\n';
    const auto latest_result =
        message_repository.ListDialogMessages(
            10001,
            10002,
            0,
            20
        );

    if (!ExpectPrivateMessageQueryStatus(
            "latest_dialog",
            latest_result,
            tinyimx::
                MessageQueryStatus::kSucceeded
        )) {
        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto& latest_messages =
        latest_result.records;

    std::cout
        << "latest dialog messages count="
        << latest_messages.size()
        << '\n';

    PrintMessages(
        latest_messages
    );

    if (!ContainsMessageId(
            latest_messages,
            message_id
        )) {
        std::cerr
            << "latest messages should contain "
               "saved message_id="
            << message_id
            << '\n';

        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const std::uint64_t oldest_message_id =
        latest_messages.empty()
            ? 0
            : latest_messages.front().
                  message_id;

    if (oldest_message_id == 0) {
        std::cerr
            << "latest messages should not "
               "be empty\n";

        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto older_result =
        message_repository.ListDialogMessages(
            10001,
            10002,
            oldest_message_id,
            20
        );

    if (!ExpectPrivateMessageQueryStatus(
            "older_dialog",
            older_result,
            tinyimx::
                MessageQueryStatus::kSucceeded
        )) {
        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto& older_messages =
        older_result.records;

    std::cout
        << "older dialog messages before "
           "oldest_message_id="
        << oldest_message_id
        << ", count="
        << older_messages.size()
        << '\n';

    PrintMessages(
        older_messages
    );

    if (ContainsMessageId(
            older_messages,
            oldest_message_id
        )) {
        std::cerr
            << "older messages should not "
               "contain boundary "
               "oldest_message_id="
            << oldest_message_id
            << '\n';

        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    for (const auto& message :
         older_messages) {
        if (message.message_id >=
            oldest_message_id) {
            std::cerr
                << "older messages should all "
                   "be less than "
                   "oldest_message_id="
                << oldest_message_id
                << ", got message_id="
                << message.message_id
                << '\n';

            mysql_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }
    }

    const auto invalid_result =
        message_repository.ListDialogMessages(
            10001,
            10001,
            0,
            20
        );

    if (!ExpectPrivateMessageQueryStatus(
            "invalid_self_dialog",
            invalid_result,
            tinyimx::
                MessageQueryStatus::
                    kInvalidArgument
        )) {
        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!invalid_result.records.empty()) {
        std::cerr
            << "invalid self dialog should "
               "return no records\n";

        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto zero_limit_result =
        message_repository.ListPendingMessages(
            10002,
            0
        );

    if (!ExpectPrivateMessageQueryStatus(
            "pending_zero_limit",
            zero_limit_result,
            tinyimx::
                MessageQueryStatus::kSucceeded
        )) {
        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!zero_limit_result.records.empty()) {
        std::cerr
            << "pending zero limit should "
               "return no records\n";

        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    tinyimx::MessageRepository
        unavailable_repository(
            nullptr
        );

    const auto storage_error_result =
        unavailable_repository.
            ListPendingMessages(
                10002,
                20
            );

    if (!ExpectPrivateMessageQueryStatus(
            "pending_storage_error",
            storage_error_result,
            tinyimx::
                MessageQueryStatus::
                    kStorageError
        )) {
        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (storage_error_result.message.empty()) {
        std::cerr
            << "storage error message should "
               "not be empty\n";

        mysql_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    mysql_pool.Shutdown();

    tinyimx::Logger::
        Instance().
        Shutdown();

    std::cout
        << "message repository history "
           "demo finished\n";

    std::cout
        << "================================"
           "=====================\n";

    return 0;
}