#pragma once

#include "gateway/business/BusinessRuntimeTypes.h"
#include "common/cache/RedisAcquirePhaseTrace.h"
#include "common/observability/MessageTraceSample.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace tinyimx {

// Diagnostics only. Never resets, increases or decides the request deadline.
// 'dispatch_age' is received_at->work entry, not a pure executor queue timer.
class ChatRequestPhaseTrace final {
public:
    enum class Phase : std::size_t { Permission, Route, Persist, Unread, PeerValidate, Count };
    static constexpr std::size_t kPhases = static_cast<std::size_t>(Phase::Count);
    struct Snapshot {
        const char* path{nullptr};
        std::uint64_t user_id{0}, message_id{0}, session_epoch{0};
        std::uint32_t request_seq{0};
        std::int64_t dispatch_age_us{0}, entry_budget_us{0}, work_us{0};
        std::int64_t started_us{0}; std::uint64_t tid{0};
        // Unlike dispatch_age, queue_wait starts immediately before actual
        // stripe insertion (or unordered TrySubmit). Includes scheduling;
        // excludes validation/global and stripe admission-lock wait.
        std::int64_t executor_submit_us{-1}, executor_enqueue_us{-1}, executor_start_us{-1};
        std::int64_t executor_admission_us{-1}, executor_queue_wait_us{-1};
        std::int64_t ack_handoff_us{-1};
        int ack_handoff_ok{-1};
        std::array<std::int64_t,kPhases> phase_started_us{{-1,-1,-1,-1,-1}};
        std::array<std::int64_t,kPhases> phase_finished_us{{-1,-1,-1,-1,-1}};
        std::array<std::int64_t,kPhases> duration_us{{-1,-1,-1,-1,-1}};
        std::array<std::int64_t,kPhases> start_budget_us{{0,0,0,0,0}};
        bool has_deadline{false}, failed{false}, sampled_fast{false};
    };
    using Sink = void(*)(const Snapshot&) noexcept;

    static bool FastSamplingEnabled() noexcept {
        static const bool enabled = [] {
            const char* value = std::getenv("TINYIMX_CHAT_PHASE_SAMPLE_ENABLE");
            return value && value[0] == '1' && value[1] == '\0';
        }();
        return enabled;
    }

