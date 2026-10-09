#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx::social {

enum class FriendRequestCreateOutcome {
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

enum class FriendRequestListOutcome {
    kSucceeded = 0,
    kInvalidArgument,
    kInvalidCursor,
    kInvalidRecord,
    kStorageError,
};

enum class FriendRequestAcceptOutcome {
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

enum class FriendRequestRejectOutcome {
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

struct FriendRequestView {
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

struct CreateFriendRequestApplicationResult {
    FriendRequestCreateOutcome outcome{FriendRequestCreateOutcome::kStorageError};
    std::uint64_t request_id{0};
    std::string message;

    [[nodiscard]] bool Success() const noexcept {
        return outcome == FriendRequestCreateOutcome::kCreated ||
               outcome == FriendRequestCreateOutcome::kReopened ||
               outcome == FriendRequestCreateOutcome::kAlreadyPending;
    }
    [[nodiscard]] bool Changed() const noexcept {
        return outcome == FriendRequestCreateOutcome::kCreated ||
               outcome == FriendRequestCreateOutcome::kReopened;
    }
};

struct ListPendingFriendRequestsApplicationResult {
    FriendRequestListOutcome outcome{FriendRequestListOutcome::kStorageError};
    std::vector<FriendRequestView> requests;
    bool has_more{false};
    std::string next_before_created_at;
    std::uint64_t next_before_request_id{0};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return outcome == FriendRequestListOutcome::kSucceeded;
    }
};

struct AcceptFriendRequestApplicationResult {
    FriendRequestAcceptOutcome outcome{FriendRequestAcceptOutcome::kStorageError};
    std::uint64_t request_id{0};
    std::uint64_t requester_user_id{0};
    std::uint64_t receiver_user_id{0};
    std::string message;

    [[nodiscard]] bool Success() const noexcept {
        return outcome == FriendRequestAcceptOutcome::kAccepted ||
               outcome == FriendRequestAcceptOutcome::kAlreadyAccepted;
    }
    [[nodiscard]] bool Changed() const noexcept {
        return outcome == FriendRequestAcceptOutcome::kAccepted;
    }
};

struct RejectFriendRequestApplicationResult {
    FriendRequestRejectOutcome outcome{FriendRequestRejectOutcome::kStorageError};
    std::uint64_t request_id{0};
    std::uint64_t requester_user_id{0};
    std::uint64_t receiver_user_id{0};
    std::string message;

    [[nodiscard]] bool Success() const noexcept {
        return outcome == FriendRequestRejectOutcome::kRejected ||
               outcome == FriendRequestRejectOutcome::kAlreadyRejected;
    }
    [[nodiscard]] bool Changed() const noexcept {
        return outcome == FriendRequestRejectOutcome::kRejected;
    }
};

}  // namespace tinyimx::social
