#pragma once

#include "common/db/MySqlConnectionPool.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx {

class MySqlConnection;

enum class GroupRepositoryStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kNotFound,
    kInvalidRecord,
    kStorageError,
};

struct GroupRecord {
    std::uint64_t group_id{0};
    std::string name;
    std::string description;
    std::string avatar_url;
    std::uint64_t owner_user_id{0};
    std::uint32_t status{0};
    std::uint32_t join_policy{0};
    std::uint32_t max_members{0};
    std::uint64_t version{0};
    std::uint64_t member_version{0};
    std::string created_at;
    std::string updated_at;
    std::string disbanded_at;
    std::uint64_t disbanded_by_user_id{0};
};

struct GroupMemberRecord {
    std::uint64_t group_id{0};
    std::uint64_t user_id{0};
    std::uint32_t role{0};
    std::uint32_t status{0};
    std::uint64_t membership_epoch{0};
    std::string muted_until;
    std::string joined_at;
    std::string left_at;
    std::string updated_at;
};

struct GroupOperationRecord {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::string operation_type;
    std::string request_fingerprint;
    std::uint64_t group_id{0};
    std::uint64_t result_version{0};
    std::uint64_t result_member_version{0};
    std::uint64_t result_membership_epoch{0};
};

struct GroupFindResult {
    GroupRepositoryStatus status{GroupRepositoryStatus::kStorageError};
    bool found{false};
    GroupRecord record;
    std::string message;
    [[nodiscard]] bool Succeeded() const noexcept { return status == GroupRepositoryStatus::kSucceeded; }
};

struct GroupMemberFindResult {
    GroupRepositoryStatus status{GroupRepositoryStatus::kStorageError};
    bool found{false};
    GroupMemberRecord record;
    std::string message;
    [[nodiscard]] bool Succeeded() const noexcept { return status == GroupRepositoryStatus::kSucceeded; }
};

struct GroupOperationFindResult {
    GroupRepositoryStatus status{GroupRepositoryStatus::kStorageError};
    bool found{false};
    GroupOperationRecord record;
    std::string message;
    [[nodiscard]] bool Succeeded() const noexcept { return status == GroupRepositoryStatus::kSucceeded; }
};

struct GroupMutationStorageResult {
    GroupRepositoryStatus status{GroupRepositoryStatus::kStorageError};
    std::uint64_t group_id{0};
    std::uint64_t affected_rows{0};
    std::string message;
    [[nodiscard]] bool Succeeded() const noexcept { return status == GroupRepositoryStatus::kSucceeded; }
};

struct GroupBooleanResult {
    GroupRepositoryStatus status{GroupRepositoryStatus::kStorageError};
    bool value{false};
    std::string message;
    [[nodiscard]] bool Succeeded() const noexcept { return status == GroupRepositoryStatus::kSucceeded; }
};

struct GroupCountResult {
    GroupRepositoryStatus status{GroupRepositoryStatus::kStorageError};
    std::uint64_t value{0};
    std::string message;
    [[nodiscard]] bool Succeeded() const noexcept { return status == GroupRepositoryStatus::kSucceeded; }
};

struct GroupMemberListStorageResult {
    GroupRepositoryStatus status{GroupRepositoryStatus::kStorageError};
    std::vector<GroupMemberRecord> records;
    bool has_more{false};
    std::string message;
    [[nodiscard]] bool Succeeded() const noexcept { return status == GroupRepositoryStatus::kSucceeded; }
};

struct GroupListStorageResult {
    GroupRepositoryStatus status{GroupRepositoryStatus::kStorageError};
    std::vector<GroupRecord> records;
    bool has_more{false};
    std::string message;
    [[nodiscard]] bool Succeeded() const noexcept { return status == GroupRepositoryStatus::kSucceeded; }
};

class GroupRepository final {
public:
    explicit GroupRepository(MySqlConnectionPool* pool);

    GroupRepository(const GroupRepository&) = delete;
    GroupRepository& operator=(const GroupRepository&) = delete;

    [[nodiscard]] GroupFindResult FindGroupById(std::uint64_t group_id);
    [[nodiscard]] GroupOperationFindResult FindOperation(
        std::uint64_t actor_user_id,
        const std::string& client_operation_id
    );

