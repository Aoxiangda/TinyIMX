#include "services/repository/FriendRequestRepository.h"

#include "common/logging/LogMacros.h"
#include "services/repository/FriendRepository.h"

#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
#include <cctype>

namespace tinyimx {
namespace {

    constexpr std::uint32_t kActiveUserStatus = 1;
    constexpr std::size_t kMaxRequestMessageBytes = 255;

    std::uint64_t ToUInt64(const std::string& value) {
        return static_cast<std::uint64_t>(
            std::stoull(value)
        );
    }

    std::uint32_t ToUInt32(const std::string& value) {
        return static_cast<std::uint32_t>(
            std::stoul(value)
        );
    }

    struct ExistingRequest {
            std::uint64_t request_id{0};
            std::uint64_t from_user_id{0};
            std::uint64_t to_user_id{0};
            std::uint32_t request_status{0};
        };

        AcceptFriendRequestResult MakeAcceptResult(
        AcceptFriendRequestStatus status,
        std::uint64_t request_id,
        std::uint64_t requester_user_id,
        std::uint64_t receiver_user_id,
        std::string message
    ) {
        AcceptFriendRequestResult result;

        result.status = status;
        result.request_id = request_id;

        result.requester_user_id = requester_user_id;
        result.receiver_user_id = receiver_user_id;

        result.message = std::move(message);

        return result;
    }

    RejectFriendRequestResult MakeRejectResult(
        RejectFriendRequestStatus status,
        std::uint64_t request_id,
        std::uint64_t requester_user_id,
        std::uint64_t receiver_user_id,
        std::string message
    ) {
        RejectFriendRequestResult result;

        result.status = status;
        result.request_id = request_id;

        result.requester_user_id = requester_user_id;
        result.receiver_user_id = receiver_user_id;

        result.message = std::move(message);

        return result;
    }

    CreateFriendRequestResult MakeResult(
        CreateFriendRequestStatus status,
        std::uint64_t request_id,
        std::string message
    ) {
        CreateFriendRequestResult result;
        result.status = status;
        result.request_id = request_id;
        result.message = std::move(message);
        return result;
    }

