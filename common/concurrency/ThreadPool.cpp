#include "common/concurrency/ThreadPool.h"

#include "common/logging/LogMacros.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

namespace tinyimx {

ThreadPool::ThreadPool(ThreadPoolOptions options)
    : options_(std::move(options)),
      task_queue_(options_.queue_capacity) {
    if (options_.worker_threads == 0) {
        throw std::invalid_argument("thread pool worker_threads must be greater than 0");
    }

    if (options_.queue_capacity == 0) {
        throw std::invalid_argument("thread pool queue_capacity must be greater than 0");
    }
}

ThreadPool::~ThreadPool() {
    Shutdown(ShutdownMode::kForce);
}

bool ThreadPool::Start() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);

    if (!CompareExchangeState(ThreadPoolState::kCreated,
                              ThreadPoolState::kRunning)) {
        return false;
    }
    /*

        workers_.reserve(options_.worker_threads);

        for (std::size_t i = 0; i < options_.worker_threads; ++i) {
            workers_.emplace_back([this, i]() {
                WorkerLoop(i);
            });
        }
    */
    current_worker_count_.store(0, std::memory_order_relaxed);
    peak_worker_count_.store(0, std::memory_order_relaxed);
    threads_created_count_.store(0, std::memory_order_relaxed);
    threads_destroyed_count_.store(0, std::memory_order_relaxed);
    dynamic_resize_count_.store(0, std::memory_order_relaxed);
    pending_worker_exit_count_.store(0, std::memory_order_relaxed);
    next_worker_id_.store(0, std::memory_order_relaxed);
    exited_worker_count_.store(0, std::memory_order_relaxed);

    for (std::size_t i = 0; i < options_.worker_threads; ++i) {
        if (!AddWorker()) {
            LOG_ERROR("failed to add initial worker"
                    << ", name=" << options_.name
                    << ", index=" << i);
            Shutdown(ShutdownMode::kForce);
            return false;
        }
    }

    if (options_.enable_dynamic_resize) {
        manager_running_.store(true, std::memory_order_relaxed);
        manager_thread_ = std::thread([this]() {
            ManagerLoop();
        });

        LOG_INFO("thread pool dynamic resize enabled"
                << ", name=" << options_.name
                << ", min_threads=" << options_.min_threads
                << ", max_threads=" << options_.max_threads
                << ", scale_up_threshold=" << options_.scale_up_threshold
                << ", scale_down_threshold=" << options_.scale_down_threshold);
    }


    LOG_INFO("thread pool started"
             << ", name=" << options_.name
             << ", worker_threads=" << options_.worker_threads
             << ", queue_capacity=" << options_.queue_capacity);

    return true;
}

