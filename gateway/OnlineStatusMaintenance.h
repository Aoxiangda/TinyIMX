#pragma once

#include "services/cache/OnlineStatusCache.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace tinyimx {

// Owns maintenance scheduling only. Redis ownership-checked EXPIRE is safe to
// run independently of offline cleanup; missing-record restoration is returned
// to Gateway's user-ordered, session-cancelable presence executor.
class OnlineStatusMaintenance final {
public:
    using Clock = std::chrono::steady_clock;
    using Result = RefreshOnlineIfMatchResult;
    using Request = OnlineStatusRefreshRequest;
    using BatchFunction = std::function<std::vector<Result>(const std::vector<Request>&)>;
    struct Options {
        std::size_t workers{4};
        std::size_t max_pending{4096};
        std::size_t max_batch{16};
        std::chrono::milliseconds flush_delay{5};
        std::chrono::milliseconds default_deadline{10000};
    };
    struct Job {
        Request request;
        Clock::time_point deadline{};
        std::function<bool()> still_valid;
        std::function<void(const Result&)> finished;
    };
    enum class SubmitStatus { kAccepted, kOverloaded, kShuttingDown, kInvalidArgument };
    struct Stats {
        std::uint64_t submitted{0}, accepted{0}, rejected_overload{0}, rejected_shutdown{0}, rejected_invalid{0};
        std::uint64_t completed{0}, deadline_before_io{0}, cancelled_before_io{0}, cancelled_before_callback{0};
        std::uint64_t worker_exception{0}, completion_exception{0};
        std::uint64_t batch_calls{0}, batch_items{0};
        std::uint64_t refreshed{0}, missing{0}, mismatch{0}, invalid_record{0}, invalid_argument{0}, redis_error{0};
        std::size_t pending{0}, peak_pending{0}, max_observed_batch{0};
        std::uint64_t Terminals() const noexcept {
            return completed + deadline_before_io + cancelled_before_io +
                cancelled_before_callback + worker_exception + completion_exception;
        }
    };
    OnlineStatusMaintenance(Options options, BatchFunction refresh)
        : options_(options), refresh_(std::move(refresh)) {}
    OnlineStatusMaintenance(const OnlineStatusMaintenance&) = delete;
    OnlineStatusMaintenance& operator=(const OnlineStatusMaintenance&) = delete;
    ~OnlineStatusMaintenance() { Stop(); }

