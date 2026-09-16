#include "services/repository/GroupRepository.h"

#include "common/db/MySqlConnection.h"

#include <exception>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace tinyimx {
namespace {

std::uint64_t ToUInt64(const std::string& value) {
    return value.empty() ? 0U : static_cast<std::uint64_t>(std::stoull(value));
}

std::uint32_t ToUInt32(const std::string& value) {
    return value.empty() ? 0U : static_cast<std::uint32_t>(std::stoul(value));
}

GroupFindResult ParseGroupResult(const MySqlQueryResult& query) {
    GroupFindResult result;
    result.status = GroupRepositoryStatus::kSucceeded;
    if (query.rows.empty()) {
        result.found = false;
        result.message = "group not found";
        return result;
    }
    if (query.rows.size() != 1 || query.rows.front().size() < 14) {
        result.status = GroupRepositoryStatus::kInvalidRecord;
        result.message = "invalid group row shape";
        return result;
    }

    try {
        const auto& row = query.rows.front();
        GroupRecord record;
        record.group_id = ToUInt64(row[0]);
        record.name = row[1];
        record.description = row[2];
        record.avatar_url = row[3];
        record.owner_user_id = ToUInt64(row[4]);
        record.status = ToUInt32(row[5]);
        record.join_policy = ToUInt32(row[6]);
        record.max_members = ToUInt32(row[7]);
        record.version = ToUInt64(row[8]);
        record.member_version = ToUInt64(row[9]);
        record.created_at = row[10];
        record.updated_at = row[11];
        record.disbanded_at = row[12];
        record.disbanded_by_user_id = ToUInt64(row[13]);

        if (record.group_id == 0 ||
            record.owner_user_id == 0 ||
            record.name.empty() ||
            (record.status != 1 && record.status != 2) ||
            (record.join_policy != 1 && record.join_policy != 2) ||
            record.max_members < 2 || record.max_members > 5000 ||
            record.version == 0 || record.member_version == 0) {
            result.status = GroupRepositoryStatus::kInvalidRecord;
            result.message = "group row violates domain invariants";
            return result;
        }

        result.found = true;
        result.record = std::move(record);
        result.message = "group queried";
        return result;
    } catch (const std::exception& error) {
        result.status = GroupRepositoryStatus::kInvalidRecord;
        result.message = std::string("failed to parse group row: ") + error.what();
        return result;
    }
}

GroupMemberFindResult ParseMemberResult(const MySqlQueryResult& query) {
    GroupMemberFindResult result;
    result.status = GroupRepositoryStatus::kSucceeded;
    if (query.rows.empty()) {
        result.found = false;
        result.message = "group member not found";
        return result;
    }
    if (query.rows.size() != 1 || query.rows.front().size() < 9) {
        result.status = GroupRepositoryStatus::kInvalidRecord;
        result.message = "invalid group member row shape";
        return result;
    }

    try {
        const auto& row = query.rows.front();
        GroupMemberRecord record;
        record.group_id = ToUInt64(row[0]);
        record.user_id = ToUInt64(row[1]);
        record.role = ToUInt32(row[2]);
        record.status = ToUInt32(row[3]);
        record.membership_epoch = ToUInt64(row[4]);
        record.muted_until = row[5];
        record.joined_at = row[6];
        record.left_at = row[7];
        record.updated_at = row[8];

        if (record.group_id == 0 || record.user_id == 0 ||
            record.role < 1 || record.role > 3 ||
            record.status < 1 || record.status > 3 ||
            record.membership_epoch == 0) {
            result.status = GroupRepositoryStatus::kInvalidRecord;
            result.message = "group member row violates domain invariants";
            return result;
        }

        result.found = true;
        result.record = std::move(record);
        result.message = "group member queried";
        return result;
    } catch (const std::exception& error) {
        result.status = GroupRepositoryStatus::kInvalidRecord;
        result.message = std::string("failed to parse group member row: ") + error.what();
        return result;
    }
}

GroupOperationFindResult ParseOperationResult(const MySqlQueryResult& query) {
    GroupOperationFindResult result;
    result.status = GroupRepositoryStatus::kSucceeded;
    if (query.rows.empty()) {
        result.found = false;
        result.message = "group operation not found";
        return result;
    }
    if (query.rows.size() != 1 || query.rows.front().size() < 8) {
        result.status = GroupRepositoryStatus::kInvalidRecord;
        result.message = "invalid group operation row shape";
        return result;
    }

    try {
        const auto& row = query.rows.front();
        GroupOperationRecord record;
        record.actor_user_id = ToUInt64(row[0]);
        record.client_operation_id = row[1];
        record.operation_type = row[2];
        record.request_fingerprint = row[3];
        record.group_id = ToUInt64(row[4]);
        record.result_version = ToUInt64(row[5]);
        record.result_member_version = ToUInt64(row[6]);
        record.result_membership_epoch = ToUInt64(row[7]);

        if (record.actor_user_id == 0 || record.client_operation_id.empty() ||
            record.operation_type.empty() || record.request_fingerprint.size() != 64 ||
            record.group_id == 0 || record.result_version == 0 ||
            record.result_member_version == 0) {
            result.status = GroupRepositoryStatus::kInvalidRecord;
            result.message = "group operation row violates domain invariants";
            return result;
        }

        result.found = true;
        result.record = std::move(record);
        result.message = "group operation queried";
        return result;
    } catch (const std::exception& error) {
        result.status = GroupRepositoryStatus::kInvalidRecord;
        result.message = std::string("failed to parse group operation row: ") + error.what();
        return result;
    }
}

GroupMutationStorageResult ExecuteMutation(
    MySqlConnection* connection,
    const std::string& sql,
    const std::string& action
) {
    GroupMutationStorageResult result;
    if (connection == nullptr) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = action + " failed: connection is null";
        return result;
    }
    if (!connection->Execute(sql)) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = action + " failed: " + connection->LastError();
        return result;
    }
    result.status = GroupRepositoryStatus::kSucceeded;
    result.affected_rows = connection->AffectedRows();
    result.message = action + " succeeded";
    return result;
}

}  // namespace

