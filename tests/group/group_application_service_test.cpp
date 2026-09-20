#include "services/group/application/GroupApplicationService.h"

#include <cstdint>
#include <iostream>
#include <utility>

namespace {

class FakeRepository final : public tinyimx::group::GroupRepositoryPort {
public:
    tinyimx::group::GroupRepositoryMutationResult mutation_result;
    tinyimx::group::GroupRepositoryGetResult get_result;
    tinyimx::group::GroupMemberListResult member_list_result;
    tinyimx::group::GroupListResult group_list_result;
    tinyimx::group::GroupSendPermissionResult permission_result;

    tinyimx::group::CreateGroupCommand last_create;
    tinyimx::group::UpdateGroupCommand last_update;
    tinyimx::group::DisbandGroupCommand last_disband;
    tinyimx::group::JoinGroupCommand last_join;
    tinyimx::group::LeaveGroupCommand last_leave;
    tinyimx::group::InviteMemberCommand last_invite;
    tinyimx::group::KickMemberCommand last_kick;
    tinyimx::group::SetMemberRoleCommand last_role;
    tinyimx::group::SetMemberMuteCommand last_mute;
    tinyimx::group::TransferOwnershipCommand last_transfer;
    tinyimx::group::ListGroupMembersQuery last_member_list;
    tinyimx::group::ListMyGroupsQuery last_group_list;
    std::uint64_t last_get_actor{0};
    std::uint64_t last_get_group{0};
    std::uint64_t last_permission_actor{0};
    std::uint64_t last_permission_group{0};

    tinyimx::group::GroupRepositoryMutationResult CreateGroup(
        const tinyimx::group::CreateGroupCommand& c) override { last_create=c; return mutation_result; }
    tinyimx::group::GroupRepositoryGetResult GetGroup(std::uint64_t a, std::uint64_t g) override {
        last_get_actor=a; last_get_group=g; return get_result;
    }
    tinyimx::group::GroupRepositoryMutationResult UpdateGroup(
        const tinyimx::group::UpdateGroupCommand& c) override { last_update=c; return mutation_result; }
    tinyimx::group::GroupRepositoryMutationResult DisbandGroup(
        const tinyimx::group::DisbandGroupCommand& c) override { last_disband=c; return mutation_result; }
    tinyimx::group::GroupRepositoryMutationResult JoinGroup(
        const tinyimx::group::JoinGroupCommand& c) override { last_join=c; return mutation_result; }
    tinyimx::group::GroupRepositoryMutationResult LeaveGroup(
        const tinyimx::group::LeaveGroupCommand& c) override { last_leave=c; return mutation_result; }
    tinyimx::group::GroupRepositoryMutationResult InviteMember(
        const tinyimx::group::InviteMemberCommand& c) override { last_invite=c; return mutation_result; }
    tinyimx::group::GroupRepositoryMutationResult KickMember(
        const tinyimx::group::KickMemberCommand& c) override { last_kick=c; return mutation_result; }
    tinyimx::group::GroupRepositoryMutationResult SetMemberRole(
        const tinyimx::group::SetMemberRoleCommand& c) override { last_role=c; return mutation_result; }
    tinyimx::group::GroupRepositoryMutationResult SetMemberMute(
        const tinyimx::group::SetMemberMuteCommand& c) override { last_mute=c; return mutation_result; }
    tinyimx::group::GroupRepositoryMutationResult TransferOwnership(
        const tinyimx::group::TransferOwnershipCommand& c) override { last_transfer=c; return mutation_result; }
    tinyimx::group::GroupMemberListResult ListGroupMembers(
        const tinyimx::group::ListGroupMembersQuery& q) override { last_member_list=q; return member_list_result; }
    tinyimx::group::GroupListResult ListMyGroups(
        const tinyimx::group::ListMyGroupsQuery& q) override { last_group_list=q; return group_list_result; }
    tinyimx::group::GroupSendPermissionResult CheckGroupSendPermission(
        std::uint64_t a, std::uint64_t g) override {
        last_permission_actor=a; last_permission_group=g; return permission_result;
    }
    tinyimx::group::GroupSendPreparationResult PrepareGroupMessageSend(
        std::uint64_t a, std::uint64_t g) override {
        tinyimx::group::GroupSendPreparationResult out;
        out.status = permission_result.status;
        out.allowed = permission_result.allowed;
        out.role = permission_result.role;
        out.membership_epoch = permission_result.membership_epoch;
        out.member_version = permission_result.member_version;
        if (out.allowed) out.recipient_user_ids = {8, 9};
        out.message = permission_result.message;
        last_permission_actor=a; last_permission_group=g;
        return out;
    }
};

int Fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using namespace tinyimx::group;

