#include "gateway/business/BusinessExecutor.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

using tinyimx::BusinessCancellationPolicy;
using tinyimx::BusinessClock;
using tinyimx::BusinessExecutor;
using tinyimx::BusinessExecutorOptions;
using tinyimx::BusinessOrderingKey;
using tinyimx::BusinessSubmitStatus;

bool Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }

    return true;
}

bool WaitFor(
    std::condition_variable& cv,
    std::unique_lock<std::mutex>& lock,
    const std::function<bool()>& predicate,
    std::chrono::milliseconds timeout = 2s
) {
    return cv.wait_for(lock, timeout, predicate);
}

BusinessExecutor::TaskSpec MakeNoopMustRun(
    const std::string& operation
) {
    BusinessExecutor::TaskSpec task;
    task.request.operation = operation;
    task.cancellation_policy = BusinessCancellationPolicy::kMustRun;
    task.work = [](const BusinessExecutor::ExecutionContext&) {
        return BusinessExecutor::Completion{};
    };
    return task;
}

bool LifecycleClean(const tinyimx::BusinessExecutorStats& stats) {
    return
        stats.current_pending_tasks == 0 &&
        stats.current_active_tasks == 0 &&
        stats.pending_completions == 0;
}

bool TestProductionBaselineStartDrain() {
    BusinessExecutorOptions options;

    if (!Expect(options.worker_threads == 4, "production default workers=4") ||
        !Expect(options.max_pending_tasks == 128, "production default max_pending=128") ||
        !Expect(options.stripe_count == 64, "production default stripes=64") ||
        !Expect(options.per_stripe_queue_capacity == 32,
                "production default per_stripe=32") ||
        !Expect(options.default_deadline == 3000ms,
                "production default deadline=3000ms") ||
        !Expect(options.shutdown_timeout == 30000ms,
                "production default shutdown=30000ms")) {
        return false;
    }

    BusinessExecutor executor(options);
    if (!executor.Start()) {
        return Expect(false, "production baseline executor starts");
    }

    constexpr std::size_t kTasks = 32;
    for (std::size_t i = 0; i < kTasks; ++i) {
        auto task = MakeNoopMustRun("acceptance.production_baseline");
        if (executor.Submit(std::move(task)) != BusinessSubmitStatus::kAccepted) {
            return Expect(false, "production baseline accepts short work");
        }
    }

    const bool within_budget = executor.ShutdownGraceful();
    const auto stats = executor.GetStats();

    return
        Expect(within_budget, "production baseline drains within budget") &&
        Expect(stats.accepted_total == kTasks, "production baseline accepted count") &&
        Expect(stats.completed_total == kTasks, "production baseline completed count") &&
        Expect(stats.worker_exception_total == 0, "production baseline worker exceptions=0") &&
        Expect(stats.completion_exception_total == 0,
               "production baseline completion exceptions=0") &&
        Expect(LifecycleClean(stats), "production baseline lifecycle residue=0");
}

bool TestInvalidRuntimeConfigurationRejected() {
    BusinessExecutorOptions options;
    options.worker_threads = 1;
    options.max_pending_tasks = 4;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 5;

    BusinessExecutor executor(options);
    return Expect(!executor.Start(), "per-stripe capacity above global capacity is rejected");
}