GroupRepository::GroupRepository(MySqlConnectionPool* pool)
    : pool_(pool) {
}

GroupFindResult GroupRepository::FindGroupById(std::uint64_t group_id) {
    GroupFindResult result;
    if (pool_ == nullptr || group_id == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindGroupById arguments";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "FindGroupById failed to acquire connection";
        return result;
    }
    return FindGroupByIdOnConnection(connection.operator->(), group_id, false);
}

GroupOperationFindResult GroupRepository::FindOperation(
    std::uint64_t actor_user_id,
    const std::string& client_operation_id
) {
    GroupOperationFindResult result;
    if (pool_ == nullptr || actor_user_id == 0 || client_operation_id.empty()) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindOperation arguments";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "FindOperation failed to acquire connection";
        return result;
    }
    return FindOperationOnConnection(
        connection.operator->(), actor_user_id, client_operation_id, false
    );
}

GroupFindResult GroupRepository::FindGroupByIdOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    bool for_update
) {
    GroupFindResult result;
    if (connection == nullptr || group_id == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindGroupByIdOnConnection arguments";
        return result;
    }

    std::string sql =
        "SELECT group_id, name, description, avatar_url, owner_user_id, "
        "status, join_policy, max_members, version, member_version, "
        "created_at, updated_at, IFNULL(disbanded_at, ''), "
        "IFNULL(disbanded_by_user_id, 0) "
        "FROM im_groups WHERE group_id = " + std::to_string(group_id) + " LIMIT 1";
    if (for_update) {
        sql += " FOR UPDATE";
    }

    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "FindGroupByIdOnConnection query failed: " + connection->LastError();
        return result;
    }
    return ParseGroupResult(query);
}

