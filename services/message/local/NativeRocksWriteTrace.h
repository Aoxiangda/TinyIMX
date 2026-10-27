#pragma once

#include "services/message/local/NativePersistencePhaseTrace.h"
#include <rocksdb/perf_context.h>
#include <rocksdb/perf_level.h>

namespace tinyimx::message::local {
// The pinned11.8.1 AwaitState timer covers waits after its short pause fast path.
// In pipelined writes, pre/post and memtable counters can include other waits.
// These are individual counter deltas, never an additive wall-time partition.
// Existing thread-local counters are not reset, and the caller's level is
// restored after successful and exceptional writes.
class NativeRocksWriteTrace final {
    using Trace=NativePersistencePhaseTrace;
    using Counters=std::array<std::uint64_t,Trace::RocksCount>;
public:
    static bool Enabled()noexcept{
        static const bool enabled=[]{
            const char* value=std::getenv("TINYIMX_NATIVE_ROCKS_WRITE_TRACE_ENABLE");
            return value&&value[0]=='1'&&value[1]=='\0';
        }();
        return enabled;
    }
    explicit NativeRocksWriteTrace(bool enabled=Enabled())noexcept
        :enabled_(enabled&&Trace::Active()){
        if(!enabled_)return;
        context_=rocksdb::get_perf_context();
        previous_level_=rocksdb::GetPerfLevel();
        before_=Read(*context_);
        rocksdb::SetPerfLevel(rocksdb::PerfLevel::kEnableTime);
    }
    NativeRocksWriteTrace(const NativeRocksWriteTrace&)=delete;
    NativeRocksWriteTrace& operator=(const NativeRocksWriteTrace&)=delete;
    ~NativeRocksWriteTrace()noexcept{
        if(!enabled_)return;
        const auto after=Read(*context_);
        std::array<std::int64_t,Trace::RocksCount> delta;
        for(std::size_t i=0;i<delta.size();++i)
            delta[i]=after[i]>=before_[i]?static_cast<std::int64_t>(after[i]-before_[i]):-1;
        Trace::RecordRocksWrite(delta);
        rocksdb::SetPerfLevel(previous_level_);
    }
private:
    static Counters Read(const rocksdb::PerfContext& c)noexcept{
        return {c.write_thread_wait_nanos,c.write_wal_time,c.write_memtable_time,
            c.write_pre_and_post_process_time,c.db_mutex_lock_nanos,c.db_condition_wait_nanos,
            c.write_delay_time,c.write_scheduling_flushes_compactions_time};
    }
    bool enabled_;
    rocksdb::PerfContext* context_{nullptr};
    rocksdb::PerfLevel previous_level_{rocksdb::PerfLevel::kDisable};
    Counters before_{};
};
}