bool TestHotKeyIsolationPreservesOtherKey() {
    BusinessExecutorOptions options;
    options.worker_threads = 2;
    options.max_pending_tasks = 4;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 2;
    options.default_deadline = 3s;
    options.shutdown_timeout = 3s;

    BusinessExecutor executor(options);
    if (!executor.Start()) {
        return Expect(false, "hot-key executor starts");
    }

    constexpr BusinessOrderingKey kHotKey = 1;
    constexpr BusinessOrderingKey kNormalKey = 2;

    std::mutex mutex;
    std::condition_variable cv;
    bool hot_started = false;
    bool release_hot = false;
    bool normal_done = false;

    BusinessExecutor::TaskSpec hot_first;
    hot_first.request.operation = "acceptance.hot.first";
    hot_first.request.ordering_key = kHotKey;
    hot_first.cancellation_policy = BusinessCancellationPolicy::kMustRun;
    hot_first.work = [&](const BusinessExecutor::ExecutionContext&) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            hot_started = true;
        }
        cv.notify_all();

        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return release_hot; });
        return BusinessExecutor::Completion{};
    };

    if (executor.Submit(std::move(hot_first)) != BusinessSubmitStatus::kAccepted) {
        return Expect(false, "first hot-key task accepted");
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!WaitFor(cv, lock, [&]() { return hot_started; })) {
            return Expect(false, "first hot-key task starts");
        }
    }

    auto hot_second = MakeNoopMustRun("acceptance.hot.second");
    hot_second.request.ordering_key = kHotKey;
    const auto second_status = executor.Submit(std::move(hot_second));

    auto hot_third = MakeNoopMustRun("acceptance.hot.third");
    hot_third.request.ordering_key = kHotKey;
    const auto third_status = executor.Submit(std::move(hot_third));

    BusinessExecutor::TaskSpec normal;
    normal.request.operation = "acceptance.hot.normal";
    normal.request.ordering_key = kNormalKey;
    normal.cancellation_policy = BusinessCancellationPolicy::kMustRun;
    normal.work = [&](const BusinessExecutor::ExecutionContext&) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            normal_done = true;
        }
        cv.notify_all();
        return BusinessExecutor::Completion{};
    };

    const auto normal_status = executor.Submit(std::move(normal));

    bool normal_finished_while_hot_blocked = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        normal_finished_while_hot_blocked =
            WaitFor(cv, lock, [&]() { return normal_done; }, 1s);
        release_hot = true;
    }
    cv.notify_all();

    executor.ShutdownGraceful();
    const auto stats = executor.GetStats();

    return
        Expect(second_status == BusinessSubmitStatus::kAccepted,
               "second hot-key task accepted") &&
        Expect(third_status == BusinessSubmitStatus::kHotKeyOverloaded,
               "third hot-key task is locally rejected") &&
        Expect(normal_status == BusinessSubmitStatus::kAccepted,
               "different key remains admissible") &&
        Expect(normal_finished_while_hot_blocked,
               "different key executes while hot key is blocked") &&
        Expect(stats.rejected_hot_key_total == 1,
               "hot-key rejection counter increments") &&
        Expect(LifecycleClean(stats), "hot-key test lifecycle residue=0");
}

bool TestGlobalOverloadFastReject() {
    BusinessExecutorOptions options;
    options.worker_threads = 1;
    options.max_pending_tasks = 2;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 2;
    options.default_deadline = 3s;
    options.shutdown_timeout = 3s;

    BusinessExecutor executor(options);
    if (!executor.Start()) {
        return Expect(false, "overload executor starts");
    }

    std::mutex mutex;
    std::condition_variable cv;
    bool first_started = false;
    bool release = false;

    auto make_blocker = [&](const char* name) {
        BusinessExecutor::TaskSpec task;
        task.request.operation = name;
        task.cancellation_policy = BusinessCancellationPolicy::kMustRun;
        task.work = [&](const BusinessExecutor::ExecutionContext&) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                first_started = true;
            }
            cv.notify_all();
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return release; });
            return BusinessExecutor::Completion{};
        };
        return task;
    };

    auto first = make_blocker("acceptance.overload.first");
    if (executor.Submit(std::move(first)) != BusinessSubmitStatus::kAccepted) {
        return Expect(false, "overload first accepted");
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!WaitFor(cv, lock, [&]() { return first_started; })) {
            return Expect(false, "overload first starts");
        }
    }

    auto second = MakeNoopMustRun("acceptance.overload.second");
    if (executor.Submit(std::move(second)) != BusinessSubmitStatus::kAccepted) {
        return Expect(false, "overload second accepted");
    }

    auto third = MakeNoopMustRun("acceptance.overload.third");
    const auto begin = BusinessClock::now();
    const auto third_status = executor.Submit(std::move(third));
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        BusinessClock::now() - begin
    );

    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    cv.notify_all();

    executor.ShutdownGraceful();
    const auto stats = executor.GetStats();

    return
        Expect(third_status == BusinessSubmitStatus::kOverloaded,
               "global overload rejects new admission") &&
        Expect(elapsed < 100ms, "global overload rejection is non-blocking") &&
        Expect(stats.rejected_overload_total == 1,
               "global overload counter increments") &&
        Expect(LifecycleClean(stats), "global overload lifecycle residue=0");
}

