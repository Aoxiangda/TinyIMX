#include "tests/concurrency/TestFramework.h"

#include "common/net/Buffer.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/TcpServer.h"
#include "common/protocol/GatewayGroupPeerProtocol.h"
#include "common/protocol/ProtocolCodec.h"
#include "gateway/GatewayPeerTransport.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace tinyimx::test {

void RegisterGroupPeerDeliveryTests(TestRunner& runner) {
    runner.Add(
        "GatewayPeerTransport.GroupForwardCorrelatesDurableIdentity",
        []() {
            constexpr std::uint16_t kPort = 19016;
            EventLoop loop;
            TcpServer server(&loop, InetAddress("127.0.0.1", kPort), "group-peer-test");
            ProtocolCodec codec;

            server.SetMessageCallback([&](const TcpConnectionPtr& connection, Buffer* buffer) {
                const DecodeResult decoded = codec.Decode(buffer);
                for (const auto& packet : decoded.packets) {
                    if (packet.type != MessageType::kGatewayForwardGroupMessageRequest) {
                        continue;
                    }
                    GatewayForwardGroupMessageRequest request;
                    std::string error;
                    TINYIMX_EXPECT_TRUE(DeserializeGatewayForwardGroupMessageRequest(
                        packet.body, &request, &error));
                    TINYIMX_EXPECT_EQ(request.message_id, static_cast<std::uint64_t>(7001));
                    TINYIMX_EXPECT_EQ(request.recipient_user_id, static_cast<std::uint64_t>(10003));
                    TINYIMX_EXPECT_EQ(request.source_gateway_id, std::string("gateway-a"));

                    GatewayForwardGroupMessageResponse response;
                    response.status = GatewayForwardGroupMessageStatus::kSubmitted;
                    response.message_id = request.message_id;
                    response.recipient_user_id = request.recipient_user_id;
                    response.target_gateway_id = "gateway-b";
                    response.duplicate = false;

                    std::string body;
                    TINYIMX_EXPECT_TRUE(SerializeGatewayForwardGroupMessageResponse(
                        response, &body, &error));
                    Packet out;
                    out.type = MessageType::kGatewayForwardGroupMessageResponse;
                    out.seq = packet.seq;
                    out.body = std::move(body);
                    Buffer encoded;
                    TINYIMX_EXPECT_TRUE(codec.Encode(out, &encoded, &error));
                    connection->Send(encoded.RetrieveAllAsString());
                }
            });
            TINYIMX_EXPECT_TRUE(server.Start());

            GatewayPeerTransportOptions options;
            options.local_gateway_id = "gateway-a";
            options.local_lease_token = "lease-a";
            options.remote_gateway_id = "gateway-b";
            options.remote_host = "127.0.0.1";
            options.remote_port = kPort;
            options.connect_timeout = std::chrono::milliseconds(1000);
            options.request_timeout = std::chrono::milliseconds(1000);

            auto transport = std::make_shared<GatewayPeerTransport>(&loop, options);
            std::atomic<bool> sent{false};
            bool callback_called = false;
            bool succeeded = false;
            std::uint64_t response_recipient = 0;

            transport->SetConnectionStateCallback([&](bool connected) {
                if (!connected || sent.exchange(true)) return;
                TINYIMX_EXPECT_TRUE(transport->ForwardGroupMessage(
                    7001,
                    10003,
                    [&](GatewayGroupPeerTransportResult result) {
                        callback_called = true;
                        succeeded = result.Succeeded() && result.response.Submitted();
                        response_recipient = result.response.recipient_user_id;
                        loop.Quit();
                    }));
            });

            TINYIMX_EXPECT_TRUE(transport->Start());
            bool timed_out = false;
            loop.RunAfter(std::chrono::seconds(3), [&]() {
                timed_out = true;
                loop.Quit();
            });
            loop.Loop();
            transport->Stop();
            server.Stop();

            TINYIMX_EXPECT_TRUE(!timed_out);
            TINYIMX_EXPECT_TRUE(callback_called);
            TINYIMX_EXPECT_TRUE(succeeded);
            TINYIMX_EXPECT_EQ(response_recipient, static_cast<std::uint64_t>(10003));
        }
    );
}

}  // namespace tinyimx::test
