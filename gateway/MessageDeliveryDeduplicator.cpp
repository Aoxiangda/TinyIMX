#include "gateway/MessageDeliveryDeduplicator.h"

#include <algorithm>

namespace tinyimx {
namespace {
constexpr std::uint64_t kPrivateCompatibilityRecipient = 1;
}

MessageDeliveryDeduplicator::MessageDeliveryDeduplicator(std::size_t max_recent_delivered)
    : max_recent_delivered_(std::max<std::size_t>(max_recent_delivered, 1)) {}

MessageDeliveryDedupBeginStatus MessageDeliveryDeduplicator::Begin(
    const DeliveryIdentity& identity
) {
    if (!identity.Valid()) return MessageDeliveryDedupBeginStatus::kInvalidArgument;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = states_.find(identity);
    if (it == states_.end()) {
        states_.emplace(identity, State::kProcessing);
        return MessageDeliveryDedupBeginStatus::kAcquired;
    }
    return it->second == State::kDelivered
        ? MessageDeliveryDedupBeginStatus::kAlreadyDelivered
        : MessageDeliveryDedupBeginStatus::kAlreadyProcessing;
}

bool MessageDeliveryDeduplicator::MarkDelivered(const DeliveryIdentity& identity) {
    if (!identity.Valid()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = states_.find(identity);
    if (it == states_.end()) return false;
    if (it->second == State::kDelivered) return true;
    it->second = State::kDelivered;
    delivered_order_.push_back(identity);
    TrimDeliveredLocked();
    return true;
}

bool MessageDeliveryDeduplicator::Abort(const DeliveryIdentity& identity) {
    if (!identity.Valid()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = states_.find(identity);
    if (it == states_.end()) return true;
    if (it->second == State::kDelivered) return false;
    states_.erase(it);
    return true;
}

MessageDeliveryDedupBeginStatus MessageDeliveryDeduplicator::Begin(std::uint64_t message_id) {
    return Begin(PrivateDeliveryIdentity(message_id, kPrivateCompatibilityRecipient));
}

bool MessageDeliveryDeduplicator::MarkDelivered(std::uint64_t message_id) {
    return MarkDelivered(PrivateDeliveryIdentity(message_id, kPrivateCompatibilityRecipient));
}

bool MessageDeliveryDeduplicator::Abort(std::uint64_t message_id) {
    return Abort(PrivateDeliveryIdentity(message_id, kPrivateCompatibilityRecipient));
}

std::size_t MessageDeliveryDeduplicator::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return states_.size();
}

std::size_t MessageDeliveryDeduplicator::DeliveredCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return delivered_order_.size();
}

void MessageDeliveryDeduplicator::TrimDeliveredLocked() {
    while (delivered_order_.size() > max_recent_delivered_) {
        const DeliveryIdentity oldest = delivered_order_.front();
        delivered_order_.pop_front();
        const auto it = states_.find(oldest);
        if (it != states_.end() && it->second == State::kDelivered) states_.erase(it);
    }
}

}  // namespace tinyimx
