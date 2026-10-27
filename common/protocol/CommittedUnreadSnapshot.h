#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace tinyimx {
// A point-in-time result of the same durable private-message transaction.
// This DTO carries no repository or transport dependency.
struct CommittedUnreadSnapshot {
    std::uint64_t message_id{0};
    std::uint64_t receiver_user_id{0};
    std::uint64_t peer_user_id{0};
    std::uint64_t private_unread{0};
    std::uint64_t total_unread{0};

    [[nodiscard]] bool ValidFor(std::uint64_t mid, std::uint64_t receiver,
                                std::uint64_t peer) const noexcept {
        return mid != 0 && receiver != 0 && peer != 0 && receiver != peer &&
               message_id == mid && receiver_user_id == receiver &&
               peer_user_id == peer && private_unread != 0 && private_unread <= total_unread &&
               total_unread <= static_cast<std::uint64_t>(
                   std::numeric_limits<std::int64_t>::max());
    }
};

// Only a newly created native result may bypass the Redis read. A reused CID,
// SQL/old-peer reply, or a writer-enabled Gateway retains its repair gate.
inline bool CanUseCommittedUnreadSnapshot(bool projection_writer, bool created,
    const std::optional<CommittedUnreadSnapshot>& value, std::uint64_t mid,
    std::uint64_t receiver, std::uint64_t peer) noexcept {
    return !projection_writer && created && value && value->ValidFor(mid,receiver,peer);
}
} // namespace tinyimx
