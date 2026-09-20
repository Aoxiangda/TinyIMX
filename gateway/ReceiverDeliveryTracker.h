#pragma once

#include "gateway/DeliveryIdentity.h"

#include <cstddef>
#include <cstdint>
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
    kInvalidArgument
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
    explicit ReceiverDeliveryTracker(std::size_t max_recent_confirmed = 10000);
    ReceiverDeliveryTracker(const ReceiverDeliveryTracker&) = delete;
    ReceiverDeliveryTracker& operator=(const ReceiverDeliveryTracker&) = delete;

    ReceiverDeliveryRegisterStatus RegisterAttempt(
        const DeliveryIdentity& identity,
        std::uint32_t delivery_seq
    );
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

private:
    enum class State { kWaitingAck = 0, kConfirmed };
    struct Entry {
        std::uint32_t current_delivery_seq{0};
        std::uint32_t attempt_count{0};
        State state{State::kWaitingAck};
        std::unordered_set<std::uint32_t> attempt_seqs;
    };
    void TrimConfirmedLocked();

private:
    const std::size_t max_recent_confirmed_;
    mutable std::mutex mutex_;
    std::unordered_map<DeliveryIdentity, Entry, DeliveryIdentityHash> entries_;
    std::deque<DeliveryIdentity> confirmed_order_;
};

}  // namespace tinyimx
