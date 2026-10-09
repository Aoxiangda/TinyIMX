#include "tests/concurrency/TestFramework.h"

#include "common/concurrency/MpmcBlockingQueue.h"

#include <iostream>

namespace tinyimx::test {

void RegisterMpmcBlockingQueueTests(TestRunner& runner) {
    runner.Add("MpmcBlockingQueue.DiscardPolicy", []() {
        MpmcBlockingQueue<int> queue(1);

        TINYIMX_EXPECT_EQ(
            queue.Push(1, QueueFullPolicy::kDiscard),
            TaskPushResult::kOk
        );

        TINYIMX_EXPECT_EQ(
            queue.Push(2, QueueFullPolicy::kDiscard),
            TaskPushResult::kDiscarded
        );

        TINYIMX_EXPECT_EQ(queue.DiscardCounter(), static_cast<std::size_t>(1));

        int value = 0;
        TINYIMX_EXPECT_TRUE(queue.Pop(&value));
        TINYIMX_EXPECT_EQ(value, 1);
    });

    runner.Add("MpmcBlockingQueue.OverwritePolicy", []() {
        MpmcBlockingQueue<int> queue(2);

        TINYIMX_EXPECT_EQ(
            queue.Push(1, QueueFullPolicy::kOverwrite),
            TaskPushResult::kOk
        );

        TINYIMX_EXPECT_EQ(
            queue.Push(2, QueueFullPolicy::kOverwrite),
            TaskPushResult::kOk
        );

        TINYIMX_EXPECT_EQ(
            queue.Push(3, QueueFullPolicy::kOverwrite),
            TaskPushResult::kOk
        );

        TINYIMX_EXPECT_EQ(queue.OverwriteCounter(), static_cast<std::size_t>(1));

        int value = 0;

        TINYIMX_EXPECT_TRUE(queue.Pop(&value));
        TINYIMX_EXPECT_EQ(value, 2);

        TINYIMX_EXPECT_TRUE(queue.Pop(&value));
        TINYIMX_EXPECT_EQ(value, 3);
    });

    runner.Add("MpmcBlockingQueue.StopForceCountsShutdownDiscard", []() {
        MpmcBlockingQueue<int> queue(3);

        TINYIMX_EXPECT_EQ(
            queue.Push(1, QueueFullPolicy::kDiscard),
            TaskPushResult::kOk
        );

        TINYIMX_EXPECT_EQ(
            queue.Push(2, QueueFullPolicy::kDiscard),
            TaskPushResult::kOk
        );

        queue.Stop(true);

        TINYIMX_EXPECT_EQ(
            queue.ShutdownDiscardCounter(),
            static_cast<std::size_t>(2)
        );

        TINYIMX_EXPECT_EQ(
            queue.OverwriteCounter(),
            static_cast<std::size_t>(0)
        );

        int value = 0;
        TINYIMX_EXPECT_TRUE(!queue.Pop(&value));
    });

    runner.Add("MpmcBlockingQueue.StopGracefulKeepsPendingTask", []() {
        MpmcBlockingQueue<int> queue(2);

        TINYIMX_EXPECT_EQ(
            queue.Push(10, QueueFullPolicy::kDiscard),
            TaskPushResult::kOk
        );

        queue.Stop(false);

        int value = 0;

        TINYIMX_EXPECT_TRUE(queue.Pop(&value));
        TINYIMX_EXPECT_EQ(value, 10);

        TINYIMX_EXPECT_TRUE(!queue.Pop(&value));
        TINYIMX_EXPECT_EQ(
            queue.ShutdownDiscardCounter(),
            static_cast<std::size_t>(0)
        );
    });
}

}  // namespace tinyimx::test