bool TestCancelableStaleAndDeadline() {
    auto run_stale_case = []() {
        BusinessExecutorOptions options;
        options.worker_threads = 1;
        options.max_pending_tasks = 4;
        options.stripe_count = 8;
        options.per_stripe_queue_capacity = 4;
        options.default_deadline = 3s;
        options.shutdown_timeout = 3s;

        BusinessExecutor executor(options);
        if (!executor.Start()) {
            return false;
        }

        std::mutex mutex;
        std::condition_variable cv;
        bool blocker_started = false;
        bool release = false;
        std::atomic<bool> session_valid{true};
        std::atomic<int> stale_work{0};

        BusinessExecutor::TaskSpec blocker;
        blocker.request.operation = "acceptance.cancel.blocker";
        blocker.cancellation_policy = BusinessCancellationPolicy::kMustRun;
        blocker.work = [&](const BusinessExecutor::ExecutionContext&) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                blocker_started = true;
            }
            cv.notify_all();
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return release; });
            return BusinessExecutor::Completion{};
        };

        if (executor.Submit(std::move(blocker)) != BusinessSubmitStatus::kAccepted) {
            return false;
        }

        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!WaitFor(cv, lock, [&]() { return blocker_started; })) {
                return false;
            }
        }

        BusinessExecutor::TaskSpec stale;
        stale.request.operation = "acceptance.cancel.stale";
        stale.cancellation_policy = BusinessCancellationPolicy::kCancelable;
        stale.still_valid = [&]() {
            return session_valid.load(std::memory_order_acquire);
        };
        stale.work = [&](const BusinessExecutor::ExecutionContext&) {
            stale_work.fetch_add(1, std::memory_order_relaxed);
            return BusinessExecutor::Completion{};
        };

        if (executor.Submit(std::move(stale)) != BusinessSubmitStatus::kAccepted) {
            return false;
        }

        session_valid.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mutex);
            release = true;
        }
        cv.notify_all();

        executor.ShutdownGraceful();
        const auto stats = executor.GetStats();

        return
            stale_work.load(std::memory_order_relaxed) == 0 &&
            stats.cancelled_before_start_total == 1 &&
            LifecycleClean(stats);
    };

    auto run_deadline_case = []() {
        BusinessExecutorOptions options;
        options.worker_threads = 1;
        options.max_pending_tasks = 4;
        options.stripe_count = 8;
        options.per_stripe_queue_capacity = 4;
        options.default_deadline = 3s;
        options.shutdown_timeout = 3s;

        BusinessExecutor executor(options);
        if (!executor.Start()) {
            return false;
        }

        std::mutex mutex;
        std::condition_variable cv;
        bool blocker_started = false;
        bool release = false;
        std::atomic<int> expired_work{0};

        BusinessExecutor::TaskSpec blocker;
        blocker.request.operation = "acceptance.deadline.blocker";
        blocker.cancellation_policy = BusinessCancellationPolicy::kMustRun;
        blocker.work = [&](const BusinessExecutor::ExecutionContext&) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                blocker_started = true;
            }
            cv.notify_all();
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return release; });
            return BusinessExecutor::Completion{};
        };

        if (executor.Submit(std::move(blocker)) != BusinessSubmitStatus::kAccepted) {
            return false;
        }

        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!WaitFor(cv, lock, [&]() { return blocker_started; })) {
                return false;
            }
        }

        BusinessExecutor::TaskSpec expired;
        expired.request.operation = "acceptance.deadline.queued";
        expired.request.deadline = BusinessClock::now() + 50ms;
        expired.cancellation_policy = BusinessCancellationPolicy::kCancelable;
        expired.work = [&](const BusinessExecutor::ExecutionContext&) {
            expired_work.fetch_add(1, std::memory_order_relaxed);
            return BusinessExecutor::Completion{};
        };

        if (executor.Submit(std::move(expired)) != BusinessSubmitStatus::kAccepted) {
            return false;
        }

        std::this_thread::sleep_for(100ms);
        {
            std::lock_guard<std::mutex> lock(mutex);
            release = true;
        }
        cv.notify_all();

        executor.ShutdownGraceful();
        const auto stats = executor.GetStats();

        return
            expired_work.load(std::memory_order_relaxed) == 0 &&
            stats.deadline_expired_before_start_total == 1 &&
            LifecycleClean(stats);
    };

    return
        Expect(run_stale_case(), "cancelable stale Session work is skipped") &&
        Expect(run_deadline_case(), "queued expired work is skipped");
}

