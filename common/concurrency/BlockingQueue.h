#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>

namespace tinyimx {

template <typename T>
class BlockingQueue {

public:
    explicit BlockingQueue(std::size_t capacity)
        : capacity_(capacity) {}

    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;

    bool Push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_.wait(lock, [this]() {
            return stopped_ || queue_.size() < capacity_;
        });

        if (stopped_) {
            return false;
        }

        queue_.push(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    bool Pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_.wait(lock, [this]() {
            return stopped_ || !queue_.empty();
        });

        if (queue_.empty()) {
            return false;
        }

        item = std::move(queue_.front());
        queue_.pop();

        not_full_.notify_one();
        return true;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }

        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::size_t capacity_;
    mutable std::mutex mutex_;

    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::queue<T> queue_;

    bool stopped_{false};
};

}  // namespace tinyimx