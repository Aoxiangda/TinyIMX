#pragma once
#include <rocksdb/options.h>
#include <cstdlib>

namespace tinyimx::message::local {
// Experimental parity with the pinned peer's 512 KiB SST writeback cadence.
// A range writeback hint spreads large flush/compaction output. It is not a
// durability acknowledgement: sync-WAL writes and SST close/fsync stay intact.
// Keep OFF until the complete-business, long-load A/B retention rule passes.
inline void ApplyNativeSstWritebackPolicy(rocksdb::Options& options)noexcept {
    const char* value=std::getenv("TINYIMX_NATIVE_SST_SMOOTH_WRITEBACK_ENABLE");
    if(value&&value[0]=='1'&&value[1]=='\0')options.bytes_per_sync=512*1024;
}
}
