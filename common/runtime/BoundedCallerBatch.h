#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tinyimx {
// Synchronous caller-led batches. No background thread or detached work;
// each accepted caller holds the shared coordinator until its result is ready.
template<class Input, class Result>
class BoundedCallerBatch final {
public:
    struct Limits {
        std::size_t batch{64};
        std::size_t queued{256};
        std::chrono::microseconds collect{500};
    };
    explicit BoundedCallerBatch(Limits limits = {}) : limits_(limits) {
        if (limits_.batch == 0 || limits_.queued == 0 || limits_.collect.count() < 0)
            throw std::invalid_argument("invalid caller batch limits");
    }
    BoundedCallerBatch(const BoundedCallerBatch&) = delete;
    BoundedCallerBatch& operator=(const BoundedCallerBatch&) = delete;

    template<class Execute>
    Result Run(Input input, Execute&& execute, const Result& failure) {
        static_assert(std::is_nothrow_move_assignable_v<Result>);
        auto node = std::make_shared<Node>(std::move(input), failure);
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= limits_.queued) return failure;
        queue_.push_back(node);
        changed_.notify_all();
        for (;;) {
            if (node->done) return std::move(node->result);
            if (running_) {
                changed_.wait(lock, [&] { return node->done || !running_; });
                continue;
            }
            running_ = true;
            const auto until = std::chrono::steady_clock::now() + limits_.collect;
            changed_.wait_until(lock, until, [&] { return queue_.size() >= limits_.batch; });
            std::vector<std::shared_ptr<Node>> batch;
            std::vector<Input> inputs;
            // Allocate before removing any waiter; an allocation failure must
            // not leave running_ set or accepted waiters without a result.
            try {
                const auto count = std::min(queue_.size(), limits_.batch);
                batch.reserve(count); inputs.reserve(count);
                for (std::size_t i = 0; i < count; ++i) {
                    batch.push_back(queue_[i]); inputs.push_back(queue_[i]->input);
                }
            } catch (...) {
                running_ = false;
                for (auto& item : queue_) item->done = true;
                queue_.clear(); changed_.notify_all();
                return std::move(node->result);
            }
            for (std::size_t i = 0; i < batch.size(); ++i) queue_.pop_front();
            lock.unlock();
            std::vector<Result> results;
            bool valid = false;
            try {
                results = execute(inputs);
                valid = results.size() == batch.size();
            } catch (...) {
                // Preallocated waiter failure results wake everyone, including
                // after an uncertain side effect. Never invoke execute twice.
            }
            lock.lock();
            for (std::size_t i = 0; i < batch.size(); ++i) {
                if (valid) batch[i]->result = std::move(results[i]);
                batch[i]->done = true;
            }
            running_ = false;
            changed_.notify_all();
        }
    }

    std::size_t QueuedCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    struct Node {
        Node(Input value, const Result& failure) : input(std::move(value)), result(failure) {}
        Input input;
        Result result{};
        bool done{false};
    };
    Limits limits_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::shared_ptr<Node>> queue_;
    bool running_{false};
};
} // namespace tinyimx
