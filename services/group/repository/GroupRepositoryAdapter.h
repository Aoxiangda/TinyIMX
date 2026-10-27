#pragma once

#include "services/group/application/GroupRepositoryPort.h"
#include <string>

namespace tinyimx {
class GroupRepository;
class MySqlConnectionPool;
}

namespace tinyimx::outbox {
class OutboxRepository;
}

namespace tinyimx::group {

class GroupPermissionPolicy;

class GroupRepositoryAdapter final : public GroupRepositoryPort {
public:
    GroupRepositoryAdapter(
        tinyimx::GroupRepository* repository,
        tinyimx::MySqlConnectionPool* pool,
        tinyimx::outbox::OutboxRepository* outbox_repository,
        const GroupPermissionPolicy* permission_policy,
        std::string event_topic = "tinyimx-message-events"
    );

    [[nodiscard]] GroupRepositoryMutationResult CreateGroup(
        const CreateGroupCommand& command
    ) override;
    [[nodiscard]] GroupRepositoryGetResult GetGroup(
        std::uint64_t actor_user_id,
        std::uint64_t group_id
    ) override;
    [[nodiscard]] GroupRepositoryMutationResult UpdateGroup(
        const UpdateGroupCommand& command
    ) override;
    [[nodiscard]] GroupRepositoryMutationResult DisbandGroup(
        const DisbandGroupCommand& command
    ) override;

    [[nodiscard]] GroupRepositoryMutationResult JoinGroup(
        const JoinGroupCommand& command
    ) override;
    [[nodiscard]] GroupRepositoryMutationResult LeaveGroup(
        const LeaveGroupCommand& command
    ) override;
    [[nodiscard]] GroupRepositoryMutationResult InviteMember(
        const InviteMemberCommand& command
    ) override;
    [[nodiscard]] GroupRepositoryMutationResult KickMember(
        const KickMemberCommand& command
    ) override;
    [[nodiscard]] GroupRepositoryMutationResult SetMemberRole(
        const SetMemberRoleCommand& command
    ) override;
    [[nodiscard]] GroupRepositoryMutationResult SetMemberMute(
        const SetMemberMuteCommand& command
    ) override;
    [[nodiscard]] GroupRepositoryMutationResult TransferOwnership(
        const TransferOwnershipCommand& command
    ) override;

    [[nodiscard]] GroupMemberListResult ListGroupMembers(
        const ListGroupMembersQuery& query
    ) override;
    [[nodiscard]] GroupListResult ListMyGroups(
        const ListMyGroupsQuery& query
    ) override;
    [[nodiscard]] GroupSendPermissionResult CheckGroupSendPermission(
        std::uint64_t actor_user_id,
        std::uint64_t group_id
    ) override;
    [[nodiscard]] GroupSendPreparationResult PrepareGroupMessageSend(
        std::uint64_t actor_user_id,
        std::uint64_t group_id
    ) override;

private:
    tinyimx::GroupRepository* repository_{nullptr};
    tinyimx::MySqlConnectionPool* pool_{nullptr};
    tinyimx::outbox::OutboxRepository* outbox_{nullptr};
    const GroupPermissionPolicy* permission_{nullptr};
    const std::string event_topic_;
};

}  // namespace tinyimx::group
