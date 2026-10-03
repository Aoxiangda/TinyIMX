#pragma once

#include "gateway/business/BusinessRuntimeTypes.h"
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
        std::array<std::int64_t,kPhases> duration_us{{-1,-1,-1,-1,-1}};
        std::array<std::int64_t,kPhases> start_budget_us{{0,0,0,0,0}};
        bool has_deadline{false}, failed{false};
    };
    using Sink = void(*)(const Snapshot&) noexcept;

    explicit ChatRequestPhaseTrace(const BusinessRequestContext& request,
                                  const char* path, Sink sink,
                                  BusinessTimePoint start = BusinessClock::now()) noexcept
        : request_(request), started_(start), sink_(sink) {
        snapshot_.path = path;
        snapshot_.user_id = request.user_id;
        snapshot_.session_epoch = request.session_epoch;
        snapshot_.request_seq = request.request_seq;
        snapshot_.has_deadline = request.HasDeadline();
        snapshot_.dispatch_age_us = Micros(start-request.received_at);
        snapshot_.entry_budget_us = Budget(start);
    }
    ChatRequestPhaseTrace(const ChatRequestPhaseTrace&) = delete;
    ChatRequestPhaseTrace& operator=(const ChatRequestPhaseTrace&) = delete;
    ~ChatRequestPhaseTrace() noexcept {
        snapshot_.work_us = Micros(BusinessClock::now()-started_);
        if (sink_ && (snapshot_.failed || snapshot_.dispatch_age_us >= 100000 ||
                      snapshot_.work_us >= 100000)) sink_(snapshot_);
    }
    void SetMessageId(std::uint64_t id) noexcept { snapshot_.message_id = id; }
    void Fail() noexcept { snapshot_.failed = true; }
    const Snapshot& Current() const noexcept { return snapshot_; }
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
        const auto elapsed = Micros(BusinessClock::now()-start);
        snapshot_.duration_us[i] = snapshot_.duration_us[i] < 0 ? elapsed
                                                             : snapshot_.duration_us[i]+elapsed;
    }
    const BusinessRequestContext& request_;
    BusinessTimePoint started_;
    Sink sink_;
    Snapshot snapshot_;
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

} // namespace tinyimx
