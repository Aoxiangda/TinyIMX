#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tinyimx::group {

enum class GroupApplicationStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kNotFound,
    kPermissionDenied,
    kFailedPrecondition,
    kAlreadyExists,
    kResourceExhausted,
    kAborted,
    kInvalidRecord,
    kStorageError,
};

enum class GroupStatus : std::uint32_t {
    kActive = 1,
    kDisbanded = 2,
};

enum class GroupJoinPolicy : std::uint32_t {
    kInviteOnly = 1,
    kOpen = 2,
};

enum class GroupRole : std::uint32_t {
    kOwner = 1,
    kAdmin = 2,
    kMember = 3,
};

enum class GroupMemberStatus : std::uint32_t {
    kActive = 1,
    kLeft = 2,
    kKicked = 3,
};

enum class GroupMembershipEndReason : std::uint32_t {
    kLeft = 1,
    kKicked = 2,
    kGroupDisbanded = 3,
};

enum class GroupMutationOutcome {
    kApplied = 0,
    kReused,
    kIdempotencyConflict,
};

struct GroupView {
    std::uint64_t group_id{0};
    std::string name;
    std::string description;
    std::string avatar_url;
    std::uint64_t owner_user_id{0};
    GroupStatus status{GroupStatus::kActive};
    GroupJoinPolicy join_policy{GroupJoinPolicy::kInviteOnly};
    std::uint32_t max_members{500};
    std::uint64_t version{0};
    std::uint64_t member_version{0};
    std::string created_at;
    std::string updated_at;
    std::string disbanded_at;
    std::uint64_t disbanded_by_user_id{0};
};

struct GroupMemberView {
    std::uint64_t group_id{0};
    std::uint64_t user_id{0};
    GroupRole role{GroupRole::kMember};
    GroupMemberStatus status{GroupMemberStatus::kActive};
    std::uint64_t membership_epoch{0};
    std::string muted_until;
    std::string joined_at;
    std::string left_at;
    std::string updated_at;
};

struct CreateGroupCommand {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::string name;
    std::string description;
    std::string avatar_url;
    GroupJoinPolicy join_policy{GroupJoinPolicy::kInviteOnly};
    std::uint32_t max_members{500};
};

struct UpdateGroupCommand {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t expected_version{0};
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<std::string> avatar_url;
    std::optional<GroupJoinPolicy> join_policy;
};

struct DisbandGroupCommand {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t expected_version{0};
};

struct JoinGroupCommand {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
};

struct LeaveGroupCommand {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
};

struct InviteMemberCommand {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t target_user_id{0};
};

struct KickMemberCommand {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t target_user_id{0};
};

struct SetMemberRoleCommand {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t target_user_id{0};
    GroupRole role{GroupRole::kMember};
};

struct SetMemberMuteCommand {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t target_user_id{0};
    // Empty means unmute. Non-empty values are normalized by the application
    // layer to MySQL DATETIME(3): YYYY-MM-DD HH:MM:SS.mmm.
    std::string muted_until;
};

struct TransferOwnershipCommand {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t target_user_id{0};
};

struct ListGroupMembersQuery {
    std::uint64_t actor_user_id{0};
    std::uint64_t group_id{0};
    std::uint64_t after_user_id{0};
    std::uint32_t limit{50};
};

struct ListMyGroupsQuery {
    std::uint64_t actor_user_id{0};
    std::uint64_t after_group_id{0};
    std::uint32_t limit{50};
};

struct GroupRepositoryMutationResult {
    GroupApplicationStatus status{GroupApplicationStatus::kStorageError};
    GroupMutationOutcome outcome{GroupMutationOutcome::kApplied};
    std::optional<GroupView> group;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == GroupApplicationStatus::kSucceeded;
    }

    [[nodiscard]] bool Accepted() const noexcept {
        return Completed() &&
               (outcome == GroupMutationOutcome::kApplied ||
                outcome == GroupMutationOutcome::kReused);
    }
};

struct GroupRepositoryGetResult {
    GroupApplicationStatus status{GroupApplicationStatus::kStorageError};
    std::optional<GroupView> group;
    std::string message;

    [[nodiscard]] bool Found() const noexcept {
        return status == GroupApplicationStatus::kSucceeded && group.has_value();
    }
};

struct GroupMemberListResult {
    GroupApplicationStatus status{GroupApplicationStatus::kStorageError};
    std::vector<GroupMemberView> members;
    bool has_more{false};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == GroupApplicationStatus::kSucceeded;
    }
};

struct GroupListResult {
    GroupApplicationStatus status{GroupApplicationStatus::kStorageError};
    std::vector<GroupView> groups;
    bool has_more{false};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == GroupApplicationStatus::kSucceeded;
    }
};

struct GroupSendPermissionResult {
    GroupApplicationStatus status{GroupApplicationStatus::kStorageError};
    bool allowed{false};
    GroupRole role{GroupRole::kMember};
    std::uint64_t membership_epoch{0};
    std::uint64_t member_version{0};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == GroupApplicationStatus::kSucceeded;
    }
};

using CreateGroupApplicationResult = GroupRepositoryMutationResult;
using UpdateGroupApplicationResult = GroupRepositoryMutationResult;
using DisbandGroupApplicationResult = GroupRepositoryMutationResult;
using JoinGroupApplicationResult = GroupRepositoryMutationResult;
using LeaveGroupApplicationResult = GroupRepositoryMutationResult;
using InviteMemberApplicationResult = GroupRepositoryMutationResult;
using KickMemberApplicationResult = GroupRepositoryMutationResult;
using SetMemberRoleApplicationResult = GroupRepositoryMutationResult;
using SetMemberMuteApplicationResult = GroupRepositoryMutationResult;
using TransferOwnershipApplicationResult = GroupRepositoryMutationResult;
using GetGroupApplicationResult = GroupRepositoryGetResult;
using ListGroupMembersApplicationResult = GroupMemberListResult;
using ListMyGroupsApplicationResult = GroupListResult;
using CheckGroupSendPermissionApplicationResult = GroupSendPermissionResult;

}  // namespace tinyimx::group
