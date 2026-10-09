#pragma once

#include "common/concurrency/ThreadPoolTypes.h"

#include <chrono>
#include <cstddef>
#include <string>

namespace tinyimx {

enum class BenchmarkMode {
    kFixedCount = 0,
    kQueuePolicy,
    kDynamicResize
};

struct ThreadPoolBenchmarkConfig {
    BenchmarkMode mode{BenchmarkMode::kFixedCount};

    std::string name{"thread_pool_benchmark"};

    std::size_t worker_threads{4};
    std::size_t min_threads{2};
    std::size_t max_threads{8};

    std::size_t queue_capacity{1024};
    QueueFullPolicy queue_full_policy{QueueFullPolicy::kBlock};

    bool enable_dynamic_resize{false};

    double scale_up_threshold{0.75};
    double scale_down_threshold{0.20};

    std::chrono::milliseconds manager_check_interval{100};
    std::chrono::milliseconds scale_cooldown{300};
    std::chrono::milliseconds worker_idle_timeout{300};

    std::size_t total_tasks{100000};
    std::size_t submit_thread_count{4};

    std::chrono::milliseconds task_sleep{0};

    bool enable_monitoring{true};
    std::chrono::milliseconds monitoring_interval{1000};
};

struct ThreadPoolBenchmarkResult {
    std::string name;
    BenchmarkMode mode{BenchmarkMode::kFixedCount};

    // attempted_tasks：用户尝试提交的任务总数。
    // accepted_tasks：线程池实际接受的任务数。
    std::size_t attempted_tasks{0};
    std::size_t accepted_tasks{0};

    std::size_t completed_tasks{0};
    std::size_t failed_tasks{0};
    std::size_t rejected_tasks{0};

    std::size_t discarded_tasks{0};
    std::size_t overwritten_tasks{0};
    std::size_t shutdown_discarded_tasks{0};

    double elapsed_ms{0.0};
    double throughput_per_second{0.0};

    std::size_t initial_worker_count{0};

    // 对于 dynamic benchmark，这里表示 idle 观察阶段的 worker 数。
    // 对于 fixed / queue benchmark，这里通常是 shutdown 后的 worker 数。
    std::size_t current_worker_count{0};

    std::size_t peak_worker_count{0};
    std::size_t threads_created_count{0};
    std::size_t threads_destroyed_count{0};
    std::size_t dynamic_resize_count{0};

    // dynamic benchmark 专用：Shutdown 后的最终状态。
    std::size_t final_current_worker_count{0};
    std::size_t final_threads_destroyed_count{0};

    std::size_t queue_capacity{0};
    std::size_t peak_queue_size{0};
    double peak_queue_usage_rate{0.0};

    double average_task_time_ms{0.0};
};

class ThreadPoolBenchmark {
public:
    explicit ThreadPoolBenchmark(ThreadPoolBenchmarkConfig config);

    ThreadPoolBenchmarkResult Run();

    static void PrintResult(const ThreadPoolBenchmarkResult& result);

    static bool AppendCsvResult(
        const ThreadPoolBenchmarkResult& result,
        const std::string& file_path
    );

private:
    ThreadPoolBenchmarkResult RunFixedCount();
    ThreadPoolBenchmarkResult RunQueuePolicy();
    ThreadPoolBenchmarkResult RunDynamicResize();

    ThreadPoolOptions BuildOptions(const std::string& pool_name) const;

    ThreadPoolBenchmarkResult BuildResult(
        const std::string& name,
        BenchmarkMode mode,
        std::size_t total_tasks,
        std::size_t submitted_tasks,
        double elapsed_ms,
        const ThreadPoolStats& stats
    ) const;

private:
    ThreadPoolBenchmarkConfig config_;
};

std::string BenchmarkModeToString(BenchmarkMode mode);

}  // namespace tinyimx