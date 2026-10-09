#include "services/group/application/GroupPermissionPolicy.h"

namespace tinyimx::group {

bool GroupPermissionPolicy::CanView(const GroupMemberView& actor) const noexcept {
    return actor.user_id != 0 && actor.status == GroupMemberStatus::kActive;
}

bool GroupPermissionPolicy::CanUpdateMetadata(GroupRole actor_role) const noexcept {
    return actor_role == GroupRole::kOwner || actor_role == GroupRole::kAdmin;
}

bool GroupPermissionPolicy::CanUpdateJoinPolicy(GroupRole actor_role) const noexcept {
    return actor_role == GroupRole::kOwner;
}

bool GroupPermissionPolicy::CanDisband(GroupRole actor_role) const noexcept {
    return actor_role == GroupRole::kOwner;
}

bool GroupPermissionPolicy::CanInvite(GroupRole actor_role) const noexcept {
    return actor_role == GroupRole::kOwner || actor_role == GroupRole::kAdmin;
}

bool GroupPermissionPolicy::CanKick(
    GroupRole actor_role,
    GroupRole target_role
) const noexcept {
    if (target_role == GroupRole::kOwner) {
        return false;
    }
    if (actor_role == GroupRole::kOwner) {
        return true;
    }
    return actor_role == GroupRole::kAdmin && target_role == GroupRole::kMember;
}

bool GroupPermissionPolicy::CanSetRole(
    GroupRole actor_role,
    GroupRole target_role
) const noexcept {
    return actor_role == GroupRole::kOwner && target_role != GroupRole::kOwner;
}

bool GroupPermissionPolicy::CanMute(
    GroupRole actor_role,
    GroupRole target_role
) const noexcept {
    if (target_role == GroupRole::kOwner) {
        return false;
    }
    if (actor_role == GroupRole::kOwner) {
        return true;
    }
    return actor_role == GroupRole::kAdmin && target_role == GroupRole::kMember;
}

bool GroupPermissionPolicy::CanTransferOwnership(
    GroupRole actor_role,
    GroupRole target_role
) const noexcept {
    return actor_role == GroupRole::kOwner && target_role != GroupRole::kOwner;
}

bool GroupPermissionPolicy::CanLeave(GroupRole actor_role) const noexcept {
    return actor_role != GroupRole::kOwner;
}

bool GroupPermissionPolicy::CanSend(
    const GroupMemberView& actor,
    bool mute_active
) const noexcept {
    return actor.user_id != 0 &&
           actor.status == GroupMemberStatus::kActive &&
           !mute_active;
}

}  // namespace tinyimx::group
