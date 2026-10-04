#pragma once

#include "common/logging/LogMacros.h"
#include <array>
#include <atomic>
#include <chrono>
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

namespace tinyimx::diagnostics {
inline bool AuthPhaseEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_AUTH_PHASE_TRACE_ENABLE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

class AuthPhaseLogLimiter final {
public:
    bool Admit(std::uint64_t second) noexcept {
        const auto base = second << 8;
        auto old = state_.load(std::memory_order_relaxed);
        for (;;) {
            const auto bucket = old & ~std::uint64_t{255};
            if (base < bucket) return false;
            const auto count = base == bucket ? old & 255 : 0;
            if (count >= 8) return false;
            if (state_.compare_exchange_weak(old, base | (count + 1),
                                            std::memory_order_relaxed)) return true;
        }
    }
private:
    std::atomic<std::uint64_t> state_{0};
};

// Numeric-only evidence. kind1 status=LoginVerifyStatus; kind2 status=gRPC
// code and outcome=AuthenticateOutcome. Handler timing excludes gRPC admission
// before method entry and transport after return. CPU missing remains -1.
class AuthPhaseTrace final {
public:
    enum class Phase : std::size_t { Lookup = 0, Password = 1 };
    struct Snapshot {
        std::uint64_t uid{0}, tid{0};
        std::int64_t started_us{0}, total_us{0}, cpu_us{-1};
        std::array<std::int64_t, 2> phase_us{-1, -1};
        std::array<std::int64_t, 2> phase_cpu_us{-1, -1};
        int kind{0}, status{-1}, outcome{0};
        bool threw{false};
    };
    using Now = std::int64_t(*)() noexcept;
    using Sink = void(*)(const Snapshot&) noexcept;
    AuthPhaseTrace(int kind, bool enabled = AuthPhaseEnabled(), Sink sink = Log,
                   Now now = WallNow, Now cpu = CpuNow) noexcept
        : enabled_(enabled), sink_(sink), now_(now), cpu_(cpu) {
        s_.kind = kind;
        if (enabled_) {
            s_.started_us = now_();
            cpu_start_ = cpu_();
#if defined(__linux__)
            s_.tid = static_cast<std::uint64_t>(::syscall(SYS_gettid));
#endif
        }
    }
    AuthPhaseTrace(const AuthPhaseTrace&) = delete;
    AuthPhaseTrace& operator=(const AuthPhaseTrace&) = delete;
    ~AuthPhaseTrace() noexcept {
        if (!enabled_) return;
        s_.total_us = now_() - s_.started_us;
        s_.cpu_us = CpuDelta(cpu_start_, cpu_());
        if (sink_ && (s_.status != 0 || s_.outcome != 0 ||
                      (s_.uid > 0 && s_.uid % 16 == 0))) sink_(s_);
    }
    void Result(int status, std::uint64_t uid, int outcome = 0) noexcept {
        s_.status = status; s_.uid = uid; s_.outcome = outcome;
    }
    template<class F> decltype(auto) Measure(Phase phase, F&& fn) {
        if (!enabled_) return std::invoke(std::forward<F>(fn));
        const auto wall = now_(), cpu = cpu_();
        try {
            if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
                std::invoke(std::forward<F>(fn));
                Finish(phase, wall, cpu);
                return;
            } else {
                decltype(auto) value = std::invoke(std::forward<F>(fn));
                Finish(phase, wall, cpu);
                return value;
            }
        } catch (...) {
            s_.threw = true; Finish(phase, wall, cpu); throw;
        }
    }
private:
    static std::int64_t WallNow() noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    static std::int64_t CpuNow() noexcept {
#if defined(__linux__)
        timespec value{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0)
            return static_cast<std::int64_t>(value.tv_sec) * 1000000 + value.tv_nsec / 1000;
#endif
        return -1;
    }
    static std::int64_t CpuDelta(std::int64_t before, std::int64_t after) noexcept {
        return before >= 0 && after >= before ? after - before : -1;
    }
    void Finish(Phase phase, std::int64_t wall, std::int64_t cpu) noexcept {
        const auto index = static_cast<std::size_t>(phase);
        const auto elapsed = now_() - wall, used = CpuDelta(cpu, cpu_());
        if (s_.phase_us[index] < 0) {
            s_.phase_us[index] = elapsed; s_.phase_cpu_us[index] = used;
        } else {
            s_.phase_us[index] += elapsed;
            if (s_.phase_cpu_us[index] >= 0 && used >= 0) s_.phase_cpu_us[index] += used;
            else s_.phase_cpu_us[index] = -1;
        }
    }
    static void Log(const Snapshot& s) noexcept {
        static AuthPhaseLogLimiter limiter;
        if (!limiter.Admit(static_cast<std::uint64_t>(WallNow() / 1000000))) return;
        try {
            LOG_WARN("auth_phase kind=" << s.kind << " uid=" << s.uid
                << " tid=" << s.tid << " started_us=" << s.started_us
                << " total_us=" << s.total_us << " cpu_us=" << s.cpu_us
                << " lookup_us=" << s.phase_us[0] << " lookup_cpu_us=" << s.phase_cpu_us[0]
                << " password_us=" << s.phase_us[1] << " password_cpu_us=" << s.phase_cpu_us[1]
                << " status=" << s.status << " outcome=" << s.outcome << " threw=" << s.threw);
        } catch (...) { /* Diagnostics cannot alter authentication. */ }
    }
    bool enabled_; Sink sink_; Now now_, cpu_; std::int64_t cpu_start_{-1}; Snapshot s_;
};
} // namespace tinyimx::diagnostics
