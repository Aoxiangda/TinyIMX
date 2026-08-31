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


    runner.Add("ThreadPool.TrySubmitBasic", []() {
    auto options =
        MakeBaseOptions(
            "test-try-submit-basic"
        );

    ThreadPool pool(options);

    TINYIMX_EXPECT_TRUE(
        pool.Start()
    );


    std::atomic<int> counter{0};

    std::promise<void>
        task_finished_promise;

    auto task_finished_future =
        task_finished_promise.get_future();


    const TaskPushResult push_result =
        pool.TrySubmit(
            [
                &counter,
                &task_finished_promise
            ]() {
                counter.fetch_add(
                    1,
                    std::memory_order_relaxed
                );

                task_finished_promise.
                    set_value();
            }
        );


    TINYIMX_EXPECT_TRUE(
        push_result ==
        TaskPushResult::kOk
    );


    const auto wait_status =
        task_finished_future.wait_for(
            std::chrono::seconds(1)
        );


    TINYIMX_EXPECT_TRUE(
        wait_status ==
        std::future_status::ready
    );


    pool.Shutdown(
        ShutdownMode::kGraceful
    );


    const auto stats =
        pool.GetStats();


    TINYIMX_EXPECT_EQ(
        counter.load(
            std::memory_order_relaxed
        ),
        1
    );

    TINYIMX_EXPECT_EQ(
        stats.submitted_task_count,
        static_cast<std::size_t>(1)
    );

    TINYIMX_EXPECT_EQ(
        stats.completed_task_count,
        static_cast<std::size_t>(1)
    );

    TINYIMX_EXPECT_EQ(
        stats.failed_task_count,
        static_cast<std::size_t>(0)
    );

    TINYIMX_EXPECT_EQ(
        stats.rejected_task_count,
        static_cast<std::size_t>(0)
    );
});


runner.Add(
    "ThreadPool.TrySubmitQueueFullNonBlocking",
    []() {
        ThreadPoolOptions options;

        options.name =
            "test-try-submit-queue-full";

        options.worker_threads = 1;

        options.queue_capacity = 1;

        /*
         * 故意配置成kBlock。
         *
         * 就是为了证明：
         *
         * TrySubmit()
         *
         * 不受全局Block策略影响，
         * Queue Full时仍然立即返回。
         */
        options.queue_full_policy =
            QueueFullPolicy::kBlock;


        ThreadPool pool(options);

        TINYIMX_EXPECT_TRUE(
            pool.Start()
        );


        /*
         * Task #1：
         *
         * 占住唯一Worker。
         */
        std::promise<void>
            release_worker_promise;

        auto release_worker_future =
            release_worker_promise.
                get_future().
                share();


        std::promise<void>
            worker_started_promise;

        auto worker_started_future =
            worker_started_promise.
                get_future();


        const TaskPushResult
            running_result =
                pool.TrySubmit(
                    [
                        release_worker_future,
                        &worker_started_promise
                    ]() mutable {
                        worker_started_promise.
                            set_value();

                        release_worker_future.
                            wait();
                    }
                );


        TINYIMX_EXPECT_TRUE(
            running_result ==
            TaskPushResult::kOk
        );


        /*
         * 确保Task #1真的已经占住Worker。
         */
        TINYIMX_EXPECT_TRUE(
            worker_started_future.
                wait_for(
                    std::chrono::seconds(1)
                ) ==
            std::future_status::ready
        );


        /*
         * Task #2：
         *
         * Worker忙，
         * 因此它进入唯一Queue槽位。
         *
         * 此时系统状态：
         *
         * Worker：Task #1
         * Queue ：Task #2
         *
         * Queue已经100% Full。
         */
        const TaskPushResult
            queued_result =
                pool.TrySubmit(
                    []() {}
                );


        TINYIMX_EXPECT_TRUE(
            queued_result ==
            TaskPushResult::kOk
        );


        /*
         * Task #3：
         *
         * 此时Queue已经满。
         *
         * 用独立测试线程调用TrySubmit，
         * 防止实现错误时把整个测试程序
         * 永久卡死。
         */
        auto rejected_future =
            std::async(
                std::launch::async,
                [&pool]() {
                    return pool.TrySubmit(
                        []() {}
                    );
                }
            );


        /*
         * 正确实现必须在300ms内立即返回。
         *
         * 实际通常远小于1ms。
         */
        const auto submit_status =
            rejected_future.wait_for(
                std::chrono::milliseconds(
                    300
                )
            );


        const bool
            returned_without_waiting =
                submit_status ==
                std::future_status::ready;


        /*
         * 无论测试成功失败，
         * 都先释放Worker。
         *
         * 这是为了保证：
         *
         * 如果TrySubmit错误地发生阻塞，
         * 测试本身也不会死锁。
         */
        release_worker_promise.
            set_value();


        const TaskPushResult
            rejected_result =
                rejected_future.get();


        pool.Shutdown(
            ShutdownMode::kGraceful
        );


        /*
         * 核心断言1：
         *
         * Queue Full不能阻塞。
         */
        TINYIMX_EXPECT_TRUE(
            returned_without_waiting
        );


        /*
         * 核心断言2：
         *
         * Queue Full应该明确返回Discarded。
         */
        TINYIMX_EXPECT_TRUE(
            rejected_result ==
            TaskPushResult::kDiscarded
        );


        const auto stats =
            pool.GetStats();


        /*
         * 真正被接受的只有：
         *
         * Task #1
         * Task #2
         */
        TINYIMX_EXPECT_EQ(
            stats.submitted_task_count,
            static_cast<std::size_t>(2)
        );


        /*
         * Task #3必须计入Rejected。
         */
        TINYIMX_EXPECT_EQ(
            stats.rejected_task_count,
            static_cast<std::size_t>(1)
        );


        TINYIMX_EXPECT_GE(
            stats.discarded_task_count,
            static_cast<std::size_t>(1)
        );
    }
);


runner.Add(
    "ThreadPool.TrySubmitAfterShutdown",
    []() {
        auto options =
            MakeBaseOptions(
                "test-try-submit-after-shutdown"
            );


        ThreadPool pool(options);

        TINYIMX_EXPECT_TRUE(
            pool.Start()
        );


        pool.Shutdown(
            ShutdownMode::kGraceful
        );


        const TaskPushResult
            push_result =
                pool.TrySubmit(
                    []() {}
                );


        TINYIMX_EXPECT_TRUE(
            push_result ==
            TaskPushResult::kStopped
        );


        const auto stats =
            pool.GetStats();


        TINYIMX_EXPECT_EQ(
            stats.submitted_task_count,
            static_cast<std::size_t>(0)
        );


        TINYIMX_EXPECT_EQ(
            stats.completed_task_count,
            static_cast<std::size_t>(0)
        );


        TINYIMX_EXPECT_EQ(
            stats.rejected_task_count,
            static_cast<std::size_t>(1)
        );


        TINYIMX_EXPECT_TRUE(
            stats.state ==
            ThreadPoolState::kStopped
        );
    }
);


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