    FakeRepository repository;
    repository.mutation_result.status = GroupApplicationStatus::kSucceeded;
    repository.mutation_result.outcome = GroupMutationOutcome::kApplied;
    repository.get_result.status = GroupApplicationStatus::kSucceeded;
    GroupView get_group;
    get_group.group_id=10; get_group.name="team"; get_group.owner_user_id=7;
    get_group.version=1; get_group.member_version=1;
    repository.get_result.group=get_group;
    repository.member_list_result.status=GroupApplicationStatus::kSucceeded;
    repository.group_list_result.status=GroupApplicationStatus::kSucceeded;
    repository.permission_result.status=GroupApplicationStatus::kSucceeded;

    GroupApplicationService service(&repository);

    CreateGroupCommand create;
    create.actor_user_id=7; create.client_operation_id="op-create";
    create.name="  backend team  "; create.join_policy=GroupJoinPolicy::kInviteOnly; create.max_members=500;
    if (!service.CreateGroup(create).Completed() || repository.last_create.name!="backend team")
        return Fail("CreateGroup validation/normalization failed");
    create.max_members=1;
    if (service.CreateGroup(create).status!=GroupApplicationStatus::kInvalidArgument)
        return Fail("CreateGroup accepted invalid max_members");

    if (!service.GetGroup(7,10).Found() || repository.last_get_actor!=7 || repository.last_get_group!=10)
        return Fail("GetGroup did not delegate correctly");

    UpdateGroupCommand update;
    update.actor_user_id=7; update.client_operation_id="op-update"; update.group_id=10;
    update.expected_version=1; update.name="  platform team  ";
    if (!service.UpdateGroup(update).Completed() || !repository.last_update.name ||
        *repository.last_update.name!="platform team")
        return Fail("UpdateGroup validation/normalization failed");

    JoinGroupCommand join{8,"op-join",10};
    if (!service.JoinGroup(join).Completed() || repository.last_join.actor_user_id!=8)
        return Fail("JoinGroup did not delegate correctly");

    LeaveGroupCommand leave{8,"op-leave",10};
    if (!service.LeaveGroup(leave).Completed()) return Fail("LeaveGroup did not delegate correctly");

    InviteMemberCommand invite{7,"op-invite",10,8};
    if (!service.InviteMember(invite).Completed() || repository.last_invite.target_user_id!=8)
        return Fail("InviteMember did not delegate correctly");
    invite.target_user_id=7;
    if (service.InviteMember(invite).status!=GroupApplicationStatus::kInvalidArgument)
        return Fail("InviteMember accepted self-target");

    KickMemberCommand kick{7,"op-kick",10,8};
    if (!service.KickMember(kick).Completed()) return Fail("KickMember did not delegate correctly");

    SetMemberRoleCommand role{7,"op-role",10,8,GroupRole::kAdmin};
    if (!service.SetMemberRole(role).Completed() || repository.last_role.role!=GroupRole::kAdmin)
        return Fail("SetMemberRole did not delegate correctly");
    role.role=GroupRole::kOwner;
    if (service.SetMemberRole(role).status!=GroupApplicationStatus::kInvalidArgument)
        return Fail("SetMemberRole accepted OWNER role mutation");

    SetMemberMuteCommand mute{7,"op-mute",10,8,"2026-09-20T12:30:00.000Z"};
    if (!service.SetMemberMute(mute).Completed() ||
        repository.last_mute.muted_until!="2026-09-20 12:30:00.000")
        return Fail("SetMemberMute UTC normalization failed");
    mute.client_operation_id="op-mute-bad"; mute.muted_until="tomorrow";
    if (service.SetMemberMute(mute).status!=GroupApplicationStatus::kInvalidArgument)
        return Fail("SetMemberMute accepted invalid timestamp");

    TransferOwnershipCommand transfer{7,"op-transfer",10,8};
    if (!service.TransferOwnership(transfer).Completed())
        return Fail("TransferOwnership did not delegate correctly");

    auto members=service.ListGroupMembers({7,10,0,0});
    if (!members.Succeeded() || repository.last_member_list.limit!=50)
        return Fail("ListGroupMembers default page normalization failed");
    if (service.ListGroupMembers({7,10,0,101}).status!=GroupApplicationStatus::kInvalidArgument)
        return Fail("ListGroupMembers accepted oversized page");

    auto groups=service.ListMyGroups({7,0,0});
    if (!groups.Succeeded() || repository.last_group_list.limit!=50)
        return Fail("ListMyGroups default page normalization failed");

    if (!service.CheckGroupSendPermission(7,10).Succeeded() ||
        repository.last_permission_actor!=7 || repository.last_permission_group!=10)
        return Fail("CheckGroupSendPermission did not delegate correctly");

    DisbandGroupCommand disband{7,"op-disband",10,2};
    if (!service.DisbandGroup(disband).Completed()) return Fail("DisbandGroup did not delegate correctly");

    std::cout << "PASS: group application service A1+A2 validation and delegation\n";
    return 0;
}
