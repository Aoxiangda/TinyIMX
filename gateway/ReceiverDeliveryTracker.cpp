#include "gateway/ReceiverDeliveryTracker.h"

#include <algorithm>
#include <utility>

namespace tinyimx {

ReceiverDeliveryTracker::ReceiverDeliveryTracker(std::size_t max_recent_confirmed)
    : max_recent_confirmed_(std::max<std::size_t>(max_recent_confirmed, 1)) {}

ReceiverDeliveryRegisterStatus ReceiverDeliveryTracker::RegisterAttempt(
    const DeliveryIdentity& identity,
    std::uint32_t delivery_seq
) {
    if (!identity.Valid() || delivery_seq == 0) return ReceiverDeliveryRegisterStatus::kInvalidArgument;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(identity);
    if (it == entries_.end()) {
        if (identity.domain == DeliveryDomain::kPrivateMessage) {
            if (private_receivers_.find(identity.message_id) != private_receivers_.end())
                return ReceiverDeliveryRegisterStatus::kReceiverMismatch;
        }
        Entry entry;
        entry.current_delivery_seq = delivery_seq;
        entry.attempt_count = 1;
        entry.attempt_seqs.insert(delivery_seq);
        entries_.emplace(identity, std::move(entry));
        if (identity.domain == DeliveryDomain::kPrivateMessage) {
            try {
                private_receivers_.emplace(identity.message_id, identity.recipient_user_id);
            } catch (...) {
                entries_.erase(identity);
                throw;
            }
        }
        return ReceiverDeliveryRegisterStatus::kRegistered;
    }
    Entry& entry = it->second;
    if (entry.state == State::kConfirmed) return ReceiverDeliveryRegisterStatus::kAlreadyConfirmed;
    const auto [_, inserted] = entry.attempt_seqs.insert(delivery_seq);
    if (!inserted) return ReceiverDeliveryRegisterStatus::kDuplicateAttempt;
    entry.current_delivery_seq = delivery_seq;
    ++entry.attempt_count;
    return ReceiverDeliveryRegisterStatus::kRetryRegistered;
}

ReceiverDeliveryRetryRegisterStatus ReceiverDeliveryTracker::RegisterRetryAttempt(
    const DeliveryIdentity& identity,
    std::uint32_t expected_timed_out_delivery_seq,
    std::uint32_t new_delivery_seq,
    std::uint32_t max_attempts
) {
    if (!identity.Valid() || expected_timed_out_delivery_seq == 0 || new_delivery_seq == 0 || max_attempts == 0)
        return ReceiverDeliveryRetryRegisterStatus::kInvalidArgument;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(identity);
    if (it == entries_.end()) {
        if (identity.domain == DeliveryDomain::kPrivateMessage) {
            if (private_receivers_.find(identity.message_id) != private_receivers_.end())
                return ReceiverDeliveryRetryRegisterStatus::kReceiverMismatch;
        }
        return ReceiverDeliveryRetryRegisterStatus::kUnknownMessage;
    }
    Entry& entry = it->second;
    if (entry.state == State::kConfirmed) return ReceiverDeliveryRetryRegisterStatus::kAlreadyConfirmed;
    if (entry.current_delivery_seq != expected_timed_out_delivery_seq)
        return ReceiverDeliveryRetryRegisterStatus::kStaleAttempt;
    if (entry.attempt_count >= max_attempts)
        return ReceiverDeliveryRetryRegisterStatus::kAttemptLimitReached;
    const auto [_, inserted] = entry.attempt_seqs.insert(new_delivery_seq);
    if (!inserted) return ReceiverDeliveryRetryRegisterStatus::kDuplicateAttempt;
    entry.current_delivery_seq = new_delivery_seq;
    ++entry.attempt_count;
    return ReceiverDeliveryRetryRegisterStatus::kRetryRegistered;
}

ReceiverDeliveryAckStatus ReceiverDeliveryTracker::Acknowledge(
    const DeliveryIdentity& identity,
    std::uint32_t delivery_seq
) {
    if (!identity.Valid() || delivery_seq == 0) return ReceiverDeliveryAckStatus::kInvalidArgument;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(identity);
    if (it == entries_.end()) {
        if (identity.domain == DeliveryDomain::kPrivateMessage) {
            if (private_receivers_.find(identity.message_id) != private_receivers_.end())
                return ReceiverDeliveryAckStatus::kReceiverMismatch;
        }
        return ReceiverDeliveryAckStatus::kUnknownMessage;
    }
    Entry& entry = it->second;
    if (entry.attempt_seqs.find(delivery_seq) == entry.attempt_seqs.end())
        return ReceiverDeliveryAckStatus::kUnknownAttempt;
    if (entry.state == State::kConfirmed) return ReceiverDeliveryAckStatus::kDuplicate;
    entry.state = State::kConfirmed;
    confirmed_order_.push_back(identity);
    TrimConfirmedLocked();
    return ReceiverDeliveryAckStatus::kConfirmed;
}

bool ReceiverDeliveryTracker::GetSnapshot(
    const DeliveryIdentity& identity,
    ReceiverDeliverySnapshot* snapshot
) const {
    if (!identity.Valid() || snapshot == nullptr) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(identity);
    if (it == entries_.end()) return false;
    snapshot->domain = identity.domain;
    snapshot->message_id = identity.message_id;
    snapshot->receiver_user_id = identity.recipient_user_id;
    snapshot->current_delivery_seq = it->second.current_delivery_seq;
    snapshot->attempt_count = it->second.attempt_count;
    snapshot->confirmed = it->second.state == State::kConfirmed;
    return true;
}

ReceiverDeliveryRegisterStatus ReceiverDeliveryTracker::RegisterAttempt(
    std::uint64_t message_id,
    std::uint64_t receiver_user_id,
    std::uint32_t delivery_seq
) {
    return RegisterAttempt(PrivateDeliveryIdentity(message_id, receiver_user_id), delivery_seq);
}

ReceiverDeliveryRetryRegisterStatus ReceiverDeliveryTracker::RegisterRetryAttempt(
    std::uint64_t message_id,
    std::uint64_t receiver_user_id,
    std::uint32_t expected_timed_out_delivery_seq,
    std::uint32_t new_delivery_seq,
    std::uint32_t max_attempts
) {
    return RegisterRetryAttempt(
        PrivateDeliveryIdentity(message_id, receiver_user_id),
        expected_timed_out_delivery_seq,
        new_delivery_seq,
        max_attempts
    );
}

ReceiverDeliveryAckStatus ReceiverDeliveryTracker::Acknowledge(
    std::uint64_t message_id,
    std::uint64_t receiver_user_id,
    std::uint32_t delivery_seq
) {
    return Acknowledge(PrivateDeliveryIdentity(message_id, receiver_user_id), delivery_seq);
}

bool ReceiverDeliveryTracker::GetSnapshot(
    std::uint64_t message_id,
    ReceiverDeliverySnapshot* snapshot
) const {
    if (message_id == 0 || snapshot == nullptr) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto recipient = private_receivers_.find(message_id);
    if (recipient == private_receivers_.end()) return false;
    const auto identity = PrivateDeliveryIdentity(message_id, recipient->second);
    const auto it = entries_.find(identity);
    if (it == entries_.end()) return false;
    snapshot->domain = identity.domain;
    snapshot->message_id = identity.message_id;
    snapshot->receiver_user_id = identity.recipient_user_id;
    snapshot->current_delivery_seq = it->second.current_delivery_seq;
    snapshot->attempt_count = it->second.attempt_count;
    snapshot->confirmed = it->second.state == State::kConfirmed;
    return true;
}

std::size_t ReceiverDeliveryTracker::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

std::size_t ReceiverDeliveryTracker::ConfirmedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return confirmed_order_.size();
}

void ReceiverDeliveryTracker::TrimConfirmedLocked() {
    while (confirmed_order_.size() > max_recent_confirmed_) {
        const DeliveryIdentity oldest = confirmed_order_.front();
        confirmed_order_.pop_front();
        const auto it = entries_.find(oldest);
        if (it != entries_.end() && it->second.state == State::kConfirmed) {
            if (oldest.domain == DeliveryDomain::kPrivateMessage)
                private_receivers_.erase(oldest.message_id);
            entries_.erase(it);
        }
    }
}

}  // namespace tinyimx
