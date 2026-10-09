#include "common/protocol/GroupMessageDeliveryProtocol.h"
#include "common/protocol/GatewayGroupPeerProtocol.h"
#include "common/protocol/Packet.h"
#include "tests/concurrency/TestFramework.h"

namespace tinyimx::test {

void RegisterGroupMessageDeliveryProtocolTests(TestRunner& runner) {
    runner.Add("M17B2.PacketIds", [] {
        TINYIMX_EXPECT_EQ(static_cast<std::uint16_t>(MessageType::kGroupMessageDelivery), 2051);
        TINYIMX_EXPECT_EQ(static_cast<std::uint16_t>(MessageType::kGroupMessageDeliveryAck), 2052);
        TINYIMX_EXPECT_EQ(static_cast<std::uint16_t>(MessageType::kGatewayForwardGroupMessageRequest), 3003);
        TINYIMX_EXPECT_EQ(static_cast<std::uint16_t>(MessageType::kGatewayForwardGroupMessageResponse), 3004);
        TINYIMX_EXPECT_TRUE(IsKnownMessageType(MessageType::kGroupMessageDelivery));
        TINYIMX_EXPECT_TRUE(IsGatewayInternalMessageType(MessageType::kGatewayForwardGroupMessageRequest));
    });

    runner.Add("M17B2.GroupDeliveryRoundTrip", [] {
        GroupMessageDelivery input;
        input.message_id = 101;
        input.group_id = 47;
        input.from_user_id = 10001;
        input.message_type = 1;
        input.content = "hello";
        input.created_at = "2026-09-19 12:00:00.000";
        std::string body;
        TINYIMX_EXPECT_TRUE(SerializeGroupMessageDelivery(input, &body));
        GroupMessageDelivery output;
        TINYIMX_EXPECT_TRUE(DeserializeGroupMessageDelivery(body, &output));
        TINYIMX_EXPECT_EQ(output.message_id, input.message_id);
        TINYIMX_EXPECT_EQ(output.group_id, input.group_id);
        TINYIMX_EXPECT_EQ(output.content, input.content);

        GroupMessageDeliveryAck ack{101};
        TINYIMX_EXPECT_TRUE(SerializeGroupMessageDeliveryAck(ack, &body));
        GroupMessageDeliveryAck ack_out;
        TINYIMX_EXPECT_TRUE(DeserializeGroupMessageDeliveryAck(body, &ack_out));
        TINYIMX_EXPECT_EQ(ack_out.message_id, 101ULL);
    });

    runner.Add("M17B2.GroupPeerIdentityOnlyRoundTrip", [] {
        GatewayForwardGroupMessageRequest input;
        input.message_id = 101;
        input.recipient_user_id = 10002;
        input.source_gateway_id = "gateway-a";
        input.source_lease_token = "lease-a";
        std::string body;
        TINYIMX_EXPECT_TRUE(SerializeGatewayForwardGroupMessageRequest(input, &body));
        TINYIMX_EXPECT_TRUE(body.find("content") == std::string::npos);
        GatewayForwardGroupMessageRequest output;
        TINYIMX_EXPECT_TRUE(DeserializeGatewayForwardGroupMessageRequest(body, &output));
        TINYIMX_EXPECT_EQ(output.message_id, 101ULL);
        TINYIMX_EXPECT_EQ(output.recipient_user_id, 10002ULL);
    });
}

}  // namespace tinyimx::test
