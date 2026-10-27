#pragma once
#include "common/db/StorageWaitTiming.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <utility>

namespace tinyimx::message::local {
// Diagnostic-only wall/CPU spans. Identity and Reference totals contain their
// subspans. EventAdmission contains RelayHealth, which contains RelayHealthMutex.
// Never add inclusive totals and their children as independent critical work.
// The existing persistence flag is OFF in all formal capacity arms. No clocks
// or thread-local scope changes occur when the flag is disabled.
class NativePersistencePhaseTrace final {
public:
    enum Phase:std::size_t {Identity,IdentityAcquirePing,IdentityQuery,LocalLock,
        References,ReferenceMutex,ReferenceInFlightWait,ReferenceAcquirePing,ReferenceSql,MidAllocate,EngineSync,
        LifetimeLock,CidLookup,BodyValidation,EventAdmission,EventSlotReserve,RecordBuild,StateRead,BatchBuild,EventEncode,PostWriteSignal,RelayHealth,RelayHealthMutex,Count};
    enum RocksField:std::size_t {RocksThreadWait,RocksWal,RocksMemtable,RocksPrePost,
        RocksMutex,RocksCondition,RocksDelay,RocksScheduling,RocksCount};
    struct Snapshot {
        std::uint64_t from{0},to{0},mid{0},reply_mid{0},tid{0};
        std::int64_t started_us{-1},finished_us{-1},cpu_us{-1};
        int status{-1},outcome{-1},fresh_actors{-1};bool threw{false};
        // A batch span contains sequential preparation and one shared commit.
        // It must never be reported as the execution time of a scalar member.
        unsigned batch_inputs{1},batch_results{0},batch_created{0},batch_failed{0};
        std::array<std::uint64_t,64> batch_mids{};
        std::array<std::int64_t,Count> wall_us{},phase_cpu_us{};
        std::array<unsigned,Count> calls{};
        unsigned rocks_write_calls{0};
        std::array<std::int64_t,RocksCount> rocks_ns{};
        Snapshot(){wall_us.fill(-1);phase_cpu_us.fill(-1);rocks_ns.fill(-1);}
    };
    using Now=std::int64_t(*)()noexcept;
    using Sink=void(*)(const Snapshot&)noexcept;
    static bool Enabled()noexcept{
        static const bool enabled=[](){const char*v=std::getenv("TINYIMX_PERSIST_PHASE_TRACE_ENABLE");return v&&v[0]=='1'&&v[1]=='\0';}();
        return enabled;
    }
    NativePersistencePhaseTrace(std::uint64_t from,std::uint64_t to,bool enabled=Enabled(),
        Sink sink=Log,Now now=tinyimx::diagnostics::SteadyMicros,
        Now cpu=tinyimx::diagnostics::ThreadCpuMicros)noexcept
        :enabled_(enabled),sink_(sink),now_(now),cpu_(cpu){
        s_.from=from;s_.to=to;
        if(enabled_){s_.started_us=now_();cpu_start_=cpu_();s_.tid=tinyimx::diagnostics::CurrentThreadId();previous_=current_;current_=this;}
    }
    NativePersistencePhaseTrace(const NativePersistencePhaseTrace&)=delete;
    NativePersistencePhaseTrace& operator=(const NativePersistencePhaseTrace&)=delete;
    ~NativePersistencePhaseTrace()noexcept{if(!finished_)Finish(-1,-1,0);}
    template<class Fn>auto Measure(Phase phase,Fn&&fn)->std::invoke_result_t<Fn>{
        static_assert(!std::is_reference_v<std::invoke_result_t<Fn>>);
        if(!enabled_||finished_)return std::forward<Fn>(fn)();
        const auto start=now_(),cpu=cpu_();
        try{
            if constexpr(std::is_void_v<std::invoke_result_t<Fn>>){std::forward<Fn>(fn)();Record(phase,start,cpu);}
            else{auto value=std::forward<Fn>(fn)();Record(phase,start,cpu);return value;}
        }catch(...){Record(phase,start,cpu);s_.threw=true;throw;}
    }
    template<class Fn>static auto MeasureCurrent(Phase phase,Fn&&fn)->std::invoke_result_t<Fn>{
        if(current_)return current_->Measure(phase,std::forward<Fn>(fn));
        return std::forward<Fn>(fn)();
    }
    struct Stamp{std::int64_t wall{-1},cpu{-1};};
    static Stamp StartCurrent()noexcept{return current_?Stamp{current_->now_(),current_->cpu_()}:Stamp{};}
    static void RecordCurrent(Phase phase,Stamp start)noexcept{if(current_&&start.wall>=0)current_->Record(phase,start.wall,start.cpu);}
    static bool Active()noexcept{return current_!=nullptr;}
    static void RecordRocksWrite(const std::array<std::int64_t,RocksCount>& delta)noexcept{
        if(!current_)return;
        ++current_->s_.rocks_write_calls;
        for(std::size_t i=0;i<delta.size();++i)if(delta[i]>=0)
            current_->s_.rocks_ns[i]=(current_->s_.rocks_ns[i]<0?0:current_->s_.rocks_ns[i])+delta[i];
    }
    static void FreshActors(unsigned value)noexcept{if(current_){
        if(current_->s_.batch_inputs>1)current_->s_.fresh_actors=std::max(0,current_->s_.fresh_actors)+static_cast<int>(value);
        else current_->s_.fresh_actors=static_cast<int>(value);
    }}
    void Allocated(std::uint64_t mid)noexcept{if(enabled_)s_.mid=mid;}
    void BatchInputs(unsigned count)noexcept{if(enabled_)s_.batch_inputs=count;}
    template<class Results>void FinishBatch(const Results& results)noexcept{
        if(!enabled_){Finish(-1,-1,0);return;}
        int status=0;std::uint64_t first=0;
        for(const auto& r:results){
            if(s_.batch_results<s_.batch_mids.size())s_.batch_mids[s_.batch_results]=r.message_id;
            ++s_.batch_results;if(!first&&r.message_id)first=r.message_id;
            using Status=decltype(r.status);using Outcome=decltype(r.outcome);
            if(r.status!=Status::kSucceeded||r.outcome==Outcome::kIdempotencyConflict){
                ++s_.batch_failed;if(r.status!=Status::kSucceeded)status=static_cast<int>(r.status);
            }else if(r.outcome==Outcome::kCreated)++s_.batch_created;
        }
        Finish(status,s_.batch_created?0:1,first);
    }
    void Finish(int status,int outcome,std::uint64_t reply_mid)noexcept{
        if(finished_)return;finished_=true;if(!enabled_)return;
        s_.status=status;s_.outcome=outcome;s_.reply_mid=reply_mid;if(reply_mid)s_.mid=reply_mid;
        s_.finished_us=now_();s_.cpu_us=tinyimx::diagnostics::CpuDelta(cpu_start_,cpu_());
        current_=previous_;if(sink_)sink_(s_);
    }
    const Snapshot& Current()const noexcept{return s_;}
private:
    void Record(Phase phase,std::int64_t wall,std::int64_t cpu)noexcept{
        const auto elapsed=now_()-wall,spent=tinyimx::diagnostics::CpuDelta(cpu,cpu_());
        s_.wall_us[phase]=(s_.wall_us[phase]<0?0:s_.wall_us[phase])+elapsed;
        if(spent>=0)s_.phase_cpu_us[phase]=(s_.phase_cpu_us[phase]<0?0:s_.phase_cpu_us[phase])+spent;
        ++s_.calls[phase];
    }
    static void Log(const Snapshot&s)noexcept{
        bool selected=tinyimx::diagnostics::MessageTraceSelected(s.mid);
        for(unsigned i=0;i<std::min<unsigned>(s.batch_results,s.batch_mids.size());++i)
            if(s.batch_mids[i])selected=selected||tinyimx::diagnostics::MessageTraceSelected(s.batch_mids[i]);
        if(!selected&&s.status==0&&!s.threw&&s.finished_us-s.started_us<10000)return;
        static tinyimx::diagnostics::MessageTraceLogBudget budget;
        if(!budget.Admit(static_cast<std::uint64_t>(tinyimx::diagnostics::SteadyMicros()/1000000),selected))return;
        try{
            std::string members;
            for(unsigned i=0;i<std::min<unsigned>(s.batch_results,s.batch_mids.size());++i){
                if(i)members+=',';members+=std::to_string(s.batch_mids[i]);
            }
            LOG_WARN("native_persist_phase mid="<<s.mid<<" reply_mid="<<s.reply_mid<<" from="<<s.from<<" to="<<s.to<<" tid="<<s.tid
                <<" started_us="<<s.started_us<<" finished_us="<<s.finished_us<<" total_us="<<s.finished_us-s.started_us<<" cpu_us="<<s.cpu_us
                <<" identity_us="<<s.wall_us[Identity]<<" identity_acquire_ping_us="<<s.wall_us[IdentityAcquirePing]<<" identity_query_us="<<s.wall_us[IdentityQuery]
                <<" local_lock_us="<<s.wall_us[LocalLock]<<" references_us="<<s.wall_us[References]<<" reference_mutex_us="<<s.wall_us[ReferenceMutex]
                <<" reference_inflight_wait_us="<<s.wall_us[ReferenceInFlightWait]
                <<" reference_acquire_ping_us="<<s.wall_us[ReferenceAcquirePing]<<" reference_sql_us="<<s.wall_us[ReferenceSql]
                <<" mid_allocate_us="<<s.wall_us[MidAllocate]<<" engine_sync_us="<<s.wall_us[EngineSync]
                <<" lifetime_lock_us="<<s.wall_us[LifetimeLock]<<" cid_lookup_us="<<s.wall_us[CidLookup]<<" body_validation_us="<<s.wall_us[BodyValidation]
                <<" event_admission_us="<<s.wall_us[EventAdmission]<<" event_slot_us="<<s.wall_us[EventSlotReserve]<<" record_build_us="<<s.wall_us[RecordBuild]
                <<" state_read_us="<<s.wall_us[StateRead]<<" batch_build_us="<<s.wall_us[BatchBuild]<<" event_encode_us="<<s.wall_us[EventEncode]<<" post_write_signal_us="<<s.wall_us[PostWriteSignal]
                <<" relay_health_us="<<s.wall_us[RelayHealth]<<" relay_health_mutex_us="<<s.wall_us[RelayHealthMutex]
                <<" identity_cpu_us="<<s.phase_cpu_us[Identity]<<" reference_cpu_us="<<s.phase_cpu_us[References]<<" engine_cpu_us="<<s.phase_cpu_us[EngineSync]
                <<" event_admission_cpu_us="<<s.phase_cpu_us[EventAdmission]<<" relay_health_cpu_us="<<s.phase_cpu_us[RelayHealth]
                <<" rocks_write_calls="<<s.rocks_write_calls<<" rocks_thread_wait_ns="<<s.rocks_ns[RocksThreadWait]
                <<" rocks_wal_ns="<<s.rocks_ns[RocksWal]<<" rocks_memtable_ns="<<s.rocks_ns[RocksMemtable]
                <<" rocks_prepost_ns="<<s.rocks_ns[RocksPrePost]<<" rocks_mutex_ns="<<s.rocks_ns[RocksMutex]
                <<" rocks_condition_ns="<<s.rocks_ns[RocksCondition]<<" rocks_delay_ns="<<s.rocks_ns[RocksDelay]
                <<" rocks_scheduling_ns="<<s.rocks_ns[RocksScheduling]
                <<" state_read_cpu_us="<<s.phase_cpu_us[StateRead]<<" batch_build_cpu_us="<<s.phase_cpu_us[BatchBuild]<<" event_encode_cpu_us="<<s.phase_cpu_us[EventEncode]<<" post_write_signal_cpu_us="<<s.phase_cpu_us[PostWriteSignal]
                <<" fresh_actors="<<s.fresh_actors<<" status="<<s.status<<" outcome="<<s.outcome<<" threw="<<s.threw
                <<" batch_inputs="<<s.batch_inputs<<" batch_results="<<s.batch_results<<" batch_created="<<s.batch_created
                <<" batch_failed="<<s.batch_failed<<" batch_mids="<<members);
        }catch(...){/* Numeric diagnostics must never change a persistence result. */}
    }
    inline static thread_local NativePersistencePhaseTrace*current_{nullptr};
    bool enabled_,finished_{false};Sink sink_;Now now_,cpu_;std::int64_t cpu_start_{-1};Snapshot s_;
    NativePersistencePhaseTrace*previous_{nullptr};
};
}
