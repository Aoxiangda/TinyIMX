#pragma once
#include "gateway/business/BusinessRuntimeTypes.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <type_traits>
#include <utility>
#if defined(__linux__)
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

namespace tinyimx {
// Default-OFF numeric evidence. Every phase retains the original callable,
// request deadline and connection/session fences. Times use guest monotonic.
class LoginRequestPhaseTrace final {
public:
    enum class Phase : std::size_t { Authenticate = 0, Presence = 1, Unread = 2 };
    struct Snapshot {
        std::uint64_t uid{0}, tid{0};
        std::uint32_t seq{0};
        std::int64_t received_us{-1}, started_us{-1}, finished_us{-1};
        std::int64_t response_us{-1}, cpu_us{-1};
        std::array<std::int64_t,3> phase_start_us{{-1,-1,-1}};
        std::array<std::int64_t,3> phase_end_us{{-1,-1,-1}};
        std::array<std::int64_t,3> phase_cpu_us{{-1,-1,-1}};
        bool success{false}, failed{false};
    };
    using Now = std::int64_t(*)() noexcept;
    using Sink = void(*)(const Snapshot&) noexcept;
    static bool Enabled() noexcept {
        static const bool value = [] {
            const char* p = std::getenv("TINYIMX_LOGIN_PHASE_TRACE_ENABLE");
            return p && p[0] == '1' && p[1] == '\0';
        }();
        return value;
    }
    LoginRequestPhaseTrace(const BusinessRequestContext& request, Sink sink,
        bool enabled = Enabled(), Now now = WallNow, Now cpu = CpuNow) noexcept
        : enabled_(enabled), sink_(sink), now_(now), cpu_(cpu) {
        s_.seq = request.request_seq;
        if (enabled_) {
            s_.received_us = std::chrono::duration_cast<std::chrono::microseconds>(
                request.received_at.time_since_epoch()).count();
            s_.started_us = now_(); cpu_start_ = cpu_();
#if defined(__linux__)
            s_.tid = static_cast<std::uint64_t>(::syscall(SYS_gettid));
#endif
        }
    }
    LoginRequestPhaseTrace(const LoginRequestPhaseTrace&) = delete;
    LoginRequestPhaseTrace& operator=(const LoginRequestPhaseTrace&) = delete;
    ~LoginRequestPhaseTrace() noexcept {
        if (!enabled_) return;
        s_.finished_us = now_(); s_.cpu_us = Delta(cpu_start_, cpu_());
        if (sink_ && (s_.failed || (s_.uid > 0 && s_.uid % 16 == 0))) sink_(s_);
    }
    void SetUserId(std::uint64_t uid) noexcept { s_.uid = uid; }
    void ResponseStart(bool success) noexcept {
        if (!enabled_) return;
        s_.response_us = now_(); s_.success = success; s_.failed |= !success;
    }
    template<class F> auto Measure(Phase phase, F&& fn) -> std::invoke_result_t<F> {
        static_assert(!std::is_reference_v<std::invoke_result_t<F>>,
                      "Login phase result must own its value");
        if (!enabled_) return std::invoke(std::forward<F>(fn));
        const auto i = static_cast<std::size_t>(phase);
        const auto cpu = cpu_();
        s_.phase_start_us[i] = now_();
        try {
            if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
                std::invoke(std::forward<F>(fn)); Finish(i, cpu);
            } else {
                auto value = std::invoke(std::forward<F>(fn)); Finish(i, cpu);
                return value;
            }
        } catch (...) { s_.failed = true; Finish(i, cpu); throw; }
    }
private:
    static std::int64_t WallNow() noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            BusinessClock::now().time_since_epoch()).count();
    }
    static std::int64_t CpuNow() noexcept {
#if defined(__linux__)
        timespec t{};
        if (::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0)
            return static_cast<std::int64_t>(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
#endif
        return -1;
    }
    static std::int64_t Delta(std::int64_t a, std::int64_t b) noexcept {
        return a >= 0 && b >= a ? b - a : -1;
    }
    void Finish(std::size_t i, std::int64_t cpu) noexcept {
        s_.phase_end_us[i] = now_(); s_.phase_cpu_us[i] = Delta(cpu, cpu_());
    }
    bool enabled_; Sink sink_; Now now_, cpu_;
    std::int64_t cpu_start_{-1}; Snapshot s_;
};
} // namespace tinyimx
