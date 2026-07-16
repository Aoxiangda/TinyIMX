#include "tests/concurrency/TestFramework.h"

#include "common/concurrency/ThreadPool.h"

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace tinyimx::test {

namespace {

ThreadPoolOptions MakeBaseOptions(const std::string& name) {
    ThreadPoolOptions options;
    options.name = name;
    options.worker_threads = 2;
    options.queue_capacity = 64;
    options.queue_full_policy = QueueFullPolicy::kBlock;
    options.enable_dynamic_resize = false;
    return options;
}

}  // namespace

void RegisterThreadPoolTests(TestRunner& runner) {
    runner.Add("ThreadPool.BasicFuture", []() {
        ThreadPool pool(MakeBaseOptions("test-basic-future"));
        TINYIMX_EXPECT_TRUE(pool.Start());

        std::vector<std::future<int>> futures;

        for (int i = 0; i < 20; ++i) {
            futures.push_back(pool.Submit([i]() {
                return i * i;
            }));
        }

        int sum = 0;
        for (auto& future : futures) {
            sum += future.get();
        }

        pool.Shutdown(ShutdownMode::kGraceful);

        const auto stats = pool.GetStats();

        TINYIMX_EXPECT_EQ(sum, 2470);
        TINYIMX_EXPECT_EQ(stats.completed_task_count, static_cast<std::size_t>(20));
        TINYIMX_EXPECT_EQ(stats.failed_task_count, static_cast<std::size_t>(0));
        TINYIMX_EXPECT_EQ(stats.rejected_task_count, static_cast<std::size_t>(0));
        TINYIMX_EXPECT_EQ(stats.current_worker_count, static_cast<std::size_t>(0));
    });

    runner.Add("ThreadPool.ExceptionFuture", []() {
        ThreadPool pool(MakeBaseOptions("test-exception-future"));
        TINYIMX_EXPECT_TRUE(pool.Start());

        auto ok_future = pool.Submit([]() {
            return 100;
        });

        auto bad_future = pool.Submit([]() -> int {
            throw std::runtime_error("intentional test exception");
        });

        TINYIMX_EXPECT_EQ(ok_future.get(), 100);
        TINYIMX_EXPECT_THROW(bad_future.get());

        pool.Shutdown(ShutdownMode::kGraceful);

        const auto stats = pool.GetStats();

        TINYIMX_EXPECT_EQ(stats.completed_task_count, static_cast<std::size_t>(1));
        TINYIMX_EXPECT_EQ(stats.failed_task_count, static_cast<std::size_t>(1));
    });

    runner.Add("ThreadPool.PauseResume", []() {
        auto options = MakeBaseOptions("test-pause-resume");
        options.worker_threads = 2;
        options.queue_capacity = 16;

        ThreadPool pool(options);
        TINYIMX_EXPECT_TRUE(pool.Start());

        TINYIMX_EXPECT_TRUE(pool.Pause());

        std::atomic<int> counter{0};

        for (int i = 0; i < 6; ++i) {
            pool.Submit([&counter]() {
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        TINYIMX_EXPECT_EQ(counter.load(std::memory_order_relaxed), 0);

        TINYIMX_EXPECT_TRUE(pool.Resume());

        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        pool.Shutdown(ShutdownMode::kGraceful);

        TINYIMX_EXPECT_EQ(counter.load(std::memory_order_relaxed), 6);

        const auto stats = pool.GetStats();
        TINYIMX_EXPECT_EQ(stats.completed_task_count, static_cast<std::size_t>(6));
    });

    runner.Add("ThreadPool.DiscardPolicy", []() {
        ThreadPoolOptions options;
        options.name = "test-discard-policy";
        options.worker_threads = 1;
        options.queue_capacity = 2;
        options.queue_full_policy = QueueFullPolicy::kDiscard;

        ThreadPool pool(options);
        TINYIMX_EXPECT_TRUE(pool.Start());

        std::promise<void> release_worker_promise;
        auto release_worker_future = release_worker_promise.get_future().share();

        std::promise<void> blocking_started_promise;
        auto blocking_started_future = blocking_started_promise.get_future();

        pool.Submit([release_worker_future, &blocking_started_promise]() mutable {
            blocking_started_promise.set_value();
            release_worker_future.wait();
        });

        blocking_started_future.wait();

        int rejected_count = 0;

        for (int i = 0; i < 20; ++i) {
            try {
                pool.Submit([]() {});
            } catch (const std::exception&) {
                ++rejected_count;
            }
        }

        release_worker_promise.set_value();

        pool.Shutdown(ShutdownMode::kGraceful);

        const auto stats = pool.GetStats();

        TINYIMX_EXPECT_TRUE(rejected_count > 0);
        TINYIMX_EXPECT_GE(stats.discarded_task_count, static_cast<std::size_t>(1));
        TINYIMX_EXPECT_EQ(stats.overwritten_task_count, static_cast<std::size_t>(0));
    });

    runner.Add("ThreadPool.OverwritePolicy", []() {
        ThreadPoolOptions options;
        options.name = "test-overwrite-policy";
        options.worker_threads = 1;
        options.queue_capacity = 2;
        options.queue_full_policy = QueueFullPolicy::kOverwrite;

        ThreadPool pool(options);
        TINYIMX_EXPECT_TRUE(pool.Start());

        std::promise<void> release_worker_promise;
        auto release_worker_future = release_worker_promise.get_future().share();

        std::promise<void> blocking_started_promise;
        auto blocking_started_future = blocking_started_promise.get_future();

        pool.Submit([release_worker_future, &blocking_started_promise]() mutable {
            blocking_started_promise.set_value();
            release_worker_future.wait();
        });

        blocking_started_future.wait();

        std::vector<std::future<int>> futures;

        for (int i = 0; i < 20; ++i) {
            futures.push_back(pool.Submit([i]() {
                return i;
            }));
        }

        release_worker_promise.set_value();

        int ready_count = 0;
        int broken_count = 0;

        for (auto& future : futures) {
            try {
                future.get();
                ++ready_count;
            } catch (const std::exception&) {
                ++broken_count;
            }
        }

        pool.Shutdown(ShutdownMode::kGraceful);

        const auto stats = pool.GetStats();

        TINYIMX_EXPECT_GE(stats.overwritten_task_count, static_cast<std::size_t>(1));
        TINYIMX_EXPECT_TRUE(ready_count > 0);
        TINYIMX_EXPECT_TRUE(broken_count > 0);
        TINYIMX_EXPECT_EQ(stats.shutdown_discarded_task_count, static_cast<std::size_t>(0));
    });

    runner.Add("ThreadPool.ForceShutdown", []() {
        ThreadPoolOptions options;
        options.name = "test-force-shutdown";
        options.worker_threads = 1;
        options.queue_capacity = 16;
        options.queue_full_policy = QueueFullPolicy::kBlock;

        ThreadPool pool(options);
        TINYIMX_EXPECT_TRUE(pool.Start());

        std::promise<void> long_task_started_promise;
        auto long_task_started_future = long_task_started_promise.get_future();

        pool.Submit([&long_task_started_promise]() mutable {
            long_task_started_promise.set_value();
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        });

        long_task_started_future.wait();

        for (int i = 0; i < 10; ++i) {
            pool.Submit([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            });
        }

        pool.Shutdown(ShutdownMode::kForce);

        const auto stats = pool.GetStats();

        TINYIMX_EXPECT_EQ(stats.completed_task_count, static_cast<std::size_t>(1));
        TINYIMX_EXPECT_EQ(stats.overwritten_task_count, static_cast<std::size_t>(0));
        TINYIMX_EXPECT_EQ(stats.shutdown_discarded_task_count, static_cast<std::size_t>(10));
        TINYIMX_EXPECT_EQ(stats.current_worker_count, static_cast<std::size_t>(0));
    });
}

}  // namespace tinyimx::test