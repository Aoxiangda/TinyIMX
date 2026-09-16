#include "services/group/application/GroupPermissionPolicy.h"

#include <iostream>

namespace {
int Fail(const char* message) { std::cerr << "FAIL: " << message << '\n'; return 1; }
}

int main() {
    using namespace tinyimx::group;
    GroupPermissionPolicy policy;
    GroupMemberView owner; owner.user_id=1; owner.role=GroupRole::kOwner; owner.status=GroupMemberStatus::kActive;
    GroupMemberView admin=owner; admin.user_id=2; admin.role=GroupRole::kAdmin;
    GroupMemberView member=owner; member.user_id=3; member.role=GroupRole::kMember;

    if (!policy.CanView(owner) || !policy.CanUpdateMetadata(owner.role) ||
        !policy.CanUpdateJoinPolicy(owner.role) || !policy.CanDisband(owner.role) ||
        !policy.CanInvite(owner.role)) return Fail("owner permission matrix incomplete");
    if (!policy.CanUpdateMetadata(admin.role) || policy.CanUpdateJoinPolicy(admin.role) ||
        policy.CanDisband(admin.role) || !policy.CanInvite(admin.role))
        return Fail("admin permission matrix incorrect");
    if (policy.CanUpdateMetadata(member.role) || policy.CanDisband(member.role) ||
        policy.CanInvite(member.role)) return Fail("member gained administration permission");

    if (policy.CanKick(admin.role,GroupRole::kAdmin) ||
        !policy.CanKick(admin.role,GroupRole::kMember) ||
        policy.CanKick(owner.role,GroupRole::kOwner)) return Fail("kick hierarchy invariant failed");
    if (!policy.CanSetRole(owner.role,GroupRole::kAdmin) ||
        policy.CanSetRole(admin.role,GroupRole::kMember) ||
        policy.CanSetRole(owner.role,GroupRole::kOwner)) return Fail("role hierarchy invariant failed");
    if (!policy.CanMute(owner.role,GroupRole::kAdmin) ||
        !policy.CanMute(admin.role,GroupRole::kMember) ||
        policy.CanMute(admin.role,GroupRole::kAdmin) ||
        policy.CanMute(owner.role,GroupRole::kOwner)) return Fail("mute hierarchy invariant failed");
    if (!policy.CanTransferOwnership(owner.role,GroupRole::kMember) ||
        !policy.CanTransferOwnership(owner.role,GroupRole::kAdmin) ||
        policy.CanTransferOwnership(admin.role,GroupRole::kMember) ||
        policy.CanTransferOwnership(owner.role,GroupRole::kOwner)) return Fail("ownership transfer invariant failed");
    if (policy.CanLeave(GroupRole::kOwner) || !policy.CanLeave(GroupRole::kAdmin) ||
        !policy.CanLeave(GroupRole::kMember)) return Fail("owner leave invariant failed");

    if (!policy.CanSend(member,false)) return Fail("active unmuted member should send");
    if (policy.CanSend(member,true)) return Fail("muted member should not send");
    member.status=GroupMemberStatus::kKicked;
    if (policy.CanView(member) || policy.CanSend(member,false)) return Fail("inactive member retained permissions");

    std::cout << "PASS: group permission policy A1+A2 invariants\n";
    return 0;
}
