#include "tests/concurrency/TestFramework.h"

#include "common/net/Buffer.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/TcpClient.h"
#include "common/net/TcpServer.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <string>

namespace tinyimx::test {


void RegisterTcpClientTests(
    TestRunner& runner
) {
    /*
     * 场景1：
     *
     * TcpClient
     *      ↓
     * TcpServer
     *      ↓
     * echo
     *      ↓
     * TcpClient
     */
    runner.Add(
        "TcpClient.ConnectAndEcho",
        []() {
            constexpr std::uint16_t
                kTestPort = 19004;

            EventLoop loop;

            TINYIMX_EXPECT_TRUE(
                loop.IsValid()
            );

            InetAddress
                listen_address(
                    "127.0.0.1",
                    kTestPort
                );

            TcpServer server(
                &loop,
                listen_address,
                "net-test-tcp-client-server"
            );

            server.SetMessageCallback(
                [](
                    const TcpConnectionPtr&
                        connection,
                    Buffer* buffer
                ) {
                    connection->Send(
                        buffer->
                            RetrieveAllAsString()
                    );
                }
            );

            TINYIMX_EXPECT_TRUE(
                server.Start()
            );

            TcpClient client(
                &loop,
                listen_address,
                "net-test-tcp-client"
            );

            std::atomic<int>
                connected_count{0};

            std::atomic<int>
                disconnected_count{0};

            std::string response;

            client.SetConnectionCallback(
                [&](
                    const TcpConnectionPtr&
                        connection
                ) {
                    if (
                        connection->
                            IsConnected()
                    ) {
                        connected_count.
                            fetch_add(
                                1,
                                std::memory_order_relaxed
                            );

                        connection->Send(
                            "hello-tcp-client"
                        );
                    } else {
                        disconnected_count.
                            fetch_add(
                                1,
                                std::memory_order_relaxed
                            );

                        loop.Quit();
                    }
                }
            );

            client.SetMessageCallback(
                [&](
                    const TcpConnectionPtr&,
                    Buffer* buffer
                ) {
                    response =
                        buffer->
                            RetrieveAllAsString();

                    client.Disconnect();
                }
            );

            bool timed_out = false;

            /*
             * 防止测试因为 Bug 永远挂住。
             */
            loop.RunAfter(
                std::chrono::seconds(3),
                [&]() {
                    timed_out = true;
                    loop.Quit();
                }
            );

            TINYIMX_EXPECT_TRUE(
                client.Connect()
            );

            loop.Loop();

            client.Stop();
            server.Stop();

            TINYIMX_EXPECT_TRUE(
                !timed_out
            );

            TINYIMX_EXPECT_EQ(
                response,
                std::string(
                    "hello-tcp-client"
                )
            );

            TINYIMX_EXPECT_EQ(
                connected_count.load(),
                1
            );

            TINYIMX_EXPECT_EQ(
                disconnected_count.load(),
                1
            );
        }
    );


    /*
     * 场景2：
     *
     * 目标端口无人监听时，
     * 不能阻塞 Reactor，
     * 必须收到连接失败回调。
     */
    runner.Add(
        "TcpClient.ConnectRefused",
        []() {
            constexpr std::uint16_t
                kUnusedPort = 19005;

            EventLoop loop;

            TINYIMX_EXPECT_TRUE(
                loop.IsValid()
            );

            InetAddress
                server_address(
                    "127.0.0.1",
                    kUnusedPort
                );

            TcpClient client(
                &loop,
                server_address,
                "net-test-refused-client"
            );

            std::atomic<int>
                error_count{0};

            int observed_error = 0;

            client.SetConnectErrorCallback(
                [&](int error_number) {
                    observed_error =
                        error_number;

                    error_count.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );

                    loop.Quit();
                }
            );

            bool timed_out = false;

            loop.RunAfter(
                std::chrono::seconds(3),
                [&]() {
                    timed_out = true;
                    loop.Quit();
                }
            );

            TINYIMX_EXPECT_TRUE(
                client.Connect()
            );

            loop.Loop();

            client.Stop();

            TINYIMX_EXPECT_TRUE(
                !timed_out
            );

            TINYIMX_EXPECT_EQ(
                error_count.load(),
                1
            );

            TINYIMX_EXPECT_EQ(
                observed_error,
                ECONNREFUSED
            );

            TINYIMX_EXPECT_TRUE(
                !client.IsConnected()
            );
        }
    );
}

}  // namespace tinyimx::test