GroupMemberFindResult GroupRepository::FindMemberOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t user_id,
    bool for_update
) {
    GroupMemberFindResult result;
    if (connection == nullptr || group_id == 0 || user_id == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindMemberOnConnection arguments";
        return result;
    }

    std::string sql =
        "SELECT group_id, user_id, role, status, membership_epoch, "
        "IFNULL(muted_until, ''), joined_at, IFNULL(left_at, ''), updated_at "
        "FROM im_group_members WHERE group_id = " + std::to_string(group_id) +
        " AND user_id = " + std::to_string(user_id) + " LIMIT 1";
    if (for_update) {
        sql += " FOR UPDATE";
    }

    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "FindMemberOnConnection query failed: " + connection->LastError();
        return result;
    }
    return ParseMemberResult(query);
}

GroupOperationFindResult GroupRepository::FindOperationOnConnection(
    MySqlConnection* connection,
    std::uint64_t actor_user_id,
    const std::string& client_operation_id,
    bool for_update
) {
    GroupOperationFindResult result;
    if (connection == nullptr || actor_user_id == 0 || client_operation_id.empty()) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindOperationOnConnection arguments";
        return result;
    }

    const std::string escaped = connection->EscapeString(client_operation_id);
    std::string sql =
        "SELECT actor_user_id, client_operation_id, operation_type, "
        "request_fingerprint, group_id, result_version, result_member_version, "
        "IFNULL(result_membership_epoch, 0) FROM im_group_operation_dedup "
        "WHERE actor_user_id = " + std::to_string(actor_user_id) +
        " AND client_operation_id = '" + escaped + "' LIMIT 1";
    if (for_update) {
        sql += " FOR UPDATE";
    }

    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "FindOperationOnConnection query failed: " + connection->LastError();
        return result;
    }
    return ParseOperationResult(query);
}

GroupBooleanResult GroupRepository::UserExistsOnConnection(
    MySqlConnection* connection,
    std::uint64_t user_id
) {
    GroupBooleanResult result;
    if (connection == nullptr || user_id == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid UserExistsOnConnection arguments";
        return result;
    }
    MySqlQueryResult query;
    const std::string sql =
        "SELECT user_id FROM im_users WHERE user_id = " + std::to_string(user_id) + " LIMIT 1";
    if (!connection->Query(sql, &query)) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "UserExistsOnConnection query failed: " + connection->LastError();
        return result;
    }
    result.status = GroupRepositoryStatus::kSucceeded;
    result.value = !query.rows.empty();
    result.message = result.value ? "user exists" : "user not found";
    return result;
}

GroupMutationStorageResult GroupRepository::InsertGroupOnConnection(
    MySqlConnection* connection,
    std::uint64_t owner_user_id,
    const std::string& name,
    const std::string& description,
    const std::string& avatar_url,
    std::uint32_t join_policy,
    std::uint32_t max_members
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || owner_user_id == 0 || name.empty() ||
        (join_policy != 1 && join_policy != 2) || max_members < 2 || max_members > 5000) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid InsertGroupOnConnection arguments";
        return result;
    }

    const std::string sql =
        "INSERT INTO im_groups (name, description, avatar_url, owner_user_id, status, "
        "join_policy, max_members, version, member_version) VALUES ('" +
        connection->EscapeString(name) + "', '" + connection->EscapeString(description) +
        "', '" + connection->EscapeString(avatar_url) + "', " +
        std::to_string(owner_user_id) + ", 1, " + std::to_string(join_policy) + ", " +
        std::to_string(max_members) + ", 1, 1)";

    result = ExecuteMutation(connection, sql, "insert group");
    if (result.Succeeded()) {
        result.group_id = connection->LastInsertId();
        if (result.group_id == 0) {
            result.status = GroupRepositoryStatus::kInvalidRecord;
            result.message = "insert group returned zero group_id";
        }
    }
    return result;
}

