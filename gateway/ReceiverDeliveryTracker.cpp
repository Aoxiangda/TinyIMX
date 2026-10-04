#include "gateway/ReceiverDeliveryTracker.h"

#include <algorithm>
#include <utility>

namespace tinyimx {

ReceiverDeliveryTracker::ReceiverDeliveryTracker(std::size_t max_recent_confirmed)
    : ReceiverDeliveryTracker(max_recent_confirmed, WindowOptions{}) {}

ReceiverDeliveryTracker::ReceiverDeliveryTracker(std::size_t max_recent_confirmed, WindowOptions options)
    : max_recent_confirmed_(std::max<std::size_t>(max_recent_confirmed, 1)), window_options_(options) {
    window_options_.max_waiting_entries = std::max<std::size_t>(1, options.max_waiting_entries);
    window_options_.max_waiting_per_recipient = std::max<std::size_t>(1, options.max_waiting_per_recipient);
    window_options_.max_waiting_bytes = std::max<std::size_t>(1, options.max_waiting_bytes);
    window_options_.max_bytes_per_recipient = std::max<std::size_t>(1, options.max_bytes_per_recipient);
    window_options_.max_attempt_sequences = std::max<std::size_t>(1, options.max_attempt_sequences);
    window_options_.recovery_resend_cooldown = std::max(options.recovery_resend_cooldown, std::chrono::milliseconds{1});
}

bool ReceiverDeliveryTracker::ByteRoomLocked(std::uint64_t recipient, std::size_t extra) const {
    const auto it = waiting_by_recipient_.find(recipient);
    const auto bytes = it == waiting_by_recipient_.end() ? 0 : it->second.bytes;
    return waiting_bytes_ <= window_options_.max_waiting_bytes &&
           extra <= window_options_.max_waiting_bytes - waiting_bytes_ &&
           bytes <= window_options_.max_bytes_per_recipient &&
           extra <= window_options_.max_bytes_per_recipient - bytes;
}

ReceiverDeliveryRegisterStatus ReceiverDeliveryTracker::RegisterAttempt(
    const DeliveryIdentity& identity,
    std::uint32_t delivery_seq,
    std::size_t wire_bytes
) {
    if (!identity.Valid() || delivery_seq == 0) return ReceiverDeliveryRegisterStatus::kInvalidArgument;
    std::lock_guard<std::mutex> lock(mutex_);
    return RegisterAttemptLocked(identity, delivery_seq, wire_bytes, Clock::now());
}

ReceiverDeliveryRegisterStatus ReceiverDeliveryTracker::RegisterAttemptLocked(
    const DeliveryIdentity& identity, std::uint32_t delivery_seq,
    std::size_t wire_bytes, Clock::time_point now
) {
    auto it = entries_.find(identity);
    if (it == entries_.end()) {
        if (identity.domain == DeliveryDomain::kPrivateMessage) {
            if (private_receivers_.find(identity.message_id) != private_receivers_.end())
                return ReceiverDeliveryRegisterStatus::kReceiverMismatch;
        }
        const auto window = waiting_by_recipient_.find(identity.recipient_user_id);
        const auto recipient_entries = window == waiting_by_recipient_.end() ? 0 : window->second.entries;
        if (waiting_entries_ >= window_options_.max_waiting_entries ||
            recipient_entries >= window_options_.max_waiting_per_recipient ||
            !ByteRoomLocked(identity.recipient_user_id, wire_bytes))
            return ReceiverDeliveryRegisterStatus::kWindowFull;
        Entry entry;
        entry.current_delivery_seq = delivery_seq;
        entry.attempt_count = 1;
        entry.attempt_seqs.insert(delivery_seq);
        entry.wire_bytes = wire_bytes;
        entry.last_attempt_at = now;
        const auto [recipient, new_recipient] = waiting_by_recipient_.try_emplace(identity.recipient_user_id);
        try {
            entries_.emplace(identity, std::move(entry));
            if (identity.domain == DeliveryDomain::kPrivateMessage)
                private_receivers_.emplace(identity.message_id, identity.recipient_user_id);
        } catch (...) {
            entries_.erase(identity);
            if (new_recipient) waiting_by_recipient_.erase(recipient);
            throw;
        }
        ++recipient->second.entries;
        recipient->second.bytes += wire_bytes;
        ++waiting_entries_;
        waiting_bytes_ += wire_bytes;
        ++attempt_sequences_;
        return ReceiverDeliveryRegisterStatus::kRegistered;
    }
    Entry& entry = it->second;
    if (entry.state == State::kConfirmed) return ReceiverDeliveryRegisterStatus::kAlreadyConfirmed;
    if (entry.attempt_seqs.find(delivery_seq) != entry.attempt_seqs.end())
        return ReceiverDeliveryRegisterStatus::kDuplicateAttempt;
    const auto extra = wire_bytes > entry.wire_bytes ? wire_bytes - entry.wire_bytes : 0;
    if (entry.attempt_seqs.size() >= window_options_.max_attempt_sequences ||
        !ByteRoomLocked(identity.recipient_user_id, extra)) return ReceiverDeliveryRegisterStatus::kWindowFull;
    const auto [_, inserted] = entry.attempt_seqs.insert(delivery_seq);
    if (!inserted) return ReceiverDeliveryRegisterStatus::kDuplicateAttempt;
    entry.current_delivery_seq = delivery_seq;
    ++entry.attempt_count;
    ++attempt_sequences_;
    entry.last_attempt_at = now;
    entry.wire_bytes += extra;
    waiting_bytes_ += extra;
    waiting_by_recipient_.at(identity.recipient_user_id).bytes += extra;
    return ReceiverDeliveryRegisterStatus::kRetryRegistered;
}

ReceiverDeliveryTracker::ReplayRegistration ReceiverDeliveryTracker::RegisterReplayAttempt(
    const DeliveryIdentity& identity, std::uint32_t proposed_seq, std::size_t wire_bytes, Clock::time_point now
) {
    ReplayRegistration out;
    if (!identity.Valid() || proposed_seq == 0) return out;
    std::lock_guard lock(mutex_);
    const auto it = entries_.find(identity);
    if (it != entries_.end() && it->second.state != State::kConfirmed) {
        auto& entry = it->second;
        if (now < entry.last_attempt_at + window_options_.recovery_resend_cooldown) {
            out.status = ReceiverDeliveryRegisterStatus::kWaitingAck;
            return out;
        }
        if (entry.attempt_seqs.size() >= window_options_.max_attempt_sequences) {
            const auto extra = wire_bytes > entry.wire_bytes ? wire_bytes - entry.wire_bytes : 0;
            if (!ByteRoomLocked(identity.recipient_user_id, extra)) {
                out.status = ReceiverDeliveryRegisterStatus::kWindowFull;
                return out;
            }
            entry.wire_bytes += extra;
            waiting_bytes_ += extra;
            waiting_by_recipient_.at(identity.recipient_user_id).bytes += extra;
            entry.last_attempt_at = now;
            out.status = ReceiverDeliveryRegisterStatus::kRetryRegistered;
            out.delivery_seq = entry.current_delivery_seq;
            out.reused_attempt = true;
            return out;
        }
    }
    out.status = RegisterAttemptLocked(identity, proposed_seq, wire_bytes, now);
    if (out.status == ReceiverDeliveryRegisterStatus::kRegistered ||
        out.status == ReceiverDeliveryRegisterStatus::kRetryRegistered) out.delivery_seq = proposed_seq;
    return out;
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
    if (entry.attempt_count >= max_attempts || entry.attempt_seqs.size() >= window_options_.max_attempt_sequences)
        return ReceiverDeliveryRetryRegisterStatus::kAttemptLimitReached;
    const auto [_, inserted] = entry.attempt_seqs.insert(new_delivery_seq);
    if (!inserted) return ReceiverDeliveryRetryRegisterStatus::kDuplicateAttempt;
    entry.current_delivery_seq = new_delivery_seq;
    ++entry.attempt_count;
    ++attempt_sequences_;
    entry.last_attempt_at = Clock::now();
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
    confirmed_order_.push_back(identity);
    entry.state = State::kConfirmed;
    --waiting_entries_;
    waiting_bytes_ -= entry.wire_bytes;
    const auto recipient = waiting_by_recipient_.find(identity.recipient_user_id);
    --recipient->second.entries;
    recipient->second.bytes -= entry.wire_bytes;
    entry.wire_bytes = 0;
    if (recipient->second.entries == 0) waiting_by_recipient_.erase(recipient);
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

ReceiverDeliveryTracker::WindowStats ReceiverDeliveryTracker::GetWindowStats() const {
    std::lock_guard lock(mutex_);
    return {waiting_entries_, waiting_bytes_, waiting_by_recipient_.size(), attempt_sequences_};
}

void ReceiverDeliveryTracker::TrimConfirmedLocked() {
    while (confirmed_order_.size() > max_recent_confirmed_) {
        const DeliveryIdentity oldest = confirmed_order_.front();
        confirmed_order_.pop_front();
        const auto it = entries_.find(oldest);
        if (it != entries_.end() && it->second.state == State::kConfirmed) {
            attempt_sequences_ -= it->second.attempt_seqs.size();
            if (oldest.domain == DeliveryDomain::kPrivateMessage)
                private_receivers_.erase(oldest.message_id);
            entries_.erase(it);
        }
    }
}

}  // namespace tinyimx
