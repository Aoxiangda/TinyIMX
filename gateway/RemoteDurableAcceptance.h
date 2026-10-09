#pragma once

#include "common/protocol/ClientChatProtocol.h"
#include "services/rpc/MessageRpcTypes.h"

#include <optional>
#include <utility>

namespace tinyimx {

// A Sender ACK accepts a durable logical message. It does not certify a new
// Receiver delivery attempt. The RPC adapter already validates the response;
// retain these checks here so the acceptance boundary is independently testable.
inline std::optional<ClientChatAck> BuildRemoteDurableAcceptance(
    const rpc::PersistPrivateMessageRpcCallResult& call,
    const rpc::PersistPrivateMessageRpcRequest& expected,
    std::int64_t private_unread,
    std::int64_t total_unread,
    const std::string& gateway_id,
    const std::string& host,
    std::uint16_t port) {
    if (!call.ok() || !call.value->Accepted()) return std::nullopt;
    const auto& result = *call.value;
    const auto& record = result.record;
    if (result.message_id == 0 || record.message_id != result.message_id ||
        expected.from_user_id == 0 || expected.to_user_id == 0 ||
        expected.from_user_id == expected.to_user_id ||
        expected.client_message_id.empty() ||
        expected.client_message_id.size() > kMaxClientMessageIdSize ||
        expected.content.empty() || expected.message_type < 1 || expected.message_type > 3 ||
        record.from_user_id != expected.from_user_id ||
        record.to_user_id != expected.to_user_id ||
        record.client_message_id != expected.client_message_id ||
        record.message_type != expected.message_type ||
        record.content != expected.content ||
        (result.Created() && record.delivery_state != rpc::MessageDeliveryState::kPending)) {
        return std::nullopt;
    }
    switch (record.delivery_state) {
        case rpc::MessageDeliveryState::kPending:
        case rpc::MessageDeliveryState::kReceiverConfirmed:
        case rpc::MessageDeliveryState::kRead:
        case rpc::MessageDeliveryState::kFailed: break;
        default: return std::nullopt;
    }
    ClientChatAck ack;
    ack.success = true;
    ack.stored_persistent = true;
    // false means no offline outcome has been observed at this boundary.
    // A later transport failure is a delivery outcome, not a second Sender ACK.
    ack.stored_offline = false;
    ack.delivered = record.delivery_state == rpc::MessageDeliveryState::kReceiverConfirmed ||
                    record.delivery_state == rpc::MessageDeliveryState::kRead;
    ack.reused = result.Reused();
    ack.client_message_id = expected.client_message_id;
    ack.message_id = result.message_id;
    ack.from_user_id = expected.from_user_id;
    ack.to_user_id = expected.to_user_id;
    ack.receiver_private_unread = private_unread;
    ack.receiver_total_unread = total_unread;
    ack.remote_gateway_id = gateway_id;
    ack.remote_host = host;
    ack.remote_port = port;
    // Retain existing reason vocabulary; delivered is based only on RPC durable
    // state, never on the peer Send result or the MessageDeliveryDeduplicator.
    ack.reason = ack.delivered ? "remote_receiver_confirmed"
                               : "remote_delivery_awaiting_receiver_ack";
    return ack;
}

struct RemoteAcceptanceDispatchResult {
    bool valid{false};
    bool ack_handoff_ok{false};
    bool forward_submitted{false};
};

// emit and forward are immediate handoffs. They must not wait for peer ACK.
// A failed Sender handoff must not cancel the already durable delivery attempt.
// No callback owns a second response. Different requests with the same logical
// ID still receive separate responses on their respective Packet.seq values.
template<class Emit, class Forward>
RemoteAcceptanceDispatchResult DispatchRemoteDurableAcceptance(
    const std::optional<ClientChatAck>& acceptance, Emit&& emit, Forward&& forward) {
    if (!acceptance || !acceptance->success || !acceptance->stored_persistent ||
        acceptance->message_id == 0) return {};
    RemoteAcceptanceDispatchResult r;
    r.valid = true;
    r.ack_handoff_ok = std::forward<Emit>(emit)(*acceptance);
    r.forward_submitted = std::forward<Forward>(forward)();
    return r;
}

} // namespace tinyimx
