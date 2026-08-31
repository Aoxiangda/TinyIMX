#include "gateway/business/BusinessExecutor.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <thread>
#include <utility>

namespace {

using namespace std::chrono_literals;

using tinyimx::BusinessCancellationPolicy;
using tinyimx::BusinessClock;
using tinyimx::BusinessExecutor;
using tinyimx::BusinessExecutorOptions;
using tinyimx::BusinessSubmitStatus;


bool TestSubmitAndCompletion() {
    BusinessExecutorOptions options;

    options.worker_threads = 2;
    options.max_pending_tasks = 16;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 8;
    options.default_deadline = 2s;
    options.shutdown_timeout = 2s;

    BusinessExecutor executor(options);

    if (!executor.Start()) {
        std::cerr
            << "[FAIL] executor start failed\n";

        return false;
    }

    std::atomic<int> work_count{0};
    std::atomic<int> completion_count{0};

    BusinessExecutor::TaskSpec task;

    task.request.operation =
        "test.submit_completion";

    task.work =
        [&](const BusinessExecutor::ExecutionContext&) {
            work_count.fetch_add(
                1,
                std::memory_order_relaxed
            );

            return BusinessExecutor::Completion(
                [&]() {
                    completion_count.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                }
            );
        };

    /*
     * 当前只是Runtime unit test。
     *
     * 暂时直接执行completion。
     *
     * 后续Gateway integration里，
     * dispatcher才会真正变成：
     *
     * EventLoop::QueueInLoop(...)
     */
    task.dispatcher =
        [](BusinessExecutor::Completion completion) {
            if (completion) {
                completion();
            }
        };

    const auto status =
        executor.Submit(
            std::move(task)
        );

    const bool drained =
        executor.ShutdownGraceful();

    const auto stats =
        executor.GetStats();

    if (
        status !=
            BusinessSubmitStatus::kAccepted ||
        !drained ||
        work_count.load() != 1 ||
        completion_count.load() != 1 ||
        stats.accepted_total != 1 ||
        stats.completed_total != 1 ||
        stats.current_pending_tasks != 0 ||
        stats.pending_completions != 0
    ) {
        std::cerr
            << "[FAIL] submit/completion lifecycle"
            << ", status="
            << tinyimx::BusinessSubmitStatusToString(
                   status
               )
            << ", work="
            << work_count.load()
            << ", completion="
            << completion_count.load()
            << ", accepted="
            << stats.accepted_total
            << ", completed="
            << stats.completed_total
            << ", pending="
            << stats.current_pending_tasks
            << ", pending_completion="
            << stats.pending_completions
            << '\n';

        return false;
    }

    return true;
}

bool TestGlobalOverloadNonBlocking() {
    BusinessExecutorOptions options;

    options.worker_threads = 1;
    options.max_pending_tasks = 2;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 2;
    options.default_deadline = 5s;
    options.shutdown_timeout = 3s;

    BusinessExecutor executor(options);

    if (!executor.Start()) {
        return false;
    }

    std::mutex mutex;
    std::condition_variable cv;

    bool release_worker = false;
    bool worker_started = false;

    auto make_blocking_task =
        [&]() {
            BusinessExecutor::TaskSpec task;

            task.request.operation =
                "test.overload.block";

            task.work =
                [&](const BusinessExecutor::ExecutionContext&) {
                    {
                        std::lock_guard<std::mutex>
                            lock(mutex);

                        worker_started = true;
                    }

                    cv.notify_all();

                    std::unique_lock<std::mutex>
                        lock(mutex);

                    cv.wait(
                        lock,
                        [&]() {
                            return release_worker;
                        }
                    );

                    return
                        BusinessExecutor::Completion{};
                };

            return task;
        };

    auto first =
        make_blocking_task();

    if (
        executor.Submit(std::move(first)) !=
        BusinessSubmitStatus::kAccepted
    ) {
        return false;
    }

    /*
     * 确定Task 1已经真正占住唯一Worker。
     */
    {
        std::unique_lock<std::mutex>
            lock(mutex);

        if (
            !cv.wait_for(
                lock,
                1s,
                [&]() {
                    return worker_started;
                }
            )
        ) {
            std::cerr
                << "[FAIL] first worker did not start\n";

            return false;
        }
    }

    /*
     * Task 2进入pending。
     *
     * max_pending_tasks = 2：
     *
     * Task1 running
     * Task2 queued
     *
     * Runtime已满。
     */
    auto second =
        make_blocking_task();

    if (
        executor.Submit(std::move(second)) !=
        BusinessSubmitStatus::kAccepted
    ) {
        return false;
    }

    BusinessExecutor::TaskSpec third;

    third.request.operation =
        "test.overload.reject";

    third.work =
        [](const BusinessExecutor::ExecutionContext&) {
            return
                BusinessExecutor::Completion{};
        };

    const auto begin =
        BusinessClock::now();

    const auto status =
        executor.Submit(
            std::move(third)
        );

    const auto elapsed =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(
            BusinessClock::now() -
            begin
        );

    {
        std::lock_guard<std::mutex>
            lock(mutex);

        release_worker = true;
    }

    cv.notify_all();

    executor.ShutdownGraceful();

    if (
        status !=
            BusinessSubmitStatus::kOverloaded ||
        elapsed >= 100ms
    ) {
        std::cerr
            << "[FAIL] overload admission"
            << ", status="
            << tinyimx::BusinessSubmitStatusToString(
                   status
               )
            << ", elapsed_ms="
            << elapsed.count()
            << '\n';

        return false;
    }

    return true;
}


bool TestImmediateExpiredRequestRejected() {
    BusinessExecutorOptions options;

    options.worker_threads = 1;
    options.max_pending_tasks = 8;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 8;
    options.default_deadline = 2s;
    options.shutdown_timeout = 2s;

    BusinessExecutor executor(options);

    if (!executor.Start()) {
        return false;
    }

    std::atomic<int> work_count{0};

    BusinessExecutor::TaskSpec task;

    task.request.operation =
        "test.expired_immediate";

    task.request.deadline =
        BusinessClock::now() -
        1ms;

    task.work =
        [&](const BusinessExecutor::ExecutionContext&) {
            work_count.fetch_add(
                1,
                std::memory_order_relaxed
            );

            return
                BusinessExecutor::Completion{};
        };

    const auto status =
        executor.Submit(
            std::move(task)
        );

    executor.ShutdownGraceful();

    const auto stats =
        executor.GetStats();

    if (
        status !=
            BusinessSubmitStatus::
                kDeadlineExpired ||
        work_count.load() != 0 ||
        stats.current_pending_tasks != 0 ||
        stats.rejected_deadline_total != 1
    ) {
        std::cerr
            << "[FAIL] immediate deadline"
            << ", status="
            << tinyimx::BusinessSubmitStatusToString(
                   status
               )
            << ", work="
            << work_count.load()
            << ", rejected_deadline="
            << stats.rejected_deadline_total
            << '\n';

        return false;
    }

    return true;
}


bool TestDeadlineExpiresBeforeStart() {
    BusinessExecutorOptions options;

    options.worker_threads = 1;
    options.max_pending_tasks = 4;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 4;
    options.default_deadline = 5s;
    options.shutdown_timeout = 3s;

    BusinessExecutor executor(options);

    if (!executor.Start()) {
        return false;
    }

    std::mutex mutex;
    std::condition_variable cv;

    bool blocker_started = false;
    bool release_blocker = false;

    BusinessExecutor::TaskSpec blocker;

    blocker.request.operation =
        "test.deadline.blocker";

    blocker.work =
        [&](const BusinessExecutor::ExecutionContext&) {
            {
                std::lock_guard<std::mutex>
                    lock(mutex);

                blocker_started = true;
            }

            cv.notify_all();

            std::unique_lock<std::mutex>
                lock(mutex);

            cv.wait(
                lock,
                [&]() {
                    return release_blocker;
                }
            );

            return
                BusinessExecutor::Completion{};
        };

    if (
        executor.Submit(
            std::move(blocker)
        ) !=
        BusinessSubmitStatus::kAccepted
    ) {
        return false;
    }

    {
        std::unique_lock<std::mutex>
            lock(mutex);

        if (
            !cv.wait_for(
                lock,
                1s,
                [&]() {
                    return blocker_started;
                }
            )
        ) {
            return false;
        }
    }

    std::atomic<int> expired_work_count{0};

    BusinessExecutor::TaskSpec expired_task;

    expired_task.request.operation =
        "test.deadline.queued";

    expired_task.request.deadline =
        BusinessClock::now() +
        50ms;

    expired_task.work =
        [&](const BusinessExecutor::ExecutionContext&) {
            expired_work_count.fetch_add(
                1,
                std::memory_order_relaxed
            );

            return
                BusinessExecutor::Completion{};
        };

    if (
        executor.Submit(
            std::move(expired_task)
        ) !=
        BusinessSubmitStatus::kAccepted
    ) {
        return false;
    }

    /*
     * 让第二个Task在queue里超过deadline。
     */
    std::this_thread::sleep_for(
        100ms
    );

    {
        std::lock_guard<std::mutex>
            lock(mutex);

        release_blocker = true;
    }

    cv.notify_all();

    executor.ShutdownGraceful();

    const auto stats =
        executor.GetStats();

    if (
        expired_work_count.load() != 0 ||
        stats.deadline_expired_before_start_total
            != 1
    ) {
        std::cerr
            << "[FAIL] queued deadline"
            << ", work="
            << expired_work_count.load()
            << ", expired_before_start="
            << stats.
                deadline_expired_before_start_total
            << '\n';

        return false;
    }

    return true;
}


bool TestCancelableTaskSkipsInvalidSession() {
    BusinessExecutorOptions options;

    options.worker_threads = 1;
    options.max_pending_tasks = 4;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 4;
    options.default_deadline = 5s;
    options.shutdown_timeout = 3s;

    BusinessExecutor executor(options);

    if (!executor.Start()) {
        return false;
    }

    std::mutex mutex;
    std::condition_variable cv;

    bool blocker_started = false;
    bool release_blocker = false;

    BusinessExecutor::TaskSpec blocker;

    blocker.request.operation =
        "test.cancel.blocker";

    blocker.work =
        [&](const BusinessExecutor::ExecutionContext&) {
            {
                std::lock_guard<std::mutex>
                    lock(mutex);

                blocker_started = true;
            }

            cv.notify_all();

            std::unique_lock<std::mutex>
                lock(mutex);

            cv.wait(
                lock,
                [&]() {
                    return release_blocker;
                }
            );

            return
                BusinessExecutor::Completion{};
        };

    if (
        executor.Submit(
            std::move(blocker)
        ) !=
        BusinessSubmitStatus::kAccepted
    ) {
        return false;
    }

    {
        std::unique_lock<std::mutex>
            lock(mutex);

        if (
            !cv.wait_for(
                lock,
                1s,
                [&]() {
                    return blocker_started;
                }
            )
        ) {
            return false;
        }
    }

    std::atomic<bool> session_valid{true};
    std::atomic<int> work_count{0};

    BusinessExecutor::TaskSpec task;

    task.request.operation =
        "test.cancel.invalid_session";

    task.cancellation_policy =
        BusinessCancellationPolicy::
            kCancelable;

    task.still_valid =
        [&]() {
            return
                session_valid.load(
                    std::memory_order_acquire
                );
        };

    task.work =
        [&](const BusinessExecutor::ExecutionContext&) {
            work_count.fetch_add(
                1,
                std::memory_order_relaxed
            );

            return
                BusinessExecutor::Completion{};
        };

    if (
        executor.Submit(
            std::move(task)
        ) !=
        BusinessSubmitStatus::kAccepted
    ) {
        return false;
    }

    /*
     * Task仍在queue里时，
     * 模拟Session replacement/disconnect。
     */
    session_valid.store(
        false,
        std::memory_order_release
    );

    {
        std::lock_guard<std::mutex>
            lock(mutex);

        release_blocker = true;
    }

    cv.notify_all();

    executor.ShutdownGraceful();

    const auto stats =
        executor.GetStats();

    if (
        work_count.load() != 0 ||
        stats.cancelled_before_start_total != 1
    ) {
        std::cerr
            << "[FAIL] cancelable invalid session"
            << ", work="
            << work_count.load()
            << ", cancelled="
            << stats.cancelled_before_start_total
            << '\n';

        return false;
    }

    return true;
}


bool TestMustRunSurvivesSessionInvalidation() {
    BusinessExecutorOptions options;

    options.worker_threads = 1;
    options.max_pending_tasks = 8;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 8;
    options.default_deadline = 2s;
    options.shutdown_timeout = 2s;

    BusinessExecutor executor(options);

    if (!executor.Start()) {
        return false;
    }

    std::atomic<int> work_count{0};

    BusinessExecutor::TaskSpec task;

    task.request.operation =
        "test.must_run";

    task.cancellation_policy =
        BusinessCancellationPolicy::
            kMustRun;

    /*
     * 明确模拟：
     * Session已经无效。
     */
    task.still_valid =
        []() {
            return false;
        };

    task.work =
        [&](const BusinessExecutor::ExecutionContext&) {
            work_count.fetch_add(
                1,
                std::memory_order_relaxed
            );

            return
                BusinessExecutor::Completion{};
        };

    const auto status =
        executor.Submit(
            std::move(task)
        );

    executor.ShutdownGraceful();

    if (
        status !=
            BusinessSubmitStatus::kAccepted ||
        work_count.load() != 1
    ) {
        std::cerr
            << "[FAIL] must-run semantics"
            << ", status="
            << tinyimx::BusinessSubmitStatusToString(
                   status
               )
            << ", work="
            << work_count.load()
            << '\n';

        return false;
    }

    return true;
}


bool TestWorkerExceptionIsolated() {
    BusinessExecutorOptions options;

    options.worker_threads = 1;
    options.max_pending_tasks = 8;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 8;
    options.default_deadline = 2s;
    options.shutdown_timeout = 2s;

    BusinessExecutor executor(options);

    if (!executor.Start()) {
        return false;
    }

    BusinessExecutor::TaskSpec bad_task;

    bad_task.request.operation =
        "test.worker_exception";

    bad_task.work =
        [](const BusinessExecutor::ExecutionContext&)
            -> BusinessExecutor::Completion {
            throw std::runtime_error(
                "intentional business runtime test"
            );
        };

    if (
        executor.Submit(
            std::move(bad_task)
        ) !=
        BusinessSubmitStatus::kAccepted
    ) {
        return false;
    }

    std::atomic<int> healthy_work_count{0};

    BusinessExecutor::TaskSpec good_task;

    good_task.request.operation =
        "test.worker_after_exception";

    good_task.work =
        [&](const BusinessExecutor::ExecutionContext&) {
            healthy_work_count.fetch_add(
                1,
                std::memory_order_relaxed
            );

            return
                BusinessExecutor::Completion{};
        };

    if (
        executor.Submit(
            std::move(good_task)
        ) !=
        BusinessSubmitStatus::kAccepted
    ) {
        return false;
    }

    executor.ShutdownGraceful();

    const auto stats =
        executor.GetStats();

    if (
        stats.worker_exception_total != 1 ||
        healthy_work_count.load() != 1
    ) {
        std::cerr
            << "[FAIL] worker exception isolation"
            << ", exceptions="
            << stats.worker_exception_total
            << ", healthy_work="
            << healthy_work_count.load()
            << '\n';

        return false;
    }

    return true;
}


bool TestDrainRejectsNewAdmission() {
    BusinessExecutorOptions options;

    options.worker_threads = 1;
    options.max_pending_tasks = 8;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 8;
    options.default_deadline = 2s;
    options.shutdown_timeout = 2s;

    BusinessExecutor executor(options);

    if (!executor.Start()) {
        return false;
    }

    executor.BeginDrain();

    BusinessExecutor::TaskSpec task;

    task.request.operation =
        "test.drain.reject";

    task.work =
        [](const BusinessExecutor::ExecutionContext&) {
            return
                BusinessExecutor::Completion{};
        };

    const auto status =
        executor.Submit(
            std::move(task)
        );

    const bool drained =
        executor.ShutdownGraceful();

    if (
        status !=
            BusinessSubmitStatus::
                kShuttingDown ||
        !drained
    ) {
        std::cerr
            << "[FAIL] drain admission"
            << ", status="
            << tinyimx::BusinessSubmitStatusToString(
                   status
               )
            << ", drained="
            << drained
            << '\n';

        return false;
    }

    return true;
}

bool TestSameKeyOrdered() {
    BusinessExecutorOptions options;

    options.worker_threads = 4;
    options.max_pending_tasks = 64;
    options.stripe_count = 8;

    /*
     * 这里故意给足够大。
     *
     * 本测试只验证ordering，
     * 不验证hot-key overload。
     */
    options.per_stripe_queue_capacity = 32;

    options.default_deadline = 5s;
    options.shutdown_timeout = 5s;

    BusinessExecutor executor(options);

    if (!executor.Start()) {
        return false;
    }


    std::mutex order_mutex;

    std::vector<int> execution_order;


    constexpr int kTaskCount = 20;

    constexpr
        tinyimx::BusinessOrderingKey
        kSameKey = 10001;


    for (
        int i = 0;
        i < kTaskCount;
        ++i
    ) {
        BusinessExecutor::TaskSpec task;

        task.request.operation =
            "test.same_key_order";

        task.request.ordering_key =
            kSameKey;


        task.work =
            [
                &order_mutex,
                &execution_order,
                i
            ](
                const BusinessExecutor::
                    ExecutionContext&
            ) {
                {
                    std::lock_guard<std::mutex>
                        lock(order_mutex);

                    execution_order.push_back(i);
                }

                std::this_thread::sleep_for(
                    1ms
                );

                return
                    BusinessExecutor::
                        Completion{};
            };


        const auto status =
            executor.Submit(
                std::move(task)
            );


        if (
            status !=
            BusinessSubmitStatus::kAccepted
        ) {
            std::cerr
                << "[FAIL] same-key submit"
                << ", index="
                << i
                << ", status="
                << tinyimx::
                    BusinessSubmitStatusToString(
                        status
                    )
                << '\n';

            executor.ShutdownGraceful();

            return false;
        }
    }


    executor.ShutdownGraceful();


    if (
        execution_order.size() !=
        static_cast<std::size_t>(
            kTaskCount
        )
    ) {
        return false;
    }


    for (
        int i = 0;
        i < kTaskCount;
        ++i
    ) {
        if (
            execution_order[
                static_cast<std::size_t>(i)
            ] != i
        ) {
            std::cerr
                << "[FAIL] same-key order"
                << ", position="
                << i
                << ", actual="
                << execution_order[
                       static_cast<
                           std::size_t
                       >(i)
                   ]
                << '\n';

            return false;
        }
    }


    return true;
}

bool TestDifferentKeysParallel() {
    BusinessExecutorOptions options;

    options.worker_threads = 2;
    options.max_pending_tasks = 16;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 8;
    options.default_deadline = 3s;
    options.shutdown_timeout = 3s;

    BusinessExecutor executor(options);

    if (!executor.Start()) {
        return false;
    }


    std::mutex mutex;
    std::condition_variable cv;

    int started = 0;

    bool release = false;


    auto make_task =
        [&](tinyimx::BusinessOrderingKey key) {
            BusinessExecutor::TaskSpec task;

            task.request.operation =
                "test.different_keys";

            task.request.ordering_key =
                key;


            task.work =
                [&](const BusinessExecutor::
                        ExecutionContext&) {
                    std::unique_lock<std::mutex>
                        lock(mutex);

                    ++started;

                    cv.notify_all();


                    cv.wait(
                        lock,
                        [&]() {
                            return release;
                        }
                    );


                    return
                        BusinessExecutor::
                            Completion{};
                };


            return task;
        };


    /*
     * stripe_count=8：
     *
     * key 1 → stripe1
     * key 2 → stripe2
     */
    auto task1 =
        make_task(1);

    auto task2 =
        make_task(2);


    if (
        executor.Submit(
            std::move(task1)
        ) !=
        BusinessSubmitStatus::kAccepted
    ) {
        return false;
    }


    if (
        executor.Submit(
            std::move(task2)
        ) !=
        BusinessSubmitStatus::kAccepted
    ) {
        return false;
    }


    bool both_started = false;


    {
        std::unique_lock<std::mutex>
            lock(mutex);

        both_started =
            cv.wait_for(
                lock,
                1s,
                [&]() {
                    return started == 2;
                }
            );

        release = true;
    }


    cv.notify_all();


    executor.ShutdownGraceful();


    if (!both_started) {
        std::cerr
            << "[FAIL] different keys "
               "did not execute in parallel"
            << ", started="
            << started
            << '\n';

        return false;
    }


    return true;
}


bool TestHotStripeBounded() {
    BusinessExecutorOptions options;

    options.worker_threads = 1;

    /*
     * Global容量故意给大，
     * 避免测试误触发global overload。
     */
    options.max_pending_tasks = 16;

    options.stripe_count = 8;

    /*
     * 同Stripe最多：
     *
     * 1 running
     * 1 queued
     */
    options.per_stripe_queue_capacity = 2;

    options.default_deadline = 5s;
    options.shutdown_timeout = 3s;

    BusinessExecutor executor(options);

    if (!executor.Start()) {
        return false;
    }


    std::mutex mutex;
    std::condition_variable cv;

    bool first_started = false;
    bool release_first = false;


    constexpr
        tinyimx::BusinessOrderingKey
        kHotKey = 7;


    BusinessExecutor::TaskSpec first;

    first.request.operation =
        "test.hot_key.first";

    first.request.ordering_key =
        kHotKey;


    first.work =
        [&](const BusinessExecutor::
                ExecutionContext&) {
            {
                std::lock_guard<std::mutex>
                    lock(mutex);

                first_started = true;
            }

            cv.notify_all();


            std::unique_lock<std::mutex>
                lock(mutex);

            cv.wait(
                lock,
                [&]() {
                    return release_first;
                }
            );


            return
                BusinessExecutor::
                    Completion{};
        };


    if (
        executor.Submit(
            std::move(first)
        ) !=
        BusinessSubmitStatus::kAccepted
    ) {
        return false;
    }


    {
        std::unique_lock<std::mutex>
            lock(mutex);

        if (
            !cv.wait_for(
                lock,
                1s,
                [&]() {
                    return first_started;
                }
            )
        ) {
            return false;
        }
    }


    BusinessExecutor::TaskSpec second;

    second.request.operation =
        "test.hot_key.second";

    second.request.ordering_key =
        kHotKey;

    second.work =
        [](const BusinessExecutor::
               ExecutionContext&) {
            return
                BusinessExecutor::
                    Completion{};
        };


    const auto second_status =
        executor.Submit(
            std::move(second)
        );


    BusinessExecutor::TaskSpec third;

    third.request.operation =
        "test.hot_key.third";

    third.request.ordering_key =
        kHotKey;

    third.work =
        [](const BusinessExecutor::
               ExecutionContext&) {
            return
                BusinessExecutor::
                    Completion{};
        };


    const auto third_status =
        executor.Submit(
            std::move(third)
        );


    {
        std::lock_guard<std::mutex>
            lock(mutex);

        release_first = true;
    }


    cv.notify_all();


    executor.ShutdownGraceful();


    const auto stats =
        executor.GetStats();


    if (
        second_status !=
            BusinessSubmitStatus::kAccepted ||
        third_status !=
            BusinessSubmitStatus::
                kHotKeyOverloaded ||
        stats.rejected_hot_key_total != 1
    ) {
        std::cerr
            << "[FAIL] hot-key backpressure"
            << ", second="
            << tinyimx::
                BusinessSubmitStatusToString(
                    second_status
                )
            << ", third="
            << tinyimx::
                BusinessSubmitStatusToString(
                    third_status
                )
            << ", rejected_hot_key="
            << stats.rejected_hot_key_total
            << '\n';

        return false;
    }


    return true;
}


bool TestCompletionFencedAfterDispatch() {
    using namespace std::chrono_literals;


    BusinessExecutorOptions options;

    options.worker_threads = 1;
    options.max_pending_tasks = 8;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 8;
    options.default_deadline = 2s;
    options.shutdown_timeout = 2s;


    BusinessExecutor executor(
        options
    );


    if (!executor.Start()) {
        return false;
    }


    std::atomic<bool>
        completion_valid{true};

    std::atomic<int>
        work_count{0};

    std::atomic<int>
        completion_count{0};


    std::mutex mutex;

    std::condition_variable cv;

    bool dispatched = false;

    BusinessExecutor::Completion
        pending_completion;


    BusinessExecutor::TaskSpec task;

    task.request.operation =
        "test.completion_fence";


    task.completion_still_valid =
        [&]() {
            return
                completion_valid.load(
                    std::memory_order_acquire
                );
        };


    task.work =
        [&](const BusinessExecutor::
                ExecutionContext&) {
            work_count.fetch_add(
                1,
                std::memory_order_relaxed
            );


            return
                BusinessExecutor::Completion(
                    [&]() {
                        completion_count.fetch_add(
                            1,
                            std::memory_order_relaxed
                        );
                    }
                );
        };


    /*
     * 故意不立即执行completion。
     *
     * 模拟：
     *
     * QueueInLoop()
     * 已经成功
     * 但EventLoop还没执行callback。
     */
    task.dispatcher =
        [&](BusinessExecutor::Completion completion) {
            {
                std::lock_guard<std::mutex>
                    lock(mutex);

                pending_completion =
                    std::move(
                        completion
                    );

                dispatched = true;
            }

            cv.notify_all();
        };


    if (
        executor.Submit(
            std::move(task)
        ) !=
        BusinessSubmitStatus::
            kAccepted
    ) {
        return false;
    }


    {
        std::unique_lock<std::mutex>
            lock(mutex);


        if (
            !cv.wait_for(
                lock,
                1s,
                [&]() {
                    return dispatched;
                }
            )
        ) {
            return false;
        }
    }


    /*
     * 模拟QueueInLoop以后、
     * callback执行以前发生Session replacement。
     */
    completion_valid.store(
        false,
        std::memory_order_release
    );


    BusinessExecutor::Completion
        completion_to_run;


    {
        std::lock_guard<std::mutex>
            lock(mutex);

        completion_to_run =
            std::move(
                pending_completion
            );
    }


    if (completion_to_run) {
        completion_to_run();
    }


    executor.ShutdownGraceful();


    const auto stats =
        executor.GetStats();


    return
        work_count.load() == 1 &&
        completion_count.load() == 0 &&
        stats.completion_dropped_total == 1 &&
        stats.current_pending_tasks == 0 &&
        stats.pending_completions == 0;
}

bool TestMustRunCompletionCanBeFenced() {
    BusinessExecutorOptions options;

    options.worker_threads = 1;
    options.max_pending_tasks = 8;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 8;
    options.default_deadline = 2s;
    options.shutdown_timeout = 2s;


    BusinessExecutor executor(
        options
    );


    if (!executor.Start()) {
        return false;
    }


    std::atomic<int>
        work_count{0};

    std::atomic<int>
        completion_count{0};

    std::atomic<int>
        dispatcher_count{0};


    BusinessExecutor::TaskSpec task;

    task.request.operation =
        "test.must_run_completion_fence";


    task.cancellation_policy =
        BusinessCancellationPolicy::
            kMustRun;


    /*
     * Work资格已经失效。
     *
     * 但kMustRun必须继续。
     */
    task.still_valid =
        []() {
            return false;
        };


    /*
     * Completion已经不能投递。
     */
    task.completion_still_valid =
        []() {
            return false;
        };


    task.work =
        [&](const BusinessExecutor::
                ExecutionContext&) {
            work_count.fetch_add(
                1,
                std::memory_order_relaxed
            );


            return
                BusinessExecutor::Completion(
                    [&]() {
                        completion_count.fetch_add(
                            1,
                            std::memory_order_relaxed
                        );
                    }
                );
        };


    task.dispatcher =
        [&](BusinessExecutor::Completion completion) {
            dispatcher_count.fetch_add(
                1,
                std::memory_order_relaxed
            );

            if (completion) {
                completion();
            }
        };


    const auto status =
        executor.Submit(
            std::move(task)
        );


    executor.ShutdownGraceful();


    const auto stats =
        executor.GetStats();


    if (
        status !=
            BusinessSubmitStatus::kAccepted ||
        work_count.load() != 1 ||
        completion_count.load() != 0 ||
        dispatcher_count.load() != 0 ||
        stats.completion_dropped_total != 1 ||
        stats.current_pending_tasks != 0 ||
        stats.pending_completions != 0
    ) {
        std::cerr
            << "[FAIL] must-run completion fence"
            << ", status="
            << tinyimx::
                BusinessSubmitStatusToString(
                    status
                )
            << ", work="
            << work_count.load()
            << ", completion="
            << completion_count.load()
            << ", dispatcher="
            << dispatcher_count.load()
            << ", dropped="
            << stats.completion_dropped_total
            << '\n';

        return false;
    }


    return true;
}



using TestFunction = bool (*)();

struct TestCase {
    const char* name;
    TestFunction run;
};

} // namespace

