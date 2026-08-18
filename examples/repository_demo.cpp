#include "common/config/Config.h"
#include "common/db/MySqlConnection.h"
#include "common/logging/Logger.h"
#include "services/repository/MessageRepository.h"
#include "services/repository/UserRepository.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

void PrintUser(
    const tinyimx::UserRecord& user
) {
    std::cout
        << "user:"
        << " user_id=" << user.user_id
        << " username=" << user.username
        << " nickname=" << user.nickname
        << " avatar_url=" << user.avatar_url
        << " status=" << user.status
        << '\n';
}

bool ExpectUserLookupStatus(
    const std::string& name,
    const tinyimx::UserLookupResult& result,
    tinyimx::UserLookupStatus expected_status
) {
    std::cout
        << name
        << "_status="
        << tinyimx::
            UserLookupStatusToString(
                result.status
            )
        << ", has_user="
        << (result.user.has_value()
                ? "true"
                : "false")
        << '\n';

    if (result.status !=
        expected_status) {
        std::cerr
            << name
            << " expected status="
            << tinyimx::
                UserLookupStatusToString(
                    expected_status
                )
            << ", actual status="
            << tinyimx::
                UserLookupStatusToString(
                    result.status
                )
            << ", message="
            << result.message
            << '\n';

        return false;
    }

    return true;
}

bool ExpectUpdateLastLoginStatus(
    const std::string& name,
    const tinyimx::
        UpdateLastLoginResult& result,
    tinyimx::
        UpdateLastLoginStatus
            expected_status
) {
    std::cout
        << name
        << "_status="
        << tinyimx::
            UpdateLastLoginStatusToString(
                result.status
            )
        << ", affected_rows="
        << result.affected_rows
        << '\n';

    if (result.status !=
        expected_status) {
        std::cerr
            << name
            << " expected status="
            << tinyimx::
                UpdateLastLoginStatusToString(
                    expected_status
                )
            << ", actual status="
            << tinyimx::
                UpdateLastLoginStatusToString(
                    result.status
                )
            << ", message="
            << result.message
            << '\n';

        return false;
    }

    return true;
}

void PrintMessage(
    const tinyimx::PrivateMessageRecord& message
) {
    std::cout
        << "message:"
        << " message_id=" << message.message_id
        << " from=" << message.from_user_id
        << " to=" << message.to_user_id
        << " status=" << message.delivery_status
        << " content=" << message.content
        << '\n';
}