GroupMutationStorageResult GroupRepository::InsertOwnerMemberOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t owner_user_id
) {
    if (connection == nullptr || group_id == 0 || owner_user_id == 0) {
        GroupMutationStorageResult result;
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid InsertOwnerMemberOnConnection arguments";
        return result;
    }
    const std::string sql =
        "INSERT INTO im_group_members (group_id, user_id, role, status, membership_epoch) VALUES (" +
        std::to_string(group_id) + ", " + std::to_string(owner_user_id) + ", 1, 1, 1)";
    return ExecuteMutation(connection, sql, "insert owner group member");
}

GroupMutationStorageResult GroupRepository::InsertMembershipHistoryOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t user_id,
    std::uint64_t membership_epoch,
    std::uint32_t role_at_join
) {
    if (connection == nullptr || group_id == 0 || user_id == 0 ||
        membership_epoch == 0 || role_at_join < 1 || role_at_join > 3) {
        GroupMutationStorageResult result;
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid InsertMembershipHistoryOnConnection arguments";
        return result;
    }
    const std::string sql =
        "INSERT INTO im_group_membership_history "
        "(group_id, user_id, membership_epoch, role_at_join) VALUES (" +
        std::to_string(group_id) + ", " + std::to_string(user_id) + ", " +
        std::to_string(membership_epoch) + ", " + std::to_string(role_at_join) + ")";
    return ExecuteMutation(connection, sql, "insert group membership history");
}

GroupMutationStorageResult GroupRepository::UpdateGroupOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    bool update_name,
    const std::string& name,
    bool update_description,
    const std::string& description,
    bool update_avatar_url,
    const std::string& avatar_url,
    bool update_join_policy,
    std::uint32_t join_policy,
    std::uint64_t new_version
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || group_id == 0 || new_version == 0 ||
        (!update_name && !update_description && !update_avatar_url && !update_join_policy) ||
        (update_join_policy && join_policy != 1 && join_policy != 2)) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid UpdateGroupOnConnection arguments";
        return result;
    }

    std::vector<std::string> clauses;
    if (update_name) {
        clauses.push_back("name = '" + connection->EscapeString(name) + "'");
    }
    if (update_description) {
        clauses.push_back("description = '" + connection->EscapeString(description) + "'");
    }
    if (update_avatar_url) {
        clauses.push_back("avatar_url = '" + connection->EscapeString(avatar_url) + "'");
    }
    if (update_join_policy) {
        clauses.push_back("join_policy = " + std::to_string(join_policy));
    }
    clauses.push_back("version = " + std::to_string(new_version));
    clauses.push_back("updated_at = CURRENT_TIMESTAMP(3)");

    std::ostringstream sql;
    sql << "UPDATE im_groups SET ";
    for (std::size_t i = 0; i < clauses.size(); ++i) {
        if (i != 0) {
            sql << ", ";
        }
        sql << clauses[i];
    }
    sql << " WHERE group_id = " << group_id << " AND status = 1";

    result = ExecuteMutation(connection, sql.str(), "update group");
    result.group_id = group_id;
    return result;
}

GroupMutationStorageResult GroupRepository::DisbandGroupOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t actor_user_id,
    std::uint64_t new_version,
    std::uint64_t new_member_version
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || group_id == 0 || actor_user_id == 0 ||
        new_version == 0 || new_member_version == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid DisbandGroupOnConnection arguments";
        return result;
    }
    const std::string sql =
        "UPDATE im_groups SET status = 2, version = " + std::to_string(new_version) +
        ", member_version = " + std::to_string(new_member_version) +
        ", disbanded_at = CURRENT_TIMESTAMP(3), disbanded_by_user_id = " +
        std::to_string(actor_user_id) +
        ", updated_at = CURRENT_TIMESTAMP(3) WHERE group_id = " +
        std::to_string(group_id) + " AND status = 1";
    result = ExecuteMutation(connection, sql, "disband group");
    result.group_id = group_id;
    return result;
}