    bool IsDateTimeCursorValid(
        const std::string& value
    ) {
        if (value.size() != 19) {
            return false;
        }

        if (value[4] != '-' ||
            value[7] != '-' ||
            value[10] != ' ' ||
            value[13] != ':' ||
            value[16] != ':') {
            return false;
        }

        for (std::size_t index = 0;
            index < value.size();
            ++index) {
            if (index == 4 ||
                index == 7 ||
                index == 10 ||
                index == 13 ||
                index == 16) {
                continue;
            }

            const unsigned char character =
                static_cast<unsigned char>(
                    value[index]
                );

            if (!std::isdigit(character)) {
                return false;
            }
        }

        return true;
    }

}  // namespace

const char* CreateFriendRequestStatusToString(
    CreateFriendRequestStatus status
) {
    switch (status) {
        case CreateFriendRequestStatus::kCreated:
            return "created";

        case CreateFriendRequestStatus::kReopened:
            return "reopened";

        case CreateFriendRequestStatus::kAlreadyPending:
            return "already_pending";

        case CreateFriendRequestStatus::kReversePending:
            return "reverse_pending";

        case CreateFriendRequestStatus::kAlreadyFriend:
            return "already_friend";

        case CreateFriendRequestStatus::kBlockedBySelf:
            return "blocked_by_self";

        case CreateFriendRequestStatus::kBlockedByPeer:
            return "blocked_by_peer";

        case CreateFriendRequestStatus::kSourceUserNotFound:
            return "source_user_not_found";

        case CreateFriendRequestStatus::kTargetUserNotFound:
            return "target_user_not_found";

        case CreateFriendRequestStatus::kSourceUserDisabled:
            return "source_user_disabled";

        case CreateFriendRequestStatus::kTargetUserDisabled:
            return "target_user_disabled";

        case CreateFriendRequestStatus::kRelationDataInconsistent:
            return "relation_data_inconsistent";

        case CreateFriendRequestStatus::kRequestDataInconsistent:
            return "request_data_inconsistent";

        case CreateFriendRequestStatus::kInvalidArgument:
            return "invalid_argument";

        case CreateFriendRequestStatus::kStorageError:
            return "storage_error";

        default:
            return "unknown";
    }
}

const char* AcceptFriendRequestStatusToString(
    AcceptFriendRequestStatus status
) {
    switch (status) {
        case AcceptFriendRequestStatus::kAccepted:
            return "accepted";

        case AcceptFriendRequestStatus::kAlreadyAccepted:
            return "already_accepted";

        case AcceptFriendRequestStatus::kRequestNotFound:
            return "request_not_found";

        case AcceptFriendRequestStatus::kNotRequestReceiver:
            return "not_request_receiver";

        case AcceptFriendRequestStatus::kRequestNotPending:
            return "request_not_pending";

        case AcceptFriendRequestStatus::kBlockedBySelf:
            return "blocked_by_self";

        case AcceptFriendRequestStatus::kBlockedByPeer:
            return "blocked_by_peer";

        case AcceptFriendRequestStatus::kRequesterNotFound:
            return "requester_not_found";

        case AcceptFriendRequestStatus::kReceiverNotFound:
            return "receiver_not_found";

        case AcceptFriendRequestStatus::kRequesterDisabled:
            return "requester_disabled";

        case AcceptFriendRequestStatus::kReceiverDisabled:
            return "receiver_disabled";

        case AcceptFriendRequestStatus::
            kRelationDataInconsistent:
            return "relation_data_inconsistent";

        case AcceptFriendRequestStatus::
            kRequestDataInconsistent:
            return "request_data_inconsistent";

        case AcceptFriendRequestStatus::kInvalidArgument:
            return "invalid_argument";

        case AcceptFriendRequestStatus::kStorageError:
            return "storage_error";

        default:
            return "unknown";
    }
}


const char* RejectFriendRequestStatusToString(
    RejectFriendRequestStatus status
) {
    switch (status) {
        case RejectFriendRequestStatus::kRejected:
            return "rejected";

        case RejectFriendRequestStatus::kAlreadyRejected:
            return "already_rejected";

        case RejectFriendRequestStatus::kAlreadyAccepted:
            return "already_accepted";

        case RejectFriendRequestStatus::kRequestCancelled:
            return "request_cancelled";

        case RejectFriendRequestStatus::kRequestNotFound:
            return "request_not_found";

        case RejectFriendRequestStatus::kNotRequestReceiver:
            return "not_request_receiver";

        case RejectFriendRequestStatus::
            kRequestDataInconsistent:
            return "request_data_inconsistent";

        case RejectFriendRequestStatus::kInvalidArgument:
            return "invalid_argument";

        case RejectFriendRequestStatus::kStorageError:
            return "storage_error";

        default:
            return "unknown";
    }
}

FriendRequestRepository::FriendRequestRepository(
    MySqlConnectionPool* pool
)
    : pool_(pool) {}

CreateFriendRequestResult
FriendRequestRepository::CreateFriendRequest(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& request_message
) {
    if (from_user_id == 0 || to_user_id == 0) {
        last_error_.clear();

        return MakeResult(
            CreateFriendRequestStatus::kInvalidArgument,
            0,
            "invalid user id"
        );
    }

    if (from_user_id == to_user_id) {
        last_error_.clear();

        return MakeResult(
            CreateFriendRequestStatus::kInvalidArgument,
            0,
            "cannot send friend request to self"
        );
    }

    if (request_message.size() >
        kMaxRequestMessageBytes) {
        last_error_.clear();

        return MakeResult(
            CreateFriendRequestStatus::kInvalidArgument,
            0,
            "friend request message is too long"
        );
    }

    if (pool_ == nullptr) {
        SetError(
            "friend request repository create failed: "
            "pool is null"
        );

        return MakeResult(
            CreateFriendRequestStatus::kStorageError,
            0,
            last_error_
        );
    }

    auto connection = pool_->Acquire();

    if (!connection) {
        SetError(
            "friend request repository create failed: "
            "acquire connection failed"
        );

        return MakeResult(
            CreateFriendRequestStatus::kStorageError,
            0,
            last_error_
        );
    }

    MySqlConnection* database = connection.operator->();

    auto rollback_storage_error =
        [this, database](
            const std::string& error_message
        ) -> CreateFriendRequestResult {
            std::string final_error = error_message;

            if (database != nullptr &&
                database->InTransaction()) {
                if (!database->Rollback()) {
                    final_error +=
                        "; rollback failed: " +
                        database->LastError();
                }
            }

            SetError(final_error);

            return MakeResult(
                CreateFriendRequestStatus::kStorageError,
                0,
                last_error_
            );
        };

    auto rollback_business_result =
        [database](
            CreateFriendRequestStatus status,
            std::uint64_t request_id,
            const std::string& message
        ) -> CreateFriendRequestResult {
            if (database != nullptr &&
                database->InTransaction()) {
                if (!database->Rollback()) {
                    return MakeResult(
                        CreateFriendRequestStatus::kStorageError,
                        0,
                        "rollback business transaction failed: " +
                            database->LastError()
                    );
                }
            }

            return MakeResult(
                status,
                request_id,
                message
            );
        };

    if (!database->BeginTransaction()) {
        SetError(database->LastError());

        return MakeResult(
            CreateFriendRequestStatus::kStorageError,
            0,
            last_error_
        );
    }

    /*
     * 第一步：按照user_id升序锁定两个用户。
     *
     * 不论是A申请B，还是B申请A，都会锁定同样的两行，
     * 并且锁定顺序一致，从而串行化同一用户对的好友申请操作。
     */
    const std::uint64_t low_user_id =
        from_user_id < to_user_id
            ? from_user_id
            : to_user_id;

    const std::uint64_t high_user_id =
        from_user_id < to_user_id
            ? to_user_id
            : from_user_id;

    const std::string lock_users_sql =
        "SELECT user_id, status "
        "FROM im_users "
        "WHERE user_id IN (" +
        std::to_string(low_user_id) + ", " +
        std::to_string(high_user_id) + ") "
        "ORDER BY user_id ASC "
        "FOR UPDATE";

    MySqlQueryResult user_result;

    if (!database->Query(
            lock_users_sql,
            &user_result
        )) {
        return rollback_storage_error(
            database->LastError()
        );
    }

    std::optional<std::uint32_t> source_user_status;
    std::optional<std::uint32_t> target_user_status;

    try {
        for (const auto& row : user_result.rows) {
            if (row.size() < 2) {
                return rollback_storage_error(
                    "friend request repository create failed: "
                    "invalid user row size"
                );
            }

            const std::uint64_t user_id =
                ToUInt64(row[0]);

            const std::uint32_t user_status =
                ToUInt32(row[1]);

            if (user_id == from_user_id) {
                source_user_status = user_status;
            } else if (user_id == to_user_id) {
                target_user_status = user_status;
            }
        }
    } catch (const std::exception& e) {
        return rollback_storage_error(
            std::string(
                "friend request repository create failed: "
                "parse user result failed: "
            ) + e.what()
        );
    }

    if (!source_user_status.has_value()) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::kSourceUserNotFound,
            0,
            "source user not found"
        );
    }

    if (!target_user_status.has_value()) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::kTargetUserNotFound,
            0,
            "target user not found"
        );
    }

    if (source_user_status.value() !=
        kActiveUserStatus) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::kSourceUserDisabled,
            0,
            "source user disabled"
        );
    }

    if (target_user_status.value() !=
        kActiveUserStatus) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::kTargetUserDisabled,
            0,
            "target user disabled"
        );
    }

    /*
     * 第二步：锁定并检查双向好友关系。
     */
    const std::string relation_sql =
        "SELECT user_id, peer_user_id, relation_status "
        "FROM im_user_relations "
        "WHERE "
        "(user_id = " +
        std::to_string(from_user_id) +
        " AND peer_user_id = " +
        std::to_string(to_user_id) + ") "
        "OR "
        "(user_id = " +
        std::to_string(to_user_id) +
        " AND peer_user_id = " +
        std::to_string(from_user_id) + ") "
        "ORDER BY user_id ASC, peer_user_id ASC "
        "FOR UPDATE";

    MySqlQueryResult relation_result;

    if (!database->Query(
            relation_sql,
            &relation_result
        )) {
        return rollback_storage_error(
            database->LastError()
        );
    }

    std::optional<std::uint32_t> self_relation_status;
    std::optional<std::uint32_t> peer_relation_status;

    try {
        for (const auto& row :
             relation_result.rows) {
            if (row.size() < 3) {
                return rollback_storage_error(
                    "friend request repository create failed: "
                    "invalid relation row size"
                );
            }

            const std::uint64_t user_id =
                ToUInt64(row[0]);

            const std::uint64_t peer_user_id =
                ToUInt64(row[1]);

            const std::uint32_t relation_status =
                ToUInt32(row[2]);

            if (relation_status !=
                    static_cast<std::uint32_t>(
                        UserRelationStatus::kFriend
                    ) &&
                relation_status !=
                    static_cast<std::uint32_t>(
                        UserRelationStatus::kBlocked
                    )) {
                return rollback_storage_error(
                    "friend request repository create failed: "
                    "invalid relation status"
                );
            }

            if (user_id == from_user_id &&
                peer_user_id == to_user_id) {
                self_relation_status = relation_status;
            } else if (
                user_id == to_user_id &&
                peer_user_id == from_user_id
            ) {
                peer_relation_status = relation_status;
            }
        }
    } catch (const std::exception& e) {
        return rollback_storage_error(
            std::string(
                "friend request repository create failed: "
                "parse relation result failed: "
            ) + e.what()
        );
    }

    const std::uint32_t friend_value =
        static_cast<std::uint32_t>(
            UserRelationStatus::kFriend
        );

    const std::uint32_t blocked_value =
        static_cast<std::uint32_t>(
            UserRelationStatus::kBlocked
        );

    if (self_relation_status.has_value() &&
        self_relation_status.value() ==
            blocked_value) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::kBlockedBySelf,
            0,
            "target user is blocked by source user"
        );
    }

    if (peer_relation_status.has_value() &&
        peer_relation_status.value() ==
            blocked_value) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::kBlockedByPeer,
            0,
            "source user is blocked by target user"
        );
    }

    const bool self_is_friend =
        self_relation_status.has_value() &&
        self_relation_status.value() ==
            friend_value;

    const bool peer_is_friend =
        peer_relation_status.has_value() &&
        peer_relation_status.value() ==
            friend_value;

    if (self_is_friend && peer_is_friend) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::kAlreadyFriend,
            0,
            "users are already friends"
        );
    }

    if (self_is_friend != peer_is_friend) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::
                kRelationDataInconsistent,
            0,
            "one-way friend relation detected"
        );
    }

    /*
     * 第三步：锁定并检查两个方向的好友申请。
     */
    const std::string request_query_sql =
        "SELECT "
        "request_id, "
        "from_user_id, "
        "to_user_id, "
        "request_status "
        "FROM im_friend_requests "
        "WHERE "
        "(from_user_id = " +
        std::to_string(from_user_id) +
        " AND to_user_id = " +
        std::to_string(to_user_id) + ") "
        "OR "
        "(from_user_id = " +
        std::to_string(to_user_id) +
        " AND to_user_id = " +
        std::to_string(from_user_id) + ") "
        "ORDER BY from_user_id ASC, to_user_id ASC "
        "FOR UPDATE";

    MySqlQueryResult request_result;

    if (!database->Query(
            request_query_sql,
            &request_result
        )) {
        return rollback_storage_error(
            database->LastError()
        );
    }

    std::optional<ExistingRequest> same_direction;
    std::optional<ExistingRequest> reverse_direction;

    try {
        for (const auto& row :
             request_result.rows) {
            if (row.size() < 4) {
                return rollback_storage_error(
                    "friend request repository create failed: "
                    "invalid request row size"
                );
            }

            ExistingRequest request;
            request.request_id = ToUInt64(row[0]);
            request.from_user_id = ToUInt64(row[1]);
            request.to_user_id = ToUInt64(row[2]);
            request.request_status = ToUInt32(row[3]);

            if (request.from_user_id ==
                    from_user_id &&
                request.to_user_id ==
                    to_user_id) {
                same_direction = request;
            } else if (
                request.from_user_id ==
                    to_user_id &&
                request.to_user_id ==
                    from_user_id
            ) {
                reverse_direction = request;
            }
        }
    } catch (const std::exception& e) {
        return rollback_storage_error(
            std::string(
                "friend request repository create failed: "
                "parse request result failed: "
            ) + e.what()
        );
    }

    const std::uint32_t pending_value =
        static_cast<std::uint32_t>(
            FriendRequestStatus::kPending
        );

    const std::uint32_t accepted_value =
        static_cast<std::uint32_t>(
            FriendRequestStatus::kAccepted
        );

    const std::uint32_t rejected_value =
        static_cast<std::uint32_t>(
            FriendRequestStatus::kRejected
        );

    const std::uint32_t cancelled_value =
        static_cast<std::uint32_t>(
            FriendRequestStatus::kCancelled
        );

    if (same_direction.has_value() &&
        reverse_direction.has_value() &&
        same_direction->request_status ==
            pending_value &&
        reverse_direction->request_status ==
            pending_value) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::
                kRequestDataInconsistent,
            0,
            "both request directions are pending"
        );
    }

    if (reverse_direction.has_value() &&
        reverse_direction->request_status ==
            pending_value) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::kReversePending,
            reverse_direction->request_id,
            "target user has already sent a friend request"
        );
    }

    if (same_direction.has_value() &&
        same_direction->request_status ==
            pending_value) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::kAlreadyPending,
            same_direction->request_id,
            "friend request is already pending"
        );
    }

    const bool same_accepted =
        same_direction.has_value() &&
        same_direction->request_status ==
            accepted_value;

    const bool reverse_accepted =
        reverse_direction.has_value() &&
        reverse_direction->request_status ==
            accepted_value;

    if (same_accepted || reverse_accepted) {
        last_error_.clear();

        return rollback_business_result(
            CreateFriendRequestStatus::
                kRequestDataInconsistent,
            0,
            "accepted request exists without "
            "complete friend relation"
        );
    }

    const std::string escaped_message =
        database->EscapeString(request_message);

    /*
     * 第四步：
     * 同方向存在rejected/cancelled时，重新打开原记录。
     */
    if (same_direction.has_value() &&
        (same_direction->request_status ==
             rejected_value ||
         same_direction->request_status ==
             cancelled_value)) {
        const std::string reopen_sql =
            "UPDATE im_friend_requests "
            "SET "
            "request_message = '" +
            escaped_message + "', "
            "request_status = " +
            std::to_string(pending_value) + ", "
            "created_at = CURRENT_TIMESTAMP, "
            "handled_at = NULL "
            "WHERE request_id = " +
            std::to_string(
                same_direction->request_id
            );

        if (!database->Execute(reopen_sql)) {
            return rollback_storage_error(
                database->LastError()
            );
        }

        const std::uint64_t request_id =
            same_direction->request_id;

        if (!database->Commit()) {
            return rollback_storage_error(
                database->LastError()
            );
        }

        last_error_.clear();

        return MakeResult(
            CreateFriendRequestStatus::kReopened,
            request_id,
            "friend request reopened"
        );
    }

    /*
     * 第五步：不存在同方向记录时创建新申请。
     */
    if (!same_direction.has_value()) {
        const std::string insert_sql =
            "INSERT INTO im_friend_requests ("
            "from_user_id, "
            "to_user_id, "
            "request_message, "
            "request_status"
            ") VALUES (" +
            std::to_string(from_user_id) + ", " +
            std::to_string(to_user_id) + ", '" +
            escaped_message + "', " +
            std::to_string(pending_value) +
            ")";

        if (!database->Execute(insert_sql)) {
            return rollback_storage_error(
                database->LastError()
            );
        }

        /*
         * Commit会清理连接上的last_insert_id_，
         * 因此必须在Commit之前保存request_id。
         */
        const std::uint64_t request_id =
            database->LastInsertId();

        if (request_id == 0) {
            return rollback_storage_error(
                "friend request repository create failed: "
                "last insert id is zero"
            );
        }

        if (!database->Commit()) {
            return rollback_storage_error(
                database->LastError()
            );
        }

        last_error_.clear();

        return MakeResult(
            CreateFriendRequestStatus::kCreated,
            request_id,
            "friend request created"
        );
    }

    /*
     * 走到这里说明数据库中出现了当前代码无法解释的状态值。
     */
    last_error_.clear();

    return rollback_business_result(
        CreateFriendRequestStatus::
            kRequestDataInconsistent,
        same_direction->request_id,
        "unsupported friend request state"
    );
}

