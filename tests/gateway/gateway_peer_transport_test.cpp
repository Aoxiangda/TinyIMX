#include "tests/concurrency/TestFramework.h"

#include "common/net/Buffer.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/TcpConnection.h"
#include "common/net/TcpServer.h"

#include "common/protocol/GatewayPeerProtocol.h"
#include "common/protocol/ProtocolCodec.h"

#include "gateway/GatewayPeerTransport.h"
#include "gateway/GatewayPeerTransportManager.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tinyimx::test {
namespace {


GatewayPeerTransportOptions
MakeOptions(
    std::uint16_t port,
    std::chrono::milliseconds
        request_timeout
) {
    GatewayPeerTransportOptions options;

    options.local_gateway_id =
        "gateway-a";

    options.local_lease_token =
        "lease-token-a";

    options.remote_gateway_id =
        "gateway-b";

    options.remote_host =
        "127.0.0.1";

    options.remote_port =
        port;

    options.connect_timeout =
        std::chrono::milliseconds(
            1000
        );

    options.request_timeout =
        request_timeout;

    return options;
}


bool SendResponseWithStatus(
    const TcpConnectionPtr&
        connection,
    ProtocolCodec* codec,
    const Packet& request_packet,
    std::uint64_t to_user_id,
    GatewayForwardChatStatus status,
    bool duplicate
) {
    if (
        !connection ||
        codec == nullptr
    ) {
        return false;
    }


    GatewayForwardChatRequest request;

    std::string error;


    if (
        !DeserializeGatewayForwardChatRequest(
            request_packet.body,
            &request,
            &error
        )
    ) {
        return false;
    }


    GatewayForwardChatResponse response;

    response.status =
        status;

    response.message_id =
        request.message_id;

    response.to_user_id =
        to_user_id;

    response.target_gateway_id =
        "gateway-b";

    response.duplicate =
        duplicate;


    std::string body;


    if (
        !SerializeGatewayForwardChatResponse(
            response,
            &body,
            &error
        )
    ) {
        return false;
    }


    Packet packet;

    packet.type =
        MessageType::
            kGatewayForwardChatResponse;

    packet.seq =
        request_packet.seq;

    packet.body =
        std::move(body);


    Buffer output;


    if (
        !codec->Encode(
            packet,
            &output,
            &error
        )
    ) {
        return false;
    }


    connection->Send(
        output.RetrieveAllAsString()
    );


    return true;
}


bool SendResponse(
    const TcpConnectionPtr& connection,
    ProtocolCodec* codec,
    const Packet& request_packet,
    std::uint64_t to_user_id,
    bool duplicate = false
) {
    return SendResponseWithStatus(
        connection,
        codec,
        request_packet,
        to_user_id,
        GatewayForwardChatStatus::
            kDelivered,
        duplicate
    );
}
}  // namespace


