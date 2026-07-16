#pragma once

#include "common/concurrency/CircularQueue.h"
#include "common/concurrency/ThreadPoolTypes.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <utility>

namespace tinyimx {

template <typename T>
class MpmcBlockingQueue {
public:
    explicit MpmcBlockingQueue(std::size_t capacity)
        : queue_(capacity) {}

    MpmcBlockingQueue(const MpmcBlockingQueue&) = delete;
    MpmcBlockingQueue& operator=(const MpmcBlockingQueue&) = delete;

    TaskPushResult Push(T item, QueueFullPolicy policy) {
        switch (policy) {
            case QueueFullPolicy::kBlock:
                return PushBlocking(std::move(item));
            case QueueFullPolicy::kDiscard:
                return PushDiscard(std::move(item));
            case QueueFullPolicy::kOverwrite:
                return PushOverwrite(std::move(item));
            default:
                return PushBlocking(std::move(item));
        }
    }

    bool Pop(T* item) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_.wait(lock, [this]() {
            return force_stopped_ || !queue_.Empty() ||
                   (stopped_ && queue_.Empty());
        });

        if (force_stopped_) {
            return false;
        }

        if (!queue_.Empty()) {
            bool ok = queue_.PopFront(item);
            not_full_.notify_one();
            return ok;
        }

        return false;
    }

    bool PopFor(T* item, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);

    bool ready = not_empty_.wait_for(lock, timeout, [this]() {
            return force_stopped_ || !queue_.Empty() ||
                (stopped_ && queue_.Empty());
        });

        if (!ready) {
            return false;
        }

        if (force_stopped_) {
            return false;
        }

        if (!queue_.Empty()) {
            bool ok = queue_.PopFront(item);
            not_full_.notify_one();
            return ok;
        }

        return false;
    }

    void Stop(bool discard_pending_tasks) {
        std::lock_guard<std::mutex> lock(mutex_);

        stopped_ = true;

        if (discard_pending_tasks) {
            force_stopped_ = true;
            shutdown_discard_counter_ += queue_.Size();
            queue_.Clear(false);
        }

        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.Size();
    }

    std::size_t Capacity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.Capacity();
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.Empty();
    }

    std::size_t DiscardCounter() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return discard_counter_;
    }

    std::size_t OverwriteCounter() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.OverrunCounter();
    }

    std::size_t ShutdownDiscardCounter() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_discard_counter_;
    }

private:
    TaskPushResult PushBlocking(T item) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_.wait(lock, [this]() {
            return stopped_ || !queue_.Full();
        });

        if (stopped_) {
            return TaskPushResult::kStopped;
        }

        bool ok = queue_.PushBack(std::move(item));
        if (!ok) {
            return TaskPushResult::kDiscarded;
        }

        not_empty_.notify_one();
        return TaskPushResult::kOk;
    }

    TaskPushResult PushDiscard(T item) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (stopped_) {
            return TaskPushResult::kStopped;
        }

        if (queue_.Full()) {
            ++discard_counter_;
            return TaskPushResult::kDiscarded;
        }

        bool ok = queue_.PushBack(std::move(item));
        if (!ok) {
            ++discard_counter_;
            return TaskPushResult::kDiscarded;
        }

        not_empty_.notify_one();
        return TaskPushResult::kOk;
    }

    TaskPushResult PushOverwrite(T item) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (stopped_) {
            return TaskPushResult::kStopped;
        }

        if (queue_.Full()) {
            queue_.DropFront();
        }

        bool ok = queue_.PushBack(std::move(item));
        if (!ok) {
            ++discard_counter_;
            return TaskPushResult::kDiscarded;
        }

        not_empty_.notify_one();
        return TaskPushResult::kOk;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::size_t shutdown_discard_counter_{0};

    CircularQueue<T> queue_;

    bool stopped_{false};
    bool force_stopped_{false};

    std::size_t discard_counter_{0};
};

}  // namespace tinyimx