bool TestMustRunCompletionFence() {
    BusinessExecutorOptions options;
    options.worker_threads = 1;
    options.max_pending_tasks = 4;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 4;
    options.default_deadline = 2s;
    options.shutdown_timeout = 2s;

    BusinessExecutor executor(options);
    if (!executor.Start()) {
        return Expect(false, "must-run fence executor starts");
    }

    std::atomic<int> work_count{0};
    std::atomic<int> dispatcher_count{0};
    std::atomic<int> completion_count{0};

    BusinessExecutor::TaskSpec task;
    task.request.operation = "acceptance.must_run_completion_fence";
    task.cancellation_policy = BusinessCancellationPolicy::kMustRun;
    task.still_valid = []() { return false; };
    task.completion_still_valid = []() { return false; };
    task.work = [&](const BusinessExecutor::ExecutionContext&) {
        work_count.fetch_add(1, std::memory_order_relaxed);
        return BusinessExecutor::Completion([&]() {
            completion_count.fetch_add(1, std::memory_order_relaxed);
        });
    };
    task.dispatcher = [&](BusinessExecutor::Completion completion) {
        dispatcher_count.fetch_add(1, std::memory_order_relaxed);
        if (completion) {
            completion();
        }
    };

    const auto status = executor.Submit(std::move(task));
    executor.ShutdownGraceful();
    const auto stats = executor.GetStats();

    return
        Expect(status == BusinessSubmitStatus::kAccepted,
               "must-run task is admitted") &&
        Expect(work_count.load() == 1, "must-run work survives stale Session") &&
        Expect(dispatcher_count.load() == 0,
               "stale completion is fenced before dispatch") &&
        Expect(completion_count.load() == 0, "stale completion does not execute") &&
        Expect(stats.completion_dropped_total == 1,
               "stale completion increments dropped counter") &&
        Expect(LifecycleClean(stats), "must-run completion fence lifecycle residue=0");
}