AcceptFriendRequestResult
FriendRequestRepository::AcceptFriendRequest(
    std::uint64_t request_id,
    std::uint64_t handler_user_id
) {
    if (request_id == 0 ||
        handler_user_id == 0) {
        last_error_.clear();

        return MakeAcceptResult(
            AcceptFriendRequestStatus::kInvalidArgument,
            request_id,
            0,
            0,
            "invalid request id or handler user id"
        );
    }

    if (pool_ == nullptr) {
        SetError(
            "friend request repository accept failed: "
            "pool is null"
        );

        return MakeAcceptResult(
            AcceptFriendRequestStatus::kStorageError,
            request_id,
            0,
            0,
            last_error_
        );
    }

    auto connection = pool_->Acquire();

    if (!connection) {
        SetError(
            "friend request repository accept failed: "
            "acquire connection failed"
        );

        return MakeAcceptResult(
            AcceptFriendRequestStatus::kStorageError,
            request_id,
            0,
            0,
            last_error_
        );
    }

    MySqlConnection* database =
        connection.operator->();

    auto storage_error =
        [this, database, request_id](
            const std::string& error_message,
            std::uint64_t requester_user_id,
            std::uint64_t receiver_user_id
        ) -> AcceptFriendRequestResult {
            std::string final_error = error_message;

            if (database != nullptr &&
                database->InTransaction()) {
                if (!database->Rollback()) {
                    final_error +=
                        "; rollback failed: " +
                        database->LastError();
                }
            }

            SetError(final_error);

            return MakeAcceptResult(
                AcceptFriendRequestStatus::kStorageError,
                request_id,
                requester_user_id,
                receiver_user_id,
                last_error_
            );
        };

    auto business_result =
        [this, database, request_id](
            AcceptFriendRequestStatus status,
            std::uint64_t requester_user_id,
            std::uint64_t receiver_user_id,
            const std::string& message
        ) -> AcceptFriendRequestResult {
            if (database != nullptr &&
                database->InTransaction()) {
                if (!database->Rollback()) {
                    SetError(
                        "friend request repository "
                        "business rollback failed: " +
                        database->LastError()
                    );

                    return MakeAcceptResult(
                        AcceptFriendRequestStatus::
                            kStorageError,
                        request_id,
                        requester_user_id,
                        receiver_user_id,
                        last_error_
                    );
                }
            }

            last_error_.clear();

            return MakeAcceptResult(
                status,
                request_id,
                requester_user_id,
                receiver_user_id,
                message
            );
        };

    /*
     * 第一步：事务外初步读取申请。
     *
     * 这一查询只用于取得双方user_id，
     * 后面进入事务后还会再次FOR UPDATE查询。
     */
    MySqlQueryResult preview_result;

    const std::string preview_sql =
        "SELECT "
        "from_user_id, "
        "to_user_id "
        "FROM im_friend_requests "
        "WHERE request_id = " +
        std::to_string(request_id);

    if (!database->Query(
            preview_sql,
            &preview_result
        )) {
        return storage_error(
            database->LastError(),
            0,
            0
        );
    }

    if (preview_result.rows.empty()) {
        last_error_.clear();

        return MakeAcceptResult(
            AcceptFriendRequestStatus::kRequestNotFound,
            request_id,
            0,
            0,
            "friend request not found"
        );
    }

    if (preview_result.rows.size() != 1 ||
        preview_result.rows.front().size() < 2) {
        return storage_error(
            "friend request repository accept failed: "
            "invalid preview result",
            0,
            0
        );
    }

    std::uint64_t requester_user_id = 0;
    std::uint64_t receiver_user_id = 0;

    try {
        requester_user_id =
            ToUInt64(
                preview_result.rows.front()[0]
            );

        receiver_user_id =
            ToUInt64(
                preview_result.rows.front()[1]
            );
    } catch (const std::exception& e) {
        return storage_error(
            std::string(
                "friend request repository accept failed: "
                "parse preview result failed: "
            ) + e.what(),
            0,
            0
        );
    }

    if (receiver_user_id != handler_user_id) {
        last_error_.clear();

        return MakeAcceptResult(
            AcceptFriendRequestStatus::
                kNotRequestReceiver,
            request_id,
            requester_user_id,
            receiver_user_id,
            "current user is not request receiver"
        );
    }

    if (!database->BeginTransaction()) {
        SetError(database->LastError());

        return MakeAcceptResult(
            AcceptFriendRequestStatus::kStorageError,
            request_id,
            requester_user_id,
            receiver_user_id,
            last_error_
        );
    }

    /*
     * 第二步：按固定顺序锁定双方用户。
     */
    const std::uint64_t low_user_id =
        requester_user_id < receiver_user_id
            ? requester_user_id
            : receiver_user_id;

    const std::uint64_t high_user_id =
        requester_user_id < receiver_user_id
            ? receiver_user_id
            : requester_user_id;

    const std::string lock_users_sql =
        "SELECT user_id, status "
        "FROM im_users "
        "WHERE user_id IN (" +
        std::to_string(low_user_id) + ", " +
        std::to_string(high_user_id) + ") "
        "ORDER BY user_id ASC "
        "FOR UPDATE";

    MySqlQueryResult user_result;

    if (!database->Query(
            lock_users_sql,
            &user_result
        )) {
        return storage_error(
            database->LastError(),
            requester_user_id,
            receiver_user_id
        );
    }

    std::optional<std::uint32_t>
        requester_status;

    std::optional<std::uint32_t>
        receiver_status;

    try {
        for (const auto& row : user_result.rows) {
            if (row.size() < 2) {
                return storage_error(
                    "friend request repository accept failed: "
                    "invalid user row size",
                    requester_user_id,
                    receiver_user_id
                );
            }

            const std::uint64_t user_id =
                ToUInt64(row[0]);

            const std::uint32_t user_status =
                ToUInt32(row[1]);

            if (user_id == requester_user_id) {
                requester_status = user_status;
            } else if (
                user_id == receiver_user_id
            ) {
                receiver_status = user_status;
            }
        }
    } catch (const std::exception& e) {
        return storage_error(
            std::string(
                "friend request repository accept failed: "
                "parse user result failed: "
            ) + e.what(),
            requester_user_id,
            receiver_user_id
        );
    }

    if (!requester_status.has_value()) {
        return business_result(
            AcceptFriendRequestStatus::
                kRequesterNotFound,
            requester_user_id,
            receiver_user_id,
            "requester user not found"
        );
    }

    if (!receiver_status.has_value()) {
        return business_result(
            AcceptFriendRequestStatus::
                kReceiverNotFound,
            requester_user_id,
            receiver_user_id,
            "receiver user not found"
        );
    }

    /*
     * 第三步：重新锁定并读取申请。
     */
    const std::string lock_request_sql =
        "SELECT "
        "from_user_id, "
        "to_user_id, "
        "request_status "
        "FROM im_friend_requests "
        "WHERE request_id = " +
        std::to_string(request_id) + " "
        "FOR UPDATE";

    MySqlQueryResult request_result;

    if (!database->Query(
            lock_request_sql,
            &request_result
        )) {
        return storage_error(
            database->LastError(),
            requester_user_id,
            receiver_user_id
        );
    }

    if (request_result.rows.empty()) {
        return business_result(
            AcceptFriendRequestStatus::
                kRequestNotFound,
            requester_user_id,
            receiver_user_id,
            "friend request no longer exists"
        );
    }

    if (request_result.rows.size() != 1 ||
        request_result.rows.front().size() < 3) {
        return storage_error(
            "friend request repository accept failed: "
            "invalid request result",
            requester_user_id,
            receiver_user_id
        );
    }

    std::uint64_t locked_requester_id = 0;
    std::uint64_t locked_receiver_id = 0;
    std::uint32_t request_status = 0;

    try {
        locked_requester_id =
            ToUInt64(
                request_result.rows.front()[0]
            );

        locked_receiver_id =
            ToUInt64(
                request_result.rows.front()[1]
            );

        request_status =
            ToUInt32(
                request_result.rows.front()[2]
            );
    } catch (const std::exception& e) {
        return storage_error(
            std::string(
                "friend request repository accept failed: "
                "parse locked request failed: "
            ) + e.what(),
            requester_user_id,
            receiver_user_id
        );
    }

    if (locked_requester_id !=
            requester_user_id ||
        locked_receiver_id !=
            receiver_user_id) {
        return business_result(
            AcceptFriendRequestStatus::
                kRequestDataInconsistent,
            requester_user_id,
            receiver_user_id,
            "friend request direction changed"
        );
    }

    if (locked_receiver_id !=
        handler_user_id) {
        return business_result(
            AcceptFriendRequestStatus::
                kNotRequestReceiver,
            requester_user_id,
            receiver_user_id,
            "current user is not request receiver"
        );
    }

    /*
     * 第四步：锁定双方好友关系。
     */
    const std::string relation_sql =
        "SELECT "
        "user_id, "
        "peer_user_id, "
        "relation_status "
        "FROM im_user_relations "
        "WHERE "
        "(user_id = " +
        std::to_string(requester_user_id) +
        " AND peer_user_id = " +
        std::to_string(receiver_user_id) +
        ") "
        "OR "
        "(user_id = " +
        std::to_string(receiver_user_id) +
        " AND peer_user_id = " +
        std::to_string(requester_user_id) +
        ") "
        "ORDER BY user_id ASC, peer_user_id ASC "
        "FOR UPDATE";

    MySqlQueryResult relation_result;

    if (!database->Query(
            relation_sql,
            &relation_result
        )) {
        return storage_error(
            database->LastError(),
            requester_user_id,
            receiver_user_id
        );
    }

    std::optional<std::uint32_t>
        requester_relation;

    std::optional<std::uint32_t>
        receiver_relation;

    try {
        for (const auto& row :
             relation_result.rows) {
            if (row.size() < 3) {
                return storage_error(
                    "friend request repository accept failed: "
                    "invalid relation row size",
                    requester_user_id,
                    receiver_user_id
                );
            }

            const std::uint64_t user_id =
                ToUInt64(row[0]);

            const std::uint64_t peer_user_id =
                ToUInt64(row[1]);

            const std::uint32_t status =
                ToUInt32(row[2]);

            if (user_id == requester_user_id &&
                peer_user_id == receiver_user_id) {
                requester_relation = status;
            } else if (
                user_id == receiver_user_id &&
                peer_user_id == requester_user_id
            ) {
                receiver_relation = status;
            }
        }
    } catch (const std::exception& e) {
        return storage_error(
            std::string(
                "friend request repository accept failed: "
                "parse relation result failed: "
            ) + e.what(),
            requester_user_id,
            receiver_user_id
        );
    }

    const std::uint32_t friend_value =
        static_cast<std::uint32_t>(
            UserRelationStatus::kFriend
        );

    const std::uint32_t blocked_value =
        static_cast<std::uint32_t>(
            UserRelationStatus::kBlocked
        );

    const bool requester_is_friend =
        requester_relation.has_value() &&
        requester_relation.value() ==
            friend_value;

    const bool receiver_is_friend =
        receiver_relation.has_value() &&
        receiver_relation.value() ==
            friend_value;

    const bool requester_blocked_receiver =
        requester_relation.has_value() &&
        requester_relation.value() ==
            blocked_value;

    const bool receiver_blocked_requester =
        receiver_relation.has_value() &&
        receiver_relation.value() ==
            blocked_value;

    const std::uint32_t pending_value =
        static_cast<std::uint32_t>(
            FriendRequestStatus::kPending
        );

    const std::uint32_t accepted_value =
        static_cast<std::uint32_t>(
            FriendRequestStatus::kAccepted
        );

    /*
     * 重复同意：只有申请accepted且双向关系完整，
     * 才能视为幂等成功。
     */
    if (request_status == accepted_value) {
        if (requester_is_friend &&
            receiver_is_friend) {
            return business_result(
                AcceptFriendRequestStatus::
                    kAlreadyAccepted,
                requester_user_id,
                receiver_user_id,
                "friend request already accepted"
            );
        }

        return business_result(
            AcceptFriendRequestStatus::
                kRequestDataInconsistent,
            requester_user_id,
            receiver_user_id,
            "accepted request has incomplete "
            "friend relation"
        );
    }

    if (request_status != pending_value) {
        return business_result(
            AcceptFriendRequestStatus::
                kRequestNotPending,
            requester_user_id,
            receiver_user_id,
            "friend request is not pending"
        );
    }

    /*
     * friend与blocked同时出现在两个方向，
     * 属于关系数据不一致。
     */
    if ((requester_blocked_receiver &&
         receiver_is_friend) ||
        (receiver_blocked_requester &&
         requester_is_friend)) {
        return business_result(
            AcceptFriendRequestStatus::
                kRelationDataInconsistent,
            requester_user_id,
            receiver_user_id,
            "conflicting friend and blocked relation"
        );
    }

    /*
     * 以处理者receiver的视角返回blocked_by_self/peer。
     */
    if (receiver_blocked_requester) {
        return business_result(
            AcceptFriendRequestStatus::
                kBlockedBySelf,
            requester_user_id,
            receiver_user_id,
            "requester is blocked by receiver"
        );
    }

    if (requester_blocked_receiver) {
        return business_result(
            AcceptFriendRequestStatus::
                kBlockedByPeer,
            requester_user_id,
            receiver_user_id,
            "receiver is blocked by requester"
        );
    }

    if (requester_is_friend &&
        receiver_is_friend) {
        return business_result(
            AcceptFriendRequestStatus::
                kRequestDataInconsistent,
            requester_user_id,
            receiver_user_id,
            "pending request exists for existing friends"
        );
    }

    if (requester_is_friend !=
        receiver_is_friend) {
        return business_result(
            AcceptFriendRequestStatus::
                kRelationDataInconsistent,
            requester_user_id,
            receiver_user_id,
            "one-way friend relation detected"
        );
    }

    constexpr std::uint32_t
        kActiveUserStatus = 1;

    if (requester_status.value() !=
        kActiveUserStatus) {
        return business_result(
            AcceptFriendRequestStatus::
                kRequesterDisabled,
            requester_user_id,
            receiver_user_id,
            "requester user disabled"
        );
    }

    if (receiver_status.value() !=
        kActiveUserStatus) {
        return business_result(
            AcceptFriendRequestStatus::
                kReceiverDisabled,
            requester_user_id,
            receiver_user_id,
            "receiver user disabled"
        );
    }

    /*
     * 第五步：锁定反方向申请。
     *
     * 正常业务不会同时存在两个方向的pending，
     * 如果发现则拒绝继续扩大异常数据。
     */
    const std::string reverse_request_sql =
        "SELECT request_id, request_status "
        "FROM im_friend_requests "
        "WHERE from_user_id = " +
        std::to_string(receiver_user_id) +
        " AND to_user_id = " +
        std::to_string(requester_user_id) + " "
        "FOR UPDATE";

    MySqlQueryResult reverse_result;

    if (!database->Query(
            reverse_request_sql,
            &reverse_result
        )) {
        return storage_error(
            database->LastError(),
            requester_user_id,
            receiver_user_id
        );
    }

    if (!reverse_result.rows.empty()) {
        if (reverse_result.rows.size() != 1 ||
            reverse_result.rows.front().size() < 2) {
            return storage_error(
                "friend request repository accept failed: "
                "invalid reverse request result",
                requester_user_id,
                receiver_user_id
            );
        }

        try {
            const std::uint32_t reverse_status =
                ToUInt32(
                    reverse_result.rows.front()[1]
                );

            if (reverse_status ==
                    pending_value ||
                reverse_status ==
                    accepted_value) {
                return business_result(
                    AcceptFriendRequestStatus::
                        kRequestDataInconsistent,
                    requester_user_id,
                    receiver_user_id,
                    "conflicting reverse friend request"
                );
            }
        } catch (const std::exception& e) {
            return storage_error(
                std::string(
                    "friend request repository accept failed: "
                    "parse reverse request failed: "
                ) + e.what(),
                requester_user_id,
                receiver_user_id
            );
        }
    }

    /*
     * 第六步：原子写入双向好友关系。
     */
    const std::string insert_relations_sql =
        "INSERT INTO im_user_relations ("
        "user_id, "
        "peer_user_id, "
        "relation_status"
        ") VALUES ("
        + std::to_string(requester_user_id)
        + ", "
        + std::to_string(receiver_user_id)
        + ", "
        + std::to_string(friend_value)
        + "), ("
        + std::to_string(receiver_user_id)
        + ", "
        + std::to_string(requester_user_id)
        + ", "
        + std::to_string(friend_value)
        + ")";

    if (!database->Execute(
            insert_relations_sql
        )) {
        return storage_error(
            database->LastError(),
            requester_user_id,
            receiver_user_id
        );
    }

    if (database->AffectedRows() != 2) {
        return storage_error(
            "friend request repository accept failed: "
            "unexpected relation affected rows",
            requester_user_id,
            receiver_user_id
        );
    }

    /*
     * 第七步：更新申请状态。
     *
     * WHERE中再次限制request_status=pending，
     * 是最后一道并发保护。
     */
    const std::string update_request_sql =
        "UPDATE im_friend_requests "
        "SET "
        "request_status = " +
        std::to_string(accepted_value) + ", "
        "handled_at = CURRENT_TIMESTAMP "
        "WHERE request_id = " +
        std::to_string(request_id) + " "
        "AND to_user_id = " +
        std::to_string(handler_user_id) + " "
        "AND request_status = " +
        std::to_string(pending_value);

    if (!database->Execute(
            update_request_sql
        )) {
        return storage_error(
            database->LastError(),
            requester_user_id,
            receiver_user_id
        );
    }

    if (database->AffectedRows() != 1) {
        return storage_error(
            "friend request repository accept failed: "
            "unexpected request affected rows",
            requester_user_id,
            receiver_user_id
        );
    }

    if (!database->Commit()) {
        return storage_error(
            database->LastError(),
            requester_user_id,
            receiver_user_id
        );
    }

    last_error_.clear();

    return MakeAcceptResult(
        AcceptFriendRequestStatus::kAccepted,
        request_id,
        requester_user_id,
        receiver_user_id,
        "friend request accepted"
    );
}


