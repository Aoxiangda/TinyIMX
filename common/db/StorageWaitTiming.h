#pragma once

#include "common/logging/LogMacros.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#if defined(__linux__)
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#endif

namespace tinyimx::diagnostics {
inline bool StorageWaitEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_STORAGE_WAIT_TRACE_ENABLE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}
inline std::int64_t SteadyMicros() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline std::int64_t ThreadCpuMicros() noexcept {
#if defined(__linux__)
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0)
        return static_cast<std::int64_t>(value.tv_sec) * 1000000 + value.tv_nsec / 1000;
#endif
    return -1;
}
inline std::uint64_t CurrentThreadId() noexcept {
#if defined(__linux__)
    return static_cast<std::uint64_t>(::syscall(SYS_gettid));
#else
    return 0;
#endif
}
inline std::int64_t CpuDelta(std::int64_t start, std::int64_t end) noexcept {
    return start >= 0 && end >= start ? end - start : -1;
}

// Handler timing excludes gRPC admission before method entry and transport
// after return. Successful M%64 samples correlate with repository logs.
class StorageOperationTrace final {
public:
    struct Snapshot {
        std::uint64_t from{0}, to{0}, mid{0}, tid{0};
        std::int64_t started_us{0}, total_us{0}, cpu_us{-1};
        int kind{0}, status{6}; // kind1=persist handler,2=confirm handler; app status
    };
    using Now = std::int64_t(*)() noexcept;
    using Sink = void(*)(const Snapshot&) noexcept;
    StorageOperationTrace(int kind, std::uint64_t from, std::uint64_t to,
                          std::uint64_t mid = 0, bool enabled = StorageWaitEnabled(),
                          Sink sink = Log, Now now = SteadyMicros,
                          Now cpu = ThreadCpuMicros) noexcept
        : enabled_(enabled), sink_(sink), now_(now), cpu_(cpu) {
        s_.kind=kind; s_.from=from; s_.to=to; s_.mid=mid;
        if (enabled_) { s_.started_us=now_(); cpu_start_=cpu_(); s_.tid=CurrentThreadId(); }
    }
    StorageOperationTrace(const StorageOperationTrace&) = delete;
    StorageOperationTrace& operator=(const StorageOperationTrace&) = delete;
    ~StorageOperationTrace() noexcept {
        if (!enabled_) return;
        s_.total_us=now_()-s_.started_us; s_.cpu_us=CpuDelta(cpu_start_,cpu_());
        if (sink_ && (s_.status!=0 || (s_.mid>0 && s_.mid%64==0))) sink_(s_);
    }
    void Result(int status, std::uint64_t mid) noexcept { s_.status=status; s_.mid=mid; }
private:
    static void Log(const Snapshot& s) noexcept {
        static std::atomic<std::uint64_t> state{0};
        const auto base=static_cast<std::uint64_t>(s.started_us/1000000)<<8;
        auto old=state.load(std::memory_order_relaxed);
        for (;;) {
            const auto bucket=old&~std::uint64_t{255}; if (base<bucket) return;
            const auto count=base==bucket?(old&255):0; if (count>=8) return;
            if (state.compare_exchange_weak(old,base|(count+1),std::memory_order_relaxed)) break;
        }
        try {
            LOG_WARN("storage_rpc_phase kind="<<s.kind<<" from="<<s.from<<" to="<<s.to
                <<" mid="<<s.mid<<" tid="<<s.tid<<" started_us="<<s.started_us
                <<" total_us="<<s.total_us<<" cpu_us="<<s.cpu_us<<" status="<<s.status);
        } catch (...) { /* Diagnostics never change the application result. */ }
    }
    bool enabled_; Sink sink_; Now now_,cpu_; std::int64_t cpu_start_{-1}; Snapshot s_;
};
} // namespace tinyimx::diagnostics
