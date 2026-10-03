#pragma once

#include "services/rpc/RpcCallOptions.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tinyimx::rpc {

struct ListFriendsRpcRequest {
    std::uint64_t actor_user_id{0};
    std::uint32_t limit{0};
};

struct SocialFriend {
    std::uint64_t friend_user_id{0};
    std::string username;
    std::string nickname;
    std::string avatar_url;
    std::uint32_t user_status{0};
    std::uint32_t relation_status{0};
    std::string relation_created_at;
    std::string relation_updated_at;
};

struct ListFriendsRpcResponse {
    std::vector<SocialFriend> friends;
    bool has_more{false};
};

enum class ChatPermissionRpcOutcome {
    kAllowed = 0,
    kInvalidArgument,
    kNotFriend,
    kBlockedBySelf,
    kBlockedByPeer,
    kStorageError,
};

struct CheckPrivateChatPermissionRpcRequest {
    std::uint64_t from_user_id{0};
    std::uint64_t to_user_id{0};
};

struct CheckPrivateChatPermissionRpcResponse {
    ChatPermissionRpcOutcome outcome{ChatPermissionRpcOutcome::kStorageError};
    std::string message;

    [[nodiscard]] bool Allowed() const noexcept {
        return outcome == ChatPermissionRpcOutcome::kAllowed;
    }
};

[[nodiscard]] const char* ChatPermissionRpcOutcomeToReason(
    ChatPermissionRpcOutcome outcome
) noexcept;

enum class FriendRequestCreateRpcOutcome {
    kCreated = 0,
    kReopened,
    kAlreadyPending,
    kReversePending,
    kAlreadyFriend,
    kBlockedBySelf,
    kBlockedByPeer,
    kSourceUserNotFound,
    kTargetUserNotFound,
    kSourceUserDisabled,
    kTargetUserDisabled,
    kRelationDataInconsistent,
    kRequestDataInconsistent,
    kInvalidArgument,
    kStorageError,
};

struct CreateFriendRequestRpcRequest {
    std::uint64_t from_user_id{0};
    std::uint64_t to_user_id{0};
    std::string request_message;
};

struct CreateFriendRequestRpcResponse {
    FriendRequestCreateRpcOutcome outcome{FriendRequestCreateRpcOutcome::kStorageError};
    std::uint64_t request_id{0};
    std::string message;

    [[nodiscard]] bool Success() const noexcept {
        return outcome == FriendRequestCreateRpcOutcome::kCreated ||
               outcome == FriendRequestCreateRpcOutcome::kReopened ||
               outcome == FriendRequestCreateRpcOutcome::kAlreadyPending;
    }
    [[nodiscard]] bool Changed() const noexcept {
        return outcome == FriendRequestCreateRpcOutcome::kCreated ||
               outcome == FriendRequestCreateRpcOutcome::kReopened;
    }
};

[[nodiscard]] const char* FriendRequestCreateRpcOutcomeToReason(
    FriendRequestCreateRpcOutcome outcome
) noexcept;

enum class FriendRequestListRpcOutcome {
    kSucceeded = 0,
    kInvalidArgument,
    kInvalidCursor,
    kInvalidRecord,
    kStorageError,
};

struct SocialFriendRequestRecord {
    std::uint64_t request_id{0};
    std::uint64_t from_user_id{0};
    std::uint64_t to_user_id{0};
    std::string request_message;
    std::uint32_t request_status{0};
    std::string created_at;
    std::string handled_at;
    std::string updated_at;
    std::string from_username;
    std::string from_nickname;
    std::string from_avatar_url;
    std::uint32_t from_user_status{0};
};

struct ListPendingIncomingFriendRequestsRpcRequest {
    std::uint64_t receiver_user_id{0};
    std::string before_created_at;
    std::uint64_t before_request_id{0};
    std::uint32_t limit{0};
};

struct ListPendingIncomingFriendRequestsRpcResponse {
    FriendRequestListRpcOutcome outcome{FriendRequestListRpcOutcome::kStorageError};
    std::vector<SocialFriendRequestRecord> requests;
    bool has_more{false};
    std::string next_before_created_at;
    std::uint64_t next_before_request_id{0};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return outcome == FriendRequestListRpcOutcome::kSucceeded;
    }
};

