#pragma once

#include "gateway/DeliveryIdentity.h"

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace tinyimx {

enum class ReceiverDeliveryRegisterStatus {
    kRegistered = 0,
    kRetryRegistered,
    kDuplicateAttempt,
    kAlreadyConfirmed,
    kReceiverMismatch,
    kInvalidArgument,
    kWindowFull,
    kWaitingAck
};

enum class ReceiverDeliveryAckStatus {
    kConfirmed = 0,
    kDuplicate,
    kUnknownMessage,
    kReceiverMismatch,
    kUnknownAttempt,
    kInvalidArgument
};

enum class ReceiverDeliveryRetryRegisterStatus {
    kRetryRegistered = 0,
    kAlreadyConfirmed,
    kStaleAttempt,
    kAttemptLimitReached,
    kReceiverMismatch,
    kDuplicateAttempt,
    kUnknownMessage,
    kInvalidArgument
};

struct ReceiverDeliverySnapshot {
    DeliveryDomain domain{DeliveryDomain::kPrivateMessage};
    std::uint64_t message_id{0};
    std::uint64_t receiver_user_id{0};
    std::uint32_t current_delivery_seq{0};
    std::uint32_t attempt_count{0};
    bool confirmed{false};
};

class ReceiverDeliveryTracker {
public:
    using Clock = std::chrono::steady_clock;
    struct WindowOptions {
        std::size_t max_waiting_entries{200000};
        std::size_t max_waiting_per_recipient{64};
        std::size_t max_waiting_bytes{256 * 1024 * 1024};
        std::size_t max_bytes_per_recipient{1024 * 1024};
        std::size_t max_attempt_sequences{64};
        std::chrono::milliseconds recovery_resend_cooldown{5000};
    };
    struct WindowStats {
        std::size_t waiting_entries{0}, waiting_bytes{0}, waiting_recipients{0}, attempt_sequences{0};
    };
    struct ReplayRegistration {
        ReceiverDeliveryRegisterStatus status{ReceiverDeliveryRegisterStatus::kInvalidArgument};
        std::uint32_t delivery_seq{0};
        bool reused_attempt{false};
    };
    explicit ReceiverDeliveryTracker(std::size_t max_recent_confirmed = 10000);
    ReceiverDeliveryTracker(std::size_t max_recent_confirmed, WindowOptions window_options);
    ReceiverDeliveryTracker(const ReceiverDeliveryTracker&) = delete;
    ReceiverDeliveryTracker& operator=(const ReceiverDeliveryTracker&) = delete;

    ReceiverDeliveryRegisterStatus RegisterAttempt(
        const DeliveryIdentity& identity,
        std::uint32_t delivery_seq,
        std::size_t wire_bytes = 0
    );
    // Cooldown suppresses concurrent replay amplification. At the evidence cap
    // resend the current known D rather than allocating unlimited new D ids.
    ReplayRegistration RegisterReplayAttempt(
        const DeliveryIdentity& identity, std::uint32_t proposed_seq,
        std::size_t wire_bytes, Clock::time_point now = Clock::now());
    ReceiverDeliveryRetryRegisterStatus RegisterRetryAttempt(
        const DeliveryIdentity& identity,
        std::uint32_t expected_timed_out_delivery_seq,
        std::uint32_t new_delivery_seq,
        std::uint32_t max_attempts
    );
    ReceiverDeliveryAckStatus Acknowledge(
        const DeliveryIdentity& identity,
        std::uint32_t delivery_seq
    );
    bool GetSnapshot(
        const DeliveryIdentity& identity,
        ReceiverDeliverySnapshot* snapshot
    ) const;

    // M12 private-message compatibility API.
    ReceiverDeliveryRegisterStatus RegisterAttempt(
        std::uint64_t message_id,
        std::uint64_t receiver_user_id,
        std::uint32_t delivery_seq
    );
    ReceiverDeliveryRetryRegisterStatus RegisterRetryAttempt(
        std::uint64_t message_id,
        std::uint64_t receiver_user_id,
        std::uint32_t expected_timed_out_delivery_seq,
        std::uint32_t new_delivery_seq,
        std::uint32_t max_attempts
    );
    ReceiverDeliveryAckStatus Acknowledge(
        std::uint64_t message_id,
        std::uint64_t receiver_user_id,
        std::uint32_t delivery_seq
    );
    bool GetSnapshot(
        std::uint64_t message_id,
        ReceiverDeliverySnapshot* snapshot
    ) const;

    std::size_t Size() const;
    std::size_t ConfirmedCount() const;
    WindowStats GetWindowStats() const;

private:
    enum class State { kWaitingAck = 0, kConfirmed };
    struct Entry {
        std::uint32_t current_delivery_seq{0};
        std::uint32_t attempt_count{0};
        State state{State::kWaitingAck};
        std::unordered_set<std::uint32_t> attempt_seqs;
        std::size_t wire_bytes{0};
        Clock::time_point last_attempt_at{};
    };
    struct RecipientWindow { std::size_t entries{0}, bytes{0}; };
    ReceiverDeliveryRegisterStatus RegisterAttemptLocked(
        const DeliveryIdentity&, std::uint32_t, std::size_t, Clock::time_point);
    bool ByteRoomLocked(std::uint64_t recipient, std::size_t extra_bytes) const;
    void TrimConfirmedLocked();

private:
    const std::size_t max_recent_confirmed_;
    WindowOptions window_options_;
    mutable std::mutex mutex_;
    std::unordered_map<DeliveryIdentity, Entry, DeliveryIdentityHash> entries_;
    // Private M identifies exactly one receiver. Group M has many recipients
    // and must never be entered here. All access uses the same mutex as entries_.
    std::unordered_map<std::uint64_t, std::uint64_t> private_receivers_;
    std::deque<DeliveryIdentity> confirmed_order_;
    std::unordered_map<std::uint64_t, RecipientWindow> waiting_by_recipient_;
    std::size_t waiting_entries_{0}, waiting_bytes_{0}, attempt_sequences_{0};
};

}  // namespace tinyimx
