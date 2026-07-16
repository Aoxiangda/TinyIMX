#include "benchmark/thread_pool_benchmark.h"

#include "common/concurrency/ThreadPool.h"
#include "common/logging/LogMacros.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace tinyimx {
namespace {

using Clock = std::chrono::steady_clock;

void SleepIfNeeded(std::chrono::milliseconds duration) {
    if (duration.count() > 0) {
        std::this_thread::sleep_for(duration);
    }
}

}  // namespace

ThreadPoolBenchmark::ThreadPoolBenchmark(ThreadPoolBenchmarkConfig config)
    : config_(std::move(config)) {}

ThreadPoolBenchmarkResult ThreadPoolBenchmark::Run() {
    switch (config_.mode) {
        case BenchmarkMode::kFixedCount:
            return RunFixedCount();
        case BenchmarkMode::kQueuePolicy:
            return RunQueuePolicy();
        case BenchmarkMode::kDynamicResize:
            return RunDynamicResize();
        default:
            return RunFixedCount();
    }
}

ThreadPoolOptions ThreadPoolBenchmark::BuildOptions(
    const std::string& pool_name
) const {
    ThreadPoolOptions options;

    options.name = pool_name;
    options.worker_threads = config_.worker_threads;
    options.queue_capacity = config_.queue_capacity;
    options.queue_full_policy = config_.queue_full_policy;

    options.enable_dynamic_resize = config_.enable_dynamic_resize;
    options.min_threads = config_.min_threads;
    options.max_threads = config_.max_threads;

    options.scale_up_threshold = config_.scale_up_threshold;
    options.scale_down_threshold = config_.scale_down_threshold;
    options.manager_check_interval = config_.manager_check_interval;
    options.scale_cooldown = config_.scale_cooldown;
    options.worker_idle_timeout = config_.worker_idle_timeout;

    return options;
}

