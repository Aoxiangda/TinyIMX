#include "tests/concurrency/TestFramework.h"

#include "common/net/Buffer.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/TcpServer.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace tinyimx::test {
namespace {

bool RunEchoClient(uint16_t port,
                   const std::string& request,
                   std::string* response) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    if (::connect(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            static_cast<socklen_t>(sizeof(address))
        ) != 0) {
        ::close(fd);
        return false;
    }

    const ssize_t sent = ::send(
        fd,
        request.data(),
        request.size(),
        MSG_NOSIGNAL
    );

    if (sent != static_cast<ssize_t>(request.size())) {
        ::close(fd);
        return false;
    }

    char buffer[1024] = {0};

    const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);

    if (n <= 0) {
        ::close(fd);
        return false;
    }

    if (response != nullptr) {
        *response = std::string(buffer, static_cast<std::size_t>(n));
    }

    ::close(fd);
    return true;
}

}  // namespace

void RegisterTcpServerTests(TestRunner& runner) {
    runner.Add("TcpServer.EchoOneClient", []() {
        constexpr uint16_t kTestPort = 19000;

        EventLoop loop;
        TINYIMX_EXPECT_TRUE(loop.IsValid());

        InetAddress listen_address("127.0.0.1", kTestPort);
        TINYIMX_EXPECT_TRUE(listen_address.IsValid());

        TcpServer server(&loop, listen_address, "net-test-echo-server");

        std::atomic<int> message_count{0};

        server.SetConnectionCallback([](
            const TcpConnectionPtr& connection
        ) {
            (void)connection;
        });

        server.SetMessageCallback([&](
            const TcpConnectionPtr& connection,
            Buffer* buffer
        ) {
            const std::string message =
                buffer->RetrieveAllAsString();

            message_count.fetch_add(1, std::memory_order_relaxed);

            connection->Send(message);

            loop.Quit();
        });

        TINYIMX_EXPECT_TRUE(server.Start());

        std::thread loop_thread([&]() {
            loop.Loop();
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::string response;
        const bool ok = RunEchoClient(
            kTestPort,
            "hello-net-test",
            &response
        );

        if (loop_thread.joinable()) {
            loop_thread.join();
        }

        server.Stop();

        TINYIMX_EXPECT_TRUE(ok);
        TINYIMX_EXPECT_EQ(response, std::string("hello-net-test"));
        TINYIMX_EXPECT_EQ(message_count.load(), 1);
        TINYIMX_EXPECT_EQ(server.ConnectionCount(), static_cast<std::size_t>(0));
    });
}

}  // namespace tinyimx::test