void RegisterGatewayPeerTransportTests(
    TestRunner& runner
) {
    runner.Add(
        "GatewayPeerTransport."
        "CorrelatesOutOfOrderResponses",
        []() {
            constexpr std::uint16_t
                kPort = 19006;

            EventLoop loop;

            InetAddress address(
                "127.0.0.1",
                kPort
            );

            TcpServer server(
                &loop,
                address,
                "gateway-peer-test-server"
            );

            ProtocolCodec server_codec;

            std::vector<Packet>
                received_requests;

            server.SetMessageCallback(
                [&](
                    const TcpConnectionPtr&
                        connection,
                    Buffer* buffer
                ) {
                    const DecodeResult decoded =
                        server_codec.Decode(
                            buffer
                        );

                    if (
                        decoded.status !=
                        DecodeStatus::kOk
                    ) {
                        return;
                    }

                    for (
                        const Packet& packet :
                        decoded.packets
                    ) {
                        if (
                            packet.type !=
                            MessageType::
                                kGatewayForwardChatRequest
                        ) {
                            continue;
                        }

                        received_requests.
                            push_back(
                                packet
                            );
                    }

                    /*
                     * 收到两个以后，
                     * 故意反过来返回。
                     */
                    if (
                        received_requests.size()
                        == 2
                    ) {
                        GatewayForwardChatRequest
                            second_request;

                        GatewayForwardChatRequest
                            first_request;

                        std::string error;

                        const bool second_ok =
                            DeserializeGatewayForwardChatRequest(
                                received_requests[1].
                                    body,
                                &second_request,
                                &error
                            );

                        const bool first_ok =
                            DeserializeGatewayForwardChatRequest(
                                received_requests[0].
                                    body,
                                &first_request,
                                &error
                            );

                        if (
                            !second_ok ||
                            !first_ok
                        ) {
                            return;
                        }

                        SendResponse(
                            connection,
                            &server_codec,
                            received_requests[1],
                            second_request.
                                to_user_id
                        );

                        SendResponse(
                            connection,
                            &server_codec,
                            received_requests[0],
                            first_request.
                                to_user_id
                        );
                    }
                }
            );

            TINYIMX_EXPECT_TRUE(
                server.Start()
            );

            auto transport =
                std::make_shared<
                    GatewayPeerTransport
                >(
                    &loop,
                    MakeOptions(
                        kPort,
                        std::chrono::
                            milliseconds(
                                1000
                            )
                    )
                );

            std::atomic<int>
                callback_count{0};

            std::uint64_t
                first_response_user = 0;

            std::uint64_t
                second_response_user = 0;

            bool first_success = false;
            bool second_success = false;

            std::atomic<bool>
                requests_sent{false};

            transport->
                SetConnectionStateCallback(
                    [&](bool connected) {
                        if (!connected) {
                            return;
                        }

                        if (
                            requests_sent.exchange(
                                true
                            )
                        ) {
                            return;
                        }

                        const bool first_accepted =
                            transport->
                                ForwardChat(
                                    5001,
                                    10001,
                                    10002,
                                    R"({"text":"first"})",
                                    [&](
                                        GatewayPeerTransportResult
                                            result
                                    ) {
                                        first_success =
                                            result.Succeeded();

                                        if (
                                            result.Succeeded()
                                        ) {
                                            first_response_user =
                                                result.response.
                                                    to_user_id;
                                        }

                                        callback_count.
                                            fetch_add(1);
                                    }
                                );

                        const bool second_accepted =
                            transport->
                                ForwardChat(
                                    5002,
                                    10003,
                                    10004,
                                    R"({"text":"second"})",
                                    [&](
                                        GatewayPeerTransportResult
                                            result
                                    ) {
                                        second_success =
                                            result.Succeeded();

                                        if (
                                            result.Succeeded()
                                        ) {
                                            second_response_user =
                                                result.response.
                                                    to_user_id;
                                        }

                                        callback_count.
                                            fetch_add(1);
                                    }
                                );

                        if (
                            !first_accepted ||
                            !second_accepted
                        ) {
                            loop.Quit();
                        }
                    }
                );

            TINYIMX_EXPECT_TRUE(
                transport->Start()
            );

            bool timed_out = false;

            loop.RunEvery(
                std::chrono::
                    milliseconds(10),
                [&]() {
                    if (
                        callback_count.load()
                        == 2
                    ) {
                        loop.Quit();
                    }
                }
            );

            loop.RunAfter(
                std::chrono::seconds(3),
                [&]() {
                    timed_out = true;
                    loop.Quit();
                }
            );

            loop.Loop();

            transport->Stop();
            server.Stop();

            TINYIMX_EXPECT_TRUE(
                !timed_out
            );

            TINYIMX_EXPECT_TRUE(
                first_success
            );

            TINYIMX_EXPECT_TRUE(
                second_success
            );

            TINYIMX_EXPECT_EQ(
                first_response_user,
                static_cast<std::uint64_t>(
                    10002
                )
            );

            TINYIMX_EXPECT_EQ(
                second_response_user,
                static_cast<std::uint64_t>(
                    10004
                )
            );

            TINYIMX_EXPECT_EQ(
                callback_count.load(),
                2
            );

            TINYIMX_EXPECT_EQ(
                transport->PendingCount(),
                static_cast<std::size_t>(
                    0
                )
            );
        }
    );


    runner.Add(
        "GatewayPeerTransport."
        "RequestTimeout",
        []() {
            constexpr std::uint16_t
                kPort = 19007;

            EventLoop loop;

            InetAddress address(
                "127.0.0.1",
                kPort
            );

            TcpServer server(
                &loop,
                address,
                "gateway-peer-timeout-server"
            );

            ProtocolCodec server_codec;

            std::atomic<int>
                request_count{0};

            /*
             * 服务器接收 Request，
             * 但故意不返回 Response。
             */
            server.SetMessageCallback(
                [&](
                    const TcpConnectionPtr&,
                    Buffer* buffer
                ) {
                    const DecodeResult decoded =
                        server_codec.Decode(
                            buffer
                        );

                    if (
                        decoded.status !=
                        DecodeStatus::kOk
                    ) {
                        return;
                    }

                    for (
                        const Packet& packet :
                        decoded.packets
                    ) {
                        if (
                            packet.type ==
                            MessageType::
                                kGatewayForwardChatRequest
                        ) {
                            request_count.
                                fetch_add(1);
                        }
                    }
                }
            );

            TINYIMX_EXPECT_TRUE(
                server.Start()
            );

            auto transport =
                std::make_shared<
                    GatewayPeerTransport
                >(
                    &loop,
                    MakeOptions(
                        kPort,
                        std::chrono::
                            milliseconds(
                                100
                            )
                    )
                );

            std::atomic<bool>
                request_sent{false};

            bool callback_called = false;

            GatewayPeerTransportStatus
                observed_status =
                    GatewayPeerTransportStatus::
                        kOk;

            transport->
                SetConnectionStateCallback(
                    [&](bool connected) {
                        if (!connected) {
                            return;
                        }

                        if (
                            request_sent.exchange(
                                true
                            )
                        ) {
                            return;
                        }

                        transport->
                            ForwardChat(
                                5003,
                                10001,
                                10002,
                                R"({"text":"timeout"})",
                                [&](
                                    GatewayPeerTransportResult
                                        result
                                ) {
                                    callback_called =
                                        true;

                                    observed_status =
                                        result.status;

                                    loop.Quit();
                                }
                            );
                    }
                );

            TINYIMX_EXPECT_TRUE(
                transport->Start()
            );

            bool global_timeout = false;

            loop.RunAfter(
                std::chrono::seconds(3),
                [&]() {
                    global_timeout = true;
                    loop.Quit();
                }
            );

            loop.Loop();

            transport->Stop();
            server.Stop();

            TINYIMX_EXPECT_TRUE(
                !global_timeout
            );

            TINYIMX_EXPECT_TRUE(
                callback_called
            );

            TINYIMX_EXPECT_EQ(
                request_count.load(),
                1
            );

            TINYIMX_EXPECT_EQ(
                observed_status,
                GatewayPeerTransportStatus::
                    kRequestTimeout
            );

            TINYIMX_EXPECT_EQ(
                transport->PendingCount(),
                static_cast<std::size_t>(
                    0
                )
            );
        }
    );


        runner.Add(
        "GatewayPeerTransport."
        "TimeoutAfterRemoteExecution",
        []() {
            constexpr std::uint16_t
                kPort = 19010;

            constexpr std::uint64_t
                kMessageId = 5004;


            EventLoop loop;


            InetAddress address(
                "127.0.0.1",
                kPort
            );


            TcpServer server(
                &loop,
                address,
                "gateway-peer-response-loss-server"
            );


            ProtocolCodec server_codec;


            std::atomic<int>
                request_count{0};

            bool remote_side_effect_executed =
                false;

            bool response_built_but_dropped =
                false;

            std::uint64_t
                executed_message_id = 0;


            /*
             * 这里模拟最危险的分布式故障：
             *
             * Request已经到达Gateway B，
             * B的业务副作用已经执行，
             * Delivered Response也已经成功
             * 序列化、编码。
             *
             * 但是最后故意不调用：
             *
             * connection->Send(...)
             *
             * 相当于Response在返回A之前丢失。
             */
            server.SetMessageCallback(
                [&](
                    const TcpConnectionPtr&,
                    Buffer* buffer
                ) {
                    if (buffer == nullptr) {
                        return;
                    }


                    const DecodeResult decoded =
                        server_codec.Decode(
                            buffer
                        );


                    if (
                        decoded.status !=
                        DecodeStatus::kOk
                    ) {
                        return;
                    }


                    for (
                        const Packet& packet :
                        decoded.packets
                    ) {
                        if (
                            packet.type !=
                            MessageType::
                                kGatewayForwardChatRequest
                        ) {
                            continue;
                        }


                        GatewayForwardChatRequest
                            request;

                        std::string error;


                        if (
                            !DeserializeGatewayForwardChatRequest(
                                packet.body,
                                &request,
                                &error
                            )
                        ) {
                            continue;
                        }


                        request_count.fetch_add(
                            1
                        );


                        /*
                         * 模拟：
                         *
                         * Gateway B真正业务操作
                         * 已经执行完成。
                         *
                         * 真实生产代码里这里对应：
                         *
                         * SendPacket(user)
                         * +
                         * MySQL MarkDelivered
                         */
                        remote_side_effect_executed =
                            true;

                        executed_message_id =
                            request.message_id;


                        /*
                         * B准备生成正常的
                         * Delivered Response。
                         */
                        GatewayForwardChatResponse
                            response;


                        response.status =
                            GatewayForwardChatStatus::
                                kDelivered;

                        response.message_id =
                            request.message_id;

                        response.to_user_id =
                            request.to_user_id;

                        response.target_gateway_id =
                            "gateway-b";

                        response.duplicate =
                            false;


                        std::string
                            response_body;


                        if (
                            !SerializeGatewayForwardChatResponse(
                                response,
                                &response_body,
                                &error
                            )
                        ) {
                            continue;
                        }


                        Packet response_packet;

                        response_packet.type =
                            MessageType::
                                kGatewayForwardChatResponse;

                        /*
                         * 如果真正发送，
                         * Response必须与原Request
                         * 使用同一个RPC seq。
                         */
                        response_packet.seq =
                            packet.seq;

                        response_packet.body =
                            std::move(
                                response_body
                            );


                        Buffer output;


                        if (
                            !server_codec.Encode(
                                response_packet,
                                &output,
                                &error
                            )
                        ) {
                            continue;
                        }


                        /*
                         * 到这里说明：
                         *
                         * 远端业务已执行，
                         * Response也完全合法。
                         */
                        response_built_but_dropped =
                            true;


                        /*
                         * 关键故障注入：
                         *
                         * 故意不执行：
                         *
                         * connection->Send(
                         *     output.RetrieveAllAsString()
                         * );
                         *
                         * 相当于Response Loss。
                         */
                    }
                }
            );


            TINYIMX_EXPECT_TRUE(
                server.Start()
            );


            auto transport =
                std::make_shared<
                    GatewayPeerTransport
                >(
                    &loop,
                    MakeOptions(
                        kPort,
                        std::chrono::
                            milliseconds(
                                100
                            )
                    )
                );


            std::atomic<bool>
                request_sent{false};


            bool callback_called =
                false;


            GatewayPeerTransportStatus
                observed_status =
                    GatewayPeerTransportStatus::
                        kOk;


            transport->
                SetConnectionStateCallback(
                    [&](bool connected) {
                        if (!connected) {
                            return;
                        }


                        if (
                            request_sent.exchange(
                                true
                            )
                        ) {
                            return;
                        }


                        const bool accepted =
                            transport->
                                ForwardChat(
                                    kMessageId,
                                    10001,
                                    10002,
                                    R"({"text":"response-loss"})",
                                    [&](
                                        GatewayPeerTransportResult
                                            result
                                    ) {
                                        callback_called =
                                            true;

                                        observed_status =
                                            result.status;

                                        loop.Quit();
                                    }
                                );


                        if (!accepted) {
                            loop.Quit();
                        }
                    }
                );


            TINYIMX_EXPECT_TRUE(
                transport->Start()
            );


            bool global_timeout =
                false;


            loop.RunAfter(
                std::chrono::seconds(3),
                [&]() {
                    global_timeout =
                        true;

                    loop.Quit();
                }
            );


            loop.Loop();


            transport->Stop();

            server.Stop();


            /*
             * 整个测试最关键的断言：
             *
             * A看见Timeout，
             * 但B实际上已经执行。
             */
            TINYIMX_EXPECT_TRUE(
                !global_timeout
            );


            TINYIMX_EXPECT_TRUE(
                callback_called
            );


            TINYIMX_EXPECT_EQ(
                request_count.load(),
                1
            );


            TINYIMX_EXPECT_TRUE(
                remote_side_effect_executed
            );


            TINYIMX_EXPECT_TRUE(
                response_built_but_dropped
            );


            TINYIMX_EXPECT_EQ(
                executed_message_id,
                kMessageId
            );


            TINYIMX_EXPECT_EQ(
                observed_status,
                GatewayPeerTransportStatus::
                    kRequestTimeout
            );


            TINYIMX_EXPECT_EQ(
                transport->PendingCount(),
                static_cast<std::size_t>(
                    0
                )
            );
        }
    );


        runner.Add(
        "GatewayPeerTransportManager."
        "RetriesTimeoutWithSameMessageId",
        []() {
            constexpr std::uint16_t
                kPort = 19011;

            constexpr std::uint64_t
                kMessageId = 5010;


            EventLoop loop;


            InetAddress address(
                "127.0.0.1",
                kPort
            );


            TcpServer server(
                &loop,
                address,
                "gateway-peer-safe-retry-server"
            );


            ProtocolCodec server_codec;


            std::atomic<int>
                request_count{0};

            std::atomic<int>
                side_effect_count{0};


            std::uint32_t
                first_sequence = 0;

            std::uint32_t
                second_sequence = 0;


            std::uint64_t
                first_message_id = 0;

            std::uint64_t
                second_message_id = 0;


            server.SetMessageCallback(
                [&](
                    const TcpConnectionPtr&
                        connection,
                    Buffer* buffer
                ) {
                    if (buffer == nullptr) {
                        return;
                    }


                    const DecodeResult decoded =
                        server_codec.Decode(
                            buffer
                        );


                    if (
                        decoded.status !=
                        DecodeStatus::kOk
                    ) {
                        return;
                    }


                    for (
                        const Packet& packet :
                        decoded.packets
                    ) {
                        if (
                            packet.type !=
                            MessageType::
                                kGatewayForwardChatRequest
                        ) {
                            continue;
                        }


                        GatewayForwardChatRequest
                            request;

                        std::string error;


                        if (
                            !DeserializeGatewayForwardChatRequest(
                                packet.body,
                                &request,
                                &error
                            )
                        ) {
                            continue;
                        }


                        const int attempt =
                            request_count.
                                fetch_add(1) +
                            1;


                        if (attempt == 1) {
                            /*
                             * 第一次Request：
                             *
                             * 模拟B已经真正执行，
                             * 但Response Loss。
                             */
                            first_sequence =
                                packet.seq;

                            first_message_id =
                                request.message_id;


                            side_effect_count.
                                fetch_add(1);


                            /*
                             * 不返回Response。
                             *
                             * A最终触发Timeout。
                             */
                            continue;
                        }


                        if (attempt == 2) {
                            /*
                             * Retry：
                             *
                             * 模拟B通过幂等状态发现
                             * message_id已经执行过。
                             */
                            second_sequence =
                                packet.seq;

                            second_message_id =
                                request.message_id;


                            /*
                             * 注意：
                             *
                             * 第二次绝对不能再次增加
                             * side_effect_count。
                             */


                            SendResponse(
                                connection,
                                &server_codec,
                                packet,
                                request.to_user_id,
                                true
                            );
                        }
                    }
                }
            );


            TINYIMX_EXPECT_TRUE(
                server.Start()
            );


            GatewayPeerTransportManagerOptions
                options;


            options.local_gateway_id =
                "gateway-a";

            options.local_lease_token =
                "lease-token-a";

            options.request_timeout =
                std::chrono::
                    milliseconds(100);

            options.max_request_retries =
                1;


            auto manager =
                std::make_shared<
                    GatewayPeerTransportManager
                >(
                    &loop,
                    options
                );


            TINYIMX_EXPECT_TRUE(
                manager->Start()
            );


            GatewayInstanceRecord
                remote_gateway;


            remote_gateway.gateway_id =
                "gateway-b";

            remote_gateway.lease_token =
                "lease-token-b";

            remote_gateway.listen_host =
                "127.0.0.1";

            remote_gateway.listen_port =
                kPort;


            bool callback_called =
                false;

            std::atomic<int>
                callback_count{0};


            GatewayPeerTransportStatus
                final_status =
                    GatewayPeerTransportStatus::
                        kProtocolError;


            bool final_delivered =
                false;

            bool final_duplicate =
                false;

            std::uint64_t
                final_message_id = 0;


            const bool submitted =
                manager->ForwardChat(
                    remote_gateway,
                    kMessageId,
                    10001,
                    10002,
                    R"({"text":"safe-retry"})",
                    [&](
                        GatewayPeerTransportResult
                            result
                    ) {
                        callback_called =
                            true;

                        callback_count.
                            fetch_add(1);

                        final_status =
                            result.status;

                        final_delivered =
                            result.Succeeded() &&
                            result.response.
                                Delivered();

                        final_duplicate =
                            result.response.
                                duplicate;

                        final_message_id =
                            result.response.
                                message_id;


                        loop.Quit();
                    }
                );


            TINYIMX_EXPECT_TRUE(
                submitted
            );


            bool global_timeout =
                false;


            loop.RunAfter(
                std::chrono::seconds(3),
                [&]() {
                    global_timeout =
                        true;

                    loop.Quit();
                }
            );


            loop.Loop();


            manager->Stop();

            server.Stop();


            TINYIMX_EXPECT_TRUE(
                !global_timeout
            );


            /*
             * 两次RPC Attempt。
             */
            TINYIMX_EXPECT_EQ(
                request_count.load(),
                2
            );


            /*
             * 业务副作用只有第一次真正执行。
             */
            TINYIMX_EXPECT_EQ(
                side_effect_count.load(),
                1
            );


            /*
             * 最核心不变量：
             *
             * Retry业务ID不变。
             */
            TINYIMX_EXPECT_EQ(
                first_message_id,
                kMessageId
            );

            TINYIMX_EXPECT_EQ(
                second_message_id,
                kMessageId
            );


            /*
             * 但RPC Attempt ID必须变化。
             */
            TINYIMX_EXPECT_TRUE(
                first_sequence != 0
            );

            TINYIMX_EXPECT_TRUE(
                second_sequence != 0
            );

            TINYIMX_EXPECT_TRUE(
                first_sequence !=
                second_sequence
            );


            /*
             * 上层只得到一个最终结果。
             */
            TINYIMX_EXPECT_TRUE(
                callback_called
            );

            TINYIMX_EXPECT_EQ(
                callback_count.load(),
                1
            );


            TINYIMX_EXPECT_EQ(
                final_status,
                GatewayPeerTransportStatus::
                    kOk
            );


            TINYIMX_EXPECT_TRUE(
                final_delivered
            );


            /*
             * B告诉A：
             *
             * 业务已经在第一次Attempt
             * 执行过。
             */
            TINYIMX_EXPECT_TRUE(
                final_duplicate
            );


            TINYIMX_EXPECT_EQ(
                final_message_id,
                kMessageId
            );
        }
    );

    runner.Add(
        "GatewayPeerTransportManager."
        "ReconnectsAndRefreshesEndpoint",
        []() {
            constexpr std::uint16_t
                kFirstPort = 19008;

            constexpr std::uint16_t
                kSecondPort = 19009;


            EventLoop loop;


            InetAddress first_address(
                "127.0.0.1",
                kFirstPort
            );

            InetAddress second_address(
                "127.0.0.1",
                kSecondPort
            );


            TcpServer first_server(
                &loop,
                first_address,
                "gateway-peer-reconnect-first"
            );

            TcpServer second_server(
                &loop,
                second_address,
                "gateway-peer-reconnect-second"
            );


            ProtocolCodec first_codec;
            ProtocolCodec second_codec;


            /*
            * 两个Fake Gateway Server
            * 都执行相同RPC：
            *
            * Request
            * → Delivered Response
            */
            auto install_rpc_handler =
                [](
                    TcpServer* server,
                    ProtocolCodec* codec
                ) {
                    server->
                        SetMessageCallback(
                            [
                                codec
                            ](
                                const TcpConnectionPtr&
                                    connection,
                                Buffer* buffer
                            ) {
                                if (
                                    codec == nullptr ||
                                    buffer == nullptr
                                ) {
                                    return;
                                }


                                const DecodeResult
                                    decoded =
                                        codec->Decode(
                                            buffer
                                        );


                                if (
                                    decoded.status !=
                                    DecodeStatus::kOk
                                ) {
                                    return;
                                }


                                for (
                                    const Packet& packet :
                                    decoded.packets
                                ) {
                                    if (
                                        packet.type !=
                                        MessageType::
                                            kGatewayForwardChatRequest
                                    ) {
                                        continue;
                                    }


                                    GatewayForwardChatRequest
                                        request;

                                    std::string error;


                                    if (
                                        !DeserializeGatewayForwardChatRequest(
                                            packet.body,
                                            &request,
                                            &error
                                        )
                                    ) {
                                        continue;
                                    }


                                    SendResponse(
                                        connection,
                                        codec,
                                        packet,
                                        request.to_user_id
                                    );
                                }
                            }
                        );
                };


            install_rpc_handler(
                &first_server,
                &first_codec
            );

            install_rpc_handler(
                &second_server,
                &second_codec
            );


            /*
            * Discovery最初认为：
            *
            * gateway-b = :19008
            */
            std::atomic<std::uint16_t>
                discovery_port{
                    kFirstPort
                };


            GatewayPeerTransportManagerOptions
                manager_options;

            manager_options.local_gateway_id =
                "gateway-a";

            manager_options.local_lease_token =
                "lease-token-a";

            manager_options.connect_timeout =
                std::chrono::milliseconds(
                    300
                );

            manager_options.request_timeout =
                std::chrono::milliseconds(
                    1000
                );

            /*
            * 测试缩短退避时间。
            */
            manager_options.
                reconnect_initial_delay =
                    std::chrono::
                        milliseconds(50);

            manager_options.
                reconnect_max_delay =
                    std::chrono::
                        milliseconds(200);


            auto manager =
                std::make_shared<
                    GatewayPeerTransportManager
                >(
                    &loop,
                    manager_options
                );


            manager->
                SetGatewayResolverCallback(
                    [
                        &discovery_port
                    ](
                        const std::string&
                            gateway_id
                    )
                        -> std::optional<
                            GatewayInstanceRecord
                        > {
                        if (
                            gateway_id !=
                            "gateway-b"
                        ) {
                            return std::nullopt;
                        }


                        GatewayInstanceRecord
                            record;

                        record.gateway_id =
                            "gateway-b";

                        record.lease_token =
                            "lease-token-b";

                        record.listen_host =
                            "127.0.0.1";

                        record.listen_port =
                            discovery_port.load(
                                std::memory_order_acquire
                            );

                        return record;
                    }
                );


            TINYIMX_EXPECT_TRUE(
                first_server.Start()
            );

            TINYIMX_EXPECT_TRUE(
                manager->Start()
            );


            std::atomic<int>
                second_server_connections{0};

            std::atomic<bool>
                second_request_sent{false};


            bool first_success = false;
            bool second_success = false;

            bool second_server_start_ok =
                false;


            /*
            * 当Manager自动连接到新Endpoint
            * :19009以后，Fake Server会看到
            * 第二条TCP连接。
            *
            * 到这里再发第二次RPC。
            */
            second_server.
                SetConnectionCallback(
                    [&](
                        const TcpConnectionPtr&
                            connection
                    ) {
                        if (
                            !connection ||
                            !connection->
                                IsConnected()
                        ) {
                            return;
                        }


                        second_server_connections.
                            fetch_add(
                                1,
                                std::memory_order_relaxed
                            );


                        if (
                            second_request_sent.
                                exchange(true)
                        ) {
                            return;
                        }


                        GatewayInstanceRecord
                            record;

                        record.gateway_id =
                            "gateway-b";

                        record.lease_token =
                            "lease-token-b";

                        record.listen_host =
                            "127.0.0.1";

                        record.listen_port =
                            kSecondPort;


                        const bool accepted =
                            manager->ForwardChat(
                                record,
                                5005,
                                10001,
                                10002,
                                R"({"text":"after-reconnect"})",
                                [&](
                                    GatewayPeerTransportResult
                                        result
                                ) {
                                    second_success =
                                        result.Succeeded() &&
                                        result.response.
                                            Delivered();

                                    loop.Quit();
                                }
                            );


                        if (!accepted) {
                            loop.Quit();
                        }
                    }
                );


            /*
            * 第一条请求走19008。
            */
            GatewayInstanceRecord
                first_record;

            first_record.gateway_id =
                "gateway-b";

            first_record.lease_token =
                "lease-token-b";

            first_record.listen_host =
                "127.0.0.1";

            first_record.listen_port =
                kFirstPort;


            TINYIMX_EXPECT_TRUE(
                manager->ForwardChat(
                    first_record,
                    5004,
                    10001,
                    10002,
                    R"({"text":"before-restart"})",
                    [&](
                        GatewayPeerTransportResult
                            result
                    ) {
                        first_success =
                            result.Succeeded() &&
                            result.response.
                                Delivered();


                        if (!first_success) {
                            loop.Quit();
                            return;
                        }


                        /*
                        * 模拟：
                        *
                        * gateway-b旧实例死亡。
                        */
                        first_server.Stop();


                        /*
                        * Discovery已经看到新实例：
                        *
                        * gateway-b -> :19009
                        */
                        discovery_port.store(
                            kSecondPort,
                            std::memory_order_release
                        );


                        /*
                        * 但新Gateway不是立刻起来。
                        *
                        * 让第一次reconnect真实失败，
                        * 从而验证Backoff。
                        */
                        loop.RunAfter(
                            std::chrono::
                                milliseconds(80),
                            [&]() {
                                second_server_start_ok =
                                    second_server.Start();
                            }
                        );
                    }
                )
            );


            bool global_timeout = false;


            loop.RunAfter(
                std::chrono::seconds(3),
                [&]() {
                    global_timeout = true;
                    loop.Quit();
                }
            );


            loop.Loop();


            manager->Stop();

            first_server.Stop();
            second_server.Stop();


            TINYIMX_EXPECT_TRUE(
                !global_timeout
            );

            TINYIMX_EXPECT_TRUE(
                first_success
            );

            TINYIMX_EXPECT_TRUE(
                second_server_start_ok
            );

            TINYIMX_EXPECT_TRUE(
                second_success
            );

            TINYIMX_EXPECT_TRUE(
                second_server_connections.
                    load() >= 1
            );

            /*
            * gateway-b只是Endpoint发生变化，
            * Manager最终仍只维护一个Peer。
            */
            TINYIMX_EXPECT_EQ(
                manager->TransportCount(),
                static_cast<std::size_t>(0)
            );
        }
    );

    runner.Add(
    "GatewayPeerTransportManager."
    "RetriesDuplicateInProgressWithBackoff",
    []() {
        constexpr std::uint16_t
            kPort = 19012;

        constexpr std::uint64_t
            kMessageId = 5011;


        EventLoop loop;


        TcpServer server(
            &loop,
            InetAddress(
                "127.0.0.1",
                kPort
            ),
            "gateway-peer-in-progress-server"
        );


        ProtocolCodec server_codec;


        std::atomic<int>
            request_count{0};


        std::uint32_t first_seq = 0;
        std::uint32_t second_seq = 0;

        std::uint64_t first_message_id = 0;
        std::uint64_t second_message_id = 0;


        std::chrono::
            steady_clock::time_point
                first_response_time;

        std::chrono::
            steady_clock::time_point
                second_request_time;


        server.SetMessageCallback(
            [&](
                const TcpConnectionPtr&
                    connection,
                Buffer* buffer
            ) {
                if (buffer == nullptr) {
                    return;
                }


                const DecodeResult decoded =
                    server_codec.Decode(
                        buffer
                    );


                if (
                    decoded.status !=
                    DecodeStatus::kOk
                ) {
                    return;
                }


                for (
                    const Packet& packet :
                    decoded.packets
                ) {
                    if (
                        packet.type !=
                        MessageType::
                            kGatewayForwardChatRequest
                    ) {
                        continue;
                    }


                    GatewayForwardChatRequest
                        request;

                    std::string error;


                    if (
                        !DeserializeGatewayForwardChatRequest(
                            packet.body,
                            &request,
                            &error
                        )
                    ) {
                        continue;
                    }


                    const int attempt =
                        request_count.
                            fetch_add(1) +
                        1;


                    if (attempt == 1) {
                        first_seq =
                            packet.seq;

                        first_message_id =
                            request.message_id;


                        first_response_time =
                            std::chrono::
                                steady_clock::now();


                        SendResponseWithStatus(
                            connection,
                            &server_codec,
                            packet,
                            request.to_user_id,
                            GatewayForwardChatStatus::
                                kDuplicateInProgress,
                            true
                        );

                        continue;
                    }


                    if (attempt == 2) {
                        second_seq =
                            packet.seq;

                        second_message_id =
                            request.message_id;


                        second_request_time =
                            std::chrono::
                                steady_clock::now();


                        SendResponseWithStatus(
                            connection,
                            &server_codec,
                            packet,
                            request.to_user_id,
                            GatewayForwardChatStatus::
                                kDelivered,
                            true
                        );
                    }
                }
            }
        );


        TINYIMX_EXPECT_TRUE(
            server.Start()
        );


        GatewayPeerTransportManagerOptions
            options;


        options.local_gateway_id =
            "gateway-a";

        options.local_lease_token =
            "lease-token-a";

        options.request_timeout =
            std::chrono::
                milliseconds(500);

        options.max_request_retries =
            1;

        options.request_retry_initial_delay =
            std::chrono::
                milliseconds(60);

        options.request_retry_max_delay =
            std::chrono::
                milliseconds(60);


        auto manager =
            std::make_shared<
                GatewayPeerTransportManager
            >(
                &loop,
                options
            );


        TINYIMX_EXPECT_TRUE(
            manager->Start()
        );


        GatewayInstanceRecord
            remote_gateway;

        remote_gateway.gateway_id =
            "gateway-b";

        remote_gateway.lease_token =
            "lease-token-b";

        remote_gateway.listen_host =
            "127.0.0.1";

        remote_gateway.listen_port =
            kPort;


        int callback_count = 0;

        GatewayPeerTransportResult
            final_result;


        TINYIMX_EXPECT_TRUE(
            manager->ForwardChat(
                remote_gateway,
                kMessageId,
                10001,
                10002,
                R"({"text":"in-progress"})",
                [&](
                    GatewayPeerTransportResult
                        result
                ) {
                    ++callback_count;

                    final_result =
                        std::move(result);

                    loop.Quit();
                }
            )
        );


        bool global_timeout = false;


        loop.RunAfter(
            std::chrono::seconds(3),
            [&]() {
                global_timeout = true;

                loop.Quit();
            }
        );


        loop.Loop();


        manager->Stop();
        server.Stop();


        TINYIMX_EXPECT_TRUE(
            !global_timeout
        );


        TINYIMX_EXPECT_EQ(
            request_count.load(),
            2
        );


        TINYIMX_EXPECT_EQ(
            callback_count,
            1
        );


        TINYIMX_EXPECT_EQ(
            first_message_id,
            kMessageId
        );

        TINYIMX_EXPECT_EQ(
            second_message_id,
            kMessageId
        );


        TINYIMX_EXPECT_TRUE(
            first_seq != 0
        );

        TINYIMX_EXPECT_TRUE(
            second_seq != 0
        );

        TINYIMX_EXPECT_TRUE(
            first_seq !=
            second_seq
        );


        /*
         * 配置60ms退避。
         *
         * 留一点调度裕量，
         * 验证不是立即Retry。
         */
        const auto retry_gap =
            std::chrono::
                duration_cast<
                    std::chrono::milliseconds
                >(
                    second_request_time -
                    first_response_time
                );


        TINYIMX_EXPECT_TRUE(
            retry_gap >=
            std::chrono::
                milliseconds(30)
        );


        TINYIMX_EXPECT_EQ(
            final_result.status,
            GatewayPeerTransportStatus::
                kOk
        );


        TINYIMX_EXPECT_TRUE(
            final_result.response.
                Delivered()
        );


        TINYIMX_EXPECT_TRUE(
            final_result.response.
                duplicate
        );


        TINYIMX_EXPECT_EQ(
            final_result.response.
                message_id,
            kMessageId
        );
    }
);


runner.Add(
    "GatewayPeerTransportManager."
    "DoesNotRetryUnauthorized",
    []() {
        constexpr std::uint16_t
            kPort = 19013;

        constexpr std::uint64_t
            kMessageId = 5012;


        EventLoop loop;


        TcpServer server(
            &loop,
            InetAddress(
                "127.0.0.1",
                kPort
            ),
            "gateway-peer-no-retry-server"
        );


        ProtocolCodec server_codec;


        std::atomic<int>
            request_count{0};


        server.SetMessageCallback(
            [&](
                const TcpConnectionPtr&
                    connection,
                Buffer* buffer
            ) {
                if (buffer == nullptr) {
                    return;
                }


                const DecodeResult decoded =
                    server_codec.Decode(
                        buffer
                    );


                if (
                    decoded.status !=
                    DecodeStatus::kOk
                ) {
                    return;
                }


                for (
                    const Packet& packet :
                    decoded.packets
                ) {
                    if (
                        packet.type !=
                        MessageType::
                            kGatewayForwardChatRequest
                    ) {
                        continue;
                    }


                    GatewayForwardChatRequest
                        request;

                    std::string error;


                    if (
                        !DeserializeGatewayForwardChatRequest(
                            packet.body,
                            &request,
                            &error
                        )
                    ) {
                        continue;
                    }


                    request_count.
                        fetch_add(1);


                    SendResponseWithStatus(
                        connection,
                        &server_codec,
                        packet,
                        request.to_user_id,
                        GatewayForwardChatStatus::
                            kUnauthorized,
                        false
                    );
                }
            }
        );


        TINYIMX_EXPECT_TRUE(
            server.Start()
        );


        GatewayPeerTransportManagerOptions
            options;


        options.local_gateway_id =
            "gateway-a";

        options.local_lease_token =
            "lease-token-a";

        options.request_timeout =
            std::chrono::
                milliseconds(500);

        /*
         * 即使允许Retry，
         * Unauthorized也必须禁止。
         */
        options.max_request_retries =
            1;

        options.request_retry_initial_delay =
            std::chrono::
                milliseconds(50);

        options.request_retry_max_delay =
            std::chrono::
                milliseconds(50);


        auto manager =
            std::make_shared<
                GatewayPeerTransportManager
            >(
                &loop,
                options
            );


        TINYIMX_EXPECT_TRUE(
            manager->Start()
        );


        GatewayInstanceRecord
            remote_gateway;

        remote_gateway.gateway_id =
            "gateway-b";

        remote_gateway.lease_token =
            "lease-token-b";

        remote_gateway.listen_host =
            "127.0.0.1";

        remote_gateway.listen_port =
            kPort;


        int callback_count = 0;

        GatewayPeerTransportResult
            final_result;


        TINYIMX_EXPECT_TRUE(
            manager->ForwardChat(
                remote_gateway,
                kMessageId,
                10001,
                10002,
                R"({"text":"unauthorized"})",
                [&](
                    GatewayPeerTransportResult
                        result
                ) {
                    ++callback_count;

                    final_result =
                        std::move(result);

                    loop.Quit();
                }
            )
        );


        bool global_timeout = false;


        loop.RunAfter(
            std::chrono::seconds(3),
            [&]() {
                global_timeout = true;

                loop.Quit();
            }
        );


        loop.Loop();


        manager->Stop();
        server.Stop();


        TINYIMX_EXPECT_TRUE(
            !global_timeout
        );


        /*
         * 最关键：
         *
         * Unauthorized只收到一次Request。
         */
        TINYIMX_EXPECT_EQ(
            request_count.load(),
            1
        );


        TINYIMX_EXPECT_EQ(
            callback_count,
            1
        );


        /*
         * 网络RPC本身是成功的，
         * 所以Transport status仍然是kOk。
         */
        TINYIMX_EXPECT_EQ(
            final_result.status,
            GatewayPeerTransportStatus::
                kOk
        );


        /*
         * 业务Response才是Unauthorized。
         */
        TINYIMX_EXPECT_EQ(
            final_result.response.status,
            GatewayForwardChatStatus::
                kUnauthorized
        );


        TINYIMX_EXPECT_TRUE(
            !final_result.response.
                Delivered()
        );
    }
);


}

}  // namespace tinyimx::test