int main() {
    const TestCase tests[] = {
        {
            "BusinessExecutor.SubmitAndCompletion",
            &TestSubmitAndCompletion
        },
        {
            "BusinessExecutor.GlobalOverloadNonBlocking",
            &TestGlobalOverloadNonBlocking
        },
        {
            "BusinessExecutor.ImmediateExpiredRequestRejected",
            &TestImmediateExpiredRequestRejected
        },
        {
            "BusinessExecutor.DeadlineExpiresBeforeStart",
            &TestDeadlineExpiresBeforeStart
        },
        {
            "BusinessExecutor.CancelableTaskSkipsInvalidSession",
            &TestCancelableTaskSkipsInvalidSession
        },
        {
            "BusinessExecutor.MustRunSurvivesSessionInvalidation",
            &TestMustRunSurvivesSessionInvalidation
        },
        {
            "BusinessExecutor.WorkerExceptionIsolated",
            &TestWorkerExceptionIsolated
        },
        {
            "BusinessExecutor.DrainRejectsNewAdmission",
            &TestDrainRejectsNewAdmission
        },
        {
            "BusinessExecutor.SameKeyOrdered",
            &TestSameKeyOrdered
        },
        {
            "BusinessExecutor.DifferentKeysParallel",
            &TestDifferentKeysParallel
        },
        {
            "BusinessExecutor.HotStripeBounded",
            &TestHotStripeBounded
        },
        {
            "BusinessExecutor.CompletionFencedAfterDispatch",
            &TestCompletionFencedAfterDispatch
        },
        {
            "BusinessExecutor.MustRunCompletionCanBeFenced",
            &TestMustRunCompletionCanBeFenced
        }

    };

    int failed = 0;

    std::cout
        << "========== TinyIMX Business Runtime Tests ==========\n";

    for (const auto& test : tests) {
        bool passed = false;

        try {
            passed =
                test.run();
        } catch (const std::exception& error) {
            std::cerr
                << "[EXCEPTION] "
                << test.name
                << ": "
                << error.what()
                << '\n';

            passed = false;
        } catch (...) {
            std::cerr
                << "[EXCEPTION] "
                << test.name
                << ": unknown exception\n";

            passed = false;
        }

        if (passed) {
            std::cout
                << "[PASS] "
                << test.name
                << '\n';
        } else {
            std::cout
                << "[FAIL] "
                << test.name
                << '\n';

            ++failed;
        }
    }

    std::cout
        << "====================================================\n"
        << "total="
        << std::size(tests)
        << ", failed="
        << failed
        << '\n';

    return
        failed == 0
            ? 0
            : 1;
}