void ThreadPool::Shutdown(ShutdownMode mode,
                          std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);

    ThreadPoolState current_state = State();



    if (current_state == ThreadPoolState::kStopped) {
        return;
    }

    if (current_state == ThreadPoolState::kCreated) {
        state_value_.store(
            static_cast<int>(ThreadPoolState::kStopped),
            std::memory_order_relaxed
        );
        return;
    }
    StopManager();

    if (mode == ShutdownMode::kForce) {
        state_value_.store(
            static_cast<int>(ThreadPoolState::kForceStopping),
            std::memory_order_relaxed
        );

        task_queue_.Stop(true);
        pause_cv_.notify_all();

        JoinWorkers();

        state_value_.store(
            static_cast<int>(ThreadPoolState::kStopped),
            std::memory_order_relaxed
        );

        LOG_INFO("thread pool force stopped, name=" << options_.name);
        return;
    }

    state_value_.store(
        static_cast<int>(ThreadPoolState::kShuttingDown),
        std::memory_order_relaxed
    );

    task_queue_.Stop(false);
    pause_cv_.notify_all();

    if (mode == ShutdownMode::kTimeout) {
        const auto start_time = std::chrono::steady_clock::now();

        while (exited_worker_count_.load(std::memory_order_relaxed) <
               options_.worker_threads) {
            const auto now = std::chrono::steady_clock::now();

            if (now - start_time >= timeout) {
                LOG_WARN("thread pool shutdown timeout, switch to force stop"
                         << ", name=" << options_.name);

                state_value_.store(
                    static_cast<int>(ThreadPoolState::kForceStopping),
                    std::memory_order_relaxed
                );

                task_queue_.Stop(true);
                pause_cv_.notify_all();
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    JoinWorkers();

    state_value_.store(
        static_cast<int>(ThreadPoolState::kStopped),
        std::memory_order_relaxed
    );

    LOG_INFO("thread pool stopped"
             << ", name=" << options_.name
             << ", submitted=" << submitted_task_count_.load(std::memory_order_relaxed)
             << ", completed=" << completed_task_count_.load(std::memory_order_relaxed)
             << ", failed=" << failed_task_count_.load(std::memory_order_relaxed)
             << ", rejected=" << rejected_task_count_.load(std::memory_order_relaxed));
}

bool ThreadPool::Pause() {
    bool ok = CompareExchangeState(ThreadPoolState::kRunning,
                                   ThreadPoolState::kPaused);

    if (ok) {
        LOG_INFO("thread pool paused, name=" << options_.name);
    }

    return ok;
}

bool ThreadPool::Resume() {
    bool ok = CompareExchangeState(ThreadPoolState::kPaused,
                                   ThreadPoolState::kRunning);

    if (ok) {
        pause_cv_.notify_all();
        LOG_INFO("thread pool resumed, name=" << options_.name);
    }

    return ok;
}

ThreadPoolState ThreadPool::State() const {
    return static_cast<ThreadPoolState>(
        state_value_.load(std::memory_order_relaxed)
    );
}

ThreadPoolStats ThreadPool::GetStats() const {
    ThreadPoolStats stats;

    stats.submitted_task_count =
        submitted_task_count_.load(std::memory_order_relaxed);
    stats.completed_task_count =
        completed_task_count_.load(std::memory_order_relaxed);
    stats.failed_task_count =
        failed_task_count_.load(std::memory_order_relaxed);
    stats.rejected_task_count =
        rejected_task_count_.load(std::memory_order_relaxed);

    stats.discarded_task_count = task_queue_.DiscardCounter();
    stats.overwritten_task_count = task_queue_.OverwriteCounter();
    stats.shutdown_discarded_task_count = task_queue_.ShutdownDiscardCounter();

    stats.worker_count = options_.worker_threads;
    stats.active_thread_count =
        active_thread_count_.load(std::memory_order_relaxed);

    stats.dynamic_resize_enabled = options_.enable_dynamic_resize;

    /*
        stats.current_worker_count = options_.worker_threads;
        stats.peak_worker_count = options_.worker_threads;
        stats.threads_created_count = options_.worker_threads;
        stats.threads_destroyed_count =
            exited_worker_count_.load(std::memory_order_relaxed);
        stats.dynamic_resize_count = 0;

    */
    // v1.1-Step1 暂时还没有动态扩缩容逻辑。
    // 当前实际 worker 数仍等于初始 worker 数。



    stats.current_worker_count =
        current_worker_count_.load(std::memory_order_relaxed);
    stats.peak_worker_count =
        peak_worker_count_.load(std::memory_order_relaxed);
    stats.threads_created_count =
        threads_created_count_.load(std::memory_order_relaxed);
    stats.threads_destroyed_count =
        threads_destroyed_count_.load(std::memory_order_relaxed);
    stats.dynamic_resize_count =
        dynamic_resize_count_.load(std::memory_order_relaxed);
    stats.dynamic_resize_enabled = options_.enable_dynamic_resize;

    stats.current_queue_size = task_queue_.Size();
    stats.queue_capacity = task_queue_.Capacity();
    stats.peak_queue_size =
        peak_queue_size_.load(std::memory_order_relaxed);

    if (stats.queue_capacity > 0) {
        stats.queue_usage_rate =
            static_cast<double>(stats.current_queue_size) /
            static_cast<double>(stats.queue_capacity);

        stats.peak_queue_usage_rate =
            static_cast<double>(stats.peak_queue_size) /
            static_cast<double>(stats.queue_capacity);
    }

    const std::size_t executed_task_count =
        stats.completed_task_count + stats.failed_task_count;

    if (executed_task_count > 0) {
        const std::uint64_t total_ns =
            total_task_time_ns_.load(std::memory_order_relaxed);

        stats.average_task_time_ms =
            static_cast<double>(total_ns) /
            static_cast<double>(executed_task_count) /
            1000000.0;
    }

    stats.state = State();

    return stats;
}

std::size_t ThreadPool::WorkerCount() const {
    return options_.worker_threads;
}

std::size_t ThreadPool::QueueSize() const {
    return task_queue_.Size();
}

std::size_t ThreadPool::ActiveThreadCount() const {
    return active_thread_count_.load(std::memory_order_relaxed);
}

bool ThreadPool::IsRunning() const {
    return State() == ThreadPoolState::kRunning;
}

void ThreadPool::WorkerLoop(WorkerSlot* slot) {
    if (slot == nullptr) {
        return;
    }

    const std::size_t worker_id = slot->worker_id;

    LOG_INFO("thread pool worker started"
             << ", name=" << options_.name
             << ", worker_id=" << worker_id);

    while (true) {
        if (slot->should_exit.load(std::memory_order_relaxed)) {
            break;
        }

        if (ShouldWorkerExitForScaleDown()) {
            LOG_INFO("thread pool worker exit for scale down"
                     << ", name=" << options_.name
                     << ", worker_id=" << worker_id);
            break;
        }

        Task task;
        bool has_task = false;

        if (options_.enable_dynamic_resize) {
            has_task = task_queue_.PopFor(&task, options_.worker_idle_timeout);
        } else {
            has_task = task_queue_.Pop(&task);
        }

        if (!has_task) {
            ThreadPoolState current_state = State();

            if (current_state == ThreadPoolState::kShuttingDown ||
                current_state == ThreadPoolState::kForceStopping ||
                current_state == ThreadPoolState::kStopped) {
                break;
            }

            if (options_.enable_dynamic_resize &&
                ShouldWorkerExitForScaleDown()) {
                LOG_INFO("thread pool idle worker exit for scale down"
                         << ", name=" << options_.name
                         << ", worker_id=" << worker_id);
                break;
            }

            continue;
        }

        if (!WaitIfPaused()) {
            break;
        }

        ThreadPoolState current_state = State();
        if (current_state == ThreadPoolState::kForceStopping ||
            current_state == ThreadPoolState::kStopped) {
            break;
        }

        active_thread_count_.fetch_add(1, std::memory_order_relaxed);

        try {
            task();
        } catch (const std::exception& e) {
            RecordTaskFinished(false, std::chrono::nanoseconds(0));
            LOG_ERROR("thread pool task escaped exception"
                      << ", name=" << options_.name
                      << ", worker_id=" << worker_id
                      << ", error=" << e.what());
        } catch (...) {
            RecordTaskFinished(false, std::chrono::nanoseconds(0));
            LOG_ERROR("thread pool task escaped unknown exception"
                      << ", name=" << options_.name
                      << ", worker_id=" << worker_id);
        }

        active_thread_count_.fetch_sub(1, std::memory_order_relaxed);
    }

    slot->exited.store(true, std::memory_order_relaxed);

    current_worker_count_.fetch_sub(1, std::memory_order_relaxed);
    threads_destroyed_count_.fetch_add(1, std::memory_order_relaxed);
    exited_worker_count_.fetch_add(1, std::memory_order_relaxed);

    LOG_INFO("thread pool worker exited"
             << ", name=" << options_.name
             << ", worker_id=" << worker_id);
}

bool ThreadPool::IsAcceptingTasks() const {
    ThreadPoolState state = State();

    return state == ThreadPoolState::kRunning ||
           state == ThreadPoolState::kPaused;
}

bool ThreadPool::WaitIfPaused() {
    std::unique_lock<std::mutex> lock(pause_mutex_);

    pause_cv_.wait(lock, [this]() {
        ThreadPoolState state = State();
        return state != ThreadPoolState::kPaused;
    });

    ThreadPoolState state = State();

    return state != ThreadPoolState::kForceStopping &&
           state != ThreadPoolState::kStopped;
}

void ThreadPool::RecordTaskFinished(
    bool success,
    std::chrono::nanoseconds elapsed_time
) {
    if (success) {
        completed_task_count_.fetch_add(1, std::memory_order_relaxed);
    } else {
        failed_task_count_.fetch_add(1, std::memory_order_relaxed);
    }

    if (elapsed_time.count() > 0) {
        total_task_time_ns_.fetch_add(
            static_cast<std::uint64_t>(elapsed_time.count()),
            std::memory_order_relaxed
        );
    }
}

void ThreadPool::UpdatePeakQueueSize(std::size_t queue_size) {
    std::size_t current_peak =
        peak_queue_size_.load(std::memory_order_relaxed);

    while (queue_size > current_peak &&
           !peak_queue_size_.compare_exchange_weak(
               current_peak,
               queue_size,
               std::memory_order_relaxed
           )) {
    }
}

bool ThreadPool::CompareExchangeState(ThreadPoolState expected,
                                      ThreadPoolState desired) {
    int expected_value = static_cast<int>(expected);
    int desired_value = static_cast<int>(desired);

    return state_value_.compare_exchange_strong(
        expected_value,
        desired_value,
        std::memory_order_relaxed
    );
}
/*
    void ThreadPool::JoinWorkers() {
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        workers_.clear();
    }
*/


void ThreadPool::JoinWorkers() {
    std::lock_guard<std::mutex> lock(workers_mutex_);

    for (auto& slot : worker_slots_) {
        if (slot && slot->thread.joinable()) {
            slot->thread.join();
        }
    }

    worker_slots_.clear();
}

bool ThreadPool::AddWorker() {
    std::lock_guard<std::mutex> lock(workers_mutex_);

    const std::size_t current_count =
        current_worker_count_.load(std::memory_order_relaxed);

    if (options_.enable_dynamic_resize &&
        current_count >= options_.max_threads) {
        return false;
    }

    if (!options_.enable_dynamic_resize &&
        current_count >= options_.worker_threads) {
        return false;
    }

    auto slot = std::make_unique<WorkerSlot>();
    slot->worker_id = next_worker_id_.fetch_add(1, std::memory_order_relaxed);

    WorkerSlot* raw_slot = slot.get();
    worker_slots_.push_back(std::move(slot));

    try {
        raw_slot->thread = std::thread([this, raw_slot]() {
            WorkerLoop(raw_slot);
        });
    } catch (...) {
        worker_slots_.pop_back();
        return false;
    }

    const std::size_t new_count =
        current_worker_count_.fetch_add(1, std::memory_order_relaxed) + 1;

    threads_created_count_.fetch_add(1, std::memory_order_relaxed);
    UpdatePeakWorkerCount(new_count);

    LOG_INFO("thread pool worker added"
             << ", name=" << options_.name
             << ", worker_id=" << raw_slot->worker_id
             << ", current_worker_count=" << new_count);

    return true;
}

bool ThreadPool::RequestWorkerExit(std::size_t count) {
    if (count == 0) {
        return false;
    }

    const std::size_t current_count =
        current_worker_count_.load(std::memory_order_relaxed);

    if (current_count <= options_.min_threads) {
        return false;
    }

    const std::size_t removable_count =
        std::min(count, current_count - options_.min_threads);

    if (removable_count == 0) {
        return false;
    }

    pending_worker_exit_count_.fetch_add(
        removable_count,
        std::memory_order_relaxed
    );

    dynamic_resize_count_.fetch_add(1, std::memory_order_relaxed);

    LOG_INFO("thread pool requested worker exit"
             << ", name=" << options_.name
             << ", count=" << removable_count
             << ", current_worker_count=" << current_count);

    return true;
}

bool ThreadPool::ShouldWorkerExitForScaleDown() {
    if (!options_.enable_dynamic_resize) {
        return false;
    }

    ThreadPoolState state = State();
    if (state != ThreadPoolState::kRunning &&
        state != ThreadPoolState::kPaused) {
        return false;
    }

    if (task_queue_.Size() > 0) {
        return false;
    }

    std::size_t pending =
        pending_worker_exit_count_.load(std::memory_order_relaxed);

    while (pending > 0) {
        const std::size_t current_count =
            current_worker_count_.load(std::memory_order_relaxed);

        if (current_count <= options_.min_threads) {
            return false;
        }

        if (pending_worker_exit_count_.compare_exchange_weak(
                pending,
                pending - 1,
                std::memory_order_relaxed)) {
            return true;
        }
    }

    return false;
}

void ThreadPool::ManagerLoop() {
    LOG_INFO("thread pool manager started"
             << ", name=" << options_.name
             << ", check_interval_ms="
             << options_.manager_check_interval.count());

    auto last_resize_time = std::chrono::steady_clock::now() -
                            options_.scale_cooldown;

    while (manager_running_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(options_.manager_check_interval);

        if (!manager_running_.load(std::memory_order_relaxed)) {
            break;
        }

        ThreadPoolState state = State();
        if (state != ThreadPoolState::kRunning &&
            state != ThreadPoolState::kPaused) {
            break;
        }

        CleanupExitedWorkers();

        const auto now = std::chrono::steady_clock::now();
        if (now - last_resize_time < options_.scale_cooldown) {
            continue;
        }

        const std::size_t queue_size = task_queue_.Size();
        const std::size_t queue_capacity = task_queue_.Capacity();
        const std::size_t current_workers =
            current_worker_count_.load(std::memory_order_relaxed);
        const std::size_t active_workers =
            active_thread_count_.load(std::memory_order_relaxed);

        double queue_usage_rate = 0.0;
        if (queue_capacity > 0) {
            queue_usage_rate =
                static_cast<double>(queue_size) /
                static_cast<double>(queue_capacity);
        }

        if (queue_usage_rate >= options_.scale_up_threshold &&
            current_workers < options_.max_threads) {
            if (AddWorker()) {
                dynamic_resize_count_.fetch_add(1, std::memory_order_relaxed);
                last_resize_time = now;

                LOG_INFO("thread pool scaled up"
                         << ", name=" << options_.name
                         << ", queue_usage_rate=" << queue_usage_rate
                         << ", current_workers="
                         << current_worker_count_.load(std::memory_order_relaxed));
            }

            continue;
        }

        if (queue_usage_rate <= options_.scale_down_threshold &&
            queue_size == 0 &&
            active_workers == 0 &&
            current_workers > options_.min_threads) {
            if (RequestWorkerExit(1)) {
                last_resize_time = now;

                LOG_INFO("thread pool scale down requested"
                         << ", name=" << options_.name
                         << ", current_workers=" << current_workers);
            }
        }
    }

    CleanupExitedWorkers();

    LOG_INFO("thread pool manager exited"
             << ", name=" << options_.name);
}

void ThreadPool::CleanupExitedWorkers() {
    std::lock_guard<std::mutex> lock(workers_mutex_);

    auto it = worker_slots_.begin();

    while (it != worker_slots_.end()) {
        WorkerSlot* slot = it->get();

        if (slot != nullptr &&
            slot->exited.load(std::memory_order_relaxed)) {
            if (slot->thread.joinable()) {
                slot->thread.join();
            }

            it = worker_slots_.erase(it);
            continue;
        }

        ++it;
    }
}

void ThreadPool::StopManager() {
    if (!options_.enable_dynamic_resize) {
        return;
    }

    bool expected = true;
    if (!manager_running_.compare_exchange_strong(
            expected,
            false,
            std::memory_order_relaxed)) {
        return;
    }

    if (manager_thread_.joinable()) {
        manager_thread_.join();
    }
}

void ThreadPool::UpdatePeakWorkerCount(std::size_t worker_count) {
    std::size_t current_peak =
        peak_worker_count_.load(std::memory_order_relaxed);

    while (worker_count > current_peak &&
           !peak_worker_count_.compare_exchange_weak(
               current_peak,
               worker_count,
               std::memory_order_relaxed)) {
    }
}

}  // namespace tinyimx