RejectFriendRequestResult
FriendRequestRepository::RejectFriendRequest(
    std::uint64_t request_id,
    std::uint64_t handler_user_id
) {
    if (request_id == 0 ||
        handler_user_id == 0) {
        last_error_.clear();

        return MakeRejectResult(
            RejectFriendRequestStatus::kInvalidArgument,
            request_id,
            0,
            0,
            "invalid request id or handler user id"
        );
    }

    if (pool_ == nullptr) {
        SetError(
            "friend request repository reject failed: "
            "pool is null"
        );

        return MakeRejectResult(
            RejectFriendRequestStatus::kStorageError,
            request_id,
            0,
            0,
            last_error_
        );
    }

    auto connection = pool_->Acquire();

    if (!connection) {
        SetError(
            "friend request repository reject failed: "
            "acquire connection failed"
        );

        return MakeRejectResult(
            RejectFriendRequestStatus::kStorageError,
            request_id,
            0,
            0,
            last_error_
        );
    }

    MySqlConnection* database =
        connection.operator->();

    auto storage_error =
        [this, database, request_id](
            const std::string& error_message,
            std::uint64_t requester_user_id,
            std::uint64_t receiver_user_id
        ) -> RejectFriendRequestResult {
            std::string final_error = error_message;

            if (database != nullptr &&
                database->InTransaction()) {
                if (!database->Rollback()) {
                    final_error +=
                        "; rollback failed: " +
                        database->LastError();
                }
            }

            SetError(final_error);

            return MakeRejectResult(
                RejectFriendRequestStatus::kStorageError,
                request_id,
                requester_user_id,
                receiver_user_id,
                last_error_
            );
        };

    auto business_result =
        [this, database, request_id](
            RejectFriendRequestStatus status,
            std::uint64_t requester_user_id,
            std::uint64_t receiver_user_id,
            const std::string& message
        ) -> RejectFriendRequestResult {
            if (database != nullptr &&
                database->InTransaction()) {
                if (!database->Rollback()) {
                    SetError(
                        "friend request repository "
                        "reject rollback failed: " +
                        database->LastError()
                    );

                    return MakeRejectResult(
                        RejectFriendRequestStatus::
                            kStorageError,
                        request_id,
                        requester_user_id,
                        receiver_user_id,
                        last_error_
                    );
                }
            }

            last_error_.clear();

            return MakeRejectResult(
                status,
                request_id,
                requester_user_id,
                receiver_user_id,
                message
            );
        };

    if (!database->BeginTransaction()) {
        SetError(database->LastError());

        return MakeRejectResult(
            RejectFriendRequestStatus::kStorageError,
            request_id,
            0,
            0,
            last_error_
        );
    }

    /*
     * 锁定申请记录。
     *
     * AcceptFriendRequest最终也会锁定同一条申请，
     * 所以同意和拒绝不会同时处理成功。
     */
    const std::string lock_request_sql =
        "SELECT "
        "from_user_id, "
        "to_user_id, "
        "request_status "
        "FROM im_friend_requests "
        "WHERE request_id = " +
        std::to_string(request_id) + " "
        "FOR UPDATE";

    MySqlQueryResult request_result;

    if (!database->Query(
            lock_request_sql,
            &request_result
        )) {
        return storage_error(
            database->LastError(),
            0,
            0
        );
    }

    if (request_result.rows.empty()) {
        return business_result(
            RejectFriendRequestStatus::kRequestNotFound,
            0,
            0,
            "friend request not found"
        );
    }

    if (request_result.rows.size() != 1 ||
        request_result.rows.front().size() < 3) {
        return storage_error(
            "friend request repository reject failed: "
            "invalid request result",
            0,
            0
        );
    }

    std::uint64_t requester_user_id = 0;
    std::uint64_t receiver_user_id = 0;
    std::uint32_t request_status = 0;

    try {
        requester_user_id =
            ToUInt64(
                request_result.rows.front()[0]
            );

        receiver_user_id =
            ToUInt64(
                request_result.rows.front()[1]
            );

        request_status =
            ToUInt32(
                request_result.rows.front()[2]
            );
    } catch (const std::exception& e) {
        return storage_error(
            std::string(
                "friend request repository reject failed: "
                "parse request failed: "
            ) + e.what(),
            0,
            0
        );
    }

    /*
     * 只有申请接收者有权限拒绝。
     */
    if (receiver_user_id !=
        handler_user_id) {
        return business_result(
            RejectFriendRequestStatus::
                kNotRequestReceiver,
            requester_user_id,
            receiver_user_id,
            "current user is not request receiver"
        );
    }

    const std::uint32_t pending_value =
        static_cast<std::uint32_t>(
            FriendRequestStatus::kPending
        );

    const std::uint32_t accepted_value =
        static_cast<std::uint32_t>(
            FriendRequestStatus::kAccepted
        );

    const std::uint32_t rejected_value =
        static_cast<std::uint32_t>(
            FriendRequestStatus::kRejected
        );

    const std::uint32_t cancelled_value =
        static_cast<std::uint32_t>(
            FriendRequestStatus::kCancelled
        );

    /*
     * 重复拒绝属于幂等业务结果。
     */
    if (request_status == rejected_value) {
        return business_result(
            RejectFriendRequestStatus::
                kAlreadyRejected,
            requester_user_id,
            receiver_user_id,
            "friend request already rejected"
        );
    }

    /*
     * 已经同意后不能再拒绝。
     */
    if (request_status == accepted_value) {
        return business_result(
            RejectFriendRequestStatus::
                kAlreadyAccepted,
            requester_user_id,
            receiver_user_id,
            "friend request already accepted"
        );
    }

    /*
     * 发起者已经撤销，接收方不能继续处理。
     */
    if (request_status == cancelled_value) {
        return business_result(
            RejectFriendRequestStatus::
                kRequestCancelled,
            requester_user_id,
            receiver_user_id,
            "friend request already cancelled"
        );
    }

    if (request_status != pending_value) {
        return business_result(
            RejectFriendRequestStatus::
                kRequestDataInconsistent,
            requester_user_id,
            receiver_user_id,
            "unsupported friend request status"
        );
    }

    /*
     * 最终更新仍限制request_status=pending。
     * 即使未来修改前面代码，也不会覆盖已处理状态。
     */
    const std::string update_sql =
        "UPDATE im_friend_requests "
        "SET "
        "request_status = " +
        std::to_string(rejected_value) + ", "
        "handled_at = CURRENT_TIMESTAMP "
        "WHERE request_id = " +
        std::to_string(request_id) + " "
        "AND to_user_id = " +
        std::to_string(handler_user_id) + " "
        "AND request_status = " +
        std::to_string(pending_value);

    if (!database->Execute(update_sql)) {
        return storage_error(
            database->LastError(),
            requester_user_id,
            receiver_user_id
        );
    }

    if (database->AffectedRows() != 1) {
        return storage_error(
            "friend request repository reject failed: "
            "unexpected affected rows",
            requester_user_id,
            receiver_user_id
        );
    }

    if (!database->Commit()) {
        return storage_error(
            database->LastError(),
            requester_user_id,
            receiver_user_id
        );
    }

    last_error_.clear();

    return MakeRejectResult(
        RejectFriendRequestStatus::kRejected,
        request_id,
        requester_user_id,
        receiver_user_id,
        "friend request rejected"
    );
}