bool ExpectMessageQueryStatus(
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


bool ExpectFindMessageQueryStatus(
    const std::string& name,
    const tinyimx::
        FindPrivateMessageResult& result,
    tinyimx::MessageQueryStatus
        expected_status,
    bool expected_found
) {
    std::cout
        << name
        << "_status="
        << tinyimx::
            MessageQueryStatusToString(
                result.status
            )
        << ", found="
        << (result.found
                ? "true"
                : "false")
        << ", message_id="
        << result.record.message_id
        << '\n';


    if (
        result.status !=
        expected_status
    ) {
        std::cerr
            << name
            << " expected status="
            << tinyimx::
                MessageQueryStatusToString(
                    expected_status
                )
            << ", actual status="
            << tinyimx::
                MessageQueryStatusToString(
                    result.status
                )
            << ", message="
            << result.message
            << '\n';

        return false;
    }


    /*
     * 只有查询本身Succeeded时，
     * found才表达真正的业务查询结果。
     */
    if (
        expected_status ==
            tinyimx::
                MessageQueryStatus::
                    kSucceeded &&
        result.found !=
            expected_found
    ) {
        std::cerr
            << name
            << " expected found="
            << (expected_found
                    ? "true"
                    : "false")
            << ", actual found="
            << (result.found
                    ? "true"
                    : "false")
            << '\n';

        return false;
    }


    return true;
}


bool ExpectSaveMutationStatus(
    const std::string& name,
    const tinyimx::
        SavePrivateMessageResult& result,
    tinyimx::MessageMutationStatus
        expected_status
) {
    std::cout
        << name
        << "_status="
        << tinyimx::
            MessageMutationStatusToString(
                result.status
            )
        << " message_id="
        << result.message_id
        << '\n';

    if (result.status !=
        expected_status) {
        std::cerr
            << name
            << " expected status="
            << tinyimx::
                MessageMutationStatusToString(
                    expected_status
                )
            << ", actual status="
            << tinyimx::
                MessageMutationStatusToString(
                    result.status
                )
            << ", message="
            << result.message
            << '\n';

        return false;
    }

    return true;
}

bool ExpectUpdateMutationStatus(
    const std::string& name,
    const tinyimx::
        UpdatePrivateMessagesResult& result,
    tinyimx::MessageMutationStatus
        expected_status
) {
    std::cout
        << name
        << "_status="
        << tinyimx::
            MessageMutationStatusToString(
                result.status
            )
        << " affected_rows="
        << result.affected_rows
        << '\n';

    if (result.status !=
        expected_status) {
        std::cerr
            << name
            << " expected status="
            << tinyimx::
                MessageMutationStatusToString(
                    expected_status
                )
            << ", actual status="
            << tinyimx::
                MessageMutationStatusToString(
                    result.status
                )
            << ", message="
            << result.message
            << '\n';

        return false;
    }

    return true;
}

bool ContainsMessageId(
    const std::vector<
        tinyimx::PrivateMessageRecord
    >& messages,
    std::uint64_t message_id
) {
    for (const auto& message : messages) {
        if (message.message_id == message_id) {
            return true;
        }
    }

    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path =
        "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;

    if (!config.LoadFromFile(
            config_path
        )) {
        std::cerr
            << "load config failed: "
            << config.LastError()
            << '\n';

        return 1;
    }

    if (!tinyimx::Logger::
            Instance().
            Init(
                config.Logger()
            )) {
        std::cerr
            << "logger init failed\n";

        return 1;
    }

    tinyimx::MySqlConnectionPool pool;

    if (!pool.Initialize(
            config.MySql()
        )) {
        std::cerr
            << "mysql pool init failed\n";

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    tinyimx::UserRepository
        user_repository(
            &pool
        );

    tinyimx::MessageRepository
        message_repository(
            &pool
        );

    std::cout
        << "========== Repository Demo "
           "==========\n";

    const auto user_a_result =
        user_repository.FindById(
            10001
        );

    if (!ExpectUserLookupStatus(
            "find_user_10001",
            user_a_result,
            tinyimx::
                UserLookupStatus::kFound
        ) ||
        !user_a_result.user.has_value()) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto user_b_result =
        user_repository.FindById(
            10002
        );

    if (!ExpectUserLookupStatus(
            "find_user_10002",
            user_b_result,
            tinyimx::
                UserLookupStatus::kFound
        ) ||
        !user_b_result.user.has_value()) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    PrintUser(user_a_result.user.value());

    PrintUser(user_b_result.user.value());

    const auto update_login_result =
        user_repository.UpdateLastLogin(
            10001
        );

    if (!ExpectUpdateLastLoginStatus(
            "update_last_login",
            update_login_result,
            tinyimx::
                UpdateLastLoginStatus::
                    kSucceeded
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
    const auto invalid_user_lookup =
        user_repository.FindById(
            0
        );

    if (!ExpectUserLookupStatus(
            "find_invalid_user",
            invalid_user_lookup,
            tinyimx::
                UserLookupStatus::
                    kInvalidArgument
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto missing_user_lookup =
        user_repository.FindById(
            999999999
        );

    if (!ExpectUserLookupStatus(
            "find_missing_user",
            missing_user_lookup,
            tinyimx::
                UserLookupStatus::
                    kNotFound
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto invalid_update_result =
        user_repository.UpdateLastLogin(
            0
        );

    if (!ExpectUpdateLastLoginStatus(
            "update_invalid_user",
            invalid_update_result,
            tinyimx::
                UpdateLastLoginStatus::
                    kInvalidArgument
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto missing_update_result =
        user_repository.UpdateLastLogin(
            999999999
        );

    if (!ExpectUpdateLastLoginStatus(
            "update_missing_user",
            missing_update_result,
            tinyimx::
                UpdateLastLoginStatus::
                    kNotFound
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    tinyimx::UserRepository
        unavailable_user_repository(
            nullptr
        );

    const auto lookup_storage_error =
        unavailable_user_repository.FindById(
            10001
        );

    if (!ExpectUserLookupStatus(
            "find_user_storage_error",
            lookup_storage_error,
            tinyimx::
                UserLookupStatus::
                    kStorageError
        ) ||
        lookup_storage_error.message.empty()) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto update_storage_error =
        unavailable_user_repository.
            UpdateLastLogin(
                10001
            );

    if (!ExpectUpdateLastLoginStatus(
            "update_login_storage_error",
            update_storage_error,
            tinyimx::
                UpdateLastLoginStatus::
                    kStorageError
        ) ||
        update_storage_error.message.empty()) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
    const auto invalid_username_lookup =
        user_repository.FindByUsername(
            ""
        );

    if (!ExpectUserLookupStatus(
            "find_invalid_username",
            invalid_username_lookup,
            tinyimx::
                UserLookupStatus::
                    kInvalidArgument
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
    const std::string content =
        R"({"from":10001,"to":10002,"text":"repository demo message"})";
    const auto save_result =
        message_repository.
            SavePrivateMessage(
                10001,
                10002,
                content,
                tinyimx::
                    DeliveryStatus::kPending
            );

    if (!ExpectSaveMutationStatus(
            "save_message",
            save_result,
            tinyimx::
                MessageMutationStatus::
                    kSucceeded
        ) ||
        save_result.message_id == 0) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    const std::uint64_t message_id =
        save_result.message_id;

    std::cout
        << "saved_message_id="
        << message_id
        << '\n';

    /*
    * ============================================================
    * FindPrivateMessageById:
    * case 1 - 刚刚保存的真实消息必须能够查询回来。
    * ============================================================
    */
    const auto find_saved_result =
        message_repository.
            FindPrivateMessageById(
                message_id
            );


    if (!ExpectFindMessageQueryStatus(
            "find_saved_message",
            find_saved_result,
            tinyimx::
                MessageQueryStatus::
                    kSucceeded,
            true
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
    const auto& found_message =
        find_saved_result.record;


    if (
        found_message.message_id !=
            message_id ||
        found_message.from_user_id !=
            10001 ||
        found_message.to_user_id !=
            10002 ||
        found_message.message_type !=
            static_cast<std::uint32_t>(
                tinyimx::
                    PrivateMessageType::
                        kText
            ) ||
        found_message.content !=
            content ||
        found_message.delivery_status !=
            static_cast<std::uint32_t>(
                tinyimx::
                    DeliveryStatus::
                        kPending
            ) ||
        found_message.created_at.empty()
    ) {
        std::cerr
            << "find saved message record "
            "mismatch"
            << ", message_id="
            << found_message.message_id
            << ", from="
            << found_message.from_user_id
            << ", to="
            << found_message.to_user_id
            << ", type="
            << found_message.message_type
            << ", delivery_status="
            << found_message.delivery_status
            << ", content="
            << found_message.content
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    std::cout
        << "[PASS] find existing message"
        << ", message_id="
        << found_message.message_id
        << ", from="
        << found_message.from_user_id
        << ", to="
        << found_message.to_user_id
        << ", delivery_status="
        << found_message.delivery_status
        << '\n';

        /*
    * ============================================================
    * FindPrivateMessageById:
    * case 2 - SQL成功，但是message_id不存在。
    * ============================================================
    */
    const std::uint64_t
        missing_message_id =
            std::numeric_limits<
                std::uint64_t
            >::max();


    const auto not_found_result =
        message_repository.
            FindPrivateMessageById(
                missing_message_id
            );


    if (!ExpectFindMessageQueryStatus(
            "find_missing_message",
            not_found_result,
            tinyimx::
                MessageQueryStatus::
                    kSucceeded,
            false
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    std::cout
        << "[PASS] missing message"
        << ", message_id="
        << missing_message_id
        << ", found=false"
        << '\n';

        /*
    * ============================================================
    * FindPrivateMessageById:
    * case 3 - 非法业务message_id。
    * ============================================================
    */
    const auto invalid_find_result =
        message_repository.
            FindPrivateMessageById(
                0
            );


    if (!ExpectFindMessageQueryStatus(
            "find_invalid_message_id",
            invalid_find_result,
            tinyimx::
                MessageQueryStatus::
                    kInvalidArgument,
            false
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    if (invalid_find_result.Found()) {
        std::cerr
            << "invalid message id must "
            "not report found"
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    std::cout
        << "[PASS] invalid find message id"
        << '\n';
    /*
     * ListPendingMessages现在返回
     * ListPrivateMessagesResult。
     */
    const auto pending_result =
        message_repository.
            ListPendingMessages(
                10002,
                10
            );

    if (!ExpectMessageQueryStatus(
            "pending_messages",
            pending_result,
            tinyimx::
                MessageQueryStatus::kSucceeded
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto& pending_messages =
        pending_result.records;

    std::cout
        << "pending_count="
        << pending_messages.size()
        << '\n';

    for (const auto& message :
         pending_messages) {
        PrintMessage(message);
    }

    if (!ContainsMessageId(
            pending_messages,
            message_id
        )) {
        std::cerr
            << "pending messages should "
               "contain saved message_id="
            << message_id
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

   const auto delivered_result =
        message_repository.
            MarkDelivered(
                message_id
            );

    if (!ExpectUpdateMutationStatus(
            "mark_delivered",
            delivered_result,
            tinyimx::
                MessageMutationStatus::
                    kSucceeded
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto pending_after_mark_result =
        message_repository.
            ListPendingMessages(
                10002,
                10
            );

    if (!ExpectMessageQueryStatus(
            "pending_after_mark",
            pending_after_mark_result,
            tinyimx::
                MessageQueryStatus::kSucceeded
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto& pending_after_mark =
        pending_after_mark_result.records;

    std::cout
        << "pending_count_after_mark="
        << pending_after_mark.size()
        << '\n';

    if (ContainsMessageId(
            pending_after_mark,
            message_id
        )) {
        std::cerr
            << "delivered message should "
               "not remain pending"
            << ", message_id="
            << message_id
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto mark_read_result =
        message_repository.
            MarkReadByDialog(
                10002,
                10001
            );

    if (!ExpectUpdateMutationStatus(
            "mark_read",
            mark_read_result,
            tinyimx::
                MessageMutationStatus::
                    kSucceeded
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (mark_read_result.affected_rows == 0) {
        std::cerr
            << "mark read should update at "
            "least the saved message\n";

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto
        find_after_read_result =
            message_repository.
                FindPrivateMessageById(
                    message_id
                );


    if (!find_after_read_result.Succeeded() ||
        !find_after_read_result.Found()) {
        std::cerr
            << "find message after read failed"
            << ", status="
            << tinyimx::
                MessageQueryStatusToString(
                    find_after_read_result.status
                )
            << ", message="
            << find_after_read_result.message
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    if (
        find_after_read_result.
            record.delivery_status !=
        static_cast<std::uint32_t>(
            tinyimx::
                DeliveryStatus::
                    kRead
        )
    ) {
        std::cerr
            << "message should be read"
            << ", message_id="
            << message_id
            << ", actual_status="
            << find_after_read_result.
                record.delivery_status
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    if (
        find_after_read_result.
            record.read_at.empty()
    ) {
        std::cerr
            << "read message should have "
            "read_at"
            << ", message_id="
            << message_id
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    std::cout
        << "[PASS] message reached read state"
        << ", message_id="
        << message_id
        << ", delivery_status="
        << find_after_read_result.
            record.delivery_status
        << '\n';


     const auto
        mark_delivered_after_read =
            message_repository.
                MarkDelivered(
                    message_id
                );


    if (!mark_delivered_after_read.Succeeded()) {
        std::cerr
            << "mark delivered after read "
            "should execute safely"
            << ", status="
            << tinyimx::
                MessageMutationStatusToString(
                    mark_delivered_after_read.status
                )
            << ", message="
            << mark_delivered_after_read.message
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    if (
        mark_delivered_after_read.
            affected_rows != 0
    ) {
        std::cerr
            << "read message must not be "
            "downgraded to delivered"
            << ", message_id="
            << message_id
            << ", affected_rows="
            << mark_delivered_after_read.
                affected_rows
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    std::cout
        << "[PASS] mark delivered after read "
        "changed zero rows"
        << ", message_id="
        << message_id
        << '\n';


        const auto
        find_after_redundant_delivered =
            message_repository.
                FindPrivateMessageById(
                    message_id
                );


    if (
        !find_after_redundant_delivered.
            Succeeded() ||
        !find_after_redundant_delivered.
            Found()
    ) {
        std::cerr
            << "find message after redundant "
            "mark delivered failed"
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    if (
        find_after_redundant_delivered.
            record.delivery_status !=
        static_cast<std::uint32_t>(
            tinyimx::
                DeliveryStatus::
                    kRead
        )
    ) {
        std::cerr
            << "message state regressed"
            << ", message_id="
            << message_id
            << ", expected="
            << static_cast<std::uint32_t>(
                tinyimx::
                    DeliveryStatus::
                        kRead
            )
            << ", actual="
            << find_after_redundant_delivered.
                record.delivery_status
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    std::cout
        << "[PASS] read state did not regress"
        << ", message_id="
        << message_id
        << '\n';


    const auto invalid_user_result =
        message_repository.
            ListPendingMessages(
                0,
                10
            );

    if (!ExpectMessageQueryStatus(
            "pending_invalid_user",
            invalid_user_result,
            tinyimx::
                MessageQueryStatus::
                    kInvalidArgument
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto zero_limit_result =
        message_repository.
            ListPendingMessages(
                10002,
                0
            );

    if (!ExpectMessageQueryStatus(
            "pending_zero_limit",
            zero_limit_result,
            tinyimx::
                MessageQueryStatus::kSucceeded
        ) ||
        !zero_limit_result.records.empty()) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto invalid_save_result =
    message_repository.
        SavePrivateMessage(
            0,
            10002,
            content,
            tinyimx::
                DeliveryStatus::kPending
        );

if (!ExpectSaveMutationStatus(
        "save_invalid_user",
        invalid_save_result,
        tinyimx::
            MessageMutationStatus::
                kInvalidArgument
    )) {
    pool.Shutdown();

    tinyimx::Logger::
        Instance().
        Shutdown();

    return 1;
}

const auto invalid_delivered_result =
    message_repository.
        MarkDelivered(
            0
        );

if (!ExpectUpdateMutationStatus(
        "mark_delivered_invalid_id",
        invalid_delivered_result,
        tinyimx::
            MessageMutationStatus::
                kInvalidArgument
    )) {
    pool.Shutdown();

    tinyimx::Logger::
        Instance().
        Shutdown();

    return 1;
}

const auto invalid_batch_result =
    message_repository.
        MarkDeliveredBatch(
            std::vector<std::uint64_t>{
                message_id,
                0
            }
        );

if (!ExpectUpdateMutationStatus(
        "mark_delivered_batch_invalid_id",
        invalid_batch_result,
        tinyimx::
            MessageMutationStatus::
                kInvalidArgument
    )) {
    pool.Shutdown();

    tinyimx::Logger::
        Instance().
        Shutdown();

    return 1;
}

    const auto empty_batch_result =
        message_repository.
            MarkDeliveredBatch(
                std::vector<std::uint64_t>{}
            );

    if (!ExpectUpdateMutationStatus(
            "mark_delivered_empty_batch",
            empty_batch_result,
            tinyimx::
                MessageMutationStatus::
                    kSucceeded
        ) ||
        empty_batch_result.affected_rows != 0) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto invalid_read_result =
        message_repository.
            MarkReadByDialog(
                10001,
                10001
            );

    if (!ExpectUpdateMutationStatus(
            "mark_read_invalid_dialog",
            invalid_read_result,
            tinyimx::
                MessageMutationStatus::
                    kInvalidArgument
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
    tinyimx::MessageRepository
        unavailable_repository(
            nullptr
        );

        /*
    * ============================================================
    * FindPrivateMessageById:
    * case 4 - Repository无法访问MySQL。
    * ============================================================
    */
    const auto find_storage_error =
        unavailable_repository.
            FindPrivateMessageById(
                message_id
            );


    if (!ExpectFindMessageQueryStatus(
            "find_storage_error",
            find_storage_error,
            tinyimx::
                MessageQueryStatus::
                    kStorageError,
            false
        )) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    if (
        find_storage_error.message.empty()
    ) {
        std::cerr
            << "find storage error should "
            "contain error message"
            << '\n';

        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }


    std::cout
        << "[PASS] find storage error"
        << '\n';
const auto save_storage_error =
    unavailable_repository.
        SavePrivateMessage(
            10001,
            10002,
            content,
            tinyimx::
                DeliveryStatus::kPending
        );

if (!ExpectSaveMutationStatus(
        "save_storage_error",
        save_storage_error,
        tinyimx::
            MessageMutationStatus::
                kStorageError
    ) ||
    save_storage_error.message.empty()) {
    pool.Shutdown();

    tinyimx::Logger::
        Instance().
        Shutdown();

    return 1;
}

const auto delivered_storage_error =
    unavailable_repository.
        MarkDelivered(
            message_id
        );

if (!ExpectUpdateMutationStatus(
        "mark_delivered_storage_error",
        delivered_storage_error,
        tinyimx::
            MessageMutationStatus::
                kStorageError
    ) ||
    delivered_storage_error.message.empty()) {
    pool.Shutdown();

    tinyimx::Logger::
        Instance().
        Shutdown();

    return 1;
}

const auto read_storage_error =
    unavailable_repository.
        MarkReadByDialog(
            10002,
            10001
        );

if (!ExpectUpdateMutationStatus(
        "mark_read_storage_error",
        read_storage_error,
        tinyimx::
            MessageMutationStatus::
                kStorageError
    ) ||
    read_storage_error.message.empty()) {
    pool.Shutdown();

    tinyimx::Logger::
        Instance().
        Shutdown();

    return 1;
}
    const auto storage_error_result = unavailable_repository.ListPendingMessages(
                10002,
                10
            );

    if (!ExpectMessageQueryStatus(
            "pending_storage_error",
            storage_error_result,
            tinyimx::
                MessageQueryStatus::
                    kStorageError
        ) ||
        storage_error_result.message.empty()) {
        pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    pool.Shutdown();

    std::cout
        << "Repository demo finished\n";

    std::cout
        << "================================"
           "=====\n";

    tinyimx::Logger::
        Instance().
        Shutdown();

    return 0;
}