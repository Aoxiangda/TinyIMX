#pragma once
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace tinyimx {
inline bool GroupFanoutCommitWakeEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

// One process-local, coalescing hint; durable rows remain the source of truth.
// No per-message queue or IDs grow with workload. Recovery polling is retained.
struct GroupFanoutWakeState {
    std::mutex mutex;
    std::condition_variable cv;
    std::uint64_t generation{0};
};
inline GroupFanoutWakeState& LocalGroupFanoutWakeState() {
    static GroupFanoutWakeState state;
    return state;
}
inline void NotifyGroupFanoutCommitted() {
    if (!GroupFanoutCommitWakeEnabled()) return;
    auto& state = LocalGroupFanoutWakeState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        ++state.generation;
    }
    state.cv.notify_one();
}
}  // namespace tinyimx
