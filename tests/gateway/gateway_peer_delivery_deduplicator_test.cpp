#include "tests/concurrency/TestFramework.h"

#include "gateway/GatewayPeerDeliveryDeduplicator.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace tinyimx::test {


void
RegisterGatewayPeerDeliveryDeduplicatorTests(
    TestRunner& runner
) {
    runner.Add(
        "GatewayPeerDeliveryDeduplicator."
        "SuppressesDeliveredDuplicate",
        []() {
            GatewayPeerDeliveryDeduplicator
                deduplicator(100);


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(65),
                GatewayPeerDedupBeginStatus::
                    kAcquired
            );


            TINYIMX_EXPECT_TRUE(
                deduplicator.
                    MarkDelivered(65)
            );


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(65),
                GatewayPeerDedupBeginStatus::
                    kAlreadyDelivered
            );
        }
    );


    runner.Add(
        "GatewayPeerDeliveryDeduplicator."
        "AbortAllowsRetry",
        []() {
            GatewayPeerDeliveryDeduplicator
                deduplicator(100);


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(66),
                GatewayPeerDedupBeginStatus::
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
                GatewayPeerDedupBeginStatus::
                    kAcquired
            );
        }
    );


    runner.Add(
        "GatewayPeerDeliveryDeduplicator."
        "ConcurrentBeginHasSingleOwner",
        []() {
            GatewayPeerDeliveryDeduplicator
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
                            GatewayPeerDedupBeginStatus::
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
                            GatewayPeerDedupBeginStatus::
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
        "GatewayPeerDeliveryDeduplicator."
        "EvictsOldDeliveredEntries",
        []() {
            /*
             * 只允许缓存最近2条。
             */
            GatewayPeerDeliveryDeduplicator
                deduplicator(2);


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(101),
                GatewayPeerDedupBeginStatus::
                    kAcquired
            );

            TINYIMX_EXPECT_TRUE(
                deduplicator.
                    MarkDelivered(101)
            );


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(102),
                GatewayPeerDedupBeginStatus::
                    kAcquired
            );

            TINYIMX_EXPECT_TRUE(
                deduplicator.
                    MarkDelivered(102)
            );


            TINYIMX_EXPECT_EQ(
                deduplicator.Begin(103),
                GatewayPeerDedupBeginStatus::
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
                GatewayPeerDedupBeginStatus::
                    kAcquired
            );


            TINYIMX_EXPECT_EQ(
                deduplicator.
                    DeliveredCount(),
                static_cast<std::size_t>(2)
            );
        }
    );
}

}  // namespace tinyimx::test