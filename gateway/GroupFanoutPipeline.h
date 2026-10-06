#pragma once
#include "common/db/StorageWaitTiming.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
namespace tinyimx {
inline bool GroupFanoutDeferCompletionEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
namespace diagnostics {
inline bool GroupFanoutPhaseTraceEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_GROUP_FANOUT_PHASE_TRACE_ENABLE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
// Default-off numeric observation, bounded to <=8/s/process. No clocks when off.
class GroupFanoutPhaseTrace final {
public:
    explicit GroupFanoutPhaseTrace(bool deferred) noexcept : deferred_(deferred) {
        if (!GroupFanoutPhaseTraceEnabled()) return;
        const auto now = SteadyMicros();
        static std::atomic<std::int64_t> next{0};
        auto previous = next.load(std::memory_order_relaxed);
        if (now < previous || !next.compare_exchange_strong(
                previous, now + 125000, std::memory_order_relaxed)) return;
        selected_ = true; started_ = now; cpu_start_ = ThreadCpuMicros();
    }
    GroupFanoutPhaseTrace(const GroupFanoutPhaseTrace&) = delete;
    GroupFanoutPhaseTrace& operator=(const GroupFanoutPhaseTrace&) = delete;
    ~GroupFanoutPhaseTrace() noexcept {
        if (!selected_ || count_ == 0) return;
        const auto total = SteadyMicros() - started_;
        const auto cpu = CpuDelta(cpu_start_, ThreadCpuMicros());
        try {
            LOG_WARN("group_fanout_phase deferred=" << deferred_
                << " first_mid=" << first_mid_ << " last_mid=" << last_mid_
                << " claimed=" << count_ << " dispatched=" << dispatched_
                << " completed=" << completed_ << " started_us=" << started_
                << " total_us=" << total << " thread_cpu_us=" << cpu
                << " claim_rpc_us=" << claim_ << " dispatch_sum_us=" << dispatch_sum_
                << " dispatch_max_us=" << dispatch_max_
                << " complete_rpc_sum_us=" << complete_sum_
                << " complete_rpc_max_us=" << complete_max_);
        } catch (...) { /* Observation cannot change durable delivery behavior. */ }
    }
    std::int64_t Mark() const noexcept { return selected_ ? SteadyMicros() : 0; }
    void Claimed(std::size_t count, std::uint64_t first, std::uint64_t last) noexcept {
        if (!selected_) return;
        claim_ = SteadyMicros() - started_; count_ = count;
        first_mid_ = first; last_mid_ = last;
    }
    void DispatchDone(std::int64_t start) noexcept {
        if (!selected_) return;
        const auto elapsed = SteadyMicros() - start;
        ++dispatched_; dispatch_sum_ += elapsed;
        if (elapsed > dispatch_max_) dispatch_max_ = elapsed;
    }
    void CompleteDone(std::int64_t start) noexcept {
        if (!selected_) return;
        const auto elapsed = SteadyMicros() - start;
        ++completed_; complete_sum_ += elapsed;
        if (elapsed > complete_max_) complete_max_ = elapsed;
    }
private:
    bool selected_{false}, deferred_;
    std::size_t count_{0}, dispatched_{0}, completed_{0};
    std::uint64_t first_mid_{0}, last_mid_{0};
    std::int64_t started_{0}, cpu_start_{-1}, claim_{0};
    std::int64_t dispatch_sum_{0}, dispatch_max_{0}, complete_sum_{0}, complete_max_{0};
};
} // namespace diagnostics
} // namespace tinyimx
