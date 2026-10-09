#include "services/group/application/GroupEventFactory.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace tinyimx::group {
namespace {

constexpr const char* kGroupEventsTopic = "tinyimx-message-events";
constexpr const char* kProducerService = "group-service";

std::string CurrentUtcIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;

    std::tm utc{};
    gmtime_r(&raw, &utc);

    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << millis.count() << 'Z';
    return oss.str();
}

GroupOutboxEventSpec BaseEvent(
    const GroupView& group,
    const std::string& event_type,
    const std::string& event_id
) {
    GroupOutboxEventSpec spec;
    spec.event.schema_version = 1;
    spec.event.event_id = event_id;
    spec.event.event_type = event_type;
    spec.event.aggregate_type = "group";
    spec.event.aggregate_id = std::to_string(group.group_id);
    spec.event.producer_service = kProducerService;
    spec.event.occurred_at = CurrentUtcIso8601();
    spec.event.payload = {
        {"group_id", group.group_id},
        {"owner_user_id", group.owner_user_id},
        {"status", static_cast<std::uint32_t>(group.status)},
        {"join_policy", static_cast<std::uint32_t>(group.join_policy)},
        {"version", group.version},
        {"member_version", group.member_version},
    };
    spec.topic = kGroupEventsTopic;
    spec.tag = event_type;
    spec.message_key = spec.event.event_id;
    return spec;
}

GroupOutboxEventSpec BuildGroupEvent(
    const GroupView& group,
    const std::string& event_type
) {
    return BaseEvent(
        group,
        event_type,
        event_type + ":" + std::to_string(group.group_id) + ":" +
            std::to_string(group.version)
    );
}

GroupOutboxEventSpec BuildMemberEpochEvent(
    const GroupView& group,
    const GroupMemberView& member,
    std::uint64_t actor_user_id,
    const std::string& event_type
) {
    auto spec = BaseEvent(
        group,
        event_type,
        event_type + ":" + std::to_string(group.group_id) + ":" +
            std::to_string(member.user_id) + ":" +
            std::to_string(member.membership_epoch)
    );
    spec.event.payload["actor_user_id"] = actor_user_id;
    spec.event.payload["target_user_id"] = member.user_id;
    spec.event.payload["role"] = static_cast<std::uint32_t>(member.role);
    spec.event.payload["member_status"] = static_cast<std::uint32_t>(member.status);
    spec.event.payload["membership_epoch"] = member.membership_epoch;
    return spec;
}

GroupOutboxEventSpec BuildMemberVersionEvent(
    const GroupView& group,
    const GroupMemberView& member,
    std::uint64_t actor_user_id,
    const std::string& event_type
) {
    auto spec = BaseEvent(
        group,
        event_type,
        event_type + ":" + std::to_string(group.group_id) + ":" +
            std::to_string(member.user_id) + ":" +
            std::to_string(group.member_version)
    );
    spec.event.payload["actor_user_id"] = actor_user_id;
    spec.event.payload["target_user_id"] = member.user_id;
    spec.event.payload["role"] = static_cast<std::uint32_t>(member.role);
    spec.event.payload["member_status"] = static_cast<std::uint32_t>(member.status);
    spec.event.payload["membership_epoch"] = member.membership_epoch;
    spec.event.payload["muted_until"] = member.muted_until;
    return spec;
}

}  // namespace

GroupOutboxEventSpec GroupEventFactory::GroupCreated(const GroupView& group) {
    return BuildGroupEvent(group, "group.created.v1");
}

GroupOutboxEventSpec GroupEventFactory::GroupUpdated(const GroupView& group) {
    return BuildGroupEvent(group, "group.updated.v1");
}

GroupOutboxEventSpec GroupEventFactory::GroupDisbanded(const GroupView& group) {
    return BuildGroupEvent(group, "group.disbanded.v1");
}

GroupOutboxEventSpec GroupEventFactory::MemberJoined(
    const GroupView& group,
    const GroupMemberView& member,
    std::uint64_t actor_user_id,
    bool invited
) {
    return BuildMemberEpochEvent(
        group,
        member,
        actor_user_id,
        invited ? "group.member.added.v1" : "group.member.joined.v1"
    );
}

GroupOutboxEventSpec GroupEventFactory::MemberLeft(
    const GroupView& group,
    const GroupMemberView& member,
    std::uint64_t actor_user_id
) {
    return BuildMemberEpochEvent(group, member, actor_user_id, "group.member.left.v1");
}

GroupOutboxEventSpec GroupEventFactory::MemberKicked(
    const GroupView& group,
    const GroupMemberView& member,
    std::uint64_t actor_user_id
) {
    return BuildMemberEpochEvent(group, member, actor_user_id, "group.member.kicked.v1");
}

GroupOutboxEventSpec GroupEventFactory::MemberRoleChanged(
    const GroupView& group,
    const GroupMemberView& member,
    std::uint64_t actor_user_id
) {
    return BuildMemberVersionEvent(group, member, actor_user_id, "group.member.role_changed.v1");
}

GroupOutboxEventSpec GroupEventFactory::MemberMuteChanged(
    const GroupView& group,
    const GroupMemberView& member,
    std::uint64_t actor_user_id
) {
    return BuildMemberVersionEvent(group, member, actor_user_id, "group.member.mute_changed.v1");
}

GroupOutboxEventSpec GroupEventFactory::OwnershipTransferred(
    const GroupView& group,
    const GroupMemberView& new_owner,
    std::uint64_t previous_owner_user_id
) {
    auto spec = BaseEvent(
        group,
        "group.owner_transferred.v1",
        "group.owner_transferred.v1:" + std::to_string(group.group_id) + ":" +
            std::to_string(group.member_version)
    );
    spec.event.payload["previous_owner_user_id"] = previous_owner_user_id;
    spec.event.payload["new_owner_user_id"] = new_owner.user_id;
    spec.event.payload["membership_epoch"] = new_owner.membership_epoch;
    return spec;
}

}  // namespace tinyimx::group
