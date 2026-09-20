#include "tests/concurrency/TestFramework.h"

#include "gateway/MessageDeliveryDeduplicator.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace tinyimx::test {


void
RegisterMessageDeliveryDeduplicatorTests(
    TestRunner& runner
) {
    runner.Add(
        "MessageDeliveryDeduplicator."
        "SuppressesDeliveredDuplicate",
        []() {
            MessageDeliveryDeduplicator
                deduplicator(100);


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(65),
                MessageDeliveryDedupBeginStatus::
                    kAcquired
            );


            TINYIMX_EXPECT_TRUE(
                deduplicator.
                    MarkDelivered(65)
            );


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(65),
                MessageDeliveryDedupBeginStatus::
                    kAlreadyDelivered
            );
        }
    );


    runner.Add(
        "MessageDeliveryDeduplicator."
        "AbortAllowsRetry",
        []() {
            MessageDeliveryDeduplicator
                deduplicator(100);


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(66),
                MessageDeliveryDedupBeginStatus::
                    kAcquired
            );


            TINYIMX_EXPECT_TRUE(
                deduplicator.Abort(66)
            );


            /*
             * Abort之后重新获得执行权。
             */
            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(66),
                MessageDeliveryDedupBeginStatus::
                    kAcquired
            );
        }
    );


    runner.Add(
        "MessageDeliveryDeduplicator."
        "ConcurrentBeginHasSingleOwner",
        []() {
            MessageDeliveryDeduplicator
                deduplicator(100);


            constexpr int
                kThreadCount = 16;


            std::atomic<int>
                acquired_count{0};


            std::atomic<int>
                processing_count{0};


            std::vector<std::thread>
                threads;


            for (
                int index = 0;
                index < kThreadCount;
                ++index
            ) {
                threads.emplace_back(
                    [&]() {
                        const auto status =
                            deduplicator.Begin(
                                67
                            );


                        if (
                            status ==
                            MessageDeliveryDedupBeginStatus::
                                kAcquired
                        ) {
                            acquired_count.
                                fetch_add(
                                    1,
                                    std::memory_order_relaxed
                                );
                        }


                        if (
                            status ==
                            MessageDeliveryDedupBeginStatus::
                                kAlreadyProcessing
                        ) {
                            processing_count.
                                fetch_add(
                                    1,
                                    std::memory_order_relaxed
                                );
                        }
                    }
                );
            }


            for (auto& thread : threads) {
                thread.join();
            }


            /*
             * 16个线程同时抢message_id=67，
             * 必须且只能有一个获得执行权。
             */
            TINYIMX_EXPECT_EQ(
                acquired_count.load(),
                1
            );


            TINYIMX_EXPECT_EQ(
                processing_count.load(),
                kThreadCount - 1
            );
        }
    );


    runner.Add(
        "MessageDeliveryDeduplicator."
        "EvictsOldDeliveredEntries",
        []() {
            /*
             * 只允许缓存最近2条。
             */
            MessageDeliveryDeduplicator
                deduplicator(2);


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(101),
                MessageDeliveryDedupBeginStatus::
                    kAcquired
            );

            TINYIMX_EXPECT_TRUE(
                deduplicator.
                    MarkDelivered(101)
            );


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(102),
                MessageDeliveryDedupBeginStatus::
                    kAcquired
            );

            TINYIMX_EXPECT_TRUE(
                deduplicator.
                    MarkDelivered(102)
            );


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(103),
                MessageDeliveryDedupBeginStatus::
                    kAcquired
            );

            TINYIMX_EXPECT_TRUE(
                deduplicator.
                    MarkDelivered(103)
            );


            /*
             * 101已经从内存热缓存淘汰。
             *
             * 后续真正接MySQL以后，
             * 老消息由数据库做持久化兜底。
             */
            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(101),
                MessageDeliveryDedupBeginStatus::
                    kAcquired
            );


            TINYIMX_EXPECT_EQ(
                deduplicator.
                    DeliveredCount(),
                static_cast<std::size_t>(2)
            );
        }
    );

    runner.Add(
        "MessageDeliveryDeduplicator.M17B2DeliveryIdentityIsolation",
        []() {
            MessageDeliveryDeduplicator dedup;
            const auto group_u2 = GroupDeliveryIdentity(55, 10002);
            const auto group_u3 = GroupDeliveryIdentity(55, 10003);
            const auto private_u2 = PrivateDeliveryIdentity(55, 10002);
            TINYIMX_EXPECT_EQ(dedup.Begin(group_u2), MessageDeliveryDedupBeginStatus::kAcquired);
            TINYIMX_EXPECT_EQ(dedup.Begin(group_u3), MessageDeliveryDedupBeginStatus::kAcquired);
            TINYIMX_EXPECT_EQ(dedup.Begin(private_u2), MessageDeliveryDedupBeginStatus::kAcquired);
            TINYIMX_EXPECT_TRUE(dedup.MarkDelivered(group_u2));
            TINYIMX_EXPECT_EQ(dedup.Begin(group_u2), MessageDeliveryDedupBeginStatus::kAlreadyDelivered);
            TINYIMX_EXPECT_EQ(dedup.Begin(group_u3), MessageDeliveryDedupBeginStatus::kAlreadyProcessing);
        });

}

}  // namespace tinyimx::test