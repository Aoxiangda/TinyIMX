#pragma once

#include "common/db/MySqlConnectionPool.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx {

enum class UserRelationStatus : std::uint32_t {
    kFriend = 1,
    kBlocked = 2
};

enum class ChatPermissionStatus {
    kAllowed = 0,
    kInvalidArgument,
    kNotFriend,
    kBlockedBySelf,
    kBlockedByPeer,
    kStorageError
};

struct FriendRecord {
    std::uint64_t friend_user_id{0};

    std::string username;
    std::string nickname;
    std::string avatar_url;

    std::uint32_t user_status{0};
    std::uint32_t relation_status{0};

    std::string relation_created_at;
    std::string relation_updated_at;
};


const char* ChatPermissionStatusToString(
    ChatPermissionStatus status
);

struct ChatPermissionResult {
    ChatPermissionStatus status{
        ChatPermissionStatus::kStorageError
    };

    std::string message;

    bool Allowed() const {
        return status == ChatPermissionStatus::kAllowed;
    }
};

class FriendRepository {
public:
    explicit FriendRepository(MySqlConnectionPool* pool);

    FriendRepository(const FriendRepository&) = delete;
    FriendRepository& operator=(const FriendRepository&) = delete;

    ChatPermissionResult CheckPrivateChatPermission(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id
    );

    std::vector<FriendRecord> ListFriends(std::uint64_t user_id,
                                          std::size_t limit);

    const std::string& LastError() const;

private:
    void SetError(const std::string& error_message);

    std::vector<FriendRecord> BuildFriendsFromResult(
                                const MySqlQueryResult& result);
private:
    MySqlConnectionPool* pool_{nullptr};
    std::string last_error_;
};

}  // namespace tinyimx