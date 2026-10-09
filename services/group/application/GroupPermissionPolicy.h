#pragma once

#include "services/group/application/GroupApplicationTypes.h"

namespace tinyimx::group {

class GroupPermissionPolicy final {
public:
    [[nodiscard]] bool CanView(const GroupMemberView& actor) const noexcept;
    [[nodiscard]] bool CanUpdateMetadata(GroupRole actor_role) const noexcept;
    [[nodiscard]] bool CanUpdateJoinPolicy(GroupRole actor_role) const noexcept;
    [[nodiscard]] bool CanDisband(GroupRole actor_role) const noexcept;
    [[nodiscard]] bool CanInvite(GroupRole actor_role) const noexcept;
    [[nodiscard]] bool CanKick(GroupRole actor_role, GroupRole target_role) const noexcept;
    [[nodiscard]] bool CanSetRole(GroupRole actor_role, GroupRole target_role) const noexcept;
    [[nodiscard]] bool CanMute(GroupRole actor_role, GroupRole target_role) const noexcept;
    [[nodiscard]] bool CanTransferOwnership(GroupRole actor_role, GroupRole target_role) const noexcept;
    [[nodiscard]] bool CanLeave(GroupRole actor_role) const noexcept;
    [[nodiscard]] bool CanSend(const GroupMemberView& actor, bool mute_active) const noexcept;
};

}  // namespace tinyimx::group
