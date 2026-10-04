#pragma once
#include "common/logging/LogMacros.h"
#include "services/message/application/MessageApplicationTypes.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace tinyimx::message {
class PersistenceLogLimiter final {
public:
    bool Admit(std::uint64_t second) noexcept {
        const auto base=second<<8;auto old=state_.load(std::memory_order_relaxed);
        for(;;){const auto bucket=old&~std::uint64_t{255};if(base<bucket)return false;
            const auto count=base==bucket?(old&255):0;if(count>=8)return false;
            if(state_.compare_exchange_weak(old,base|(count+1),std::memory_order_relaxed))return true;}
    }
private:std::atomic<std::uint64_t> state_{0};
};

// Numeric diagnostics only. Never changes SQL, retries, deadlines or admission.
class PrivatePersistenceTrace final {
public:
    enum class Phase:std::size_t {Precheck,Acquire,Begin,Insert,IdentityRead,OutboxInsert,Commit,RecoveryRead,Rollback,Count};
    static constexpr auto kPhases=static_cast<std::size_t>(Phase::Count);
    struct Snapshot {
        std::uint64_t from=0,to=0,message_id=0;
        std::int64_t started_us=0,total_us=0;
        std::array<std::int64_t,kPhases> phase_us{{-1,-1,-1,-1,-1,-1,-1,-1,-1}};
        int status=0,outcome=0;bool threw=false;
    };
    using Now=std::int64_t(*)() noexcept;
    using Sink=void(*)(const Snapshot&) noexcept;
    static bool EnvironmentEnabled() noexcept {
        static const bool enabled=[](){const char* value=std::getenv("TINYIMX_PERSIST_PHASE_TRACE_ENABLE");return value&&value[0]=='1'&&value[1]=='\0';}();
        return enabled;
    }
    PrivatePersistenceTrace(std::uint64_t from,std::uint64_t to,const MessageRepositoryPersistResult& result,
                            bool enabled=EnvironmentEnabled(),Sink sink=Log,Now now=SteadyMicros,
                            std::int64_t threshold_us=10000) noexcept
        : result_(result),enabled_(enabled),sink_(sink),now_(now),threshold_(threshold_us){
        snapshot_.from=from;snapshot_.to=to;if(enabled_)snapshot_.started_us=now_();
    }
    PrivatePersistenceTrace(const PrivatePersistenceTrace&)=delete;
    PrivatePersistenceTrace& operator=(const PrivatePersistenceTrace&)=delete;
    ~PrivatePersistenceTrace() noexcept {
        if(!enabled_)return;
        snapshot_.total_us=now_()-snapshot_.started_us;
        snapshot_.status=static_cast<int>(result_.status);snapshot_.outcome=static_cast<int>(result_.outcome);snapshot_.message_id=result_.message_id;
        if(sink_&&(snapshot_.threw||result_.status!=MessageApplicationStatus::kSucceeded||snapshot_.total_us>=threshold_))sink_(snapshot_);
    }
    template<class Fn>auto Measure(Phase phase,Fn&& fn)->std::invoke_result_t<Fn>{
        static_assert(!std::is_reference_v<std::invoke_result_t<Fn>>,"Own diagnostic phase result");
        if(!enabled_)return std::forward<Fn>(fn)();
        const auto start=now_();const auto i=static_cast<std::size_t>(phase);
        try {
            if constexpr(std::is_void_v<std::invoke_result_t<Fn>>){std::forward<Fn>(fn)();Record(i,start);}
            else {auto result=std::forward<Fn>(fn)();Record(i,start);return result;}
        }catch(...){Record(i,start);snapshot_.threw=true;throw;}
    }
private:
    static std::int64_t SteadyMicros() noexcept {return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    void Record(std::size_t i,std::int64_t start) noexcept {const auto elapsed=now_()-start;auto& v=snapshot_.phase_us[i];v=v<0?elapsed:v+elapsed;}
    static void Log(const Snapshot& s) noexcept {
        static PersistenceLogLimiter limiter;if(!limiter.Admit(static_cast<std::uint64_t>(s.started_us/1000000)))return;
        try {LOG_WARN("private_persist_phase from="<<s.from<<" to="<<s.to<<" mid="<<s.message_id
            <<" started_us="<<s.started_us<<" total_us="<<s.total_us<<" status="<<s.status<<" outcome="<<s.outcome<<" threw="<<s.threw
            <<" precheck_us="<<s.phase_us[0]<<" acquire_us="<<s.phase_us[1]<<" begin_us="<<s.phase_us[2]<<" insert_us="<<s.phase_us[3]
            <<" identity_read_us="<<s.phase_us[4]<<" outbox_insert_us="<<s.phase_us[5]<<" commit_us="<<s.phase_us[6]
            <<" recovery_read_us="<<s.phase_us[7]<<" rollback_us="<<s.phase_us[8]);}catch(...){/* Diagnostics cannot fail a durable request. */}
    }
    const MessageRepositoryPersistResult& result_;bool enabled_;Sink sink_;Now now_;std::int64_t threshold_;Snapshot snapshot_;
};
} // namespace tinyimx::message