bool TestDrainRejectsNewAndWaitsAccepted() {
    BusinessExecutorOptions options;
    options.worker_threads = 1;
    options.max_pending_tasks = 2;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 2;
    options.default_deadline = 3s;
    options.shutdown_timeout = 3s;

    BusinessExecutor executor(options);
    if (!executor.Start()) {
        return Expect(false, "drain executor starts");
    }

    std::mutex mutex;
    std::condition_variable cv;
    bool first_started = false;
    bool release = false;
    std::atomic<int> executed{0};

    BusinessExecutor::TaskSpec first;
    first.request.operation = "acceptance.drain.first";
    first.cancellation_policy = BusinessCancellationPolicy::kMustRun;
    first.work = [&](const BusinessExecutor::ExecutionContext&) {
        executed.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(mutex);
            first_started = true;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return release; });
        return BusinessExecutor::Completion{};
    };

    if (executor.Submit(std::move(first)) != BusinessSubmitStatus::kAccepted) {
        return Expect(false, "drain first accepted");
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!WaitFor(cv, lock, [&]() { return first_started; })) {
            return Expect(false, "drain first starts");
        }
    }

    auto second = MakeNoopMustRun("acceptance.drain.second");
    second.work = [&](const BusinessExecutor::ExecutionContext&) {
        executed.fetch_add(1, std::memory_order_relaxed);
        return BusinessExecutor::Completion{};
    };
    if (executor.Submit(std::move(second)) != BusinessSubmitStatus::kAccepted) {
        return Expect(false, "drain second accepted");
    }

    executor.BeginDrain();

    auto third = MakeNoopMustRun("acceptance.drain.third");
    const auto third_status = executor.Submit(std::move(third));

    std::atomic<bool> shutdown_returned{false};
    bool within_budget = false;
    std::thread shutdown_thread([&]() {
        within_budget = executor.ShutdownGraceful();
        shutdown_returned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(75ms);
    const bool blocked_before_release =
        !shutdown_returned.load(std::memory_order_acquire);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    cv.notify_all();
    shutdown_thread.join();

    const auto stats = executor.GetStats();

    return
        Expect(third_status == BusinessSubmitStatus::kShuttingDown,
               "BeginDrain rejects new admission") &&
        Expect(blocked_before_release, "shutdown waits accepted work") &&
        Expect(within_budget, "normal drain remains within budget") &&
        Expect(executed.load() == 2, "all accepted work completes") &&
        Expect(stats.rejected_shutdown_total == 1,
               "shutdown rejection counter increments") &&
        Expect(LifecycleClean(stats), "drain lifecycle residue=0");
}

bool TestShutdownBudgetCoversWorkerDrain() {
    BusinessExecutorOptions options;
    options.worker_threads = 1;
    options.max_pending_tasks = 2;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 2;
    options.default_deadline = 2s;
    options.shutdown_timeout = 50ms;

    BusinessExecutor executor(options);
    if (!executor.Start()) {
        return Expect(false, "shutdown budget executor starts");
    }

    std::mutex mutex;
    std::condition_variable cv;
    bool started = false;
    std::atomic<int> completed{0};

    BusinessExecutor::TaskSpec task;
    task.request.operation = "acceptance.shutdown_budget";
    task.cancellation_policy = BusinessCancellationPolicy::kMustRun;
    task.work = [&](const BusinessExecutor::ExecutionContext&) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            started = true;
        }
        cv.notify_all();
        std::this_thread::sleep_for(160ms);
        completed.fetch_add(1, std::memory_order_relaxed);
        return BusinessExecutor::Completion{};
    };

    if (executor.Submit(std::move(task)) != BusinessSubmitStatus::kAccepted) {
        return Expect(false, "shutdown budget task accepted");
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!WaitFor(cv, lock, [&]() { return started; })) {
            return Expect(false, "shutdown budget task starts");
        }
    }

    const auto begin = BusinessClock::now();
    const bool within_budget = executor.ShutdownGraceful();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        BusinessClock::now() - begin
    );
    const auto stats = executor.GetStats();

    return
        Expect(!within_budget, "shutdown reports exceeded lifecycle budget") &&
        Expect(elapsed >= 100ms, "shutdown safely waits long accepted work") &&
        Expect(completed.load() == 1, "long accepted work is not force-dropped") &&
        Expect(LifecycleClean(stats), "shutdown budget lifecycle residue=0");
}