    explicit ChatRequestPhaseTrace(const BusinessRequestContext& request,
                                  const char* path, Sink sink,
                                  BusinessTimePoint start = BusinessClock::now(),
                                  bool sample_fast = FastSamplingEnabled()) noexcept
        : request_(request), started_(start), sink_(sink), sample_fast_(sample_fast) {
        snapshot_.path = path;
        snapshot_.user_id = request.user_id;
        snapshot_.session_epoch = request.session_epoch;
        snapshot_.request_seq = request.request_seq;
        snapshot_.has_deadline = request.HasDeadline();
        snapshot_.dispatch_age_us = Micros(start-request.received_at);
        snapshot_.entry_budget_us = Budget(start);
        absolute_trace_ = sample_fast_ || diagnostics::RedisAcquireTraceEnabled();
        if (absolute_trace_) {
            snapshot_.started_us = Micros(start.time_since_epoch());
            snapshot_.tid = diagnostics::CurrentThreadId();
            if (request.executor_submitted_at != BusinessTimePoint{} &&
                request.executor_enqueued_at >= request.executor_submitted_at &&
                request.executor_started_at >= request.executor_enqueued_at &&
                start >= request.executor_started_at) {
                snapshot_.executor_submit_us = Micros(request.executor_submitted_at.time_since_epoch());
                snapshot_.executor_enqueue_us = Micros(request.executor_enqueued_at.time_since_epoch());
                snapshot_.executor_start_us = Micros(request.executor_started_at.time_since_epoch());
                snapshot_.executor_admission_us = Micros(request.executor_enqueued_at-request.executor_submitted_at);
                snapshot_.executor_queue_wait_us = Micros(request.executor_started_at-request.executor_enqueued_at);
            }
        }
    }
    ChatRequestPhaseTrace(const ChatRequestPhaseTrace&) = delete;
    ChatRequestPhaseTrace& operator=(const ChatRequestPhaseTrace&) = delete;
    ~ChatRequestPhaseTrace() noexcept {
        snapshot_.work_us = Micros(BusinessClock::now()-started_);
        // Select by durable MID so the same successful request is correlated
        // with repository/handler diagnostics. Never sample every request or
        // exclude slow/failing requests from the existing diagnostic path.
        snapshot_.sampled_fast = sample_fast_ &&
                                 diagnostics::MessageTraceSelected(snapshot_.message_id);
        if (sink_ && (snapshot_.failed || snapshot_.dispatch_age_us >= 100000 ||
                      snapshot_.work_us >= 100000 || snapshot_.sampled_fast)) sink_(snapshot_);
    }
    void SetMessageId(std::uint64_t id) noexcept { snapshot_.message_id = id; }
    // This is the SendPacket handoff boundary, not a kernel flush or client
    // receive timestamp. Residual latency includes event-loop/network delivery.
    void MarkAckHandoff(bool successful) noexcept {
        snapshot_.ack_handoff_ok = successful ? 1 : 0;
        if (absolute_trace_)
            snapshot_.ack_handoff_us = Micros(BusinessClock::now().time_since_epoch());
    }
    void Fail() noexcept { snapshot_.failed = true; }
    const Snapshot& Current() const noexcept { return snapshot_; }
    BusinessTimePoint Begin(Phase phase) noexcept {
        const auto start=BusinessClock::now();
        snapshot_.start_budget_us[static_cast<std::size_t>(phase)]=Budget(start);
        return start;
    }
    void End(Phase phase,BusinessTimePoint start) noexcept {
        Record(static_cast<std::size_t>(phase),start);
    }
    template<class Fn>
    auto Measure(Phase phase, Fn&& fn) -> std::invoke_result_t<Fn> {
        static_assert(!std::is_reference_v<std::invoke_result_t<Fn>>, "phase result must own its value");
        const auto start = BusinessClock::now();
        const auto i = static_cast<std::size_t>(phase);
        snapshot_.start_budget_us[i] = Budget(start);
        try {
            if constexpr (std::is_void_v<std::invoke_result_t<Fn>>) {
                std::forward<Fn>(fn)();
                Record(i, start);
            } else {
                auto result = std::forward<Fn>(fn)();
                Record(i, start);
                return result;
            }
        } catch (...) {
            Record(i, start); snapshot_.failed = true; throw;
        }
    }
private:
    static std::int64_t Micros(BusinessClock::duration d) noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
    }
    std::int64_t Budget(BusinessTimePoint now) const noexcept {
        return request_.HasDeadline() ? Micros(request_.deadline-now) : 0;
    }
    void Record(std::size_t i, BusinessTimePoint start) noexcept {
        const auto end = BusinessClock::now();
        const auto elapsed = Micros(end-start);
        if (absolute_trace_) {
            // Union interval for repeated phases; no extra clock reads.
            if (snapshot_.phase_started_us[i] < 0)
                snapshot_.phase_started_us[i] = Micros(start.time_since_epoch());
            snapshot_.phase_finished_us[i] = Micros(end.time_since_epoch());
        }
        snapshot_.duration_us[i] = snapshot_.duration_us[i] < 0 ? elapsed
                                                             : snapshot_.duration_us[i]+elapsed;
    }
    const BusinessRequestContext& request_;
    BusinessTimePoint started_;
    Sink sink_;
    Snapshot snapshot_;
    bool absolute_trace_{false};
    bool sample_fast_{false};
};

// Fixed one-second buckets; at most 8 diagnostic lines per bucket/process.
// Atomic attempt order may not equal log order. This is sampled diagnosis, not
// an accounting metric. Adjacent bucket boundaries can produce a short burst.
class ChatPhaseLogLimiter final {
public:
    bool Admit(std::uint64_t second) noexcept {
        // Upper bits: monotonic second. Low byte: admitted count.
        const std::uint64_t base = second << 8;
        auto old = state_.load(std::memory_order_relaxed);
        for (;;) {
            const auto old_bucket = old & ~std::uint64_t{255};
            if (base < old_bucket) return Suppress();
            const auto count = base == old_bucket ? (old & 255) : 0;
            if (count >= 8) return Suppress();
            const auto next = base | (count+1);
            if (state_.compare_exchange_weak(old, next, std::memory_order_relaxed)) return true;
        }
    }
    std::uint64_t TakeSuppressed() noexcept {
        return suppressed_.exchange(0, std::memory_order_relaxed);
    }
private:
    bool Suppress() noexcept {
        suppressed_.fetch_add(1, std::memory_order_relaxed); return false;
    }
    std::atomic<std::uint64_t> state_{0}, suppressed_{0};
};

// Reserve the existing8/s diagnostic budget for each class independently.
// Without opt-in MID sampling, only the historical slow/failure lane is used.
// With it enabled, unrelated slow requests cannot starve the matched-MID lane;
// total logging is still bounded by16/s/process, never a per-message trace.
class ChatPhasePriorityLogLimiter final {
public:
    bool Admit(std::uint64_t second, bool sampled_mid) noexcept {
        return Lane(sampled_mid).Admit(second);
    }
    std::uint64_t TakeSuppressed(bool sampled_mid) noexcept {
        return Lane(sampled_mid).TakeSuppressed();
    }
private:
    ChatPhaseLogLimiter& Lane(bool sampled_mid) noexcept {
        return sampled_mid ? matched_ : slow_;
    }
    ChatPhaseLogLimiter matched_, slow_;
};

} // namespace tinyimx
