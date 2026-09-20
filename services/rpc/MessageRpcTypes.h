#pragma once

#include "services/rpc/RpcCallOptions.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tinyimx::rpc {

// MessageService RPC/application boundary content-type contract.
//
// Keep these numeric values aligned with the durable private-message schema
// (text=1, image=2, file=3). Gateway must depend on this RPC-facing contract,
// not services/repository/MessageRepository.h; otherwise M14-C3 would
// reintroduce Repository-layer ownership into the Gateway compile boundary.
enum class PrivateMessageContentType : std::uint32_t {
    kText = 1,
    kImage = 2,
    kFile = 3,
};

enum class MessageDeliveryState : std::uint32_t {
    kPending = 0,
    kReceiverConfirmed = 1,
    kRead = 2,
    kFailed = 3,
};

enum class PersistPrivateMessageRpcOutcome {
    kCreated = 0,
    kReused,
    kIdempotencyConflict,
};

struct MessageRpcRecord {
    std::uint64_t message_id{0};
    std::string client_message_id;
    std::uint64_t from_user_id{0};
    std::uint64_t to_user_id{0};
    std::uint32_t message_type{0};
    std::string content;
    MessageDeliveryState delivery_state{MessageDeliveryState::kPending};
    std::string created_at;
    std::string receiver_confirmed_at;
    std::string read_at;
};

struct PersistPrivateMessageRpcRequest {
    std::uint64_t from_user_id{0};
    std::uint64_t to_user_id{0};
    std::string client_message_id;
    std::uint32_t message_type{0};
    std::string content;
};

struct PersistPrivateMessageRpcResponse {
    PersistPrivateMessageRpcOutcome outcome{
        PersistPrivateMessageRpcOutcome::kCreated
    };
    std::uint64_t message_id{0};
    MessageRpcRecord record;
    std::string message;

    [[nodiscard]] bool Accepted() const noexcept {
        return outcome == PersistPrivateMessageRpcOutcome::kCreated ||
               outcome == PersistPrivateMessageRpcOutcome::kReused;
    }
    [[nodiscard]] bool Created() const noexcept {
        return outcome == PersistPrivateMessageRpcOutcome::kCreated;
    }
    [[nodiscard]] bool Reused() const noexcept {
        return outcome == PersistPrivateMessageRpcOutcome::kReused;
    }
    [[nodiscard]] bool Conflict() const noexcept {
        return outcome == PersistPrivateMessageRpcOutcome::kIdempotencyConflict;
    }
};

struct PersistPrivateMessageRpcCallResult {
    RpcStatus status;
    std::optional<PersistPrivateMessageRpcResponse> value;
    bool attempted{false};

    [[nodiscard]] bool ok() const noexcept {
        return status.ok() && value.has_value();
    }

    static PersistPrivateMessageRpcCallResult Success(
        PersistPrivateMessageRpcResponse response
    ) {
        PersistPrivateMessageRpcCallResult output;
        output.status = RpcStatus::Ok();
        output.value = std::move(response);
        output.attempted = true;
        return output;
    }

    static PersistPrivateMessageRpcCallResult Failure(
        RpcErrorCode code,
        std::string message,
        bool call_attempted
    ) {
        PersistPrivateMessageRpcCallResult output;
        output.status.code = code;
        output.status.message = std::move(message);
        output.attempted = call_attempted;
        return output;
    }
};



enum class PersistGroupMessageRpcOutcome {
    kCreated = 0,
    kReused,
    kIdempotencyConflict,
};

struct GroupMessageRpcRecord {
    std::uint64_t message_id{0};
    std::string client_message_id;
    std::uint64_t group_id{0};
    std::uint64_t from_user_id{0};
    std::uint32_t message_type{0};
    std::string content;
    std::uint64_t membership_epoch{0};
    std::uint64_t member_version{0};
    std::uint32_t authorized_role{0};
    std::string created_at;
};

