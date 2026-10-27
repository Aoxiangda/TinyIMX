#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tinyimx::message::local {

// Combine only already queued actor-reference INSERTs. SQL concurrency is
// retained so one locked pair cannot serialize all unrelated cold actors.
// No timer or extra thread; every caller waits for its own durable SQL result.
class NativeReferenceReadyBatch final {
public:
    using Actors = std::set<std::uint64_t>;
    struct PinResult {
        enum class Kind { Stored, MissingParent, Unavailable };
        Kind kind;
        PinResult(bool stored) : kind(stored ? Kind::Stored : Kind::MissingParent) {}
        explicit PinResult(Kind value) : kind(value) {}
        static PinResult Unavailable() { return PinResult(Kind::Unavailable); }
        explicit operator bool() const { return kind == Kind::Stored; }
    };
    using Pin = std::function<PinResult(const Actors&)>;
    // The production persist dispatcher has eight executing lanes. Letting a
    // 32-connection SQL pool create31 leaders drains every incoming call alone;
    // no ready work can accumulate. Keep independent SQL progress (no single
    // blocked leader), but leave room for already arrived callers to combine.
    // The owner lease still retains its separate SQL slot.
    static constexpr std::size_t ParallelSqlBatches(std::size_t pool_size) noexcept {
        return pool_size > 1 ? std::min<std::size_t>(4, pool_size - 1) : 1;
    }
    std::size_t MaxParallelSqlBatches() const noexcept { return parallel_; }
    struct Stats {
        std::uint64_t calls, succeeded, batches, sql_calls, fallback_calls, rejected;
        std::size_t largest_batch, queued, pending, active_batches;
    };

    explicit NativeReferenceReadyBatch(Pin pin, std::size_t parallel = 1)
        : pin_(std::move(pin)), parallel_(parallel) {
        if (!pin_ || parallel < 1 || parallel > 32)
            throw std::invalid_argument("invalid native reference SQL batch configuration");
    }

    bool Run(Actors actors) {
        if (actors.empty()) return true;
        if (actors.size() > 2 || actors.contains(0)) return false;
        ++calls_;
        auto node = std::make_shared<Node>(std::move(actors));
        std::unique_lock lock(mutex_);
        if (pending_ >= capacity) { ++rejected_; return false; }
        queue_.push_back(node);
        ++pending_;
        for (;;) {
            if (node->done) {
                if (node->result) ++succeeded_;
                return node->result;
            }
            if (node->taken || running_ >= parallel_) {
                changed_.wait(lock, [&] {
                    return node->done || (!node->taken && running_ < parallel_);
                });
                continue;
            }
            std::vector<std::shared_ptr<Node>> nodes;
            std::vector<Actors> inputs;
            try {
                const auto count = std::min(batch_limit, queue_.size());
                nodes.reserve(count); inputs.reserve(count);
                for (std::size_t i = 0; i < count; ++i) {
                    nodes.push_back(queue_[i]); inputs.push_back(queue_[i]->actors);
                }
            } catch (...) {
                // No queue ownership changed yet. Fail this caller only;
                // executing leaders and other admitted waiters still progress.
                const auto found = std::find(queue_.begin(), queue_.end(), node);
                if (found != queue_.end()) queue_.erase(found);
                node->done = true; --pending_; changed_.notify_all();
                return false;
            }
            ++running_;
            for (auto& item : nodes) {
                item->taken = true;
                queue_.pop_front();
            }
            lock.unlock();
            std::vector<bool> results;
            bool valid = false;
            try { results = Execute(inputs); valid = results.size() == nodes.size(); }
            catch (...) { /* No retry after an exception or uncertain effect. */ }
            lock.lock();
            --running_;
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                nodes[i]->result = valid && results[i];
                nodes[i]->done = true;
                --pending_;
            }
            changed_.notify_all();
        }
    }

    Stats Snapshot() const {
        std::lock_guard lock(mutex_);
        return {calls_.load(), succeeded_.load(), batches_.load(), sql_calls_.load(),
                fallback_calls_.load(), rejected_.load(), largest_.load(),
                queue_.size(), pending_, running_};
    }

private:
    struct Node {
        explicit Node(Actors input) : actors(std::move(input)) {}
        Actors actors;
        bool taken{false}, done{false}, result{false};
    };
    std::vector<bool> Execute(const std::vector<Actors>& inputs) {
        ++batches_;
        auto previous = largest_.load(std::memory_order_relaxed);
        while (previous < inputs.size() &&
               !largest_.compare_exchange_weak(previous, inputs.size(), std::memory_order_relaxed)) {}
        Actors combined;
        for (const auto& ids : inputs) combined.insert(ids.begin(), ids.end());
        ++sql_calls_;
        const auto outcome = pin_(combined);
        if (outcome) return std::vector<bool>(inputs.size(), true);

        std::vector<bool> results(inputs.size(), false);
        if (inputs.size() == 1 || outcome.kind != PinResult::Kind::MissingParent) return results;
        // Missing parents must not deny unrelated valid calls. PinRows is
        // explicitly idempotent (primary key + ON DUPLICATE KEY UPDATE).
        // This retries reference registration only, never a message or ACK.
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            ++sql_calls_; ++fallback_calls_;
            const auto single = pin_(inputs[i]);
            results[i] = static_cast<bool>(single);
            if (single.kind == PinResult::Kind::Unavailable) break;
        }
        return results;
    }

    static constexpr std::size_t capacity = 64, batch_limit = 64;
    Pin pin_;
    const std::size_t parallel_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::shared_ptr<Node>> queue_;
    std::size_t pending_{0}, running_{0};
    std::atomic<std::uint64_t> calls_{0}, succeeded_{0}, batches_{0}, sql_calls_{0}, fallback_calls_{0}, rejected_{0};
    std::atomic<std::size_t> largest_{0};
};
} // namespace tinyimx::message::local
