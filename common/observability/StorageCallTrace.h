#pragma once
#include "common/observability/RpcExecutionTrace.h"
#include <type_traits>
#include <utility>

namespace tinyimx::diagnostics {
class StorageCallTrace final {
public:
    enum Phase {Acquire,Sql};
    struct Snapshot {
        int method{0};std::uint64_t mid{0},tid{0};
        std::int64_t started_us{-1},finished_us{-1},acquire_us{-1},sql_us{-1};
        std::int64_t acquire_cpu_us{-1},sql_cpu_us{-1};bool threw{false};
    };
    using Sink=void(*)(const Snapshot&) noexcept;
    using Now=std::int64_t(*)() noexcept;
    StorageCallTrace(int method,std::uint64_t mid,bool enabled=RpcExecutionTrace::Enabled(),
                     Sink sink=Log,Now now=SteadyMicros,Now cpu=ThreadCpuMicros)noexcept
        :enabled_(enabled),sink_(sink),now_(now),cpu_(cpu) {
        s_.method=method;s_.mid=mid;
        if(enabled_){s_.started_us=now_();s_.tid=CurrentThreadId();}
    }
    ~StorageCallTrace()noexcept {
        if(!enabled_)return;
        s_.finished_us=now_();if(sink_)sink_(s_);
    }
    template<class Fn>auto Measure(Phase phase,Fn&& fn)->std::invoke_result_t<Fn> {
        static_assert(!std::is_reference_v<std::invoke_result_t<Fn>>);
        if(!enabled_)return std::forward<Fn>(fn)();
        const auto start=now_(),cpu=cpu_();
        try {
            if constexpr(std::is_void_v<std::invoke_result_t<Fn>>){std::forward<Fn>(fn)();Record(phase,start,cpu);}
            else {auto result=std::forward<Fn>(fn)();Record(phase,start,cpu);return result;}
        }catch(...){Record(phase,start,cpu);s_.threw=true;throw;}
    }
private:
    void Record(Phase phase,std::int64_t start,std::int64_t cpu)noexcept {
        auto& wall=phase==Acquire?s_.acquire_us:s_.sql_us;
        auto& spent=phase==Acquire?s_.acquire_cpu_us:s_.sql_cpu_us;
        const auto delta=CpuDelta(cpu,cpu_());wall=now_()-start;spent=delta;
    }
    static void Log(const Snapshot& s)noexcept {
        if(s.method<1||s.method>3)return;
        const bool selected=MessageTraceSelected(s.mid);
        if(!selected&&!s.threw&&s.finished_us-s.started_us<10000)return;
        static MessageTraceLogBudget budgets[3];
        if(!budgets[s.method-1].Admit(static_cast<std::uint64_t>(SteadyMicros()/1000000),selected))return;
        try {LOG_WARN("storage_call_phase method="<<s.method<<" mid="<<s.mid<<" tid="<<s.tid
            <<" started_us="<<s.started_us<<" finished_us="<<s.finished_us
            <<" acquire_us="<<s.acquire_us<<" sql_us="<<s.sql_us
            <<" acquire_cpu_us="<<s.acquire_cpu_us<<" sql_cpu_us="<<s.sql_cpu_us<<" threw="<<s.threw);
        }catch(...){ /* Numeric diagnostics only. */ }
    }
    bool enabled_;Sink sink_;Now now_,cpu_;Snapshot s_;
};
} // namespace tinyimx::diagnostics
