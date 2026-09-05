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
