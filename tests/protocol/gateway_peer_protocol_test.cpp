#include "tests/concurrency/TestFramework.h"

#include "common/net/Buffer.h"
#include "common/protocol/GatewayPeerProtocol.h"
#include "common/protocol/Packet.h"
#include "common/protocol/ProtocolCodec.h"

#include <cstdint>
#include <string>

namespace tinyimx::test {


void RegisterGatewayPeerProtocolTests(
    TestRunner& runner
) {
    runner.Add(
        "GatewayPeerProtocol.MessageTypes",
        []() {
            TINYIMX_EXPECT_TRUE(
                IsKnownMessageType(
                    MessageType::
                        kGatewayForwardChatRequest
                )
            );

            TINYIMX_EXPECT_TRUE(
                IsKnownMessageType(
                    MessageType::
                        kGatewayForwardChatResponse
                )
            );

            TINYIMX_EXPECT_TRUE(
                IsGatewayInternalMessageType(
                    MessageType::
                        kGatewayForwardChatRequest
                )
            );

            TINYIMX_EXPECT_TRUE(
                !IsGatewayInternalMessageType(
                    MessageType::
                        kChatMessage
                )
            );

            TINYIMX_EXPECT_EQ(
                MessageTypeToString(
                    MessageType::
                        kGatewayForwardChatRequest
                ),
                std::string(
                    "gateway_forward_chat_request"
                )
            );
        }
    );


    runner.Add(
        "GatewayPeerProtocol."
        "ForwardRequestRoundTrip",
        []() {
            GatewayForwardChatRequest input;

            input.message_id =
                5001;

            input.source_gateway_id =
                "gateway-a";

            input.source_lease_token =
                "lease-token-a";

            input.from_user_id =
                10001;

            input.to_user_id =
                10002;

            input.message_body =
                R"({"from":10001,"to":10002,"text":"hello"})";


            std::string body;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                SerializeGatewayForwardChatRequest(
                    input,
                    &body,
                    &error
                )
            );


            GatewayForwardChatRequest output;


            TINYIMX_EXPECT_TRUE(
                DeserializeGatewayForwardChatRequest(
                    body,
                    &output,
                    &error
                )
            );


            TINYIMX_EXPECT_EQ(
                output.message_id,
                input.message_id
            );

            TINYIMX_EXPECT_EQ(
                output.source_gateway_id,
                input.source_gateway_id
            );

            TINYIMX_EXPECT_EQ(
                output.source_lease_token,
                input.source_lease_token
            );

            TINYIMX_EXPECT_EQ(
                output.from_user_id,
                input.from_user_id
            );

            TINYIMX_EXPECT_EQ(
                output.to_user_id,
                input.to_user_id
            );

            TINYIMX_EXPECT_EQ(
                output.message_body,
                input.message_body
            );
        }
    );