    [[nodiscard]] GroupFindResult FindGroupByIdOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        bool for_update
    );
    [[nodiscard]] GroupMemberFindResult FindMemberOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t user_id,
        bool for_update
    );
    [[nodiscard]] GroupOperationFindResult FindOperationOnConnection(
        MySqlConnection* connection,
        std::uint64_t actor_user_id,
        const std::string& client_operation_id,
        bool for_update
    );
    [[nodiscard]] GroupBooleanResult UserExistsOnConnection(
        MySqlConnection* connection,
        std::uint64_t user_id
    );
    [[nodiscard]] GroupCountResult CountActiveMembersOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id
    );
    [[nodiscard]] GroupBooleanResult IsMemberMuteActiveOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t user_id
    );

    [[nodiscard]] GroupMutationStorageResult InsertGroupOnConnection(
        MySqlConnection* connection,
        std::uint64_t owner_user_id,
        const std::string& name,
        const std::string& description,
        const std::string& avatar_url,
        std::uint32_t join_policy,
        std::uint32_t max_members
    );
    [[nodiscard]] GroupMutationStorageResult InsertOwnerMemberOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t owner_user_id
    );
    [[nodiscard]] GroupMutationStorageResult InsertMemberOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t user_id,
        std::uint32_t role,
        std::uint64_t membership_epoch
    );
    [[nodiscard]] GroupMutationStorageResult ReactivateMemberOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t user_id,
        std::uint64_t membership_epoch,
        std::uint32_t role
    );
    [[nodiscard]] GroupMutationStorageResult InsertMembershipHistoryOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t user_id,
        std::uint64_t membership_epoch,
        std::uint32_t role_at_join
    );
    [[nodiscard]] GroupMutationStorageResult CloseMembershipHistoryOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t user_id,
        std::uint64_t membership_epoch,
        std::uint32_t end_reason,
        std::uint64_t ended_by_user_id
    );
    [[nodiscard]] GroupMutationStorageResult MarkMemberInactiveOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t user_id,
        std::uint32_t status
    );
    [[nodiscard]] GroupMutationStorageResult UpdateMemberRoleOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t user_id,
        std::uint32_t role
    );
    [[nodiscard]] GroupMutationStorageResult UpdateMemberMuteOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t user_id,
        const std::string& muted_until
    );

    [[nodiscard]] GroupMutationStorageResult UpdateGroupOnConnection(
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
    );
    [[nodiscard]] GroupMutationStorageResult UpdateGroupVersionsOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t new_version,
        std::uint64_t new_member_version
    );
    [[nodiscard]] GroupMutationStorageResult UpdateGroupOwnerAndVersionsOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t new_owner_user_id,
        std::uint64_t new_version,
        std::uint64_t new_member_version
    );
    [[nodiscard]] GroupMutationStorageResult DisbandGroupOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t actor_user_id,
        std::uint64_t new_version,
        std::uint64_t new_member_version
    );
    [[nodiscard]] GroupMutationStorageResult InsertOperationOnConnection(
        MySqlConnection* connection,
        const GroupOperationRecord& operation
    );

    [[nodiscard]] GroupMemberListStorageResult ListActiveMembersOnConnection(
        MySqlConnection* connection,
        std::uint64_t group_id,
        std::uint64_t after_user_id,
        std::uint32_t limit
    );
    [[nodiscard]] GroupMemberListStorageResult ListActiveMembers(
        std::uint64_t group_id,
        std::uint64_t after_user_id,
        std::uint32_t limit
    );
    [[nodiscard]] GroupListStorageResult ListActiveGroupsForUserOnConnection(
        MySqlConnection* connection,
        std::uint64_t user_id,
        std::uint64_t after_group_id,
        std::uint32_t limit
    );
    [[nodiscard]] GroupListStorageResult ListActiveGroupsForUser(
        std::uint64_t user_id,
        std::uint64_t after_group_id,
        std::uint32_t limit
    );

private:
    MySqlConnectionPool* pool_{nullptr};  // non-owning
};

}  // namespace tinyimx
