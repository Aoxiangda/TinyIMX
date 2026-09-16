#pragma once

#include "services/group/application/GroupApplicationTypes.h"
#include "services/group/application/GroupRepositoryPort.h"

#include <cstdint>

namespace tinyimx::group {

class GroupApplicationService final {
public:
    explicit GroupApplicationService(GroupRepositoryPort* repository);

    GroupApplicationService(const GroupApplicationService&) = delete;
    GroupApplicationService& operator=(const GroupApplicationService&) = delete;

    [[nodiscard]] CreateGroupApplicationResult CreateGroup(CreateGroupCommand command);
    [[nodiscard]] GetGroupApplicationResult GetGroup(
        std::uint64_t actor_user_id,
        std::uint64_t group_id
    );
    [[nodiscard]] UpdateGroupApplicationResult UpdateGroup(UpdateGroupCommand command);
    [[nodiscard]] DisbandGroupApplicationResult DisbandGroup(DisbandGroupCommand command);

    [[nodiscard]] JoinGroupApplicationResult JoinGroup(JoinGroupCommand command);
    [[nodiscard]] LeaveGroupApplicationResult LeaveGroup(LeaveGroupCommand command);
    [[nodiscard]] InviteMemberApplicationResult InviteMember(InviteMemberCommand command);
    [[nodiscard]] KickMemberApplicationResult KickMember(KickMemberCommand command);
    [[nodiscard]] SetMemberRoleApplicationResult SetMemberRole(SetMemberRoleCommand command);
    [[nodiscard]] SetMemberMuteApplicationResult SetMemberMute(SetMemberMuteCommand command);
    [[nodiscard]] TransferOwnershipApplicationResult TransferOwnership(
        TransferOwnershipCommand command
    );

    [[nodiscard]] ListGroupMembersApplicationResult ListGroupMembers(
        ListGroupMembersQuery query
    );
    [[nodiscard]] ListMyGroupsApplicationResult ListMyGroups(ListMyGroupsQuery query);
    [[nodiscard]] CheckGroupSendPermissionApplicationResult CheckGroupSendPermission(
        std::uint64_t actor_user_id,
        std::uint64_t group_id
    );

private:
    GroupRepositoryPort* repository_{nullptr};  // non-owning
};

}  // namespace tinyimx::group