GroupMutationStorageResult GroupRepository::InsertOperationOnConnection(
    MySqlConnection* connection,
    const GroupOperationRecord& operation
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || operation.actor_user_id == 0 ||
        operation.client_operation_id.empty() || operation.operation_type.empty() ||
        operation.request_fingerprint.size() != 64 || operation.group_id == 0 ||
        operation.result_version == 0 || operation.result_member_version == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid InsertOperationOnConnection arguments";
        return result;
    }

    std::string membership_epoch_sql = "NULL";
    if (operation.result_membership_epoch != 0) {
        membership_epoch_sql = std::to_string(operation.result_membership_epoch);
    }

    const std::string sql =
        "INSERT INTO im_group_operation_dedup "
        "(actor_user_id, client_operation_id, operation_type, request_fingerprint, group_id, "
        "result_version, result_member_version, result_membership_epoch) VALUES (" +
        std::to_string(operation.actor_user_id) + ", '" +
        connection->EscapeString(operation.client_operation_id) + "', '" +
        connection->EscapeString(operation.operation_type) + "', '" +
        connection->EscapeString(operation.request_fingerprint) + "', " +
        std::to_string(operation.group_id) + ", " +
        std::to_string(operation.result_version) + ", " +
        std::to_string(operation.result_member_version) + ", " + membership_epoch_sql + ")";

    result = ExecuteMutation(connection, sql, "insert group operation dedup");
    result.group_id = operation.group_id;
    return result;
}


GroupCountResult GroupRepository::CountActiveMembersOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id
) {
    GroupCountResult result;
    if (connection == nullptr || group_id == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid CountActiveMembersOnConnection arguments";
        return result;
    }
    MySqlQueryResult query;
    const std::string sql =
        "SELECT COUNT(*) FROM im_group_members WHERE group_id = " +
        std::to_string(group_id) + " AND status = 1";
    if (!connection->Query(sql, &query)) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "CountActiveMembersOnConnection query failed: " + connection->LastError();
        return result;
    }
    if (query.rows.size() != 1 || query.rows.front().size() != 1) {
        result.status = GroupRepositoryStatus::kInvalidRecord;
        result.message = "invalid active-member count row shape";
        return result;
    }
    try {
        result.value = ToUInt64(query.rows.front().front());
        result.status = GroupRepositoryStatus::kSucceeded;
        result.message = "active member count queried";
    } catch (const std::exception& error) {
        result.status = GroupRepositoryStatus::kInvalidRecord;
        result.message = std::string("failed to parse active-member count: ") + error.what();
    }
    return result;
}

GroupBooleanResult GroupRepository::IsMemberMuteActiveOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t user_id
) {
    GroupBooleanResult result;
    if (connection == nullptr || group_id == 0 || user_id == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid IsMemberMuteActiveOnConnection arguments";
        return result;
    }
    MySqlQueryResult query;
    const std::string sql =
        "SELECT CASE WHEN muted_until IS NOT NULL AND muted_until > UTC_TIMESTAMP(3) "
        "THEN 1 ELSE 0 END FROM im_group_members WHERE group_id = " +
        std::to_string(group_id) + " AND user_id = " + std::to_string(user_id) + " LIMIT 1";
    if (!connection->Query(sql, &query)) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "IsMemberMuteActiveOnConnection query failed: " + connection->LastError();
        return result;
    }
    if (query.rows.empty()) {
        result.status = GroupRepositoryStatus::kNotFound;
        result.message = "group member not found for mute check";
        return result;
    }
    if (query.rows.size() != 1 || query.rows.front().size() != 1) {
        result.status = GroupRepositoryStatus::kInvalidRecord;
        result.message = "invalid member mute row shape";
        return result;
    }
    result.status = GroupRepositoryStatus::kSucceeded;
    result.value = query.rows.front().front() == "1";
    result.message = result.value ? "member mute is active" : "member mute is inactive";
    return result;
}

