#include "services/social/repository/FriendRequestRepositoryAdapter.h"

#include "services/repository/FriendRequestRepository.h"

#include <utility>

namespace tinyimx::social {
namespace {

FriendRequestCreateOutcome MapCreate(tinyimx::CreateFriendRequestStatus status) {
    using S = tinyimx::CreateFriendRequestStatus;
    using O = FriendRequestCreateOutcome;
    switch (status) {
        case S::kCreated: return O::kCreated;
        case S::kReopened: return O::kReopened;
        case S::kAlreadyPending: return O::kAlreadyPending;
        case S::kReversePending: return O::kReversePending;
        case S::kAlreadyFriend: return O::kAlreadyFriend;
        case S::kBlockedBySelf: return O::kBlockedBySelf;
        case S::kBlockedByPeer: return O::kBlockedByPeer;
        case S::kSourceUserNotFound: return O::kSourceUserNotFound;
        case S::kTargetUserNotFound: return O::kTargetUserNotFound;
        case S::kSourceUserDisabled: return O::kSourceUserDisabled;
        case S::kTargetUserDisabled: return O::kTargetUserDisabled;
        case S::kRelationDataInconsistent: return O::kRelationDataInconsistent;
        case S::kRequestDataInconsistent: return O::kRequestDataInconsistent;
        case S::kInvalidArgument: return O::kInvalidArgument;
        case S::kStorageError: return O::kStorageError;
    }
    return O::kStorageError;
}

FriendRequestListOutcome MapList(tinyimx::ListPendingIncomingRequestsStatus status) {
    using S = tinyimx::ListPendingIncomingRequestsStatus;
    using O = FriendRequestListOutcome;
    switch (status) {
        case S::kSucceeded: return O::kSucceeded;
        case S::kInvalidArgument: return O::kInvalidArgument;
        case S::kInvalidCursor: return O::kInvalidCursor;
        case S::kInvalidRecord: return O::kInvalidRecord;
        case S::kStorageError: return O::kStorageError;
    }
    return O::kStorageError;
}

FriendRequestAcceptOutcome MapAccept(tinyimx::AcceptFriendRequestStatus status) {
    using S = tinyimx::AcceptFriendRequestStatus;
    using O = FriendRequestAcceptOutcome;
    switch (status) {
        case S::kAccepted: return O::kAccepted;
        case S::kAlreadyAccepted: return O::kAlreadyAccepted;
        case S::kRequestNotFound: return O::kRequestNotFound;
        case S::kNotRequestReceiver: return O::kNotRequestReceiver;
        case S::kRequestNotPending: return O::kRequestNotPending;
        case S::kBlockedBySelf: return O::kBlockedBySelf;
        case S::kBlockedByPeer: return O::kBlockedByPeer;
        case S::kRequesterNotFound: return O::kRequesterNotFound;
        case S::kReceiverNotFound: return O::kReceiverNotFound;
        case S::kRequesterDisabled: return O::kRequesterDisabled;
        case S::kReceiverDisabled: return O::kReceiverDisabled;
        case S::kRelationDataInconsistent: return O::kRelationDataInconsistent;
        case S::kRequestDataInconsistent: return O::kRequestDataInconsistent;
        case S::kInvalidArgument: return O::kInvalidArgument;
        case S::kStorageError: return O::kStorageError;
    }
    return O::kStorageError;
}

FriendRequestRejectOutcome MapReject(tinyimx::RejectFriendRequestStatus status) {
    using S = tinyimx::RejectFriendRequestStatus;
    using O = FriendRequestRejectOutcome;
    switch (status) {
        case S::kRejected: return O::kRejected;
        case S::kAlreadyRejected: return O::kAlreadyRejected;
        case S::kAlreadyAccepted: return O::kAlreadyAccepted;
        case S::kRequestCancelled: return O::kRequestCancelled;
        case S::kRequestNotFound: return O::kRequestNotFound;
        case S::kNotRequestReceiver: return O::kNotRequestReceiver;
        case S::kRequestDataInconsistent: return O::kRequestDataInconsistent;
        case S::kInvalidArgument: return O::kInvalidArgument;
        case S::kStorageError: return O::kStorageError;
    }
    return O::kStorageError;
}

}  // namespace

FriendRequestRepositoryAdapter::FriendRequestRepositoryAdapter(
    tinyimx::FriendRequestRepository* repository
) : repository_(repository) {}

CreateFriendRequestApplicationResult FriendRequestRepositoryAdapter::Create(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& request_message
) {
    if (repository_ == nullptr) {
        return {FriendRequestCreateOutcome::kStorageError, 0,
                "friend request repository is unavailable"};
    }
    auto result = repository_->CreateFriendRequest(
        from_user_id, to_user_id, request_message);
    return {MapCreate(result.status), result.request_id, std::move(result.message)};
}

ListPendingFriendRequestsApplicationResult
FriendRequestRepositoryAdapter::ListPendingIncoming(
    std::uint64_t receiver_user_id,
    const std::string& before_created_at,
    std::uint64_t before_request_id,
    std::size_t limit
) {
    ListPendingFriendRequestsApplicationResult output;
    if (repository_ == nullptr) {
        output.outcome = FriendRequestListOutcome::kStorageError;
        output.message = "friend request repository is unavailable";
        return output;
    }

    auto result = repository_->ListPendingIncomingRequests(
        receiver_user_id, before_created_at, before_request_id, limit);
    output.outcome = MapList(result.status);
    output.message = std::move(result.message);
    if (!result.Succeeded()) return output;

    output.requests.reserve(result.records.size());
    for (auto& record : result.records) {
        FriendRequestView view;
        view.request_id = record.request_id;
        view.from_user_id = record.from_user_id;
        view.to_user_id = record.to_user_id;
        view.request_message = std::move(record.request_message);
        view.request_status = static_cast<std::uint32_t>(record.request_status);
        view.created_at = std::move(record.created_at);
        view.handled_at = std::move(record.handled_at);
        view.updated_at = std::move(record.updated_at);
        view.from_username = std::move(record.from_username);
        view.from_nickname = std::move(record.from_nickname);
        view.from_avatar_url = std::move(record.from_avatar_url);
        view.from_user_status = record.from_user_status;
        output.requests.push_back(std::move(view));
    }
    return output;
}

AcceptFriendRequestApplicationResult FriendRequestRepositoryAdapter::Accept(
    std::uint64_t request_id,
    std::uint64_t handler_user_id
) {
    if (repository_ == nullptr) {
        return {FriendRequestAcceptOutcome::kStorageError, request_id, 0,
                handler_user_id, "friend request repository is unavailable"};
    }
    auto result = repository_->AcceptFriendRequest(request_id, handler_user_id);
    return {MapAccept(result.status), result.request_id,
            result.requester_user_id, result.receiver_user_id,
            std::move(result.message)};
}

RejectFriendRequestApplicationResult FriendRequestRepositoryAdapter::Reject(
    std::uint64_t request_id,
    std::uint64_t handler_user_id
) {
    if (repository_ == nullptr) {
        return {FriendRequestRejectOutcome::kStorageError, request_id, 0,
                handler_user_id, "friend request repository is unavailable"};
    }
    auto result = repository_->RejectFriendRequest(request_id, handler_user_id);
    return {MapReject(result.status), result.request_id,
            result.requester_user_id, result.receiver_user_id,
            std::move(result.message)};
}

}  // namespace tinyimx::social
