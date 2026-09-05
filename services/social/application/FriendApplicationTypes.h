#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx::social {

enum class FriendApplicationStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kInvalidRecord,
    kStorageError,
};

struct FriendView {
    std::uint64_t friend_user_id{0};
    std::string username;
    std::string nickname;
    std::string avatar_url;
    std::uint32_t user_status{0};
    std::uint32_t relation_status{0};
    std::string relation_created_at;
    std::string relation_updated_at;
};

struct FriendRepositoryListResult {
    FriendApplicationStatus status{
        FriendApplicationStatus::kStorageError
    };
    std::vector<FriendView> records;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FriendApplicationStatus::kSucceeded;
    }
};

struct ListFriendsApplicationResult {
    FriendApplicationStatus status{
        FriendApplicationStatus::kStorageError
    };
    std::vector<FriendView> friends;
    bool has_more{false};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FriendApplicationStatus::kSucceeded;
    }
};

}  // namespace tinyimx::social
