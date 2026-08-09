#pragma once

#include "common/db/MySqlConnectionPool.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx {

enum class FriendRequestStatus : std::uint32_t {
    kPending = 0,
    kAccepted = 1,
    kRejected = 2,
    kCancelled = 3
};

enum class CreateFriendRequestStatus {
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
    kStorageError
};

enum class AcceptFriendRequestStatus {
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
    kStorageError
};

const char* AcceptFriendRequestStatusToString(
    AcceptFriendRequestStatus status
);


enum class RejectFriendRequestStatus {
    kRejected = 0,
    kAlreadyRejected,

    kAlreadyAccepted,
    kRequestCancelled,

    kRequestNotFound,
    kNotRequestReceiver,

    kRequestDataInconsistent,

    kInvalidArgument,
    kStorageError
};

const char* RejectFriendRequestStatusToString(
    RejectFriendRequestStatus status
);

struct AcceptFriendRequestResult {
    AcceptFriendRequestStatus status{
        AcceptFriendRequestStatus::kStorageError
    };

    std::uint64_t request_id{0};

    std::uint64_t requester_user_id{0};
    std::uint64_t receiver_user_id{0};

    std::string message;

    bool AcceptedOrAlreadyAccepted() const {
        return
            status == AcceptFriendRequestStatus::kAccepted ||
            status == AcceptFriendRequestStatus::kAlreadyAccepted;
    }
};

struct RejectFriendRequestResult {
    RejectFriendRequestStatus status{
        RejectFriendRequestStatus::kStorageError
    };

    std::uint64_t request_id{0};

    std::uint64_t requester_user_id{0};
    std::uint64_t receiver_user_id{0};

    std::string message;

    bool RejectedOrAlreadyRejected() const {
        return
            status == RejectFriendRequestStatus::kRejected ||
            status == RejectFriendRequestStatus::kAlreadyRejected;
    }
};


const char* CreateFriendRequestStatusToString(
    CreateFriendRequestStatus status
);

struct CreateFriendRequestResult {
    CreateFriendRequestStatus status{
        CreateFriendRequestStatus::kStorageError
    };

    std::uint64_t request_id{0};
    std::string message;

    bool CreatedOrReopened() const {
        return status == CreateFriendRequestStatus::kCreated ||
               status == CreateFriendRequestStatus::kReopened;
    }
};

struct FriendRequestRecord {
    std::uint64_t request_id{0};

    std::uint64_t from_user_id{0};
    std::uint64_t to_user_id{0};

    std::string request_message;

    FriendRequestStatus request_status{
        FriendRequestStatus::kPending
    };

    std::string created_at;
    std::string handled_at;
    std::string updated_at;

    std::string from_username;
    std::string from_nickname;
    std::string from_avatar_url;

    std::uint32_t from_user_status{0};
};

enum class
ListPendingIncomingRequestsStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kInvalidCursor,
    kInvalidRecord,
    kStorageError
};

const char* ListPendingIncomingRequestsStatusToString(
    ListPendingIncomingRequestsStatus status
);

struct ListPendingIncomingRequestsResult {
    ListPendingIncomingRequestsStatus status{
        ListPendingIncomingRequestsStatus::kStorageError
    };

    std::vector<FriendRequestRecord> records;

    std::string message;

    bool Succeeded() const noexcept {
        return status == ListPendingIncomingRequestsStatus::kSucceeded;
    }
};

class FriendRequestRepository {
public:
    explicit FriendRequestRepository(
        MySqlConnectionPool* pool
    );

    FriendRequestRepository(
        const FriendRequestRepository&
    ) = delete;

    FriendRequestRepository& operator=(
        const FriendRequestRepository&
    ) = delete;

    CreateFriendRequestResult
    CreateFriendRequest(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& request_message
    );

    ListPendingIncomingRequestsResult
    ListPendingIncomingRequests(
        std::uint64_t receiver_user_id,
        const std::string& before_created_at,
        std::uint64_t before_request_id,
        std::size_t limit
    );

    AcceptFriendRequestResult
    AcceptFriendRequest(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    );

    RejectFriendRequestResult
    RejectFriendRequest(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    );

private:
    static ListPendingIncomingRequestsResult
    BuildFriendRequestRecords(
        const MySqlQueryResult& result
    );

private:
    MySqlConnectionPool* pool_{nullptr};
};

}  // namespace tinyimx