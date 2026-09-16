#pragma once

#include "services/eventing/DomainEvent.h"
#include "services/group/application/GroupApplicationTypes.h"

#include <cstdint>
#include <string>

namespace tinyimx::group {

struct GroupOutboxEventSpec {
    tinyimx::eventing::DomainEvent event;
    std::string topic;
    std::string tag;
    std::string message_key;
};

class GroupEventFactory final {
public:
    [[nodiscard]] static GroupOutboxEventSpec GroupCreated(const GroupView& group);
    [[nodiscard]] static GroupOutboxEventSpec GroupUpdated(const GroupView& group);
    [[nodiscard]] static GroupOutboxEventSpec GroupDisbanded(const GroupView& group);

    [[nodiscard]] static GroupOutboxEventSpec MemberJoined(
        const GroupView& group,
        const GroupMemberView& member,
        std::uint64_t actor_user_id,
        bool invited
    );
    [[nodiscard]] static GroupOutboxEventSpec MemberLeft(
        const GroupView& group,
        const GroupMemberView& member,
        std::uint64_t actor_user_id
    );
    [[nodiscard]] static GroupOutboxEventSpec MemberKicked(
        const GroupView& group,
        const GroupMemberView& member,
        std::uint64_t actor_user_id
    );
    [[nodiscard]] static GroupOutboxEventSpec MemberRoleChanged(
        const GroupView& group,
        const GroupMemberView& member,
        std::uint64_t actor_user_id
    );
    [[nodiscard]] static GroupOutboxEventSpec MemberMuteChanged(
        const GroupView& group,
        const GroupMemberView& member,
        std::uint64_t actor_user_id
    );
    [[nodiscard]] static GroupOutboxEventSpec OwnershipTransferred(
        const GroupView& group,
        const GroupMemberView& new_owner,
        std::uint64_t previous_owner_user_id
    );
};

}  // namespace tinyimx::group