std::vector<FriendRequestRecord>
FriendRequestRepository::ListPendingIncomingRequests(
    std::uint64_t receiver_user_id,
    const std::string& before_created_at,
    std::uint64_t before_request_id,
    std::size_t limit
) {
    std::vector<FriendRequestRecord> empty;

    if (receiver_user_id == 0) {
        SetError(
            "friend request repository list failed: "
            "invalid receiver user id"
        );
        return empty;
    }

    const bool first_page =
        before_created_at.empty() &&
        before_request_id == 0;

    const bool next_page =
        !before_created_at.empty() &&
        before_request_id != 0;

    if (!first_page && !next_page) {
        SetError(
            "friend request repository list failed: "
            "invalid pagination cursor"
        );
        return empty;
    }

    if (next_page &&
        !IsDateTimeCursorValid(
            before_created_at
        )) {
        SetError(
            "friend request repository list failed: "
            "invalid before_created_at"
        );
        return empty;
    }

    if (limit == 0) {
        last_error_.clear();
        return empty;
    }

    if (limit > 100) {
        limit = 100;
    }

    if (pool_ == nullptr) {
        SetError(
            "friend request repository list failed: "
            "pool is null"
        );
        return empty;
    }

    auto connection = pool_->Acquire();

    if (!connection) {
        SetError(
            "friend request repository list failed: "
            "acquire connection failed"
        );
        return empty;
    }

    const std::uint32_t pending_value =
        static_cast<std::uint32_t>(
            FriendRequestStatus::kPending
        );

    std::string cursor_condition;

    if (next_page) {
        const std::string escaped_created_at =
            connection->EscapeString(
                before_created_at
            );

        cursor_condition =
            "AND ("
            "fr.created_at < '" +
            escaped_created_at + "' "
            "OR ("
            "fr.created_at = '" +
            escaped_created_at + "' "
            "AND fr.request_id < " +
            std::to_string(before_request_id) +
            ")"
            ") ";
    }

    const std::string sql =
        "SELECT "
        "fr.request_id, "
        "fr.from_user_id, "
        "fr.to_user_id, "
        "fr.request_message, "
        "fr.request_status, "
        "DATE_FORMAT("
        "fr.created_at, "
        "'%Y-%m-%d %H:%i:%s'"
        "), "
        "IFNULL("
        "DATE_FORMAT("
        "fr.handled_at, "
        "'%Y-%m-%d %H:%i:%s'"
        "), "
        "''"
        "), "
        "DATE_FORMAT("
        "fr.updated_at, "
        "'%Y-%m-%d %H:%i:%s'"
        "), "
        "u.username, "
        "COALESCE(u.nickname, ''), "
        "COALESCE(u.avatar_url, ''), "
        "u.status "
        "FROM im_friend_requests AS fr "
        "INNER JOIN im_users AS u "
        "ON u.user_id = fr.from_user_id "
        "WHERE fr.to_user_id = " +
        std::to_string(receiver_user_id) + " "
        "AND fr.request_status = " +
        std::to_string(pending_value) + " " +
        cursor_condition +
        "ORDER BY "
        "fr.created_at DESC, "
        "fr.request_id DESC "
        "LIMIT " +
        std::to_string(limit);

    MySqlQueryResult result;

    if (!connection->Query(sql, &result)) {
        SetError(connection->LastError());
        return empty;
    }

    auto records =
        BuildFriendRequestRecords(result);

    if (!last_error_.empty()) {
        return empty;
    }

    last_error_.clear();
    return records;
}

