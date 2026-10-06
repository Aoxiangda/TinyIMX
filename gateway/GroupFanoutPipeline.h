#pragma once
#include "common/db/StorageWaitTiming.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "gateway/business/BusinessRuntimeTypes.h"
#include "gateway/GroupDeliveryOrdering.h"
namespace tinyimx {
inline bool GroupFanoutDeferCompletionEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
inline bool GroupFanoutPartialDrainEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_GROUP_FANOUT_PARTIAL_DRAIN_ENABLE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
inline bool GroupFanoutCompletionBatchEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_GROUP_FANOUT_COMPLETION_BATCH_ENABLE");
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
inline std::int64_t TakeGroupPhaseObservation() noexcept {
    if (!GroupFanoutPhaseTraceEnabled()) return 0;
    const auto now = SteadyMicros();
    static std::atomic<std::int64_t> next{0};
    auto previous = next.load(std::memory_order_relaxed);
    if (now < previous || !next.compare_exchange_strong(
            previous, now + 125000, std::memory_order_relaxed)) return 0;
    return now;
}
// Default-off numeric observation, bounded to <=8/s/process. No clocks when off.
class GroupFanoutPhaseTrace final {
public:
    explicit GroupFanoutPhaseTrace(bool deferred) noexcept : deferred_(deferred) {
        const auto now = TakeGroupPhaseObservation();
        if (now == 0) return;
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
                << " completed=" << completed_ << " complete_rpc_calls=" << complete_calls_
                << " completion_batch=" << completion_batch_ << " started_us=" << started_
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
        ++completed_; ++complete_calls_; complete_sum_ += elapsed;
        if (elapsed > complete_max_) complete_max_ = elapsed;
    }
    void CompleteBatchDone(std::int64_t start, std::size_t count) noexcept {
        if (!selected_) return;
        const auto elapsed = SteadyMicros() - start;
        completed_ += count; ++complete_calls_; completion_batch_ = true;
        complete_sum_ += elapsed;
        if (elapsed > complete_max_) complete_max_ = elapsed;
    }
private:
    bool selected_{false}, deferred_, completion_batch_{false};
    std::size_t count_{0}, dispatched_{0}, completed_{0}, complete_calls_{0};
    std::uint64_t first_mid_{0}, last_mid_{0};
    std::int64_t started_{0}, cpu_start_{-1}, claim_{0};
    std::int64_t dispatch_sum_{0}, dispatch_max_{0}, complete_sum_{0}, complete_max_{0};
};
// Shares the same <=8/s/process budget with the coordinator trace.
class GroupDeliveryTaskTrace final {
public:
    GroupDeliveryTaskTrace(int kind, std::uint64_t mid, std::uint64_t recipient,
                           const BusinessRequestContext& request) noexcept
        : kind_(kind), mid_(mid), recipient_(recipient) {
        started_ = TakeGroupPhaseObservation();
        if (started_ == 0) return;
        cpu_start_ = ThreadCpuMicros();
        queue_age_ = started_ - std::chrono::duration_cast<std::chrono::microseconds>(
            request.received_at.time_since_epoch()).count();
    }
    GroupDeliveryTaskTrace(const GroupDeliveryTaskTrace&) = delete;
    GroupDeliveryTaskTrace& operator=(const GroupDeliveryTaskTrace&) = delete;
    ~GroupDeliveryTaskTrace() noexcept {
        if (started_ == 0) return;
        const auto total = SteadyMicros() - started_;
        const auto cpu = CpuDelta(cpu_start_,ThreadCpuMicros());
        try {
            LOG_WARN("group_delivery_task_phase kind=" << kind_
                << " recipient_order=" << GroupDeliveryRecipientOrderingEnabled()
                << " mid=" << mid_ << " recipient=" << recipient_
                << " started_us=" << started_ << " dispatch_age_us=" << queue_age_
                << " total_us=" << total << " thread_cpu_us=" << cpu
                << " get_rpc_us=" << get_ << " confirm_rpc_us=" << confirm_);
        } catch (...) { /* Observation cannot change auth/session/ACK state. */ }
    }
    std::int64_t Mark() const noexcept { return started_ ? SteadyMicros() : 0; }
    void GetDone(std::int64_t start) noexcept { if(started_)get_=SteadyMicros()-start; }
    void ConfirmDone(std::int64_t start) noexcept { if(started_)confirm_=SteadyMicros()-start; }
private:
    int kind_;std::uint64_t mid_,recipient_;
    std::int64_t started_{0},cpu_start_{-1},queue_age_{0},get_{-1},confirm_{-1};
};
} // namespace diagnostics
} // namespace tinyimx
