#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/MessageRepository.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr std::uint64_t
    kSenderUserId = 10001;

constexpr std::uint64_t
    kReceiverUserId = 10002;


/*
 * 每次运行生成不同的测试Key。
 *
 * 注意：
 *
 * 这只是测试数据生成方式。
 * 未来真正Client会生成UUID等稳定ID。
 */
std::string MakeClientMessageId() {
    const auto now =
        std::chrono::system_clock::now()
            .time_since_epoch();

    const auto milliseconds =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(now).count();


    return
        "m12-idempotent-" +
        std::to_string(milliseconds);
}


bool ExpectStatus(
    const std::string& name,
    const tinyimx::
        IdempotentSavePrivateMessageResult&
            result,
    tinyimx::
        IdempotentSavePrivateMessageStatus
            expected
) {
    if (result.status != expected) {
        std::cerr
            << "[FAIL] "
            << name
            << ": expected="
            << tinyimx::
                IdempotentSavePrivateMessageStatusToString(
                    expected
                )
            << ", actual="
            << tinyimx::
                IdempotentSavePrivateMessageStatusToString(
                    result.status
                )
            << ", message="
            << result.message
            << '\n';

        return false;
    }


    std::cout
        << "[PASS] "
        << name
        << ", status="
        << tinyimx::
            IdempotentSavePrivateMessageStatusToString(
                result.status
            )
        << ", message_id="
        << result.message_id
        << '\n';


    return true;
}

}  // namespace


