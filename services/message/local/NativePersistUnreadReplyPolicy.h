#pragma once

#include "common/protocol/CommittedUnreadSnapshot.h"
#include <cstdlib>
#include <optional>

namespace tinyimx::message::local {
inline bool NativePersistUnreadReplyEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_NATIVE_PERSIST_UNREAD_REPLY_ENABLE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

// Call only after sync succeeds. Invalid/unrepresentable counters retain the
// legacy path, rather than narrowing uint64_t into a negative desktop count.
inline std::optional<CommittedUnreadSnapshot> NativeCreatedUnreadReply(
    std::uint64_t mid, std::uint64_t receiver, std::uint64_t peer,
    std::uint64_t private_unread, std::uint64_t total_unread) noexcept {
    if (!NativePersistUnreadReplyEnabled()) return std::nullopt;
    CommittedUnreadSnapshot result{mid, receiver, peer, private_unread, total_unread};
    if (!result.ValidFor(mid, receiver, peer)) return std::nullopt;
    return result;
}
} // namespace tinyimx::message::local
