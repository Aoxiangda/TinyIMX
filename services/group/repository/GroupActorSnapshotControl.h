#pragma once

#include <cstdlib>
#include <cstring>

namespace tinyimx::group {
// Rollout control is process-local and strict: absent/0/other values retain
// the original reads. No mutable environment reads in the request hot path.
inline bool GroupActorSnapshotEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_GROUP_ACTOR_SNAPSHOT_ENABLE");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
} // namespace tinyimx::group
