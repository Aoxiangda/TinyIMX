#include "tests/concurrency/TestFramework.h"

#include "common/concurrency/CircularQueue.h"

namespace tinyimx::test {

void RegisterCircularQueueTests(TestRunner& runner) {
    runner.Add("CircularQueue.PushPopOrder", []() {
        CircularQueue<int> queue(3);

        TINYIMX_EXPECT_TRUE(queue.Empty());
        TINYIMX_EXPECT_EQ(queue.Capacity(), static_cast<std::size_t>(3));

        TINYIMX_EXPECT_TRUE(queue.PushBack(1));
        TINYIMX_EXPECT_TRUE(queue.PushBack(2));
        TINYIMX_EXPECT_TRUE(queue.PushBack(3));
        TINYIMX_EXPECT_TRUE(queue.Full());
        TINYIMX_EXPECT_EQ(queue.Size(), static_cast<std::size_t>(3));

        int value = 0;

        TINYIMX_EXPECT_TRUE(queue.PopFront(&value));
        TINYIMX_EXPECT_EQ(value, 1);

        TINYIMX_EXPECT_TRUE(queue.PopFront(&value));
        TINYIMX_EXPECT_EQ(value, 2);

        TINYIMX_EXPECT_TRUE(queue.PopFront(&value));
        TINYIMX_EXPECT_EQ(value, 3);

        TINYIMX_EXPECT_TRUE(queue.Empty());
    });

    runner.Add("CircularQueue.FullRejectsPush", []() {
        CircularQueue<int> queue(2);

        TINYIMX_EXPECT_TRUE(queue.PushBack(10));
        TINYIMX_EXPECT_TRUE(queue.PushBack(20));
        TINYIMX_EXPECT_TRUE(queue.Full());

        TINYIMX_EXPECT_TRUE(!queue.PushBack(30));
        TINYIMX_EXPECT_EQ(queue.Size(), static_cast<std::size_t>(2));
    });

    runner.Add("CircularQueue.DropFrontCountsOverrun", []() {
        CircularQueue<int> queue(2);

        TINYIMX_EXPECT_TRUE(queue.PushBack(1));
        TINYIMX_EXPECT_TRUE(queue.PushBack(2));

        TINYIMX_EXPECT_TRUE(queue.DropFront());
        TINYIMX_EXPECT_EQ(queue.OverrunCounter(), static_cast<std::size_t>(1));

        TINYIMX_EXPECT_TRUE(queue.PushBack(3));

        int value = 0;
        TINYIMX_EXPECT_TRUE(queue.PopFront(&value));
        TINYIMX_EXPECT_EQ(value, 2);

        TINYIMX_EXPECT_TRUE(queue.PopFront(&value));
        TINYIMX_EXPECT_EQ(value, 3);
    });

    runner.Add("CircularQueue.ClearWithoutOverrun", []() {
        CircularQueue<int> queue(3);

        TINYIMX_EXPECT_TRUE(queue.PushBack(1));
        TINYIMX_EXPECT_TRUE(queue.PushBack(2));
        TINYIMX_EXPECT_TRUE(queue.PushBack(3));

        queue.Clear(false);

        TINYIMX_EXPECT_TRUE(queue.Empty());
        TINYIMX_EXPECT_EQ(queue.OverrunCounter(), static_cast<std::size_t>(0));
    });

    runner.Add("CircularQueue.ClearWithOverrun", []() {
        CircularQueue<int> queue(3);

        TINYIMX_EXPECT_TRUE(queue.PushBack(1));
        TINYIMX_EXPECT_TRUE(queue.PushBack(2));

        queue.Clear(true);

        TINYIMX_EXPECT_TRUE(queue.Empty());
        TINYIMX_EXPECT_EQ(queue.OverrunCounter(), static_cast<std::size_t>(2));
    });
}

}  // namespace tinyimx::test