[[nodiscard]] const char* FriendRequestListRpcOutcomeToReason(
    FriendRequestListRpcOutcome outcome
) noexcept;

enum class FriendRequestAcceptRpcOutcome {
    kAccepted = 0,
    kAlreadyAccepted,
    kRequestNotFound,
    kNotRequestReceiver,
    kRequestNotPending,
    kBlockedBySelf,
    kBlockedByPeer,
    kRequesterNotFound,
    kReceiverNotFound,
    kRequesterDisabled,
    kReceiverDisabled,
    kRelationDataInconsistent,
    kRequestDataInconsistent,
    kInvalidArgument,
    kStorageError,
};

struct AcceptFriendRequestRpcRequest {
    std::uint64_t request_id{0};
    std::uint64_t handler_user_id{0};
};

struct AcceptFriendRequestRpcResponse {
    FriendRequestAcceptRpcOutcome outcome{FriendRequestAcceptRpcOutcome::kStorageError};
    std::uint64_t request_id{0};
    std::uint64_t requester_user_id{0};
    std::uint64_t receiver_user_id{0};
    std::string message;

    [[nodiscard]] bool Success() const noexcept {
        return outcome == FriendRequestAcceptRpcOutcome::kAccepted ||
               outcome == FriendRequestAcceptRpcOutcome::kAlreadyAccepted;
    }
    [[nodiscard]] bool Changed() const noexcept {
        return outcome == FriendRequestAcceptRpcOutcome::kAccepted;
    }
};

[[nodiscard]] const char* FriendRequestAcceptRpcOutcomeToReason(
    FriendRequestAcceptRpcOutcome outcome
) noexcept;

enum class FriendRequestRejectRpcOutcome {
    kRejected = 0,
    kAlreadyRejected,
    kAlreadyAccepted,
    kRequestCancelled,
    kRequestNotFound,
    kNotRequestReceiver,
    kRequestDataInconsistent,
    kInvalidArgument,
    kStorageError,
};

struct RejectFriendRequestRpcRequest {
    std::uint64_t request_id{0};
    std::uint64_t handler_user_id{0};
};

struct RejectFriendRequestRpcResponse {
    FriendRequestRejectRpcOutcome outcome{FriendRequestRejectRpcOutcome::kStorageError};
    std::uint64_t request_id{0};
    std::uint64_t requester_user_id{0};
    std::uint64_t receiver_user_id{0};
    std::string message;

    [[nodiscard]] bool Success() const noexcept {
        return outcome == FriendRequestRejectRpcOutcome::kRejected ||
               outcome == FriendRequestRejectRpcOutcome::kAlreadyRejected;
    }
    [[nodiscard]] bool Changed() const noexcept {
        return outcome == FriendRequestRejectRpcOutcome::kRejected;
    }
};

[[nodiscard]] const char* FriendRequestRejectRpcOutcomeToReason(
    FriendRequestRejectRpcOutcome outcome
) noexcept;

template <typename T>
struct SocialMutationRpcCallResult {
    RpcStatus status;
    std::optional<T> value;
    bool attempted{false};

    [[nodiscard]] bool ok() const noexcept {
        return status.ok() && value.has_value();
    }

    static SocialMutationRpcCallResult Success(T response) {
        SocialMutationRpcCallResult output;
        output.status = RpcStatus::Ok();
        output.value = std::move(response);
        output.attempted = true;
        return output;
    }

    static SocialMutationRpcCallResult Failure(
        RpcErrorCode code,
        std::string message,
        bool call_attempted
    ) {
        SocialMutationRpcCallResult output;
        output.status.code = code;
        output.status.message = std::move(message);
        output.attempted = call_attempted;
        return output;
    }
};

using CreateFriendRequestRpcCallResult =
    SocialMutationRpcCallResult<CreateFriendRequestRpcResponse>;
using AcceptFriendRequestRpcCallResult =
    SocialMutationRpcCallResult<AcceptFriendRequestRpcResponse>;
using RejectFriendRequestRpcCallResult =
    SocialMutationRpcCallResult<RejectFriendRequestRpcResponse>;

}  // namespace tinyimx::rpc
