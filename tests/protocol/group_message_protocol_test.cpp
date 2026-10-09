#include "common/protocol/Packet.h"
#include "tests/concurrency/TestFramework.h"

#include <cstdint>
#include <string>

namespace tinyimx::test {

void RegisterGroupMessageProtocolTests(TestRunner& runner) {
    runner.Add("m17_b1_group_message_types_are_known", []() {
        TINYIMX_EXPECT_TRUE(IsKnownMessageType(MessageType::kGroupMessageSendRequest));
        TINYIMX_EXPECT_TRUE(IsKnownMessageType(MessageType::kGroupMessageSendResponse));
        TINYIMX_EXPECT_TRUE(
            MessageTypeToString(MessageType::kGroupMessageSendRequest) ==
            "group_message_send_request"
        );
        TINYIMX_EXPECT_TRUE(
            MessageTypeToString(MessageType::kGroupMessageSendResponse) ==
            "group_message_send_response"
        );
        TINYIMX_EXPECT_TRUE(
            static_cast<std::uint16_t>(MessageType::kGroupMessageSendRequest) == 2049
        );
        TINYIMX_EXPECT_TRUE(
            static_cast<std::uint16_t>(MessageType::kGroupMessageSendResponse) == 2050
        );
    });
}

}  // namespace tinyimx::test