GroupMutationStorageResult GroupRepository::InsertMemberOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t user_id,
    std::uint32_t role,
    std::uint64_t membership_epoch
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || group_id == 0 || user_id == 0 ||
        role < 1 || role > 3 || membership_epoch == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid InsertMemberOnConnection arguments";
        return result;
    }
    const std::string sql =
        "INSERT INTO im_group_members "
        "(group_id, user_id, role, status, membership_epoch, muted_until, joined_at, left_at) VALUES (" +
        std::to_string(group_id) + ", " + std::to_string(user_id) + ", " +
        std::to_string(role) + ", 1, " + std::to_string(membership_epoch) +
        ", NULL, CURRENT_TIMESTAMP(3), NULL)";
    result = ExecuteMutation(connection, sql, "insert group member");
    result.group_id = group_id;
    return result;
}

GroupMutationStorageResult GroupRepository::ReactivateMemberOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t user_id,
    std::uint64_t membership_epoch,
    std::uint32_t role
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || group_id == 0 || user_id == 0 ||
        membership_epoch == 0 || role < 1 || role > 3) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid ReactivateMemberOnConnection arguments";
        return result;
    }
    const std::string sql =
        "UPDATE im_group_members SET role = " + std::to_string(role) +
        ", status = 1, membership_epoch = " + std::to_string(membership_epoch) +
        ", muted_until = NULL, joined_at = CURRENT_TIMESTAMP(3), left_at = NULL, "
        "updated_at = CURRENT_TIMESTAMP(3) WHERE group_id = " + std::to_string(group_id) +
        " AND user_id = " + std::to_string(user_id) + " AND status IN (2, 3)";
    result = ExecuteMutation(connection, sql, "reactivate group member");
    result.group_id = group_id;
    return result;
}

GroupMutationStorageResult GroupRepository::CloseMembershipHistoryOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t user_id,
    std::uint64_t membership_epoch,
    std::uint32_t end_reason,
    std::uint64_t ended_by_user_id
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || group_id == 0 || user_id == 0 || membership_epoch == 0 ||
        end_reason < 1 || end_reason > 3 || ended_by_user_id == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid CloseMembershipHistoryOnConnection arguments";
        return result;
    }
    const std::string sql =
        "UPDATE im_group_membership_history SET ended_at = CURRENT_TIMESTAMP(3), end_reason = " +
        std::to_string(end_reason) + ", ended_by_user_id = " + std::to_string(ended_by_user_id) +
        " WHERE group_id = " + std::to_string(group_id) + " AND user_id = " +
        std::to_string(user_id) + " AND membership_epoch = " + std::to_string(membership_epoch) +
        " AND ended_at IS NULL";
    result = ExecuteMutation(connection, sql, "close group membership history");
    result.group_id = group_id;
    return result;
}

GroupMutationStorageResult GroupRepository::MarkMemberInactiveOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t user_id,
    std::uint32_t status
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || group_id == 0 || user_id == 0 || (status != 2 && status != 3)) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid MarkMemberInactiveOnConnection arguments";
        return result;
    }
    const std::string sql =
        "UPDATE im_group_members SET status = " + std::to_string(status) +
        ", muted_until = NULL, left_at = CURRENT_TIMESTAMP(3), updated_at = CURRENT_TIMESTAMP(3) "
        "WHERE group_id = " + std::to_string(group_id) + " AND user_id = " +
        std::to_string(user_id) + " AND status = 1";
    result = ExecuteMutation(connection, sql, "mark group member inactive");
    result.group_id = group_id;
    return result;
}

