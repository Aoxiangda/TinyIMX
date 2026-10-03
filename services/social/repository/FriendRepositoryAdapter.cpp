#include "services/social/repository/FriendRepositoryAdapter.h"

#include "services/repository/FriendRepository.h"

#include <utility>

namespace tinyimx::social {
namespace {

FriendApplicationStatus MapListStatus(
    tinyimx::ListFriendsStatus status
) {
    switch (status) {
        case tinyimx::ListFriendsStatus::kSucceeded:
            return FriendApplicationStatus::kSucceeded;
        case tinyimx::ListFriendsStatus::kInvalidArgument:
            return FriendApplicationStatus::kInvalidArgument;
        case tinyimx::ListFriendsStatus::kInvalidRecord:
            return FriendApplicationStatus::kInvalidRecord;
        case tinyimx::ListFriendsStatus::kStorageError:
            return FriendApplicationStatus::kStorageError;
    }
    return FriendApplicationStatus::kStorageError;
}

ChatPermissionApplicationStatus MapPermissionStatus(
    tinyimx::ChatPermissionStatus status
) {
    switch (status) {
        case tinyimx::ChatPermissionStatus::kAllowed:
            return ChatPermissionApplicationStatus::kAllowed;
        case tinyimx::ChatPermissionStatus::kInvalidArgument:
            return ChatPermissionApplicationStatus::kInvalidArgument;
        case tinyimx::ChatPermissionStatus::kNotFriend:
            return ChatPermissionApplicationStatus::kNotFriend;
        case tinyimx::ChatPermissionStatus::kBlockedBySelf:
            return ChatPermissionApplicationStatus::kBlockedBySelf;
        case tinyimx::ChatPermissionStatus::kBlockedByPeer:
            return ChatPermissionApplicationStatus::kBlockedByPeer;
        case tinyimx::ChatPermissionStatus::kStorageError:
            return ChatPermissionApplicationStatus::kStorageError;
    }
    return ChatPermissionApplicationStatus::kStorageError;
}

}  // namespace

FriendRepositoryAdapter::FriendRepositoryAdapter(
    tinyimx::FriendRepository* repository
)
    : repository_(repository) {
}

ChatPermissionApplicationResult
FriendRepositoryAdapter::CheckPrivateChatPermission(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id
) {
    if (repository_ == nullptr) {
        return {
            ChatPermissionApplicationStatus::kStorageError,
            "friend repository is unavailable"
        };
    }

    auto result = repository_->CheckPrivateChatPermission(
        from_user_id,
        to_user_id
    );
    return {
        MapPermissionStatus(result.status),
        std::move(result.message)
    };
}

FriendRepositoryListResult
FriendRepositoryAdapter::ListFriends(
    std::uint64_t user_id,
    std::size_t limit
) {
    FriendRepositoryListResult output;

    if (repository_ == nullptr) {
        output.status = FriendApplicationStatus::kStorageError;
        output.message = "friend repository is unavailable";
        return output;
    }

    auto result = repository_->ListFriends(user_id, limit);

    output.status = MapListStatus(result.status);
    output.message = std::move(result.message);

    if (!result.Succeeded()) {
        return output;
    }

    output.records.reserve(result.records.size());

    for (auto& record : result.records) {
        FriendView view;
        view.friend_user_id = record.friend_user_id;
        view.username = std::move(record.username);
        view.nickname = std::move(record.nickname);
        view.avatar_url = std::move(record.avatar_url);
        view.user_status = record.user_status;
        view.relation_status = record.relation_status;
        view.relation_created_at = std::move(record.relation_created_at);
        view.relation_updated_at = std::move(record.relation_updated_at);
        output.records.push_back(std::move(view));
    }

    return output;
}

}  // namespace tinyimx::social
