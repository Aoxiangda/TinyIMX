#include "services/repository/FriendRepository.h"

#include "common/logging/LogMacros.h"

#include <optional>
#include <stdexcept>

namespace tinyimx {
namespace {

std::uint64_t ToUInt64(const std::string& value) {
    return static_cast<std::uint64_t>(std::stoull(value));
}

std::uint32_t ToUInt32(const std::string& value) {
    return static_cast<std::uint32_t>(std::stoul(value));
}

}  // namespace

const char* ChatPermissionStatusToString(
    ChatPermissionStatus status
) {
    switch (status) {
        case ChatPermissionStatus::kAllowed:
            return "allowed";
        case ChatPermissionStatus::kInvalidArgument:
            return "invalid_argument";
        case ChatPermissionStatus::kNotFriend:
            return "not_friend";
        case ChatPermissionStatus::kBlockedBySelf:
            return "blocked_by_self";
        case ChatPermissionStatus::kBlockedByPeer:
            return "blocked_by_peer";
        case ChatPermissionStatus::kStorageError:
            return "storage_error";
        default:
            return "unknown";
    }
}

FriendRepository::FriendRepository(MySqlConnectionPool* pool)
    : pool_(pool) {}

ChatPermissionResult FriendRepository::CheckPrivateChatPermission(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id
) {
    ChatPermissionResult result;

    if (from_user_id == 0 || to_user_id == 0) {
        result.status = ChatPermissionStatus::kInvalidArgument;
        result.message = "invalid user id";
        return result;
    }

    if (from_user_id == to_user_id) {
        result.status = ChatPermissionStatus::kInvalidArgument;
        result.message = "from user equals to user";
        return result;
    }

    if (pool_ == nullptr) {
        SetError("friend repository check permission failed: pool is null");
        result.status = ChatPermissionStatus::kStorageError;
        result.message = last_error_;
        return result;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError(
            "friend repository check permission failed: acquire connection failed"
        );
        result.status = ChatPermissionStatus::kStorageError;
        result.message = last_error_;
        return result;
    }

    const std::string sql =
        "SELECT user_id, peer_user_id, relation_status "
        "FROM im_user_relations "
        "WHERE "
        "(user_id = " + std::to_string(from_user_id) +
        " AND peer_user_id = " + std::to_string(to_user_id) + ") "
        "OR "
        "(user_id = " + std::to_string(to_user_id) +
        " AND peer_user_id = " + std::to_string(from_user_id) + ")";

    MySqlQueryResult query_result;

    if (!connection->Query(sql, &query_result)) {
        SetError(connection->LastError());
        result.status = ChatPermissionStatus::kStorageError;
        result.message = last_error_;
        return result;
    }

    std::optional<std::uint32_t> self_status;
    std::optional<std::uint32_t> peer_status;

    try {
        for (const auto& row : query_result.rows) {
            if (row.size() < 3) {
                SetError(
                    "friend repository check permission failed: invalid row size"
                );
                result.status = ChatPermissionStatus::kStorageError;
                result.message = last_error_;
                return result;
            }

            const std::uint64_t user_id = ToUInt64(row[0]);
            const std::uint64_t peer_user_id = ToUInt64(row[1]);
            const std::uint32_t relation_status = ToUInt32(row[2]);

            if (relation_status !=
                    static_cast<std::uint32_t>(
                        UserRelationStatus::kFriend
                    ) &&
                relation_status !=
                    static_cast<std::uint32_t>(
                        UserRelationStatus::kBlocked
                    )) {
                SetError(
                    "friend repository check permission failed: invalid relation status"
                );
                result.status = ChatPermissionStatus::kStorageError;
                result.message = last_error_;
                return result;
            }

            if (user_id == from_user_id &&
                peer_user_id == to_user_id) {
                self_status = relation_status;
            } else if (user_id == to_user_id &&
                       peer_user_id == from_user_id) {
                peer_status = relation_status;
            }
        }
    } catch (const std::exception& e) {
        SetError(
            std::string(
                "friend repository check permission failed: "
            ) + e.what()
        );
        result.status = ChatPermissionStatus::kStorageError;
        result.message = last_error_;
        return result;
    }

    const auto friend_value =
        static_cast<std::uint32_t>(
            UserRelationStatus::kFriend
        );

    const auto blocked_value =
        static_cast<std::uint32_t>(
            UserRelationStatus::kBlocked
        );

    if (self_status.has_value() &&
        self_status.value() == blocked_value) {
        result.status = ChatPermissionStatus::kBlockedBySelf;
        result.message = "blocked by self";
        last_error_.clear();
        return result;
    }

    if (peer_status.has_value() &&
        peer_status.value() == blocked_value) {
        result.status = ChatPermissionStatus::kBlockedByPeer;
        result.message = "blocked by peer";
        last_error_.clear();
        return result;
    }

    if (self_status.has_value() &&
        peer_status.has_value() &&
        self_status.value() == friend_value &&
        peer_status.value() == friend_value) {
        result.status = ChatPermissionStatus::kAllowed;
        result.message = "private chat allowed";
        last_error_.clear();
        return result;
    }

    result.status = ChatPermissionStatus::kNotFriend;
    result.message = "not friend";
    last_error_.clear();
    return result;
}

std::vector<FriendRecord> FriendRepository::ListFriends(
    std::uint64_t user_id,
    std::size_t limit
) {
    std::vector<FriendRecord> empty;

    if (pool_ == nullptr) {
        SetError("friend repository list friends failed: pool is null");
        return empty;
    }

    if (user_id == 0) {
        SetError("friend repository list friends failed: invalid user id");
        return empty;
    }

    if (limit == 0) {
        return empty;
    }

    if (limit > 100) {
        limit = 100;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError(
            "friend repository list friends failed: acquire connection failed"
        );
        return empty;
    }

    const std::string user_id_text = std::to_string(user_id);

    const std::uint32_t friend_status =
        static_cast<std::uint32_t>(
            UserRelationStatus::kFriend
        );

    const std::string friend_status_text =
        std::to_string(friend_status);

    const std::string sql =
        "SELECT "
        "u.user_id, "
        "u.username, "
        "u.nickname, "
        "u.avatar_url, "
        "u.status, "
        "r.relation_status, "
        "r.created_at, "
        "r.updated_at "
        "FROM im_user_relations AS r "
        "INNER JOIN im_user_relations AS reverse_relation "
        "ON reverse_relation.user_id = r.peer_user_id "
        "AND reverse_relation.peer_user_id = r.user_id "
        "AND reverse_relation.relation_status = " + friend_status_text + " "
        "INNER JOIN im_users AS u "
        "ON u.user_id = r.peer_user_id "
        "WHERE r.user_id = " + user_id_text + " "
        "AND r.relation_status = " + friend_status_text + " "
        "ORDER BY r.updated_at DESC, r.peer_user_id ASC "
        "LIMIT " + std::to_string(limit);

    MySqlQueryResult result;

    if (!connection->Query(sql, &result)) {
        SetError(connection->LastError());
        return empty;
    }

    last_error_.clear();

    return BuildFriendsFromResult(result);
}

std::vector<FriendRecord> FriendRepository::BuildFriendsFromResult(
    const MySqlQueryResult& result
) {
    std::vector<FriendRecord> friends;
    friends.reserve(result.rows.size());

    try {
        for (const auto& row : result.rows) {
            if (row.size() < 8) {
                SetError(
                    "friend repository build friends failed: invalid row size"
                );
                return {};
            }

            FriendRecord friend_record;

            friend_record.friend_user_id = ToUInt64(row[0]);

            friend_record.username = row[1];
            friend_record.nickname = row[2];
            friend_record.avatar_url = row[3];

            friend_record.user_status = ToUInt32(row[4]);
            friend_record.relation_status = ToUInt32(row[5]);

            friend_record.relation_created_at = row[6];
            friend_record.relation_updated_at = row[7];

            friends.push_back(std::move(friend_record));
        }
    } catch (const std::exception& e) {
        SetError(
            std::string("friend repository build friends failed: ") +
            e.what()
        );
        return {};
    }

    last_error_.clear();
    return friends;
}

const std::string& FriendRepository::LastError() const {
    return last_error_;
}

void FriendRepository::SetError(
    const std::string& error_message
) {
    last_error_ = error_message;
    LOG_ERROR(error_message);
}

}  // namespace tinyimx