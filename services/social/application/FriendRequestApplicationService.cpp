#include "services/social/application/FriendRequestApplicationService.h"

#include <cstddef>
#include <utility>

namespace tinyimx::social {
namespace {

constexpr std::uint32_t kMaxFriendRequestListLimit = 50;
constexpr std::size_t kMaxFriendRequestMessageBytes = 255;

}  // namespace

FriendRequestApplicationService::FriendRequestApplicationService(
    FriendRequestRepositoryPort* repository
)
    : repository_(repository) {
}

CreateFriendRequestApplicationResult
FriendRequestApplicationService::Create(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& request_message
) {
    if (from_user_id == 0 || to_user_id == 0 || from_user_id == to_user_id ||
        request_message.size() > kMaxFriendRequestMessageBytes) {
        return {
            FriendRequestCreateOutcome::kInvalidArgument,
            0,
            "invalid friend request create application request"
        };
    }
    if (repository_ == nullptr) {
        return {
            FriendRequestCreateOutcome::kStorageError,
            0,
            "friend request repository port is unavailable"
        };
    }
    return repository_->Create(from_user_id, to_user_id, request_message);
}

ListPendingFriendRequestsApplicationResult
FriendRequestApplicationService::ListPendingIncoming(
    std::uint64_t receiver_user_id,
    const std::string& before_created_at,
    std::uint64_t before_request_id,
    std::uint32_t limit
) {
    ListPendingFriendRequestsApplicationResult result;
    const bool first_page = before_created_at.empty() && before_request_id == 0;
    const bool next_page = !before_created_at.empty() && before_request_id != 0;

    if (receiver_user_id == 0 || limit == 0 || limit > kMaxFriendRequestListLimit) {
        result.outcome = FriendRequestListOutcome::kInvalidArgument;
        result.message = "invalid friend request list application request";
        return result;
    }
    if (!first_page && !next_page) {
        result.outcome = FriendRequestListOutcome::kInvalidCursor;
        result.message = "invalid friend request pagination cursor";
        return result;
    }
    if (repository_ == nullptr) {
        result.outcome = FriendRequestListOutcome::kStorageError;
        result.message = "friend request repository port is unavailable";
        return result;
    }

    auto repository_result = repository_->ListPendingIncoming(
        receiver_user_id,
        before_created_at,
        before_request_id,
        static_cast<std::size_t>(limit) + 1U
    );
    if (!repository_result.Succeeded()) {
        return repository_result;
    }

    if (repository_result.requests.size() > limit) {
        repository_result.has_more = true;
        repository_result.requests.resize(limit);
    }
    if (repository_result.has_more && !repository_result.requests.empty()) {
        repository_result.next_before_created_at =
            repository_result.requests.back().created_at;
        repository_result.next_before_request_id =
            repository_result.requests.back().request_id;
    }
    return repository_result;
}

AcceptFriendRequestApplicationResult
FriendRequestApplicationService::Accept(
    std::uint64_t request_id,
    std::uint64_t handler_user_id
) {
    if (request_id == 0 || handler_user_id == 0) {
        return {
            FriendRequestAcceptOutcome::kInvalidArgument,
            request_id,
            0,
            handler_user_id,
            "invalid friend request accept application request"
        };
    }
    if (repository_ == nullptr) {
        return {
            FriendRequestAcceptOutcome::kStorageError,
            request_id,
            0,
            handler_user_id,
            "friend request repository port is unavailable"
        };
    }
    return repository_->Accept(request_id, handler_user_id);
}

RejectFriendRequestApplicationResult
FriendRequestApplicationService::Reject(
    std::uint64_t request_id,
    std::uint64_t handler_user_id
) {
    if (request_id == 0 || handler_user_id == 0) {
        return {
            FriendRequestRejectOutcome::kInvalidArgument,
            request_id,
            0,
            handler_user_id,
            "invalid friend request reject application request"
        };
    }
    if (repository_ == nullptr) {
        return {
            FriendRequestRejectOutcome::kStorageError,
            request_id,
            0,
            handler_user_id,
            "friend request repository port is unavailable"
        };
    }
    return repository_->Reject(request_id, handler_user_id);
}

}  // namespace tinyimx::social
