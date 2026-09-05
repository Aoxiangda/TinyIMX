#include "services/social/application/FriendApplicationService.h"

#include <cstddef>
#include <utility>

namespace tinyimx::social {
namespace {

constexpr std::uint32_t kMaxVisibleFriendListLimit = 100;

}  // namespace

FriendApplicationService::FriendApplicationService(
    FriendRepositoryPort* repository
)
    : repository_(repository) {
}

ListFriendsApplicationResult
FriendApplicationService::ListFriends(
    std::uint64_t actor_user_id,
    std::uint32_t limit
) {
    ListFriendsApplicationResult result;

    if (actor_user_id == 0 ||
        limit == 0 ||
        limit > kMaxVisibleFriendListLimit) {
        result.status =
            FriendApplicationStatus::kInvalidArgument;
        result.message =
            "invalid ListFriends application request";
        return result;
    }

    if (repository_ == nullptr) {
        result.status =
            FriendApplicationStatus::kStorageError;
        result.message =
            "friend repository port is unavailable";
        return result;
    }

    // Query one extra row so has_more is determined without exposing
    // repository implementation details to the RPC transport layer.
    const std::size_t query_limit =
        static_cast<std::size_t>(limit) + 1U;

    auto repository_result =
        repository_->ListFriends(
            actor_user_id,
            query_limit
        );

    if (!repository_result.Succeeded()) {
        result.status = repository_result.status;
        result.message =
            std::move(repository_result.message);
        return result;
    }

    result.friends =
        std::move(repository_result.records);

    if (result.friends.size() > limit) {
        result.has_more = true;
        result.friends.resize(limit);
    }

    result.status =
        FriendApplicationStatus::kSucceeded;
    result.message = "friend list queried";
    return result;
}

}  // namespace tinyimx::social
