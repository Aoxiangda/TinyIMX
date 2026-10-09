#pragma once

#include "common/db/StorageWaitTiming.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace tinyimx::diagnostics {

// Numeric, bounded, default-off observation. Does not alter pool admission,
// health PING, reconnect, slot return or the caller's original timeout.
inline bool RedisAcquireTraceEnabled() noexcept {
    static const bool enabled = [] {
        const char* flag = std::getenv("TINYIMX_REDIS_ACQUIRE_TRACE_ENABLE");
        return flag && flag[0] == '1' && flag[1] == '\0';
    }();
    return enabled;
}

class RedisAcquirePhaseTrace final {
public:
    RedisAcquirePhaseTrace() noexcept {
        if (!RedisAcquireTraceEnabled()) return;
        const auto now = SteadyMicros();
        static std::atomic<std::int64_t> next{0};
        auto previous = next.load(std::memory_order_relaxed);
        if (now < previous ||
            !next.compare_exchange_strong(previous, now + 125000,
                                          std::memory_order_relaxed)) return;
        selected_ = true;
        started_us_ = phase_us_ = now;
        cpu_start_ = ThreadCpuMicros();
        tid_ = CurrentThreadId();
    }
    RedisAcquirePhaseTrace(const RedisAcquirePhaseTrace&) = delete;
    RedisAcquirePhaseTrace& operator=(const RedisAcquirePhaseTrace&) = delete;
    ~RedisAcquirePhaseTrace() noexcept {
        if (!selected_) return;
        const auto total = SteadyMicros() - started_us_;
        const auto cpu = CpuDelta(cpu_start_, ThreadCpuMicros());
        try {
            LOG_WARN("redis_acquire_phase"
                << " status=" << status_ << " tid=" << tid_
                << " started_us=" << started_us_ << " total_us=" << total
                << " thread_cpu_us=" << cpu << " pool_size=" << size_
                << " free_slots_before=" << free_
                << " mutex_wait_us=" << mutex_wait_us_
                << " slot_wait_us=" << slot_wait_us_
                << " ping_us=" << ping_us_ << " reconnect_us=" << reconnect_us_);
        } catch (...) {
            // Observability failure must not escape into the lease lifecycle.
        }
    }
    void MutexLocked(std::size_t size, std::size_t free) noexcept {
        if (!selected_) return;
        size_ = size; free_ = free;
        const auto now = SteadyMicros();
        mutex_wait_us_ = now - started_us_; phase_us_ = now;
    }
    void SlotReady() noexcept {
        if (selected_) slot_wait_us_ = SteadyMicros() - phase_us_;
    }
    void PhaseStart() noexcept { if (selected_) phase_us_ = SteadyMicros(); }
    void PingDone() noexcept { if (selected_) ping_us_ = SteadyMicros() - phase_us_; }
    void ReconnectDone() noexcept {
        if (selected_) reconnect_us_ = SteadyMicros() - phase_us_;
    }
    void Outcome(int status) noexcept { status_ = status; }
private:
    bool selected_{false};
    int status_{-1}; // 0 lease, 1 unavailable, 2 wait/stop, 3 reconnect failure
    std::uint64_t tid_{0};
    std::size_t size_{0}, free_{0};
    std::int64_t started_us_{0}, phase_us_{0}, cpu_start_{-1};
    std::int64_t mutex_wait_us_{0}, slot_wait_us_{0}, ping_us_{-1}, reconnect_us_{-1};
};
} // namespace tinyimx::diagnostics