GroupMutationStorageResult GroupRepository::UpdateMemberRoleOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t user_id,
    std::uint32_t role
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || group_id == 0 || user_id == 0 || role < 1 || role > 3) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid UpdateMemberRoleOnConnection arguments";
        return result;
    }
    std::string sql =
        "UPDATE im_group_members SET role = " + std::to_string(role);
    if (role == 1) {
        // OWNER is never subject to a member mute. TransferOwnership is the
        // only domain path that writes role=OWNER, so clear stale mute state
        // atomically with the role transition.
        sql += ", muted_until = NULL";
    }
    sql += ", updated_at = CURRENT_TIMESTAMP(3) WHERE group_id = " +
           std::to_string(group_id) + " AND user_id = " + std::to_string(user_id) +
           " AND status = 1";
    result = ExecuteMutation(connection, sql, "update group member role");
    result.group_id = group_id;
    return result;
}

GroupMutationStorageResult GroupRepository::UpdateMemberMuteOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t user_id,
    const std::string& muted_until
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || group_id == 0 || user_id == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid UpdateMemberMuteOnConnection arguments";
        return result;
    }
    const std::string mute_sql = muted_until.empty()
        ? "NULL"
        : "'" + connection->EscapeString(muted_until) + "'";
    const std::string sql =
        "UPDATE im_group_members SET muted_until = " + mute_sql +
        ", updated_at = CURRENT_TIMESTAMP(3) WHERE group_id = " + std::to_string(group_id) +
        " AND user_id = " + std::to_string(user_id) + " AND status = 1";
    result = ExecuteMutation(connection, sql, "update group member mute");
    result.group_id = group_id;
    return result;
}

GroupMutationStorageResult GroupRepository::UpdateGroupVersionsOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t new_version,
    std::uint64_t new_member_version
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || group_id == 0 || new_version == 0 || new_member_version == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid UpdateGroupVersionsOnConnection arguments";
        return result;
    }
    const std::string sql =
        "UPDATE im_groups SET version = " + std::to_string(new_version) +
        ", member_version = " + std::to_string(new_member_version) +
        ", updated_at = CURRENT_TIMESTAMP(3) WHERE group_id = " + std::to_string(group_id) +
        " AND status = 1";
    result = ExecuteMutation(connection, sql, "update group membership versions");
    result.group_id = group_id;
    return result;
}

GroupMutationStorageResult GroupRepository::UpdateGroupOwnerAndVersionsOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t new_owner_user_id,
    std::uint64_t new_version,
    std::uint64_t new_member_version
) {
    GroupMutationStorageResult result;
    if (connection == nullptr || group_id == 0 || new_owner_user_id == 0 ||
        new_version == 0 || new_member_version == 0) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid UpdateGroupOwnerAndVersionsOnConnection arguments";
        return result;
    }
    const std::string sql =
        "UPDATE im_groups SET owner_user_id = " + std::to_string(new_owner_user_id) +
        ", version = " + std::to_string(new_version) +
        ", member_version = " + std::to_string(new_member_version) +
        ", updated_at = CURRENT_TIMESTAMP(3) WHERE group_id = " + std::to_string(group_id) +
        " AND status = 1";
    result = ExecuteMutation(connection, sql, "transfer group ownership");
    result.group_id = group_id;
    return result;
}

GroupMemberListStorageResult GroupRepository::ListActiveMembersOnConnection(
    MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t after_user_id,
    std::uint32_t limit
) {
    GroupMemberListStorageResult result;
    if (connection == nullptr || group_id == 0 || limit == 0 || limit > 100) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid ListActiveMembersOnConnection arguments";
        return result;
    }
    MySqlQueryResult query;
    const std::string sql =
        "SELECT group_id, user_id, role, status, membership_epoch, IFNULL(muted_until, ''), "
        "joined_at, IFNULL(left_at, ''), updated_at FROM im_group_members WHERE group_id = " +
        std::to_string(group_id) + " AND status = 1 AND user_id > " +
        std::to_string(after_user_id) + " ORDER BY user_id ASC LIMIT " +
        std::to_string(static_cast<std::uint64_t>(limit) + 1U);
    if (!connection->Query(sql, &query)) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "ListActiveMembersOnConnection query failed: " + connection->LastError();
        return result;
    }
    result.has_more = query.rows.size() > limit;
    const std::size_t count = result.has_more ? limit : query.rows.size();
    result.records.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        MySqlQueryResult one;
        one.rows.push_back(query.rows[i]);
        auto parsed = ParseMemberResult(one);
        if (!parsed.Succeeded() || !parsed.found) {
            result.status = parsed.status;
            result.message = parsed.message;
            result.records.clear();
            return result;
        }
        result.records.push_back(std::move(parsed.record));
    }
    result.status = GroupRepositoryStatus::kSucceeded;
    result.message = "active group members listed";
    return result;
}

