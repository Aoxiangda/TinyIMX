#pragma once
#include "common/db/StorageWaitTiming.h"
#include <array>
#include <limits>
#include <string_view>

namespace tinyimx::diagnostics {
inline std::uint64_t ParseGroupGetTraceRecipient(const char* value) noexcept {
    if (!value || !*value || (value[0]=='0' && value[1])) return 0;
    std::uint64_t result=0;
    for (;*value;++value) {
        if (*value<'0' || *value>'9') return 0;
        const auto digit=static_cast<std::uint64_t>(*value-'0');
        if (result>(std::numeric_limits<std::uint64_t>::max()-digit)/10) return 0;
        result=result*10+digit;
    }
    return result;
}
inline std::uint64_t GroupGetTraceRecipient() noexcept {
    static const auto selected=ParseGroupGetTraceRecipient(
        std::getenv("TINYIMX_GROUP_GET_BOUNDARY_TRACE_UID"));
    return selected;
}
inline std::uint64_t GroupGetTraceHash(std::string_view text) noexcept {
    std::uint64_t h=14695981039346656037ULL;
    for (unsigned char c:text) {h^=c;h*=1099511628211ULL;}
    return h;
}
class GroupGetTraceRateGate final {
public:
    bool Allow(std::int64_t micros) noexcept {
        if (micros<0) return false;
        const auto base=static_cast<std::uint64_t>(micros/1000000)<<8;
        auto old=state_.load(std::memory_order_relaxed);
        for (;;) {
            const auto bucket=old&~std::uint64_t{255};if (base<bucket) return false;
            const auto count=base==bucket?(old&255):0;if (count>=8) return false;
            if (state_.compare_exchange_weak(old,base|(count+1),std::memory_order_relaxed)) return true;
        }
    }
private:
    std::atomic<std::uint64_t> state_{0};
};
// Same-call boundaries only. Handler entry/exit excludes admission and transport.
// FNV identifiers are diagnostic correlation keys, never authentication or secrets.
class GroupGetBoundaryTrace final {
public:
    struct Identity {std::uint64_t mid{0},recipient{0},rid_hash{0},caller_hash{0};};
    struct Snapshot {
        Identity identity{};
        std::uint64_t tid{0};
        int side{0},status{-1}; // 1=client,2=handler,3=repository; -1=early path
        std::int64_t started_us{0},total_us{0},cpu_us{-1};
        std::array<std::int64_t,5> marks{{-1,-1,-1,-1,-1}};
    };
    using Now=std::int64_t(*)() noexcept;
    using Sink=void(*)(const Snapshot&) noexcept;
    GroupGetBoundaryTrace(int side,std::uint64_t mid,std::uint64_t uid,
        std::string_view rid,std::string_view caller,
        bool enabled=false,Sink sink=Log,Now now=SteadyMicros,Now cpu=ThreadCpuMicros,
        bool use_selector=true) noexcept
        : enabled_(use_selector?uid!=0 && uid==GroupGetTraceRecipient():enabled),
          sink_(sink),now_(now),cpu_(cpu) {
        s_.side=side;
        if (enabled_) {
            s_.identity={mid,uid,GroupGetTraceHash(rid),GroupGetTraceHash(caller)};
            Start();
        }
    }
    // Repository inherits the exact synchronous handler identity; no independent sample.
    GroupGetBoundaryTrace(int side,std::uint64_t mid,std::uint64_t uid,
        Sink sink=Log,Now now=SteadyMicros,Now cpu=ThreadCpuMicros) noexcept
        : enabled_(current_ && current_->mid==mid && current_->recipient==uid),
          sink_(sink),now_(now),cpu_(cpu) {
        s_.side=side;
        if (enabled_) {s_.identity=*current_;Start();}
    }
    GroupGetBoundaryTrace(const GroupGetBoundaryTrace&)=delete;
    GroupGetBoundaryTrace& operator=(const GroupGetBoundaryTrace&)=delete;
    ~GroupGetBoundaryTrace() noexcept {
        if (!enabled_) return;
        s_.total_us=now_()-s_.started_us;s_.cpu_us=CpuDelta(cpu_start_,cpu_());
        if (sink_) sink_(s_);
    }
    void Mark(unsigned index) noexcept {
        if (enabled_ && index>=1 && index<=s_.marks.size()) s_.marks[index-1]=now_();
    }
    void Result(int status) noexcept {s_.status=status;}
    const Identity* Context() const noexcept {return enabled_?&s_.identity:nullptr;}
    class Scope final {
    public:
        explicit Scope(const GroupGetBoundaryTrace& trace) noexcept : prior_(current_) {current_=trace.Context();}
        ~Scope() noexcept {current_=prior_;}
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    private:const Identity* prior_;
    };
private:
    void Start() noexcept {s_.started_us=now_();cpu_start_=cpu_();s_.tid=CurrentThreadId();}
    static void Log(const Snapshot& s) noexcept {
        static GroupGetTraceRateGate gates[3];
        if (s.side<1 || s.side>3 || !gates[s.side-1].Allow(s.started_us)) return;
        try {
            LOG_WARN("group_get_boundary_phase side="<<s.side
                <<" mid="<<s.identity.mid<<" recipient="<<s.identity.recipient
                <<" rid_hash="<<s.identity.rid_hash<<" caller_hash="<<s.identity.caller_hash
                <<" tid="<<s.tid<<" started_us="<<s.started_us<<" total_us="<<s.total_us
                <<" cpu_us="<<s.cpu_us<<" m1_us="<<s.marks[0]<<" m2_us="<<s.marks[1]
                <<" m3_us="<<s.marks[2]<<" m4_us="<<s.marks[3]<<" m5_us="<<s.marks[4]
                <<" status="<<s.status);
        } catch (...) { /* Diagnostics cannot change a business result. */ }
    }
    inline static thread_local const Identity* current_{nullptr};
    bool enabled_;
    Sink sink_;Now now_,cpu_;std::int64_t cpu_start_{-1};Snapshot s_{};
};
} // namespace tinyimx::diagnostics
