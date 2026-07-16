#pragma once

#include "common/concurrency/MpmcBlockingQueue.h"
#include "common/concurrency/ThreadPoolTypes.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>
#include <condition_variable>


namespace tinyimx {

class ThreadPool {
public:
    using Task = std::function<void()>;

public:
    explicit ThreadPool(ThreadPoolOptions options);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool Start();

    template <typename F, typename... Args>
    auto Submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;

        if (!IsAcceptingTasks()) {
            rejected_task_count_.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("thread pool is not accepting tasks");
        }

        auto bound_task = std::bind(
            std::forward<F>(f),
            std::forward<Args>(args)...
        );

        auto packaged_task = std::make_shared<std::packaged_task<ReturnType()>>(
            [this, bound_task = std::move(bound_task)]() mutable -> ReturnType {
                const auto start_time = std::chrono::steady_clock::now();

                try {
                    if constexpr (std::is_void_v<ReturnType>) {
                        bound_task();

                        const auto end_time = std::chrono::steady_clock::now();
                        RecordTaskFinished(
                            true,
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                end_time - start_time
                            )
                        );

                        return;
                    } else {
                        ReturnType result = bound_task();

                        const auto end_time = std::chrono::steady_clock::now();
                        RecordTaskFinished(
                            true,
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                end_time - start_time
                            )
                        );

                        return result;
                    }
                } catch (...) {
                    const auto end_time = std::chrono::steady_clock::now();
                    RecordTaskFinished(
                        false,
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            end_time - start_time
                        )
                    );

                    throw;
                }
            }
        );

        std::future<ReturnType> future = packaged_task->get_future();

        Task task = [packaged_task]() {
            (*packaged_task)();
        };

        TaskPushResult push_result =
            task_queue_.Push(std::move(task), options_.queue_full_policy);

        if (push_result != TaskPushResult::kOk) {
            rejected_task_count_.fetch_add(1, std::memory_order_relaxed);

            if (push_result == TaskPushResult::kStopped) {
                throw std::runtime_error("thread pool queue is stopped");
            }

            throw std::runtime_error("thread pool task rejected");
        }

        submitted_task_count_.fetch_add(1, std::memory_order_relaxed);
        UpdatePeakQueueSize(task_queue_.Size());

        return future;
    }

    void Shutdown(
        ShutdownMode mode = ShutdownMode::kGraceful,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)
    );

    bool Pause();
    bool Resume();

    ThreadPoolState State() const;
    ThreadPoolStats GetStats() const;

    std::size_t WorkerCount() const;
    std::size_t QueueSize() const;
    std::size_t ActiveThreadCount() const;

    bool IsRunning() const;


private:

    struct WorkerSlot {
        std::size_t worker_id{0};
        std::atomic<bool> should_exit{false};
        std::atomic<bool> exited{false};
        std::thread thread;
    };

    ThreadPoolOptions options_;
    MpmcBlockingQueue<Task> task_queue_;

    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex pause_mutex_;
    std::condition_variable pause_cv_;

    //std::vector<std::thread> workers_;
    mutable std::mutex workers_mutex_;
    std::vector<std::unique_ptr<WorkerSlot>> worker_slots_;

    std::atomic<int> state_value_{static_cast<int>(ThreadPoolState::kCreated)};

    std::atomic<std::size_t> submitted_task_count_{0};
    std::atomic<std::size_t> completed_task_count_{0};
    std::atomic<std::size_t> failed_task_count_{0};
    std::atomic<std::size_t> rejected_task_count_{0};

    std::atomic<std::size_t> active_thread_count_{0};
    std::atomic<std::size_t> exited_worker_count_{0};

    std::atomic<std::size_t> peak_queue_size_{0};
    std::atomic<std::uint64_t> total_task_time_ns_{0};

    std::thread manager_thread_;
    std::atomic<bool> manager_running_{false};

    std::atomic<std::size_t> current_worker_count_{0};
    std::atomic<std::size_t> peak_worker_count_{0};
    std::atomic<std::size_t> threads_created_count_{0};
    std::atomic<std::size_t> threads_destroyed_count_{0};
    std::atomic<std::size_t> dynamic_resize_count_{0};

    std::atomic<std::size_t> pending_worker_exit_count_{0};
    std::atomic<std::size_t> next_worker_id_{0};


private:
    //void WorkerLoop(std::size_t worker_index);
    void WorkerLoop(WorkerSlot* slot);
    bool IsAcceptingTasks() const;
    bool WaitIfPaused();

    void RecordTaskFinished(bool success,
                            std::chrono::nanoseconds elapsed_time);
    void UpdatePeakQueueSize(std::size_t queue_size);

    bool CompareExchangeState(ThreadPoolState expected,
                              ThreadPoolState desired);

    void JoinWorkers();

    void ManagerLoop();

    bool AddWorker();
    bool RequestWorkerExit(std::size_t count);
    bool ShouldWorkerExitForScaleDown();

    void CleanupExitedWorkers();
    void StopManager();

    void UpdatePeakWorkerCount(std::size_t worker_count);
};

}  // namespace tinyimx