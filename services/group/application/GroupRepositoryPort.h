#pragma once

#include "services/group/application/GroupApplicationTypes.h"

#include <cstdint>

namespace tinyimx::group {

class GroupRepositoryPort {
public:
    virtual ~GroupRepositoryPort() = default;

    GroupRepositoryPort() = default;
    GroupRepositoryPort(const GroupRepositoryPort&) = delete;
    GroupRepositoryPort& operator=(const GroupRepositoryPort&) = delete;

    [[nodiscard]] virtual GroupRepositoryMutationResult CreateGroup(
        const CreateGroupCommand& command
    ) = 0;
    [[nodiscard]] virtual GroupRepositoryGetResult GetGroup(
        std::uint64_t actor_user_id,
        std::uint64_t group_id
    ) = 0;
    [[nodiscard]] virtual GroupRepositoryMutationResult UpdateGroup(
        const UpdateGroupCommand& command
    ) = 0;
    [[nodiscard]] virtual GroupRepositoryMutationResult DisbandGroup(
        const DisbandGroupCommand& command
    ) = 0;

    [[nodiscard]] virtual GroupRepositoryMutationResult JoinGroup(
        const JoinGroupCommand& command
    ) = 0;
    [[nodiscard]] virtual GroupRepositoryMutationResult LeaveGroup(
        const LeaveGroupCommand& command
    ) = 0;
    [[nodiscard]] virtual GroupRepositoryMutationResult InviteMember(
        const InviteMemberCommand& command
    ) = 0;
    [[nodiscard]] virtual GroupRepositoryMutationResult KickMember(
        const KickMemberCommand& command
    ) = 0;
    [[nodiscard]] virtual GroupRepositoryMutationResult SetMemberRole(
        const SetMemberRoleCommand& command
    ) = 0;
    [[nodiscard]] virtual GroupRepositoryMutationResult SetMemberMute(
        const SetMemberMuteCommand& command
    ) = 0;
    [[nodiscard]] virtual GroupRepositoryMutationResult TransferOwnership(
        const TransferOwnershipCommand& command
    ) = 0;

    [[nodiscard]] virtual GroupMemberListResult ListGroupMembers(
        const ListGroupMembersQuery& query
    ) = 0;
    [[nodiscard]] virtual GroupListResult ListMyGroups(
        const ListMyGroupsQuery& query
    ) = 0;
    [[nodiscard]] virtual GroupSendPermissionResult CheckGroupSendPermission(
        std::uint64_t actor_user_id,
        std::uint64_t group_id
    ) = 0;
};

}  // namespace tinyimx::group