    bool Start() {
        std::lock_guard lifecycle(lifecycle_mutex_);
        if (started_ || !refresh_ || options_.workers == 0 || options_.workers > 8 ||
            options_.max_pending == 0 || options_.max_batch == 0 ||
            options_.max_batch > OnlineStatusCache::kMaxRefreshBatchSize ||
            options_.flush_delay.count() <= 0 || options_.default_deadline.count() <= 0)
            return false;
        started_ = true;
        try {
            workers_.reserve(options_.workers);
            collector_ = std::thread([this] { Collect(); });
            for (std::size_t i = 0; i < options_.workers; ++i)
                workers_.emplace_back([this] { Work(); });
            std::lock_guard lock(mutex_); accepting_ = true;
            return true;
        } catch (...) {
            { std::lock_guard lock(mutex_); stopping_ = true;
              if (!collector_.joinable()) collector_done_ = true; }
            input_cv_.notify_all(); ready_cv_.notify_all();
            if (collector_.joinable()) collector_.join();
            for (auto& worker : workers_) if (worker.joinable()) worker.join();
            return false;
        }
    }
    SubmitStatus TrySubmit(Job job) {
        const auto now = Clock::now();
        std::lock_guard lock(mutex_);
        ++stats_.submitted;
        if (job.request.user_id == 0 || job.request.gateway_id.empty() ||
            job.request.connection_name.empty() || job.request.ttl_seconds <= 0 || !job.still_valid) {
            ++stats_.rejected_invalid; return SubmitStatus::kInvalidArgument;
        }
        if (!accepting_) { ++stats_.rejected_shutdown; return SubmitStatus::kShuttingDown; }
        // pending covers input, ready batches and work/callbacks in progress.
        if (stats_.pending >= options_.max_pending) {
            ++stats_.rejected_overload; return SubmitStatus::kOverloaded;
        }
        if (job.deadline == Clock::time_point{}) job.deadline = now + options_.default_deadline;
        input_.push_back(Queued{std::move(job), now});
        ++stats_.accepted; ++stats_.pending;
        stats_.peak_pending = std::max(stats_.peak_pending, stats_.pending);
        input_cv_.notify_one();
        return SubmitStatus::kAccepted;
    }
    bool IsAccepting() const { std::lock_guard lock(mutex_); return accepting_; }
    Stats GetStats() const { std::lock_guard lock(mutex_); return stats_; }
    // No callbacks survive Stop. Idle socket timeouts do not imply a global
    // absolute shutdown deadline; bootstrap keeps the cache alive during drain.
    void Stop() {
        std::lock_guard lifecycle(lifecycle_mutex_);
        { std::lock_guard lock(mutex_); accepting_ = false; stopping_ = true; }
        input_cv_.notify_all(); ready_cv_.notify_all();
        if (collector_.joinable()) collector_.join();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
    }

private:
    struct Queued { Job job; Clock::time_point enqueued; };
    using Batch = std::vector<Job>;
    enum class Terminal { kCompleted, kDeadline, kCancelledIo, kCancelledCallback, kWorkerException, kCompletionException };
    void Finish(Terminal terminal) {
        std::lock_guard lock(mutex_);
        switch (terminal) {
            case Terminal::kCompleted: ++stats_.completed; break;
            case Terminal::kDeadline: ++stats_.deadline_before_io; break;
            case Terminal::kCancelledIo: ++stats_.cancelled_before_io; break;
            case Terminal::kCancelledCallback: ++stats_.cancelled_before_callback; break;
            case Terminal::kWorkerException: ++stats_.worker_exception; break;
            case Terminal::kCompletionException: ++stats_.completion_exception; break;
        }
        --stats_.pending;
    }
    void Collect() {
        std::unique_lock lock(mutex_);
        for (;;) {
            input_cv_.wait(lock, [this] { return stopping_ || !input_.empty(); });
            if (input_.empty() && stopping_) break;
            const auto flush_at = input_.front().enqueued + options_.flush_delay;
            input_cv_.wait_until(lock, flush_at, [this] {
                return stopping_ || input_.size() >= options_.max_batch;
            });
            Batch batch; const auto count = std::min(options_.max_batch, input_.size());
            batch.reserve(count);
            for (std::size_t i = 0; i < count; ++i) {
                batch.push_back(std::move(input_.front().job)); input_.pop_front();
            }
            ready_.push_back(std::move(batch)); ready_cv_.notify_one();
        }
        collector_done_ = true; ready_cv_.notify_all();
    }
    void Work() {
        for (;;) {
            Batch batch;
            { std::unique_lock lock(mutex_);
              ready_cv_.wait(lock, [this] { return collector_done_ || !ready_.empty(); });
              if (ready_.empty() && collector_done_) return;
              batch = std::move(ready_.front()); ready_.pop_front(); }
            Process(std::move(batch));
        }
    }
    void RecordResult(const Result& result) {
        std::lock_guard lock(mutex_);
        switch (result.status) {
            case RefreshOnlineIfMatchStatus::kRefreshed: ++stats_.refreshed; break;
            case RefreshOnlineIfMatchStatus::kNotFound: ++stats_.missing; break;
            case RefreshOnlineIfMatchStatus::kMismatch: ++stats_.mismatch; break;
            case RefreshOnlineIfMatchStatus::kInvalidRecord: ++stats_.invalid_record; break;
            case RefreshOnlineIfMatchStatus::kInvalidArgument: ++stats_.invalid_argument; break;
            case RefreshOnlineIfMatchStatus::kRedisError: ++stats_.redis_error; break;
        }
    }
    void Process(Batch batch) {
        Batch valid; std::vector<Request> requests;
        valid.reserve(batch.size()); requests.reserve(batch.size());
        for (auto& job : batch) {
            if (Clock::now() >= job.deadline) { Finish(Terminal::kDeadline); continue; }
            try {
                if (!job.still_valid()) { Finish(Terminal::kCancelledIo); continue; }
                requests.push_back(job.request); valid.push_back(std::move(job));
            } catch (...) { Finish(Terminal::kWorkerException); }
        }
        if (valid.empty()) return;
        { std::lock_guard lock(mutex_); ++stats_.batch_calls; stats_.batch_items += valid.size();
          stats_.max_observed_batch = std::max(stats_.max_observed_batch, valid.size()); }
        std::vector<Result> results;
        try {
            results = refresh_(requests);
            if (results.size() != valid.size()) {
                for (std::size_t i = 0; i < valid.size(); ++i) Finish(Terminal::kWorkerException);
                return;
            }
        } catch (...) {
            for (std::size_t i = 0; i < valid.size(); ++i) Finish(Terminal::kWorkerException);
            return;
        }
        for (std::size_t i = 0; i < valid.size(); ++i) {
            RecordResult(results[i]);
            try {
                if (!valid[i].still_valid()) { Finish(Terminal::kCancelledCallback); continue; }
                if (valid[i].finished) valid[i].finished(results[i]);
                Finish(Terminal::kCompleted);
            } catch (...) { Finish(Terminal::kCompletionException); }
        }
    }
    Options options_; BatchFunction refresh_;
    mutable std::mutex mutex_; std::mutex lifecycle_mutex_;
    std::condition_variable input_cv_, ready_cv_;
    std::deque<Queued> input_; std::deque<Batch> ready_;
    bool started_{false}, accepting_{false}, stopping_{false}, collector_done_{false};
    Stats stats_; std::thread collector_; std::vector<std::thread> workers_;
};
} // namespace tinyimx
