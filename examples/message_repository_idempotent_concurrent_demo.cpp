#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/repository/MessageRepository.h"

#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t
    kSenderUserId = 10001;

constexpr std::uint64_t
    kReceiverUserId = 10002;

constexpr std::size_t
    kThreadCount = 16;


std::string MakeClientMessageId() {
    const auto now =
        std::chrono::system_clock::now()
            .time_since_epoch();


    const auto nanoseconds =
        std::chrono::duration_cast<
            std::chrono::nanoseconds
        >(now).count();


    return
        "m12-concurrent-" +
        std::to_string(nanoseconds);
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


        const std::string client_message_id =
            MakeClientMessageId();


        const std::string content =
            R"({"from":10001,"text":"m12 concurrent idempotency validation","to":10002})";


        std::cout
            << "========== TinyIMX M12 Concurrent "
            << "Idempotent Save Demo ==========\n";

        std::cout
            << "thread_count="
            << kThreadCount
            << '\n';

        std::cout
            << "client_message_id="
            << client_message_id
            << '\n';


        /*
         * =====================================================
         * 所有线程只有在完成：
         *
         * FindByClientMessageId -> Not Found
         *
         * 后才会来到这里。
         *
         * 16个线程全部到达后一起释放，
         * 确定性制造INSERT竞争。
         * =====================================================
         */
        auto pre_insert_barrier =
            std::make_shared<
                std::barrier<>
            >(
                static_cast<std::ptrdiff_t>(
                    kThreadCount
                )
            );


        repository.
            SetIdempotentPreInsertHookForTest(
                [pre_insert_barrier]() {
                    pre_insert_barrier->
                        arrive_and_wait();
                }
            );


        std::vector<
            tinyimx::
                IdempotentSavePrivateMessageResult
        > results(
            kThreadCount
        );


        std::vector<std::thread> threads;

        threads.reserve(
            kThreadCount
        );


        for (
            std::size_t index = 0;
            index < kThreadCount;
            ++index
        ) {
            threads.emplace_back(
                [
                    &repository,
                    &results,
                    &client_message_id,
                    &content,
                    index
                ]() {
                    results[index] =
                        repository.
                            SavePrivateMessageIdempotent(
                                kSenderUserId,
                                kReceiverUserId,
                                content,
                                client_message_id,
                                tinyimx::
                                    PrivateMessageType::
                                        kText
                            );
                }
            );
        }


        for (auto& thread : threads) {
            thread.join();
        }


        /*
         * 所有并发操作已经结束，
         * 立即移除Test Hook。
         */
        repository.
            SetIdempotentPreInsertHookForTest(
                {}
            );


        std::size_t created_count = 0;
        std::size_t reused_count = 0;
        std::size_t conflict_count = 0;
        std::size_t failure_count = 0;


        std::uint64_t
            common_message_id = 0;


        for (
            std::size_t index = 0;
            index < results.size();
            ++index
        ) {
            const auto& result =
                results[index];


            switch (result.status) {
                case tinyimx::
                    IdempotentSavePrivateMessageStatus::
                        kCreated:
                    ++created_count;
                    break;


                case tinyimx::
                    IdempotentSavePrivateMessageStatus::
                        kReused:
                    ++reused_count;
                    break;


                case tinyimx::
                    IdempotentSavePrivateMessageStatus::
                        kIdempotencyConflict:
                    ++conflict_count;
                    break;


                default:
                    ++failure_count;
                    break;
            }


            if (!result.Succeeded()) {
                std::cerr
                    << "[FAIL] thread="
                    << index
                    << ", status="
                    << tinyimx::
                        IdempotentSavePrivateMessageStatusToString(
                            result.status
                        )
                    << ", message="
                    << result.message
                    << '\n';

                success = false;

                continue;
            }


            if (result.message_id == 0) {
                std::cerr
                    << "[FAIL] thread="
                    << index
                    << " returned message_id=0\n";

                success = false;

                continue;
            }


            if (common_message_id == 0) {
                common_message_id =
                    result.message_id;
            } else if (
                result.message_id !=
                    common_message_id
            ) {
                std::cerr
                    << "[FAIL] thread="
                    << index
                    << " returned different message_id="
                    << result.message_id
                    << ", expected="
                    << common_message_id
                    << '\n';

                success = false;
            }
        }


        std::cout
            << "created_count="
            << created_count
            << '\n';

        std::cout
            << "reused_count="
            << reused_count
            << '\n';

        std::cout
            << "conflict_count="
            << conflict_count
            << '\n';

        std::cout
            << "failure_count="
            << failure_count
            << '\n';

        std::cout
            << "common_message_id="
            << common_message_id
            << '\n';


        /*
         * =====================================================
         * 最核心断言：
         *
         * 16个并发请求，
         * 只有一个线程能真正创建。
         * =====================================================
         */
        if (created_count == 1) {
            std::cout
                << "[PASS] ExactlyOneCreated\n";
        } else {
            std::cerr
                << "[FAIL] ExactlyOneCreated"
                << ", actual="
                << created_count
                << '\n';

            success = false;
        }


        if (
            reused_count ==
            kThreadCount - 1
        ) {
            std::cout
                << "[PASS] AllOtherRequestsReused\n";
        } else {
            std::cerr
                << "[FAIL] AllOtherRequestsReused"
                << ", expected="
                << kThreadCount - 1
                << ", actual="
                << reused_count
                << '\n';

            success = false;
        }


        if (conflict_count == 0) {
            std::cout
                << "[PASS] NoIdentityConflict\n";
        } else {
            std::cerr
                << "[FAIL] NoIdentityConflict"
                << ", actual="
                << conflict_count
                << '\n';

            success = false;
        }


        if (failure_count == 0) {
            std::cout
                << "[PASS] NoStorageFailure\n";
        } else {
            std::cerr
                << "[FAIL] NoStorageFailure"
                << ", actual="
                << failure_count
                << '\n';

            success = false;
        }


        /*
         * =====================================================
         * 再读取最终稳定数据库事实。
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
                common_message_id &&
            final_query.record.from_user_id ==
                kSenderUserId &&
            final_query.record.to_user_id ==
                kReceiverUserId &&
            final_query.record.content ==
                content
        ) {
            std::cout
                << "[PASS] FinalRecordMatches\n";
        } else {
            std::cerr
                << "[FAIL] FinalRecordMatches\n";

            success = false;
        }


        std::cout
            << "verification_client_message_id="
            << client_message_id
            << '\n';


        pool.Shutdown();


        std::cout
            << "====================================================\n";


        if (success) {
            std::cout
                << "M12 concurrent idempotent save "
                << "validation passed\n";
        } else {
            std::cerr
                << "M12 concurrent idempotent save "
                << "validation failed\n";
        }


        std::cout
            << "====================================================\n";
    }


    tinyimx::Logger::Instance().
        Shutdown();


    return success ? 0 : 1;
}