GroupMemberListStorageResult GroupRepository::ListActiveMembers(
    std::uint64_t group_id,
    std::uint64_t after_user_id,
    std::uint32_t limit
) {
    GroupMemberListStorageResult result;
    if (pool_ == nullptr || group_id == 0 || limit == 0 || limit > 100) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid ListActiveMembers arguments";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "ListActiveMembers failed to acquire connection";
        return result;
    }
    return ListActiveMembersOnConnection(connection.operator->(), group_id, after_user_id, limit);
}

GroupListStorageResult GroupRepository::ListActiveGroupsForUserOnConnection(
    MySqlConnection* connection,
    std::uint64_t user_id,
    std::uint64_t after_group_id,
    std::uint32_t limit
) {
    GroupListStorageResult result;
    if (connection == nullptr || user_id == 0 || limit == 0 || limit > 100) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid ListActiveGroupsForUserOnConnection arguments";
        return result;
    }
    MySqlQueryResult query;
    const std::string sql =
        "SELECT g.group_id, g.name, g.description, g.avatar_url, g.owner_user_id, g.status, "
        "g.join_policy, g.max_members, g.version, g.member_version, g.created_at, g.updated_at, "
        "IFNULL(g.disbanded_at, ''), IFNULL(g.disbanded_by_user_id, 0) "
        "FROM im_group_members m JOIN im_groups g ON g.group_id = m.group_id "
        "WHERE m.user_id = " + std::to_string(user_id) +
        " AND m.status = 1 AND g.status = 1 AND g.group_id > " +
        std::to_string(after_group_id) + " ORDER BY g.group_id ASC LIMIT " +
        std::to_string(static_cast<std::uint64_t>(limit) + 1U);
    if (!connection->Query(sql, &query)) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "ListActiveGroupsForUserOnConnection query failed: " + connection->LastError();
        return result;
    }
    result.has_more = query.rows.size() > limit;
    const std::size_t count = result.has_more ? limit : query.rows.size();
    result.records.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        MySqlQueryResult one;
        one.rows.push_back(query.rows[i]);
        auto parsed = ParseGroupResult(one);
        if (!parsed.Succeeded() || !parsed.found) {
            result.status = parsed.status;
            result.message = parsed.message;
            result.records.clear();
            return result;
        }
        result.records.push_back(std::move(parsed.record));
    }
    result.status = GroupRepositoryStatus::kSucceeded;
    result.message = "active groups for user listed";
    return result;
}

GroupListStorageResult GroupRepository::ListActiveGroupsForUser(
    std::uint64_t user_id,
    std::uint64_t after_group_id,
    std::uint32_t limit
) {
    GroupListStorageResult result;
    if (pool_ == nullptr || user_id == 0 || limit == 0 || limit > 100) {
        result.status = GroupRepositoryStatus::kInvalidArgument;
        result.message = "invalid ListActiveGroupsForUser arguments";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = GroupRepositoryStatus::kStorageError;
        result.message = "ListActiveGroupsForUser failed to acquire connection";
        return result;
    }
    return ListActiveGroupsForUserOnConnection(connection.operator->(), user_id, after_group_id, limit);
}

}  // namespace tinyimx
