#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/MessageRepository.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr std::uint64_t kSenderUserId = 10001;
constexpr std::uint64_t kReceiverUserId = 10002;


/*
 * 每次Demo生成一个新的client_message_id，
 * 避免多次运行时与数据库历史测试数据发生冲突。
 *
 * 真正Client后续会使用UUID等稳定ID。
 * 这里使用时间戳只是为了测试样本唯一。
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
        "m12-client-query-" +
        std::to_string(milliseconds);
}


bool ExpectQueryStatus(
    const std::string& name,
    const tinyimx::FindPrivateMessageResult& result,
    tinyimx::MessageQueryStatus expected_status
) {
    if (result.status != expected_status) {
        std::cerr
            << "[FAIL] "
            << name
            << ": expected_status="
            << tinyimx::MessageQueryStatusToString(
                   expected_status
               )
            << ", actual_status="
            << tinyimx::MessageQueryStatusToString(
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
        << tinyimx::MessageQueryStatusToString(
               result.status
           )
        << ", found="
        << result.found
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


    /*
     * 1. 加载工程真实Config。
     */
    tinyimx::Config config;


    if (!config.LoadFromFile(config_path)) {
        std::cerr
            << "load config failed: "
            << config.LastError()
            << '\n';

        return 1;
    }


    /*
     * 2. 初始化Logger。
     */
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
        /*
         * 3. 本阶段必须使用真实MySQL。
         */
        if (!config.MySql().enable) {
            std::cerr
                << "[FAIL] mysql must be enabled "
                << "for client-message-id demo\n";

            tinyimx::Logger::Instance().Shutdown();

            return 1;
        }


        tinyimx::MySqlConnectionPool pool;


        if (!pool.Initialize(config.MySql())) {
            std::cerr
                << "[FAIL] mysql pool initialize failed\n";

            tinyimx::Logger::Instance().Shutdown();

            return 1;
        }


        tinyimx::MessageRepository repository(
            &pool
        );


        const std::string client_message_id =
            MakeClientMessageId();


        const std::string content =
            R"({"from":10001,"text":"m12 client id query validation","to":10002})";


        std::cout
            << "========== TinyIMX M12 Client Message ID "
            << "Repository Demo ==========\n";

        std::cout
            << "client_message_id="
            << client_message_id
            << '\n';


        /*
         * =====================================================
         * Test 1
         *
         * 使用真实client_message_id保存一条消息。
         * =====================================================
         */
        const tinyimx::SavePrivateMessageResult
            save_result =
                repository.SavePrivateMessage(
                    kSenderUserId,
                    kReceiverUserId,
                    content,
                    tinyimx::DeliveryStatus::
                        kPending,
                    tinyimx::PrivateMessageType::
                        kText,
                    client_message_id
                );


        if (!save_result.Succeeded()) {
            std::cerr
                << "[FAIL] SavePrivateMessage"
                << ", status="
                << tinyimx::
                    MessageMutationStatusToString(
                        save_result.status
                    )
                << ", message="
                << save_result.message
                << '\n';

            success = false;
        } else {
            std::cout
                << "[PASS] SavePrivateMessage"
                << ", message_id="
                << save_result.message_id
                << '\n';
        }


        const std::uint64_t saved_message_id =
            save_result.message_id;


        if (
            !save_result.Succeeded() ||
            saved_message_id == 0
        ) {
            pool.Shutdown();

            tinyimx::Logger::Instance().Shutdown();

            return 1;
        }


        /*
         * =====================================================
         * Test 2
         *
         * 使用正确：
         *
         * (from_user_id, client_message_id)
         *
         * 查回刚才的消息。
         * =====================================================
         */
        const tinyimx::FindPrivateMessageResult
            found_result =
                repository.
                    FindPrivateMessageByClientMessageId(
                        kSenderUserId,
                        client_message_id
                    );


        success &=
            ExpectQueryStatus(
                "FindExisting",
                found_result,
                tinyimx::MessageQueryStatus::
                    kSucceeded
            );


        if (!found_result.Found()) {
            std::cerr
                << "[FAIL] FindExisting: "
                << "expected found=true\n";

            success = false;
        } else {
            std::cout
                << "[PASS] FindExisting found=true\n";
        }


        /*
         * =====================================================
         * Test 3
         *
         * server_message_id必须与第一次Save完全一致。
         * =====================================================
         */
        if (
            found_result.Found() &&
            found_result.record.message_id ==
                saved_message_id
        ) {
            std::cout
                << "[PASS] SameServerMessageId"
                << ", message_id="
                << saved_message_id
                << '\n';
        } else {
            std::cerr
                << "[FAIL] SameServerMessageId"
                << ", saved="
                << saved_message_id
                << ", found="
                << found_result.record.message_id
                << '\n';

            success = false;
        }


        /*
         * =====================================================
         * Test 4
         *
         * 完整Record也必须保持一致。
         *
         * 后续Gateway正是依靠这些字段判断：
         *
         * 相同client_message_id到底是真Retry，
         * 还是客户端错误地复用了幂等Key。
         * =====================================================
         */
        if (
            found_result.Found() &&
            found_result.record.client_message_id ==
                client_message_id &&
            found_result.record.from_user_id ==
                kSenderUserId &&
            found_result.record.to_user_id ==
                kReceiverUserId &&
            found_result.record.message_type ==
                static_cast<std::uint32_t>(
                    tinyimx::PrivateMessageType::
                        kText
                ) &&
            found_result.record.content ==
                content &&
            found_result.record.delivery_status ==
                static_cast<std::uint32_t>(
                    tinyimx::DeliveryStatus::
                        kPending
                )
        ) {
            std::cout
                << "[PASS] RecordIdentityMatches\n";
        } else {
            std::cerr
                << "[FAIL] RecordIdentityMatches\n";

            success = false;
        }


        /*
         * =====================================================
         * Test 5
         *
         * 相同client_message_id，
         * 但sender不同，
         * 不能命中user10001的消息。
         *
         * 验证业务唯一键确实是：
         *
         * (sender, client_message_id)
         * =====================================================
         */
        const tinyimx::FindPrivateMessageResult
            wrong_sender_result =
                repository.
                    FindPrivateMessageByClientMessageId(
                        kReceiverUserId,
                        client_message_id
                    );


        success &=
            ExpectQueryStatus(
                "DifferentSender",
                wrong_sender_result,
                tinyimx::MessageQueryStatus::
                    kSucceeded
            );


        if (!wrong_sender_result.Found()) {
            std::cout
                << "[PASS] DifferentSender not found\n";
        } else {
            std::cerr
                << "[FAIL] DifferentSender "
                << "unexpectedly found message_id="
                << wrong_sender_result.
                    record.message_id
                << '\n';

            success = false;
        }


        /*
         * =====================================================
         * Test 6
         *
         * 不存在的client_message_id：
         *
         * Query本身成功，
         * 但是found=false。
         * =====================================================
         */
        const tinyimx::FindPrivateMessageResult
            missing_result =
                repository.
                    FindPrivateMessageByClientMessageId(
                        kSenderUserId,
                        client_message_id +
                            "-missing"
                    );


        success &=
            ExpectQueryStatus(
                "MissingClientMessageId",
                missing_result,
                tinyimx::MessageQueryStatus::
                    kSucceeded
            );


        if (!missing_result.Found()) {
            std::cout
                << "[PASS] MissingClientMessageId "
                << "not found\n";
        } else {
            std::cerr
                << "[FAIL] MissingClientMessageId "
                << "unexpectedly found\n";

            success = false;
        }


        /*
         * =====================================================
         * Test 7
         *
         * sender=0属于非法业务参数。
         * =====================================================
         */
        const tinyimx::FindPrivateMessageResult
            zero_sender_result =
                repository.
                    FindPrivateMessageByClientMessageId(
                        0,
                        client_message_id
                    );


        success &=
            ExpectQueryStatus(
                "ZeroSender",
                zero_sender_result,
                tinyimx::MessageQueryStatus::
                    kInvalidArgument
            );


        /*
         * =====================================================
         * Test 8
         *
         * client_message_id不能为空。
         * =====================================================
         */
        const tinyimx::FindPrivateMessageResult
            empty_client_id_result =
                repository.
                    FindPrivateMessageByClientMessageId(
                        kSenderUserId,
                        ""
                    );


        success &=
            ExpectQueryStatus(
                "EmptyClientMessageId",
                empty_client_id_result,
                tinyimx::MessageQueryStatus::
                    kInvalidArgument
            );


        /*
         * =====================================================
         * Test 9
         *
         * Live DB字段是varchar(64)。
         *
         * Repository必须在进入SQL前拒绝
         * 超长幂等Key。
         * =====================================================
         */
        const std::string too_long_client_id(
            65,
            'x'
        );


        const tinyimx::FindPrivateMessageResult
            too_long_result =
                repository.
                    FindPrivateMessageByClientMessageId(
                        kSenderUserId,
                        too_long_client_id
                    );


        success &=
            ExpectQueryStatus(
                "TooLongClientMessageId",
                too_long_result,
                tinyimx::MessageQueryStatus::
                    kInvalidArgument
            );


        /*
         * =====================================================
         * Test 10
         *
         * Repository没有Storage依赖时，
         * 必须明确返回StorageError。
         * =====================================================
         */
        tinyimx::MessageRepository
            unavailable_repository(
                nullptr
            );


        const tinyimx::FindPrivateMessageResult
            unavailable_result =
                unavailable_repository.
                    FindPrivateMessageByClientMessageId(
                        kSenderUserId,
                        client_message_id
                    );


        success &=
            ExpectQueryStatus(
                "UnavailableRepository",
                unavailable_result,
                tinyimx::MessageQueryStatus::
                    kStorageError
            );


        pool.Shutdown();


        std::cout
            << "=========================================================\n";


        if (success) {
            std::cout
                << "M12 client message id repository "
                << "validation passed\n";
        } else {
            std::cerr
                << "M12 client message id repository "
                << "validation failed\n";
        }


        std::cout
            << "=========================================================\n";
    }


    tinyimx::Logger::Instance().Shutdown();


    return success ? 0 : 1;
}