struct PersistGroupMessageRpcRequest {
    std::uint64_t from_user_id{0};
    std::uint64_t group_id{0};
    std::string client_message_id;
    std::uint32_t message_type{0};
    std::string content;
};

struct PersistGroupMessageRpcResponse {
    PersistGroupMessageRpcOutcome outcome{PersistGroupMessageRpcOutcome::kCreated};
    std::uint64_t message_id{0};
    GroupMessageRpcRecord record;
    std::string message;
    [[nodiscard]] bool Accepted() const noexcept {
        return outcome == PersistGroupMessageRpcOutcome::kCreated ||
               outcome == PersistGroupMessageRpcOutcome::kReused;
    }
    [[nodiscard]] bool Created() const noexcept {
        return outcome == PersistGroupMessageRpcOutcome::kCreated;
    }
    [[nodiscard]] bool Reused() const noexcept {
        return outcome == PersistGroupMessageRpcOutcome::kReused;
    }
    [[nodiscard]] bool Conflict() const noexcept {
        return outcome == PersistGroupMessageRpcOutcome::kIdempotencyConflict;
    }
};

struct PersistGroupMessageRpcCallResult {
    RpcStatus status;
    std::optional<PersistGroupMessageRpcResponse> value;
    bool attempted{false};
    [[nodiscard]] bool ok() const noexcept { return status.ok() && value.has_value(); }
    static PersistGroupMessageRpcCallResult Success(PersistGroupMessageRpcResponse response) {
        PersistGroupMessageRpcCallResult output;
        output.status = RpcStatus::Ok();
        output.value = std::move(response);
        output.attempted = true;
        return output;
    }
    static PersistGroupMessageRpcCallResult Failure(
        RpcErrorCode code, std::string message, bool call_attempted
    ) {
        PersistGroupMessageRpcCallResult output;
        output.status.code = code;
        output.status.message = std::move(message);
        output.attempted = call_attempted;
        return output;
    }
};

enum class GroupDeliveryRpcState : std::uint32_t {
    kPending = 1,
    kDeferredOffline = 2,
    kDelivered = 3,
};

enum class GroupDeliveryAttemptRpcOutcome : std::uint32_t {
    kSubmitted = 1,
    kOffline = 2,
    kRetryableFailure = 3,
};

struct GroupDeliveryRpcRecord {
    std::uint64_t message_id{0};
    std::uint64_t group_id{0};
    std::uint64_t recipient_user_id{0};
    GroupDeliveryRpcState delivery_state{GroupDeliveryRpcState::kPending};
    std::uint32_t attempt_count{0};
    std::string last_gateway_id;
    std::string lease_owner;
    std::string lease_token;
    std::string lease_until;
    std::string next_retry_at;
    std::string last_error_code;
    std::string created_at;
    std::string updated_at;
    std::string delivered_at;
};

struct GroupDeliveryWorkRpcRecord {
    GroupMessageRpcRecord message;
    GroupDeliveryRpcRecord delivery;
};

struct GetGroupMessageDeliveryRpcRequest {
    std::uint64_t message_id{0};
    std::uint64_t recipient_user_id{0};
};
struct GetGroupMessageDeliveryRpcResponse { GroupDeliveryWorkRpcRecord work; };

struct ClaimGroupMessageDeliveriesRpcRequest {
    std::string lease_owner;
    std::string lease_token;
    std::uint32_t limit{0};
    std::uint32_t lease_ms{0};
    std::uint64_t message_id{0};
};
struct ClaimGroupMessageDeliveriesRpcResponse {
    std::vector<GroupDeliveryWorkRpcRecord> work_items;
};

struct ClaimGroupMessageDeliveriesForRecipientRpcRequest {
    std::uint64_t recipient_user_id{0};
    std::string lease_owner;
    std::string lease_token;
    std::uint32_t limit{0};
    std::uint32_t lease_ms{0};
};