bool TestPendingCompletionBlocksShutdown() {
    BusinessExecutorOptions options;
    options.worker_threads = 1;
    options.max_pending_tasks = 4;
    options.stripe_count = 8;
    options.per_stripe_queue_capacity = 4;
    options.default_deadline = 2s;
    options.shutdown_timeout = 2s;

    BusinessExecutor executor(options);
    if (!executor.Start()) {
        return Expect(false, "pending completion executor starts");
    }

    std::mutex mutex;
    std::condition_variable cv;
    bool dispatched = false;
    BusinessExecutor::Completion pending_completion;
    std::atomic<int> completion_count{0};

    BusinessExecutor::TaskSpec task;
    task.request.operation = "acceptance.pending_completion";
    task.cancellation_policy = BusinessCancellationPolicy::kMustRun;
    task.work = [&](const BusinessExecutor::ExecutionContext&) {
        return BusinessExecutor::Completion([&]() {
            completion_count.fetch_add(1, std::memory_order_relaxed);
        });
    };
    task.dispatcher = [&](BusinessExecutor::Completion completion) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            pending_completion = std::move(completion);
            dispatched = true;
        }
        cv.notify_all();
    };

    if (executor.Submit(std::move(task)) != BusinessSubmitStatus::kAccepted) {
        return Expect(false, "pending completion task accepted");
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!WaitFor(cv, lock, [&]() { return dispatched; })) {
            return Expect(false, "completion reaches dispatcher");
        }
    }

    const auto before = executor.GetStats();
    if (!Expect(before.pending_completions == 1,
                "pending completion is part of runtime lifecycle")) {
        return false;
    }

    std::atomic<bool> shutdown_returned{false};
    bool within_budget = false;
    std::thread shutdown_thread([&]() {
        within_budget = executor.ShutdownGraceful();
        shutdown_returned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(75ms);
    const bool blocked = !shutdown_returned.load(std::memory_order_acquire);

    BusinessExecutor::Completion to_run;
    {
        std::lock_guard<std::mutex> lock(mutex);
        to_run = std::move(pending_completion);
    }
    if (to_run) {
        to_run();
    }

    shutdown_thread.join();
    const auto stats = executor.GetStats();

    return
        Expect(blocked, "shutdown waits pending EventLoop completion") &&
        Expect(within_budget, "completion drain finishes within budget") &&
        Expect(completion_count.load() == 1, "pending completion executes once") &&
        Expect(LifecycleClean(stats), "pending completion lifecycle residue=0");
}

bool TestCompletionFailureIsolation() {
    auto dispatcher_exception_case = []() {
        BusinessExecutorOptions options;
        options.worker_threads = 1;
        options.max_pending_tasks = 4;
        options.stripe_count = 8;
        options.per_stripe_queue_capacity = 4;
        options.default_deadline = 2s;
        options.shutdown_timeout = 2s;

        BusinessExecutor executor(options);
        if (!executor.Start()) {
            return false;
        }

        BusinessExecutor::TaskSpec bad;
        bad.request.operation = "acceptance.dispatcher_exception";
        bad.cancellation_policy = BusinessCancellationPolicy::kMustRun;
        bad.work = [](const BusinessExecutor::ExecutionContext&) {
            return BusinessExecutor::Completion([]() {});
        };
        bad.dispatcher = [](BusinessExecutor::Completion) {
            throw std::runtime_error("intentional dispatcher exception");
        };

        if (executor.Submit(std::move(bad)) != BusinessSubmitStatus::kAccepted) {
            return false;
        }

        std::atomic<int> good_count{0};
        auto good = MakeNoopMustRun("acceptance.dispatcher_after_good");
        good.work = [&](const BusinessExecutor::ExecutionContext&) {
            good_count.fetch_add(1, std::memory_order_relaxed);
            return BusinessExecutor::Completion{};
        };
        if (executor.Submit(std::move(good)) != BusinessSubmitStatus::kAccepted) {
            return false;
        }

        executor.ShutdownGraceful();
        const auto stats = executor.GetStats();
        return
            stats.completion_dropped_total == 1 &&
            good_count.load() == 1 &&
            LifecycleClean(stats);
    };

    auto completion_exception_case = []() {
        BusinessExecutorOptions options;
        options.worker_threads = 1;
        options.max_pending_tasks = 4;
        options.stripe_count = 8;
        options.per_stripe_queue_capacity = 4;
        options.default_deadline = 2s;
        options.shutdown_timeout = 2s;

        BusinessExecutor executor(options);
        if (!executor.Start()) {
            return false;
        }

        BusinessExecutor::TaskSpec bad;
        bad.request.operation = "acceptance.completion_exception";
        bad.cancellation_policy = BusinessCancellationPolicy::kMustRun;
        bad.work = [](const BusinessExecutor::ExecutionContext&) {
            return BusinessExecutor::Completion([]() {
                throw std::runtime_error("intentional completion exception");
            });
        };
        bad.dispatcher = [](BusinessExecutor::Completion completion) {
            if (completion) {
                completion();
            }
        };

        if (executor.Submit(std::move(bad)) != BusinessSubmitStatus::kAccepted) {
            return false;
        }

        std::atomic<int> good_count{0};
        auto good = MakeNoopMustRun("acceptance.completion_after_good");
        good.work = [&](const BusinessExecutor::ExecutionContext&) {
            good_count.fetch_add(1, std::memory_order_relaxed);
            return BusinessExecutor::Completion{};
        };
        if (executor.Submit(std::move(good)) != BusinessSubmitStatus::kAccepted) {
            return false;
        }

        executor.ShutdownGraceful();
        const auto stats = executor.GetStats();
        return
            stats.completion_exception_total == 1 &&
            good_count.load() == 1 &&
            LifecycleClean(stats);
    };

    return
        Expect(dispatcher_exception_case(),
               "dispatcher exception is isolated and lifecycle converges") &&
        Expect(completion_exception_case(),
               "completion exception is isolated and runtime survives");
}

struct TestCase {
    const char* name;
    bool (*function)();
};

}  // namespace

