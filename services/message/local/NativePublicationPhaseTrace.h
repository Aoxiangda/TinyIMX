#pragma once
#include "common/db/StorageWaitTiming.h"
#include <charconv>
#include <string_view>

namespace tinyimx::message::local {
// Diagnostic spans only. Formal capacity arms keep this flag OFF. Stop() records
// clocks; Emit() may log, so callers measuring a mutex must emit after unlocking.
// Scan spans are contained in the poll critical section, not extra disjoint work.
class NativePublicationPhaseTrace final {
public:
    enum Phase { Publish, ProgressWrite, PollMutex, Scan, CompletionMutex, ProgressSync,
                 ProgressLockAcquire, ProgressEngineWrite, Count };
    struct Snapshot {
        Phase phase{Publish};
        std::uint64_t mid{0}, tid{0}, units{0};
        std::int64_t started_us{-1}, finished_us{-1}, cpu_us{-1};
        int status{-1};
    };
    using Now=std::int64_t(*)()noexcept;
    using Sink=void(*)(const Snapshot&)noexcept;
    static bool Enabled()noexcept {
        static const bool enabled=[] {
            const char* v=std::getenv("TINYIMX_NATIVE_PUBLICATION_PHASE_TRACE_ENABLE");
            return v&&v[0]=='1'&&v[1]=='\0';
        }();
        return enabled;
    }
    static NativePublicationPhaseTrace Deferred(Phase phase,std::string_view event={},std::uint64_t units=0)noexcept {
        return NativePublicationPhaseTrace(phase,event,units,Enabled(),Log,
            tinyimx::diagnostics::SteadyMicros,tinyimx::diagnostics::ThreadCpuMicros,false);
    }
    NativePublicationPhaseTrace(Phase phase,std::string_view event={},std::uint64_t units=0,
        bool enabled=Enabled(),Sink sink=Log,Now now=tinyimx::diagnostics::SteadyMicros,
        Now cpu=tinyimx::diagnostics::ThreadCpuMicros,bool start=true)noexcept
        :enabled_(enabled),sink_(sink),now_(now),cpu_(cpu) {
        s_.phase=phase;s_.units=units;
        if(enabled_) {
            constexpr std::string_view prefix="message.created.v1:";
            if(event.starts_with(prefix)) {
                const auto number=event.substr(prefix.size());std::uint64_t id=0;
                const auto parsed=std::from_chars(number.data(),number.data()+number.size(),id);
                if(parsed.ec==std::errc{}&&parsed.ptr==number.data()+number.size())s_.mid=id;
            }
            if(start)Start();
        }
    }
    NativePublicationPhaseTrace(const NativePublicationPhaseTrace&)=delete;
    NativePublicationPhaseTrace& operator=(const NativePublicationPhaseTrace&)=delete;
    ~NativePublicationPhaseTrace()noexcept { Emit(); }
    void Start()noexcept {
        if(!enabled_||started_)return;
        started_=true;s_.started_us=now_();cpu_start_=cpu_();s_.tid=tinyimx::diagnostics::CurrentThreadId();
    }
    void Stop()noexcept {
        if(!enabled_||!started_||stopped_)return;
        stopped_=true;s_.finished_us=now_();s_.cpu_us=tinyimx::diagnostics::CpuDelta(cpu_start_,cpu_());
    }
    void Result(int status,std::uint64_t units)noexcept {
        if(enabled_){s_.status=status;s_.units=units;}
    }
    void Emit()noexcept {
        if(emitted_)return;emitted_=true;
        if(!enabled_||!started_)return;
        Stop();if(sink_)sink_(s_);
    }
private:
    static void Log(const Snapshot&s)noexcept {
        if(s.phase<Publish||s.phase>=Count)return;
        const bool selected=tinyimx::diagnostics::MessageTraceSelected(s.mid);
        if(!selected&&s.status==0&&s.finished_us-s.started_us<1000)return;
        static tinyimx::diagnostics::MessageTraceLogBudget budgets[Count];
        if(!budgets[s.phase].Admit(static_cast<std::uint64_t>(s.finished_us/1000000),selected))return;
        try {
            LOG_WARN("native_publication_phase phase="<<static_cast<int>(s.phase)<<" mid="<<s.mid
                <<" tid="<<s.tid<<" started_us="<<s.started_us<<" finished_us="<<s.finished_us
                <<" total_us="<<s.finished_us-s.started_us<<" cpu_us="<<s.cpu_us
                <<" status="<<s.status<<" units="<<s.units);
        }catch(...) { /* Diagnostics cannot change publication or shutdown. */ }
    }
    bool enabled_,started_{false},stopped_{false},emitted_{false};
    Sink sink_;Now now_,cpu_;std::int64_t cpu_start_{-1};Snapshot s_;
};
} // namespace tinyimx::message::local