ThreadPoolBenchmarkResult ThreadPoolBenchmark::RunFixedCount() {
    LOG_INFO("thread pool fixed-count benchmark started");

    auto options = BuildOptions("benchmark-fixed-count-pool");
    options.enable_dynamic_resize = false;

    ThreadPool pool(options);
    pool.Start();

    std::atomic<std::size_t> completed_counter{0};
    std::atomic<std::size_t> submitted_counter{0};
    std::atomic<std::size_t> rejected_counter{0};

    const auto start_time = Clock::now();

    std::vector<std::thread> submit_threads;
    submit_threads.reserve(config_.submit_thread_count);

    const std::size_t tasks_per_thread =
        config_.total_tasks / config_.submit_thread_count;
    const std::size_t remaining_tasks =
        config_.total_tasks % config_.submit_thread_count;

    for (std::size_t t = 0; t < config_.submit_thread_count; ++t) {
        const std::size_t task_count =
            tasks_per_thread + (t == config_.submit_thread_count - 1
                                    ? remaining_tasks
                                    : 0);

        submit_threads.emplace_back([this,
                                     &pool,
                                     &completed_counter,
                                     &submitted_counter,
                                     &rejected_counter,
                                     task_count]() {
            for (std::size_t i = 0; i < task_count; ++i) {
                try {
                    pool.Submit([this, &completed_counter]() {
                        SleepIfNeeded(config_.task_sleep);
                        completed_counter.fetch_add(
                            1,
                            std::memory_order_relaxed
                        );
                    });

                    submitted_counter.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                } catch (const std::exception&) {
                    rejected_counter.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                }
            }
        });
    }

    for (auto& thread : submit_threads) {
        thread.join();
    }

    while (completed_counter.load(std::memory_order_relaxed) +
               rejected_counter.load(std::memory_order_relaxed) <
           config_.total_tasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto end_time = Clock::now();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(
            end_time - start_time
        ).count();

    pool.Shutdown(ShutdownMode::kGraceful);

    return BuildResult(
        "fixed_count",
        BenchmarkMode::kFixedCount,
        config_.total_tasks,
        submitted_counter.load(std::memory_order_relaxed),
        elapsed_ms,
        pool.GetStats()
    );
}

ThreadPoolBenchmarkResult ThreadPoolBenchmark::RunQueuePolicy() {
    LOG_INFO("thread pool queue-policy benchmark started");

    auto options = BuildOptions("benchmark-queue-policy-pool");
    options.enable_dynamic_resize = false;

    ThreadPool pool(options);
    pool.Start();

    std::atomic<std::size_t> completed_counter{0};
    std::atomic<std::size_t> submitted_counter{0};
    std::atomic<std::size_t> rejected_counter{0};

    const auto start_time = Clock::now();

    for (std::size_t i = 0; i < config_.total_tasks; ++i) {
        try {
            pool.Submit([this, &completed_counter]() {
                SleepIfNeeded(config_.task_sleep);
                completed_counter.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
            });

            submitted_counter.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception&) {
            rejected_counter.fetch_add(1, std::memory_order_relaxed);
        }
    }

    while (completed_counter.load(std::memory_order_relaxed) +
               rejected_counter.load(std::memory_order_relaxed) <
           config_.total_tasks) {
        const auto stats = pool.GetStats();

        const std::size_t accounted =
            completed_counter.load(std::memory_order_relaxed) +
            rejected_counter.load(std::memory_order_relaxed) +
            stats.overwritten_task_count;

        if (accounted >= config_.total_tasks) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto end_time = Clock::now();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(
            end_time - start_time
        ).count();

    pool.Shutdown(ShutdownMode::kGraceful);

    return BuildResult(
        "queue_policy",
        BenchmarkMode::kQueuePolicy,
        config_.total_tasks,
        submitted_counter.load(std::memory_order_relaxed),
        elapsed_ms,
        pool.GetStats()
    );
}

ThreadPoolBenchmarkResult ThreadPoolBenchmark::RunDynamicResize() {
    LOG_INFO("thread pool dynamic-resize benchmark started");

    auto options = BuildOptions("benchmark-dynamic-resize-pool");
    options.enable_dynamic_resize = true;

    ThreadPool pool(options);
    pool.Start();

    std::atomic<std::size_t> completed_counter{0};
    std::atomic<std::size_t> submitted_counter{0};
    std::atomic<std::size_t> rejected_counter{0};

    const auto start_time = Clock::now();

    std::vector<std::future<void>> futures;
    futures.reserve(config_.total_tasks);

    for (std::size_t i = 0; i < config_.total_tasks; ++i) {
        try {
            futures.push_back(pool.Submit([this, &completed_counter]() {
                SleepIfNeeded(config_.task_sleep);
                completed_counter.fetch_add(
                    1,
                    std::memory_order_relaxed
                );
            }));

            submitted_counter.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception&) {
            rejected_counter.fetch_add(1, std::memory_order_relaxed);
        }
    }
    /*
        for (auto& future : futures) {
            future.get();
        }

        std::this_thread::sleep_for(config_.worker_idle_timeout * 3);

        const auto end_time = Clock::now();
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(
                end_time - start_time
            ).count();

        pool.Shutdown(ShutdownMode::kGraceful);

        return BuildResult(
            "dynamic_resize",
            BenchmarkMode::kDynamicResize,
            config_.total_tasks,
            submitted_counter.load(std::memory_order_relaxed),
            elapsed_ms,
            pool.GetStats()
        );
    */
    for (auto& future : futures) {
        future.get();
    }

    const auto task_end_time = Clock::now();

    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(
            task_end_time - start_time
        ).count();

    // 等待 manager 执行多轮缩容，观察是否能回到 min_threads。
    const auto idle_wait_time =
        config_.worker_idle_timeout * 8 +
        config_.scale_cooldown * 8;

    std::this_thread::sleep_for(idle_wait_time);

    const ThreadPoolStats idle_stats = pool.GetStats();

    pool.Shutdown(ShutdownMode::kGraceful);

    const ThreadPoolStats final_stats = pool.GetStats();

    auto result = BuildResult(
        "dynamic_resize",
        BenchmarkMode::kDynamicResize,
        config_.total_tasks,
        submitted_counter.load(std::memory_order_relaxed),
        elapsed_ms,
        idle_stats
    );

    result.final_current_worker_count =
        final_stats.current_worker_count;

    result.final_threads_destroyed_count =
        final_stats.threads_destroyed_count;

    return result;

}

ThreadPoolBenchmarkResult ThreadPoolBenchmark::BuildResult(
    const std::string& name,
    BenchmarkMode mode,
    std::size_t attempted_tasks,
    std::size_t accepted_tasks,
    double elapsed_ms,
    const ThreadPoolStats& stats
) const {
    ThreadPoolBenchmarkResult result;

    result.name = name;
    result.mode = mode;

    result.attempted_tasks = attempted_tasks;
    result.accepted_tasks = accepted_tasks;

    result.completed_tasks = stats.completed_task_count;
    result.failed_tasks = stats.failed_task_count;
    result.rejected_tasks = stats.rejected_task_count;

    result.discarded_tasks = stats.discarded_task_count;
    result.overwritten_tasks = stats.overwritten_task_count;
    result.shutdown_discarded_tasks =
        stats.shutdown_discarded_task_count;

    result.elapsed_ms = elapsed_ms;

    if (elapsed_ms > 0.0) {
        result.throughput_per_second =
            static_cast<double>(result.completed_tasks) /
            (elapsed_ms / 1000.0);
    }

    result.initial_worker_count = stats.worker_count;
    result.current_worker_count = stats.current_worker_count;
    result.peak_worker_count = stats.peak_worker_count;
    result.threads_created_count = stats.threads_created_count;
    result.threads_destroyed_count = stats.threads_destroyed_count;
    result.dynamic_resize_count = stats.dynamic_resize_count;

    result.final_current_worker_count = stats.current_worker_count;
    result.final_threads_destroyed_count = stats.threads_destroyed_count;

    result.queue_capacity = stats.queue_capacity;
    result.peak_queue_size = stats.peak_queue_size;
    result.peak_queue_usage_rate = stats.peak_queue_usage_rate;
    result.average_task_time_ms = stats.average_task_time_ms;

    return result;
}

void ThreadPoolBenchmark::PrintResult(const ThreadPoolBenchmarkResult& result) {
    std::cout << "\n========== ThreadPool Benchmark ==========\n";
    std::cout << "name = " << result.name << '\n';
    std::cout << "mode = " << BenchmarkModeToString(result.mode) << '\n';
    std::cout << "attempted_tasks = " << result.attempted_tasks << '\n';
    std::cout << "accepted_tasks = " << result.accepted_tasks << '\n';

    std::cout << "completed_tasks = " << result.completed_tasks << '\n';
    std::cout << "failed_tasks = " << result.failed_tasks << '\n';
    std::cout << "rejected_tasks = " << result.rejected_tasks << '\n';
    std::cout << "discarded_tasks = " << result.discarded_tasks << '\n';
    std::cout << "overwritten_tasks = " << result.overwritten_tasks << '\n';
    std::cout << "shutdown_discarded_tasks = "
              << result.shutdown_discarded_tasks << '\n';

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "elapsed_ms = " << result.elapsed_ms << '\n';
    std::cout << "throughput_per_second = "
              << result.throughput_per_second << '\n';

    std::cout << "initial_worker_count = "
              << result.initial_worker_count << '\n';
    std::cout << "current_worker_count = "
              << result.current_worker_count << '\n';
    std::cout << "peak_worker_count = "
              << result.peak_worker_count << '\n';
    std::cout << "threads_created_count = "
              << result.threads_created_count << '\n';
    std::cout << "threads_destroyed_count = "
              << result.threads_destroyed_count << '\n';
    std::cout << "dynamic_resize_count = "
              << result.dynamic_resize_count << '\n';
    std::cout << "final_current_worker_count = "
              << result.final_current_worker_count << '\n';
    std::cout << "final_threads_destroyed_count = "
              << result.final_threads_destroyed_count << '\n';

    std::cout << "queue_capacity = " << result.queue_capacity << '\n';
    std::cout << "peak_queue_size = " << result.peak_queue_size << '\n';
    std::cout << "peak_queue_usage_rate = "
              << result.peak_queue_usage_rate << '\n';
    std::cout << "average_task_time_ms = "
              << result.average_task_time_ms << '\n';
    // us
    std::cout << "average_task_time_us = "
              << result.average_task_time_ms * 1000.0 << '\n';
    std::cout << "==========================================\n";
}

std::string BenchmarkModeToString(BenchmarkMode mode) {
    switch (mode) {
        case BenchmarkMode::kFixedCount:
            return "fixed_count";
        case BenchmarkMode::kQueuePolicy:
            return "queue_policy";
        case BenchmarkMode::kDynamicResize:
            return "dynamic_resize";
        default:
            return "unknown";
    }
}

bool ThreadPoolBenchmark::AppendCsvResult(
    const ThreadPoolBenchmarkResult& result,
    const std::string& file_path
){
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path output_path(file_path);

        const fs::path parent_path = output_path.parent_path();
    if (!parent_path.empty()) {
        fs::create_directories(parent_path, ec);
        if (ec) {
            return false;
        }
    }

    bool need_header = true;

    if (fs::exists(output_path, ec) && !ec) {
        need_header = fs::file_size(output_path, ec) == 0;
        if (ec) {
            return false;
        }
    }

    std::ofstream file(file_path, std::ios::app);
    if (!file.is_open()) {
        return false;
    }

    if (need_header) {
        file << "name,mode,attempted_tasks,accepted_tasks,"
             << "completed_tasks,failed_tasks,rejected_tasks,"
             << "discarded_tasks,overwritten_tasks,"
             << "shutdown_discarded_tasks,elapsed_ms,"
             << "throughput_per_second,initial_worker_count,"
             << "current_worker_count,peak_worker_count,"
             << "threads_created_count,threads_destroyed_count,"
             << "dynamic_resize_count,final_current_worker_count,"
             << "final_threads_destroyed_count,queue_capacity,"
             << "peak_queue_size,peak_queue_usage_rate,"
             << "average_task_time_ms\n";
    }

    file << result.name << ','
         << BenchmarkModeToString(result.mode) << ','
         << result.attempted_tasks << ','
         << result.accepted_tasks << ','
         << result.completed_tasks << ','
         << result.failed_tasks << ','
         << result.rejected_tasks << ','
         << result.discarded_tasks << ','
         << result.overwritten_tasks << ','
         << result.shutdown_discarded_tasks << ','
         << result.elapsed_ms << ','
         << result.throughput_per_second << ','
         << result.initial_worker_count << ','
         << result.current_worker_count << ','
         << result.peak_worker_count << ','
         << result.threads_created_count << ','
         << result.threads_destroyed_count << ','
         << result.dynamic_resize_count << ','
         << result.final_current_worker_count << ','
         << result.final_threads_destroyed_count << ','
         << result.queue_capacity << ','
         << result.peak_queue_size << ','
         << result.peak_queue_usage_rate << ','
         << result.average_task_time_ms << '\n';

    return true;
}
}  // namespace tinyimx