std::vector<FriendRequestRecord>
FriendRequestRepository::BuildFriendRequestRecords(
    const MySqlQueryResult& result
) {
    std::vector<FriendRequestRecord> records;
    records.reserve(result.rows.size());

    try {
        for (const auto& row : result.rows) {
            if (row.size() < 12) {
                SetError(
                    "friend request repository build failed: "
                    "invalid row size"
                );
                return {};
            }

            FriendRequestRecord record;

            record.request_id =
                ToUInt64(row[0]);

            record.from_user_id =
                ToUInt64(row[1]);

            record.to_user_id =
                ToUInt64(row[2]);

            record.request_message =
                row[3];

            const std::uint32_t status =
                ToUInt32(row[4]);

            if (status !=
                static_cast<std::uint32_t>(
                    FriendRequestStatus::kPending
                )) {
                SetError(
                    "friend request repository build failed: "
                    "unexpected request status"
                );
                return {};
            }

            record.request_status =
                FriendRequestStatus::kPending;

            record.created_at = row[5];
            record.handled_at = row[6];
            record.updated_at = row[7];

            record.from_username = row[8];
            record.from_nickname = row[9];
            record.from_avatar_url = row[10];

            record.from_user_status =
                ToUInt32(row[11]);

            if (record.request_id == 0 ||
                record.from_user_id == 0 ||
                record.to_user_id == 0 ||
                record.created_at.empty()) {
                SetError(
                    "friend request repository build failed: "
                    "invalid record data"
                );
                return {};
            }

            records.push_back(
                std::move(record)
            );
        }
    } catch (const std::exception& e) {
        SetError(
            std::string(
                "friend request repository build failed: "
            ) + e.what()
        );
        return {};
    }

    last_error_.clear();
    return records;
}

const std::string&
FriendRequestRepository::LastError() const {
    return last_error_;
}

void FriendRequestRepository::SetError(
    const std::string& error_message
) {
    last_error_ = error_message;
    LOG_ERROR(error_message);
}

}  // namespace tinyimx