int main() {
    const std::vector<TestCase> tests = {
        {"BusinessRuntimeAcceptance.ProductionBaselineStartDrain",
         &TestProductionBaselineStartDrain},
        {"BusinessRuntimeAcceptance.InvalidRuntimeConfigurationRejected",
         &TestInvalidRuntimeConfigurationRejected},
        {"BusinessRuntimeAcceptance.HotKeyIsolationPreservesOtherKey",
         &TestHotKeyIsolationPreservesOtherKey},
        {"BusinessRuntimeAcceptance.GlobalOverloadFastReject",
         &TestGlobalOverloadFastReject},
        {"BusinessRuntimeAcceptance.CancelableStaleAndDeadline",
         &TestCancelableStaleAndDeadline},
        {"BusinessRuntimeAcceptance.MustRunCompletionFence",
         &TestMustRunCompletionFence},
        {"BusinessRuntimeAcceptance.DrainRejectsNewAndWaitsAccepted",
         &TestDrainRejectsNewAndWaitsAccepted},
        {"BusinessRuntimeAcceptance.ShutdownBudgetCoversWorkerDrain",
         &TestShutdownBudgetCoversWorkerDrain},
        {"BusinessRuntimeAcceptance.PendingCompletionBlocksShutdown",
         &TestPendingCompletionBlocksShutdown},
        {"BusinessRuntimeAcceptance.CompletionFailureIsolation",
         &TestCompletionFailureIsolation},
    };

    std::size_t failed = 0;

    std::cout << "========== TinyIMX M13-C2 Business Runtime Acceptance ==========" << '\n';

    for (const auto& test : tests) {
        bool ok = false;
        try {
            ok = test.function();
        } catch (const std::exception& e) {
            std::cerr << "[FAIL] " << test.name << ", exception=" << e.what() << '\n';
        } catch (...) {
            std::cerr << "[FAIL] " << test.name << ", exception=unknown" << '\n';
        }

        if (ok) {
            std::cout << "[PASS] " << test.name << '\n';
        } else {
            ++failed;
            std::cout << "[FAIL] " << test.name << '\n';
        }
    }

    std::cout << "=================================================================" << '\n'
              << "total=" << tests.size() << ", failed=" << failed << '\n';

    return failed == 0 ? 0 : 1;
}
