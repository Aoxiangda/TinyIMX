#pragma once

#include "common/db/StorageWaitTiming.h"
#include <functional>

namespace tinyimx::diagnostics {

// Admission at the C++ service method is distinct from gRPC transport admission.
// Queue timestamps are present only for the optional bounded callback dispatcher.
class RpcExecutionTrace final {
public:
    struct Snapshot {
        int method{0}, callback{0}, status{-1}; // 1=persist,2=confirm,3=get
        std::uint64_t mid{0}, from{0}, to{0}, tid{0};
        std::int64_t admitted_us{-1}, started_us{-1}, finished_us{-1}, cpu_us{-1};
    };
    using Sink = void(*)(const Snapshot&) noexcept;
    using Now = std::int64_t(*)() noexcept;
    static bool Enabled() noexcept {
        static const bool on=[] {
            const char* v=std::getenv("TINYIMX_RPC_EXECUTION_TRACE_ENABLE");
            return v && v[0]=='1' && v[1]=='\0';
        }();
        return on;
    }
    RpcExecutionTrace(int method, bool callback, std::uint64_t mid=0,
                      std::uint64_t from=0, std::uint64_t to=0,
                      bool enabled=Enabled(), Sink sink=Log,
                      Now now=SteadyMicros, Now cpu=ThreadCpuMicros) noexcept
        : enabled_(enabled),sink_(sink),now_(now),cpu_(cpu) {
        s_.method=method;s_.callback=callback;s_.mid=mid;s_.from=from;s_.to=to;
        if(enabled_) s_.admitted_us=now_();
    }
    void Start() noexcept {
        if(!enabled_)return;
        s_.started_us=now_();cpu_start_=cpu_();s_.tid=CurrentThreadId();
    }
    // A batch shares repository/CPU work. Keep the call's wall boundaries,
    // but do not multiply shared thread CPU into a per-request CPU claim.
    void StartSharedBatch() noexcept { Start();cpu_start_=-1; }
    void Finish(int status,std::uint64_t mid) noexcept {
        if(!enabled_)return;
        s_.status=status;s_.mid=mid;s_.finished_us=now_();
        if(s_.started_us>=0)s_.cpu_us=CpuDelta(cpu_start_,cpu_());
        // Every invocation ends here, including queue rejection/cancellation.
        // The sink is bounded; never interpret sampled logs as all-call accounting.
        if(sink_)sink_(s_);
        enabled_=false;
    }
    const Snapshot& Current()const noexcept{return s_;}
private:
    static void Log(const Snapshot& s)noexcept {
        if(s.method<1||s.method>3)return;
        const bool selected=MessageTraceSelected(s.mid);
        const bool slow=s.started_us<0 || s.finished_us-s.admitted_us>=10000 || s.status!=0;
        if(!selected&&!slow)return;
        static MessageTraceLogBudget budgets[3][2];
        if(!budgets[s.method-1][s.callback].Admit(
                static_cast<std::uint64_t>(SteadyMicros()/1000000),selected))return;
        try {
            LOG_WARN("rpc_execution_phase method="<<s.method<<" callback="<<s.callback
                <<" mid="<<s.mid<<" from="<<s.from<<" to="<<s.to<<" tid="<<s.tid
                <<" admitted_us="<<s.admitted_us<<" started_us="<<s.started_us
                <<" finished_us="<<s.finished_us<<" cpu_us="<<s.cpu_us<<" status="<<s.status);
        }catch(...){ /* Diagnostics never change an RPC result. */ }
    }
    bool enabled_;Sink sink_;Now now_,cpu_;std::int64_t cpu_start_{-1};Snapshot s_;
};
} // namespace tinyimx::diagnostics
