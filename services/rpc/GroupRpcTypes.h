#pragma once

#include "services/rpc/RpcCallOptions.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tinyimx::rpc {

enum class GroupRpcStatus : std::uint32_t {
    kActive = 1,
    kDisbanded = 2,
};

enum class GroupRpcJoinPolicy : std::uint32_t {
    kInviteOnly = 1,
    kOpen = 2,
};

enum class GroupRpcRole : std::uint32_t {
    kOwner = 1,
    kAdmin = 2,
    kMember = 3,
};

enum class GroupRpcMemberStatus : std::uint32_t {
    kActive = 1,
    kLeft = 2,
    kKicked = 3,
};

enum class GroupRpcMutationOutcome : std::uint32_t {
    kApplied = 1,
    kReused = 2,
    kIdempotencyConflict = 3,
};

struct GroupRpcView {
    std::uint64_t group_id{0};
    std::string name;
    std::string description;
    std::string avatar_url;
    std::uint64_t owner_user_id{0};
    GroupRpcStatus status{GroupRpcStatus::kActive};
    GroupRpcJoinPolicy join_policy{GroupRpcJoinPolicy::kInviteOnly};
    std::uint32_t max_members{0};
    std::uint64_t version{0};
    std::uint64_t member_version{0};
    std::string created_at;
    std::string updated_at;
    std::string disbanded_at;
    std::uint64_t disbanded_by_user_id{0};
};

struct GroupMemberRpcView {
    std::uint64_t group_id{0};
    std::uint64_t user_id{0};
    GroupRpcRole role{GroupRpcRole::kMember};
    GroupRpcMemberStatus status{GroupRpcMemberStatus::kActive};
    std::uint64_t membership_epoch{0};
    std::string muted_until;
    std::string joined_at;
    std::string left_at;
    std::string updated_at;
};

struct CreateGroupRpcRequest {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::string name;
    std::string description;
    std::string avatar_url;
    GroupRpcJoinPolicy join_policy{GroupRpcJoinPolicy::kInviteOnly};
    std::uint32_t max_members{500};
};

struct GetGroupRpcRequest {
    std::uint64_t actor_user_id{0};
    std::uint64_t group_id{0};
};

struct UpdateGroupRpcRequest {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t expected_version{0};
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<std::string> avatar_url;
    std::optional<GroupRpcJoinPolicy> join_policy;
};

struct DisbandGroupRpcRequest {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t expected_version{0};
};

struct JoinGroupRpcRequest {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
};

struct LeaveGroupRpcRequest {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
};

struct InviteMemberRpcRequest {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t target_user_id{0};
};

struct KickMemberRpcRequest {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t target_user_id{0};
};

struct SetMemberRoleRpcRequest {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t target_user_id{0};
    GroupRpcRole role{GroupRpcRole::kMember};
};

struct SetMemberMuteRpcRequest {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t target_user_id{0};
    std::string muted_until;
};

struct TransferOwnershipRpcRequest {
    std::uint64_t actor_user_id{0};
    std::string client_operation_id;
    std::uint64_t group_id{0};
    std::uint64_t target_user_id{0};
};

struct ListGroupMembersRpcRequest {
    std::uint64_t actor_user_id{0};
    std::uint64_t group_id{0};
    std::uint64_t after_user_id{0};
    std::uint32_t limit{50};
};

struct ListMyGroupsRpcRequest {
    std::uint64_t actor_user_id{0};
    std::uint64_t after_group_id{0};
    std::uint32_t limit{50};
};

struct CheckGroupSendPermissionRpcRequest {
    std::uint64_t actor_user_id{0};
    std::uint64_t group_id{0};
};

struct PrepareGroupMessageSendRpcRequest {
    std::uint64_t actor_user_id{0};
    std::uint64_t group_id{0};
};

struct GroupMutationRpcResponse {
    GroupRpcMutationOutcome outcome{GroupRpcMutationOutcome::kApplied};
    GroupRpcView group;
    std::string message;

    [[nodiscard]] bool Accepted() const noexcept {
        return outcome == GroupRpcMutationOutcome::kApplied ||
               outcome == GroupRpcMutationOutcome::kReused;
    }
};

struct GetGroupRpcResponse { GroupRpcView group; };

struct ListGroupMembersRpcResponse {
    std::vector<GroupMemberRpcView> members;
    bool has_more{false};
};

struct ListMyGroupsRpcResponse {
    std::vector<GroupRpcView> groups;
    bool has_more{false};
};

struct CheckGroupSendPermissionRpcResponse {
    bool allowed{false};
    GroupRpcRole role{GroupRpcRole::kMember};
    std::uint64_t membership_epoch{0};
    std::uint64_t member_version{0};
    std::string message;
};

struct PrepareGroupMessageSendRpcResponse {
    bool allowed{false};
    GroupRpcRole role{GroupRpcRole::kMember};
    std::uint64_t membership_epoch{0};
    std::uint64_t member_version{0};
    std::vector<std::uint64_t> recipient_user_ids;
    std::string message;
};

}  // namespace tinyimx::rpc
