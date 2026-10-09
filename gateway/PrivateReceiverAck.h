#pragma once
#include "gateway/ReceiverDeliveryTracker.h"
#include <utility>

namespace tinyimx {
struct PrivateReceiverAckResult {
    ReceiverDeliveryAckStatus status{ReceiverDeliveryAckStatus::kInvalidArgument};
    bool durable_confirmed{false};
};

// Only a registered private M/recipient/D can acknowledge an attempt. The
// confirmation callback must call MessageService ConfirmReceiver, which retains
// the durable row's receiver and delivery-state validation. No pre-read RPC is
// needed here. Do not undo local ACK evidence on an uncertain durable response:
// a duplicate valid ACK must remain an opportunity to repair confirmation.
template<class Confirm>
PrivateReceiverAckResult ProcessPrivateReceiverAck(
    ReceiverDeliveryTracker& tracker, std::uint64_t message_id,
    std::uint64_t authenticated_receiver, std::uint32_t sequence,
    Confirm&& confirm) {
    PrivateReceiverAckResult result;
    result.status=tracker.Acknowledge(message_id,authenticated_receiver,sequence);
    if(result.status==ReceiverDeliveryAckStatus::kConfirmed ||
       result.status==ReceiverDeliveryAckStatus::kDuplicate)
        result.durable_confirmed=std::forward<Confirm>(confirm)();
    return result;
}
} // namespace tinyimx
