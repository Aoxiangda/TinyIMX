#pragma once

#include "common/config/ConfigTypes.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tinyimx {

    enum class ThreadPoolState {
        kCreated = 0,
        kRunning,
        kPaused,
        kShuttingDown,
        kForceStopping,
        kStopped
    };

    enum class ShutdownMode {
        kGraceful = 0,
        kForce,
        kTimeout
    };

    enum class TaskPushResult {
        kOk = 0,
        kDiscarded,
        kStopped
    };

struct ThreadPoolOptions {
    std::string name{"default"};

    // 启动时 worker 数量。
    std::size_t worker_threads{4};

    // 有界任务队列容量。
    std::size_t queue_capacity{1024};

    QueueFullPolicy queue_full_policy{QueueFullPolicy::kBlock};

    // v1.1 动态扩缩容配置。
    bool enable_dynamic_resize{false};
    std::size_t min_threads{2};
    std::size_t max_threads{8};

    double scale_up_threshold{0.75};
    double scale_down_threshold{0.20};

    std::chrono::milliseconds manager_check_interval{1000};
    std::chrono::milliseconds scale_cooldown{3000};
    std::chrono::milliseconds worker_idle_timeout{5000};

    std::chrono::milliseconds shutdown_timeout{30000};

    static ThreadPoolOptions FromConfig(const ThreadPoolConfig& config) {
        ThreadPoolOptions options;

        options.worker_threads = config.worker_threads;
        options.queue_capacity = config.queue_capacity;
        options.queue_full_policy = config.queue_full_policy;

        options.enable_dynamic_resize = config.enable_dynamic_resize;
        options.min_threads = config.min_threads;
        options.max_threads = config.max_threads;

        options.scale_up_threshold = config.scale_up_threshold;
        options.scale_down_threshold = config.scale_down_threshold;

        options.manager_check_interval =
            std::chrono::milliseconds(config.manager_thread_interval_ms);

        options.scale_cooldown =
            std::chrono::milliseconds(config.scale_cooldown_ms);

        options.worker_idle_timeout =
            std::chrono::milliseconds(config.worker_idle_timeout_ms);

        return options;
    }
};


    struct ThreadPoolStats {
        std::size_t submitted_task_count{0};
        std::size_t completed_task_count{0};
        std::size_t failed_task_count{0};
        std::size_t rejected_task_count{0};


        std::size_t discarded_task_count{0};
        std::size_t overwritten_task_count{0};
        std::size_t shutdown_discarded_task_count{0};

        std::size_t worker_count{0};
        std::size_t active_thread_count{0};

        std::size_t current_worker_count{0};
        std::size_t peak_worker_count{0};

        std::size_t threads_created_count{0};
        std::size_t threads_destroyed_count{0};
        std::size_t dynamic_resize_count{0};

        bool dynamic_resize_enabled{false};
        std::size_t current_queue_size{0};
        std::size_t queue_capacity{0};
        std::size_t peak_queue_size{0};


        double queue_usage_rate{0.0};
        double peak_queue_usage_rate{0.0};
        double average_task_time_ms{0.0};

        ThreadPoolState state{ThreadPoolState::kCreated};
    };

    std::string ThreadPoolStateToString(ThreadPoolState state);

} // namespace tinyimx