    runner.Add(
        "GatewayPeerProtocol."
        "RejectsMissingLeaseToken",
        []() {
            GatewayForwardChatRequest request;

            request.message_id =
                5002;

            request.source_gateway_id =
                "gateway-a";

            request.from_user_id =
                10001;

            request.to_user_id =
                10002;

            request.message_body =
                "{}";


            std::string output;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                !SerializeGatewayForwardChatRequest(
                    request,
                    &output,
                    &error
                )
            );

            TINYIMX_EXPECT_TRUE(
                !error.empty()
            );
        }
    );


    runner.Add(
        "GatewayPeerProtocol."
        "ForwardResponseRoundTrip",
        []() {
            GatewayForwardChatResponse input;

            input.status =
                GatewayForwardChatStatus::
                    kDelivered;

            input.message_id =
                5003;

            input.to_user_id =
                10002;

            input.target_gateway_id =
                "gateway-b";

            input.duplicate =
                false;


            std::string body;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                SerializeGatewayForwardChatResponse(
                    input,
                    &body,
                    &error
                )
            );


            GatewayForwardChatResponse output;


            TINYIMX_EXPECT_TRUE(
                DeserializeGatewayForwardChatResponse(
                    body,
                    &output,
                    &error
                )
            );


            TINYIMX_EXPECT_TRUE(
                output.Delivered()
            );

            TINYIMX_EXPECT_EQ(
                output.message_id,
                static_cast<std::uint64_t>(
                    5003
                )
            );

            TINYIMX_EXPECT_EQ(
                output.to_user_id,
                static_cast<std::uint64_t>(
                    10002
                )
            );

            TINYIMX_EXPECT_EQ(
                output.target_gateway_id,
                std::string(
                    "gateway-b"
                )
            );

            TINYIMX_EXPECT_TRUE(
                !output.duplicate
            );
        }
    );


    runner.Add(
        "GatewayPeerProtocol."
        "ProtocolCodecRoundTrip",
        []() {
            GatewayForwardChatRequest request;

            request.message_id =
                5004;

            request.source_gateway_id =
                "gateway-a";

            request.source_lease_token =
                "lease-token-a";

            request.from_user_id =
                10001;

            request.to_user_id =
                10002;

            request.message_body =
                R"({"text":"cross-gateway"})";


            std::string body;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                SerializeGatewayForwardChatRequest(
                    request,
                    &body,
                    &error
                )
            );


            Packet packet;

            packet.type =
                MessageType::
                    kGatewayForwardChatRequest;

            packet.seq =
                7001;

            packet.body =
                body;


            ProtocolCodec codec;
            Buffer buffer;


            TINYIMX_EXPECT_TRUE(
                codec.Encode(
                    packet,
                    &buffer,
                    &error
                )
            );


            const DecodeResult result =
                codec.Decode(
                    &buffer
                );


            TINYIMX_EXPECT_EQ(
                result.status,
                DecodeStatus::kOk
            );

            TINYIMX_EXPECT_EQ(
                result.packets.size(),
                static_cast<std::size_t>(
                    1
                )
            );


            const Packet& decoded_packet =
                result.packets.front();


            TINYIMX_EXPECT_EQ(
                decoded_packet.type,
                MessageType::
                    kGatewayForwardChatRequest
            );

            TINYIMX_EXPECT_EQ(
                decoded_packet.seq,
                static_cast<std::uint32_t>(
                    7001
                )
            );


            GatewayForwardChatRequest
                decoded_request;


            TINYIMX_EXPECT_TRUE(
                DeserializeGatewayForwardChatRequest(
                    decoded_packet.body,
                    &decoded_request,
                    &error
                )
            );


            TINYIMX_EXPECT_EQ(
                decoded_request.message_id,
                static_cast<std::uint64_t>(
                    5004
                )
            );

            TINYIMX_EXPECT_EQ(
                decoded_request.
                    source_gateway_id,
                std::string(
                    "gateway-a"
                )
            );

            TINYIMX_EXPECT_EQ(
                decoded_request.to_user_id,
                static_cast<std::uint64_t>(
                    10002
                )
            );
        }
    );


    runner.Add(
        "GatewayPeerProtocol."
        "InvalidResponseAllowsZeroUserId",
        []() {
            GatewayForwardChatResponse input;

            input.status =
                GatewayForwardChatStatus::
                    kInvalidRequest;

            input.message_id =
                0;

            input.to_user_id =
                0;

            input.target_gateway_id =
                "gateway-b";

            input.duplicate =
                false;

            input.error_message =
                "invalid request";


            std::string body;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                SerializeGatewayForwardChatResponse(
                    input,
                    &body,
                    &error
                )
            );


            GatewayForwardChatResponse output;


            TINYIMX_EXPECT_TRUE(
                DeserializeGatewayForwardChatResponse(
                    body,
                    &output,
                    &error
                )
            );


            TINYIMX_EXPECT_EQ(
                output.status,
                GatewayForwardChatStatus::
                    kInvalidRequest
            );

            TINYIMX_EXPECT_EQ(
                output.message_id,
                static_cast<std::uint64_t>(0)
            );

            TINYIMX_EXPECT_EQ(
                output.to_user_id,
                static_cast<std::uint64_t>(0)
            );
        }
    );


    runner.Add(
        "GatewayPeerProtocol."
        "RejectsZeroMessageId",
        []() {
            GatewayForwardChatRequest request;

            request.source_gateway_id =
                "gateway-a";

            request.source_lease_token =
                "lease-token-a";

            request.from_user_id =
                10001;

            request.to_user_id =
                10002;

            request.message_body =
                "{}";


            std::string output;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                !SerializeGatewayForwardChatRequest(
                    request,
                    &output,
                    &error
                )
            );

            TINYIMX_EXPECT_TRUE(
                !error.empty()
            );
        }
    );


    runner.Add(
        "GatewayPeerProtocol."
        "DuplicateResponseRoundTrip",
        []() {
            GatewayForwardChatResponse input;

            input.status =
                GatewayForwardChatStatus::
                    kDelivered;

            input.message_id =
                5005;

            input.to_user_id =
                10002;

            input.target_gateway_id =
                "gateway-b";

            input.duplicate =
                true;


            std::string body;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                SerializeGatewayForwardChatResponse(
                    input,
                    &body,
                    &error
                )
            );


            GatewayForwardChatResponse output;


            TINYIMX_EXPECT_TRUE(
                DeserializeGatewayForwardChatResponse(
                    body,
                    &output,
                    &error
                )
            );


            TINYIMX_EXPECT_EQ(
                output.message_id,
                static_cast<std::uint64_t>(
                    5005
                )
            );

            TINYIMX_EXPECT_TRUE(
                output.duplicate
            );

            TINYIMX_EXPECT_TRUE(
                output.Delivered()
            );
        }
    );
}

}  // namespace tinyimx::test