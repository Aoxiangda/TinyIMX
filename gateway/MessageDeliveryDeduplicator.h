#pragma once

#include "gateway/DeliveryIdentity.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace tinyimx {

enum class MessageDeliveryDedupBeginStatus {
    kAcquired = 0,
    kAlreadyProcessing,
    kAlreadyDelivered,
    kInvalidArgument
};

// Process-local execution ownership only. Durable correctness remains in MySQL.
class MessageDeliveryDeduplicator {
public:
    explicit MessageDeliveryDeduplicator(std::size_t max_recent_delivered = 10000);
    MessageDeliveryDeduplicator(const MessageDeliveryDeduplicator&) = delete;
    MessageDeliveryDeduplicator& operator=(const MessageDeliveryDeduplicator&) = delete;

    MessageDeliveryDedupBeginStatus Begin(const DeliveryIdentity& identity);
    bool MarkDelivered(const DeliveryIdentity& identity);
    bool Abort(const DeliveryIdentity& identity);

    // M12 compatibility wrappers. Private message IDs are one-recipient identities.
    MessageDeliveryDedupBeginStatus Begin(std::uint64_t message_id);
    bool MarkDelivered(std::uint64_t message_id);
    bool Abort(std::uint64_t message_id);

    std::size_t Size() const;
    std::size_t DeliveredCount() const;

private:
    enum class State { kProcessing = 0, kDelivered };
    void TrimDeliveredLocked();

private:
    const std::size_t max_recent_delivered_;
    mutable std::mutex mutex_;
    std::unordered_map<DeliveryIdentity, State, DeliveryIdentityHash> states_;
    std::deque<DeliveryIdentity> delivered_order_;
};

}  // namespace tinyimx