struct CompleteGroupMessageDeliveryAttemptRpcRequest {
    std::uint64_t message_id{0};
    std::uint64_t recipient_user_id{0};
    std::string lease_token;
    GroupDeliveryAttemptRpcOutcome outcome{GroupDeliveryAttemptRpcOutcome::kRetryableFailure};
    std::string gateway_id;
    std::uint32_t retry_after_ms{0};
    std::string error_code;
};

struct ConfirmGroupMessageDeliveryRpcRequest {
    std::uint64_t message_id{0};
    std::uint64_t recipient_user_id{0};
};

struct GetPrivateMessageRpcRequest {
    std::uint64_t message_id{0};
};

struct GetPrivateMessageRpcResponse {
    MessageRpcRecord record;
};

struct ConversationRpcRecord {
    std::uint64_t peer_user_id{0};
    std::uint64_t last_message_id{0};
    std::string last_client_message_id;
    std::uint64_t last_from_user_id{0};
    std::uint64_t last_to_user_id{0};
    std::uint32_t last_message_type{0};
    std::string last_content;
    MessageDeliveryState last_delivery_state{MessageDeliveryState::kPending};
    std::string last_created_at;
    std::string last_receiver_confirmed_at;
    std::string last_read_at;
};

struct ListHistoryRpcRequest {
    std::uint64_t actor_user_id{0};
    std::uint64_t peer_user_id{0};
    std::uint64_t before_message_id{0};
    std::uint32_t limit{0};
};

struct ListHistoryRpcResponse {
    std::vector<MessageRpcRecord> messages;
    bool has_more{false};
};

struct ListConversationsRpcRequest {
    std::uint64_t actor_user_id{0};
    std::uint32_t limit{0};
};

struct ListConversationsRpcResponse {
    std::vector<ConversationRpcRecord> conversations;
    bool has_more{false};
};

struct CountPendingRpcRequest {
    std::uint64_t to_user_id{0};
};

struct CountPendingRpcResponse {
    std::uint64_t count{0};
};

struct ListPendingAfterRpcRequest {
    std::uint64_t to_user_id{0};
    std::uint64_t after_message_id{0};
    std::uint32_t limit{0};
};

struct ListPendingAfterRpcResponse {
    std::vector<MessageRpcRecord> messages;
    bool has_more{false};
};

struct ConfirmReceiverRpcRequest {
    std::uint64_t message_id{0};
    std::uint64_t receiver_user_id{0};
};

struct ConfirmReceiverBatchRpcRequest {
    std::uint64_t receiver_user_id{0};
    std::vector<std::uint64_t> message_ids;
};

struct MarkDialogReadRpcRequest {
    std::uint64_t reader_user_id{0};
    std::uint64_t peer_user_id{0};
};

struct MessageMutationRpcResponse {
    std::uint64_t affected_rows{0};
};

// Generic mutation result for C3. attempted=false means no RPC crossed the
// network boundary (validation/budget/endpoint/stub preflight). attempted=true
// with non-OK transport means the caller must treat durable state as uncertain.
struct MessageMutationRpcCallResult {
    RpcStatus status;
    std::optional<MessageMutationRpcResponse> value;
    bool attempted{false};

    [[nodiscard]] bool ok() const noexcept {
        return status.ok() && value.has_value();
    }

    static MessageMutationRpcCallResult Success(
        MessageMutationRpcResponse response
    ) {
        MessageMutationRpcCallResult output;
        output.status = RpcStatus::Ok();
        output.value = std::move(response);
        output.attempted = true;
        return output;
    }

    static MessageMutationRpcCallResult Failure(
        RpcErrorCode code,
        std::string message,
        bool call_attempted
    ) {
        MessageMutationRpcCallResult output;
        output.status.code = code;
        output.status.message = std::move(message);
        output.attempted = call_attempted;
        return output;
    }
};

}  // namespace tinyimx::rpc