int main(
    int argc,
    char* argv[]
) {
    std::string config_path =
        "config/gateway.json";


    if (argc >= 2) {
        config_path =
            argv[1];
    }


    tinyimx::Config config;


    if (!config.LoadFromFile(config_path)) {
        std::cerr
            << "load config failed: "
            << config.LastError()
            << '\n';

        return 1;
    }


    if (
        !tinyimx::Logger::Instance().Init(
            config.Logger()
        )
    ) {
        std::cerr
            << "logger init failed\n";

        return 1;
    }


    bool success = true;


    {
        if (!config.MySql().enable) {
            std::cerr
                << "[FAIL] mysql must be enabled\n";

            tinyimx::Logger::Instance().
                Shutdown();

            return 1;
        }


        tinyimx::MySqlConnectionPool pool;


        if (!pool.Initialize(config.MySql())) {
            std::cerr
                << "[FAIL] mysql pool init failed\n";

            tinyimx::Logger::Instance().
                Shutdown();

            return 1;
        }


        tinyimx::MessageRepository repository(
            &pool
        );


        const std::string
            client_message_id =
                MakeClientMessageId();


        const std::string original_content =
            R"({"from":10001,"text":"m12 idempotent save validation","to":10002})";


        std::cout
            << "========== TinyIMX M12 "
            << "Idempotent Save Demo ==========\n";

        std::cout
            << "client_message_id="
            << client_message_id
            << '\n';


        /*
         * =====================================================
         * Test 1
         *
         * 第一次请求：
         * 必须真正创建消息。
         * =====================================================
         */
        const auto first_result =
            repository.
                SavePrivateMessageIdempotent(
                    kSenderUserId,
                    kReceiverUserId,
                    original_content,
                    client_message_id,
                    tinyimx::
                        PrivateMessageType::
                            kText
                );


        success &=
            ExpectStatus(
                "FirstSaveCreates",
                first_result,
                tinyimx::
                    IdempotentSavePrivateMessageStatus::
                        kCreated
            );


        if (
            !first_result.Created() ||
            first_result.message_id == 0
        ) {
            std::cerr
                << "[FAIL] first save did not "
                << "create valid message\n";

            success = false;
        }


        const std::uint64_t
            original_message_id =
                first_result.message_id;


        /*
         * =====================================================
         * Test 2
         *
         * 完全相同的Client Retry。
         *
         * 必须：
         *
         * Reused
         * +
         * same server_message_id
         * =====================================================
         */
        const auto retry_result =
            repository.
                SavePrivateMessageIdempotent(
                    kSenderUserId,
                    kReceiverUserId,
                    original_content,
                    client_message_id,
                    tinyimx::
                        PrivateMessageType::
                            kText
                );


        success &=
            ExpectStatus(
                "SameRequestReused",
                retry_result,
                tinyimx::
                    IdempotentSavePrivateMessageStatus::
                        kReused
            );


        if (
            retry_result.Reused() &&
            retry_result.message_id ==
                original_message_id
        ) {
            std::cout
                << "[PASS] SameServerMessageId"
                << ", message_id="
                << original_message_id
                << '\n';
        } else {
            std::cerr
                << "[FAIL] SameServerMessageId"
                << ", first="
                << original_message_id
                << ", retry="
                << retry_result.message_id
                << '\n';

            success = false;
        }


        /*
         * =====================================================
         * Test 3
         *
         * Retry返回的Record必须仍然是
         * 第一次创建的那条消息。
         * =====================================================
         */
        if (
            retry_result.record.message_id ==
                original_message_id &&
            retry_result.record.client_message_id ==
                client_message_id &&
            retry_result.record.from_user_id ==
                kSenderUserId &&
            retry_result.record.to_user_id ==
                kReceiverUserId &&
            retry_result.record.content ==
                original_content &&
            retry_result.record.message_type ==
                static_cast<std::uint32_t>(
                    tinyimx::
                        PrivateMessageType::
                            kText
                )
        ) {
            std::cout
                << "[PASS] ReusedRecordIdentityMatches\n";
        } else {
            std::cerr
                << "[FAIL] ReusedRecordIdentityMatches\n";

            success = false;
        }


        /*
         * =====================================================
         * Test 4
         *
         * 相同Idempotency Key，
         * 但receiver不同。
         *
         * 这不是合法Retry。
         * =====================================================
         */
        const auto receiver_conflict_result =
            repository.
                SavePrivateMessageIdempotent(
                    kSenderUserId,

                    /*
                     * 故意改变receiver。
                     *
                     * 由于已有记录会在Precheck阶段
                     * 命中，所以不会真的进入FK INSERT。
                     */
                    10003,

                    original_content,
                    client_message_id,
                    tinyimx::
                        PrivateMessageType::
                            kText
                );


        success &=
            ExpectStatus(
                "DifferentReceiverConflicts",
                receiver_conflict_result,
                tinyimx::
                    IdempotentSavePrivateMessageStatus::
                        kIdempotencyConflict
            );


        /*
         * Conflict仍然应该告诉调用者：
         *
         * 这个client_message_id已经属于哪条消息。
         */
        if (
            receiver_conflict_result.message_id ==
                original_message_id
        ) {
            std::cout
                << "[PASS] ReceiverConflictReferencesOriginal"
                << ", message_id="
                << original_message_id
                << '\n';
        } else {
            std::cerr
                << "[FAIL] "
                << "ReceiverConflictReferencesOriginal\n";

            success = false;
        }


        /*
         * =====================================================
         * Test 5
         *
         * 相同Idempotency Key，
         * 但content不同。
         *
         * 同样必须Conflict。
         * =====================================================
         */
        const std::string different_content =
            R"({"from":10001,"text":"different request payload","to":10002})";


        const auto content_conflict_result =
            repository.
                SavePrivateMessageIdempotent(
                    kSenderUserId,
                    kReceiverUserId,
                    different_content,
                    client_message_id,
                    tinyimx::
                        PrivateMessageType::
                            kText
                );


        success &=
            ExpectStatus(
                "DifferentContentConflicts",
                content_conflict_result,
                tinyimx::
                    IdempotentSavePrivateMessageStatus::
                        kIdempotencyConflict
            );


        if (
            content_conflict_result.message_id ==
                original_message_id
        ) {
            std::cout
                << "[PASS] ContentConflictReferencesOriginal"
                << ", message_id="
                << original_message_id
                << '\n';
        } else {
            std::cerr
                << "[FAIL] "
                << "ContentConflictReferencesOriginal\n";

            success = false;
        }


        /*
         * =====================================================
         * Test 6
         *
         * 再查询一次稳定数据库状态。
         *
         * 验证Conflict没有改写原消息。
         * =====================================================
         */
        const auto final_query =
            repository.
                FindPrivateMessageByClientMessageId(
                    kSenderUserId,
                    client_message_id
                );


        if (
            final_query.Found() &&
            final_query.record.message_id ==
                original_message_id &&
            final_query.record.to_user_id ==
                kReceiverUserId &&
            final_query.record.content ==
                original_content
        ) {
            std::cout
                << "[PASS] OriginalRecordUnchanged\n";
        } else {
            std::cerr
                << "[FAIL] OriginalRecordUnchanged\n";

            success = false;
        }


        std::cout
            << "original_message_id="
            << original_message_id
            << '\n';


        /*
         * 后面手工MySQL验证需要这个值。
         */
        std::cout
            << "verification_client_message_id="
            << client_message_id
            << '\n';


        pool.Shutdown();


        std::cout
            << "==============================================\n";


        if (success) {
            std::cout
                << "M12 sequential idempotent save "
                << "validation passed\n";
        } else {
            std::cerr
                << "M12 sequential idempotent save "
                << "validation failed\n";
        }


        std::cout
            << "==============================================\n";
    }


    tinyimx::Logger::Instance().
        Shutdown();


    return success ? 0 : 1;
}