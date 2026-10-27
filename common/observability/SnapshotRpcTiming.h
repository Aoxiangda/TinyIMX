#pragma once

#include "common/db/StorageWaitTiming.h"
#include <charconv>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace tinyimx::diagnostics {

// Separate from RpcExecutionTrace: no MID exists in this RPC contract. These
// records identify a snapshot call, never a persisted message. Handler entry
// does not reveal gRPC admission/transport queue time before that entry.
class SnapshotRpcTiming final {
public:
    enum Role { Server=0, Client=1 };
    enum Method { Dialog=1, User=2, Batch=3 };
    struct Key {
        std::uint64_t origin{0}, sequence{0};
        bool Valid()const noexcept{return origin!=0&&sequence!=0;}
    };
    struct Snapshot {
        Role role{Server};Method method{Dialog};Key key{};
        std::uint64_t receiver{0},peer{0},rows{0},tid{0};
        std::int64_t entry_us{-1},finished_us{-1},thread_cpu_us{-1};
        std::int64_t operation_started_us{-1},operation_finished_us{-1},operation_cpu_us{-1};
        int status{-1},response_valid{-1};bool operation_threw{false};
    };
    using Sink=void(*)(const Snapshot&)noexcept;
    using Now=std::int64_t(*)()noexcept;
    static constexpr const char* MetadataHeader="x-tinyimx-snapshot-timing";
    static bool Enabled()noexcept {
        static const bool on=[] {
            const char* s=std::getenv("TINYIMX_SNAPSHOT_RPC_TRACE_ENABLE");
            return s&&s[0]=='1'&&s[1]=='\0';
        }();
        return on;
    }
    // Origin is process-local monotonic nanoseconds at first traced call.
    // Receipts must reject duplicate keys, and join only the same experiment;
    // this numeric diagnostic tag is neither authentication nor a global UUID.
    static Key NewKey()noexcept {
        static const std::uint64_t origin=static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count())|1;
        static std::atomic<std::uint64_t> sequence{0};
        const auto next=sequence.fetch_add(1,std::memory_order_relaxed)+1;
        return next?Key{origin,next}:Key{};
    }
    static Key ParseKey(std::string_view text)noexcept {
        if(text.size()<3||text.size()>41)return{};
        const auto split=text.find(':');
        if(split==0||split==std::string_view::npos||split+1==text.size())return{};
        Key key;
        const auto first=std::from_chars(text.data(),text.data()+split,key.origin);
        const auto last=std::from_chars(text.data()+split+1,text.data()+text.size(),key.sequence);
        if(first.ec!=std::errc{}||last.ec!=std::errc{}||first.ptr!=text.data()+split||last.ptr!=text.data()+text.size()||!key.Valid())return{};
        return key;
    }
    static std::string EncodeKey(Key key) {
        return key.Valid()?std::to_string(key.origin)+':'+std::to_string(key.sequence):std::string{};
    }
    SnapshotRpcTiming(Role role,Method method,std::uint64_t receiver,
                      std::uint64_t peer,std::uint64_t rows,Key key,
                      bool enabled=Enabled(),Sink sink=Log,
                      Now now=SteadyMicros,Now cpu=ThreadCpuMicros)noexcept
        :enabled_(enabled),sink_(sink),now_(now),cpu_(cpu) {
        s_.role=role;s_.method=method;s_.receiver=receiver;s_.peer=peer;s_.rows=rows;s_.key=key;
        if(!enabled_)return;
        s_.entry_us=now_();cpu_start_=cpu_();s_.tid=CurrentThreadId();
        if(role==Client&&!key.Valid())s_.key=NewKey();
    }
    SnapshotRpcTiming(const SnapshotRpcTiming&)=delete;
    SnapshotRpcTiming&operator=(const SnapshotRpcTiming&)=delete;
    ~SnapshotRpcTiming()noexcept {
        if(!enabled_)return;
        s_.finished_us=now_();s_.thread_cpu_us=CpuDelta(cpu_start_,cpu_());
        if(sink_)sink_(s_);
    }
    bool Active()const noexcept{return enabled_;}
    Key CurrentKey()const noexcept{return s_.key;}
    void ForgetKey()noexcept{s_.key={};}
    void Result(int status)noexcept{s_.status=status;s_.response_valid=status==0?0:-1;}
    void ResponseValid()noexcept{s_.response_valid=1;}
    // Server operation = source read. Client operation = one actual stub call,
    // excluding response validation and any legacy batch fallback RPCs.
    template<class Fn>auto Measure(Fn&& fn)->std::invoke_result_t<Fn> {
        static_assert(!std::is_reference_v<std::invoke_result_t<Fn>>);
        if(!enabled_)return std::forward<Fn>(fn)();
        s_.operation_started_us=now_();const auto cpu=cpu_();
        try {
            if constexpr(std::is_void_v<std::invoke_result_t<Fn>>){std::forward<Fn>(fn)();EndOperation(cpu);}
            else{auto result=std::forward<Fn>(fn)();EndOperation(cpu);return result;}
        }catch(...){EndOperation(cpu);s_.operation_threw=true;throw;}
    }
private:
    void EndOperation(std::int64_t start_cpu)noexcept {
        s_.operation_finished_us=now_();s_.operation_cpu_us=CpuDelta(start_cpu,cpu_());
    }
    static void Log(const Snapshot&s)noexcept {
        if(s.role<Server||s.role>Client||s.method<Dialog||s.method>Batch)return;
        const bool selected=s.key.Valid()&&MessageTraceSelected(s.key.origin^s.key.sequence);
        const bool slow=s.finished_us-s.entry_us>=10000;
        if(!selected&&!slow&&s.status==0&&!s.operation_threw)return;
        static MessageTraceLogBudget budgets[2][3];
        if(!budgets[s.role][s.method-1].Admit(static_cast<std::uint64_t>(SteadyMicros()/1000000),selected))return;
        try {
            LOG_WARN("snapshot_rpc_phase role="<<s.role<<" method="<<s.method
                <<" origin="<<s.key.origin<<" call="<<s.key.sequence
                <<" receiver="<<s.receiver<<" peer="<<s.peer<<" rows="<<s.rows<<" tid="<<s.tid
                <<" entry_us="<<s.entry_us<<" finished_us="<<s.finished_us<<" thread_cpu_us="<<s.thread_cpu_us
                <<" operation_started_us="<<s.operation_started_us<<" operation_finished_us="<<s.operation_finished_us
                <<" operation_cpu_us="<<s.operation_cpu_us<<" status="<<s.status
                <<" response_valid="<<s.response_valid<<" operation_threw="<<s.operation_threw);
        }catch(...){ /* Numeric diagnostics never decide the business result. */ }
    }
    bool enabled_;Sink sink_;Now now_,cpu_;std::int64_t cpu_start_{-1};Snapshot s_;
};
} // namespace tinyimx::diagnostics
