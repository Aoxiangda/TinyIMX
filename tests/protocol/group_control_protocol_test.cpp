#include "common/protocol/Packet.h"
#include "tests/concurrency/TestFramework.h"

#include <array>
#include <cstdint>
#include <string>

namespace tinyimx::test {

void RegisterGroupControlProtocolTests(TestRunner& runner) {
    runner.Add("m17_a3_group_message_types_are_known", []() {
        const std::array<MessageType, 26> types = {
            MessageType::kCreateGroupRequest, MessageType::kCreateGroupResponse,
            MessageType::kGetGroupRequest, MessageType::kGetGroupResponse,
            MessageType::kUpdateGroupRequest, MessageType::kUpdateGroupResponse,
            MessageType::kDisbandGroupRequest, MessageType::kDisbandGroupResponse,
            MessageType::kJoinGroupRequest, MessageType::kJoinGroupResponse,
            MessageType::kLeaveGroupRequest, MessageType::kLeaveGroupResponse,
            MessageType::kInviteGroupMemberRequest, MessageType::kInviteGroupMemberResponse,
            MessageType::kKickGroupMemberRequest, MessageType::kKickGroupMemberResponse,
            MessageType::kSetGroupMemberRoleRequest, MessageType::kSetGroupMemberRoleResponse,
            MessageType::kSetGroupMemberMuteRequest, MessageType::kSetGroupMemberMuteResponse,
            MessageType::kTransferGroupOwnershipRequest, MessageType::kTransferGroupOwnershipResponse,
            MessageType::kListGroupMembersRequest, MessageType::kListGroupMembersResponse,
            MessageType::kListMyGroupsRequest, MessageType::kListMyGroupsResponse,
        };
        for (const auto type : types) {
            TINYIMX_EXPECT_TRUE(IsKnownMessageType(type));
            TINYIMX_EXPECT_TRUE(MessageTypeToString(type) != "invalid");
        }
        TINYIMX_EXPECT_TRUE(static_cast<std::uint16_t>(MessageType::kCreateGroupRequest) == 2023);
        TINYIMX_EXPECT_TRUE(static_cast<std::uint16_t>(MessageType::kListMyGroupsResponse) == 2048);
    });
}

}  // namespace tinyimx::test
