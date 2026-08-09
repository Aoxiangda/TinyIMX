#include "tests/concurrency/TestFramework.h"

#include "common/net/Buffer.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/TcpServer.h"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>
#include <condition_variable>
#include <mutex>
#include <set>
#include <vector>


namespace tinyimx::test {
namespace {

bool RunEchoClient(
    std::uint16_t port,
    const std::string& request,
    std::string* response
) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) {
        return false;
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(
            AF_INET,
            "127.0.0.1",
            &address.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    if (::connect(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            static_cast<socklen_t>(
                sizeof(address)
            )
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

    if (sent != static_cast<ssize_t>(
            request.size()
        )) {
        ::close(fd);
        return false;
    }

    char buffer[1024] = {0};

    const ssize_t n =
        ::recv(
            fd,
            buffer,
            sizeof(buffer),
            0
        );

    if (n <= 0) {
        ::close(fd);
        return false;
    }

    if (response != nullptr) {
        *response = std::string(
            buffer,
            static_cast<std::size_t>(n)
        );
    }

    ::close(fd);

    return true;
}

int ConnectTestClient(
    std::uint16_t port
) {
    const int fd =
        ::socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (fd < 0) {
        return -1;
    }

    timeval timeout {};
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    if (::setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)
        ) != 0) {
        ::close(fd);
        return -1;
    }

    if (::setsockopt(
            fd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            sizeof(timeout)
        ) != 0) {
        ::close(fd);
        return -1;
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(
            AF_INET,
            "127.0.0.1",
            &address.sin_addr
        ) != 1) {
        ::close(fd);
        return -1;
    }

    if (::connect(
            fd,
            reinterpret_cast<sockaddr*>(
                &address
            ),
            static_cast<socklen_t>(
                sizeof(address)
            )
        ) != 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

bool SendAllBytes(
    int fd,
    const std::string& data
) {
    std::size_t sent_bytes = 0;

    while (sent_bytes < data.size()) {
        const ssize_t n =
            ::send(
                fd,
                data.data() + sent_bytes,
                data.size() - sent_bytes,
                MSG_NOSIGNAL
            );

        if (n > 0) {
            sent_bytes +=
                static_cast<std::size_t>(n);

            continue;
        }

        if (n < 0 &&
            errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
}

bool ReceiveExactBytes(
    int fd,
    const std::string& expected
) {
    std::string received;
    received.reserve(expected.size());

    while (received.size() <
           expected.size()) {
        char buffer[1024];

        const std::size_t remaining =
            expected.size() -
            received.size();

        const std::size_t receive_size =
            std::min(
                remaining,
                sizeof(buffer)
            );

        const ssize_t n =
            ::recv(
                fd,
                buffer,
                receive_size,
                0
            );

        if (n > 0) {
            received.append(
                buffer,
                static_cast<std::size_t>(n)
            );

            continue;
        }

        if (n < 0 &&
            errno == EINTR) {
            continue;
        }

        return false;
    }

    return received == expected;
}

bool WaitForPeerClose(
    int fd
) {
    while (true) {
        char buffer[256];

        const ssize_t n =
            ::recv(
                fd,
                buffer,
                sizeof(buffer),
                0
            );

        if (n == 0) {
            return true;
        }

        if (n > 0) {
            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == ECONNRESET) {
            return true;
        }

        return false;
    }
}

bool WaitForServerShutdown(
    std::uint16_t port
) {
    const int fd =
        ::socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) {
        return false;
    }

    timeval timeout {};
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    if (::setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            sizeof(timeout)
        ) != 0) {
        ::close(fd);
        return false;
    }

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(
            AF_INET,
            "127.0.0.1",
            &address.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    if (::connect(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            static_cast<socklen_t>(
                sizeof(address)
            )
        ) != 0) {
        ::close(fd);
        return false;
    }

    char buffer[128];

    while (true) {
        const ssize_t n =
            ::recv(
                fd,
                buffer,
                sizeof(buffer),
                0
            );

        if (n == 0) {
            ::close(fd);
            return true;
        }

        if (n > 0) {
            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        ::close(fd);
        return false;
    }
}

}  // namespace

void RegisterTcpServerTests(TestRunner& runner) {
    runner.Add("TcpServer.EchoOneClient", []() {
        constexpr std::uint16_t kTestPort =
            19000;

        EventLoop loop;

        TINYIMX_EXPECT_TRUE(
            loop.IsValid()
        );

        TINYIMX_EXPECT_TRUE(
            loop.IsInLoopThread()
        );

        InetAddress listen_address(
            "127.0.0.1",
            kTestPort
        );

        TINYIMX_EXPECT_TRUE(
            listen_address.IsValid()
        );

        TcpServer server(
            &loop,
            listen_address,
            "net-test-echo-server"
        );

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

            message_count.fetch_add(
                1,
                std::memory_order_relaxed
            );

            connection->Send(message);
        });

        TINYIMX_EXPECT_TRUE(
            server.Start()
        );

        bool ok = false;
        std::string response;

        std::thread client_thread([&]() {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );

            ok = RunEchoClient(
                kTestPort,
                "hello-net-test",
                &response
            );

            loop.Quit();
        });

        loop.Loop();

        if (client_thread.joinable()) {
            client_thread.join();
        }

        server.Stop();

        TINYIMX_EXPECT_TRUE(ok);

        TINYIMX_EXPECT_EQ(
            response,
            std::string("hello-net-test")
        );

        TINYIMX_EXPECT_EQ(
            message_count.load(),
            1
        );

        TINYIMX_EXPECT_EQ(
            server.ConnectionCount(),
            static_cast<std::size_t>(0)
        );
    });

    runner.Add(
        "TcpConnection.CrossThreadSend",
        []() {
            constexpr std::uint16_t kTestPort =
                19001;

            EventLoop loop;

            TINYIMX_EXPECT_TRUE(
                loop.IsValid()
            );

            TINYIMX_EXPECT_TRUE(
                loop.IsInLoopThread()
            );

            InetAddress listen_address(
                "127.0.0.1",
                kTestPort
            );

            TINYIMX_EXPECT_TRUE(
                listen_address.IsValid()
            );

            TcpServer server(
                &loop,
                listen_address,
                "net-test-cross-thread-send"
            );

            std::atomic<int>
                message_count{0};

            std::atomic<bool>
                send_called_from_non_loop_thread{
                    false
                };

            std::thread send_thread;

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

                message_count.fetch_add(
                    1,
                    std::memory_order_relaxed
                );

                send_thread = std::thread(
                    [
                        connection,
                        message,
                        &loop,
                        &send_called_from_non_loop_thread
                    ]() {
                        send_called_from_non_loop_thread.store(
                            !loop.IsInLoopThread(),
                            std::memory_order_relaxed
                        );

                        connection->Send(
                            message
                        );
                    }
                );
            });

            TINYIMX_EXPECT_TRUE(
                server.Start()
            );

            bool ok = false;
            std::string response;

            std::thread client_thread([&]() {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100)
                );

                ok = RunEchoClient(
                    kTestPort,
                    "cross-thread-send",
                    &response
                );

                loop.Quit();
            });

            loop.Loop();

            if (client_thread.joinable()) {
                client_thread.join();
            }

            if (send_thread.joinable()) {
                send_thread.join();
            }

            server.Stop();

            TINYIMX_EXPECT_TRUE(ok);

            TINYIMX_EXPECT_TRUE(
                send_called_from_non_loop_thread.load(
                    std::memory_order_relaxed
                )
            );

            TINYIMX_EXPECT_EQ(
                response,
                std::string(
                    "cross-thread-send"
                )
            );

            TINYIMX_EXPECT_EQ(
                message_count.load(
                    std::memory_order_relaxed
                ),
                1
            );
        }
    );

    runner.Add(
        "TcpConnection.CrossThreadShutdown",
        []() {
            constexpr std::uint16_t kTestPort =
                19002;

            EventLoop loop;

            TINYIMX_EXPECT_TRUE(
                loop.IsValid()
            );

            TINYIMX_EXPECT_TRUE(
                loop.IsInLoopThread()
            );

            InetAddress listen_address(
                "127.0.0.1",
                kTestPort
            );

            TINYIMX_EXPECT_TRUE(
                listen_address.IsValid()
            );

            TcpServer server(
                &loop,
                listen_address,
                "net-test-cross-thread-shutdown"
            );

            std::atomic<bool>
                shutdown_called_from_non_loop_thread{
                    false
                };

            std::atomic<int>
                connected_count{0};

            std::thread shutdown_thread;

            server.SetConnectionCallback([&](
                const TcpConnectionPtr& connection
            ) {
                if (!connection->IsConnected()) {
                    return;
                }

                connected_count.fetch_add(
                    1,
                    std::memory_order_relaxed
                );

                shutdown_thread = std::thread(
                    [
                        connection,
                        &loop,
                        &shutdown_called_from_non_loop_thread
                    ]() {
                        shutdown_called_from_non_loop_thread.store(
                            !loop.IsInLoopThread(),
                            std::memory_order_relaxed
                        );

                        connection->Shutdown();
                    }
                );
            });

            server.SetMessageCallback([](
                const TcpConnectionPtr& connection,
                Buffer* buffer
            ) {
                (void)connection;

                buffer->RetrieveAll();
            });

            TINYIMX_EXPECT_TRUE(
                server.Start()
            );

            bool client_saw_eof = false;

            std::thread client_thread([&]() {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100)
                );

                client_saw_eof =
                    WaitForServerShutdown(
                        kTestPort
                    );

                loop.Quit();
            });

            loop.Loop();

            if (client_thread.joinable()) {
                client_thread.join();
            }

            if (shutdown_thread.joinable()) {
                shutdown_thread.join();
            }

            server.Stop();

            TINYIMX_EXPECT_TRUE(
                client_saw_eof
            );

            TINYIMX_EXPECT_TRUE(
                shutdown_called_from_non_loop_thread.load(
                    std::memory_order_relaxed
                )
            );

            TINYIMX_EXPECT_EQ(
                connected_count.load(
                    std::memory_order_relaxed
                ),
                1
            );
        }
    );

    runner.Add("TcpConnection.CrossThreadForceClose",[]() {
            constexpr std::uint16_t kTestPort =
                19003;

            EventLoop loop;

            TINYIMX_EXPECT_TRUE(
                loop.IsValid()
            );

            TINYIMX_EXPECT_TRUE(
                loop.IsInLoopThread()
            );

            InetAddress listen_address(
                "127.0.0.1",
                kTestPort
            );

            TINYIMX_EXPECT_TRUE(
                listen_address.IsValid()
            );

            TcpServer server(
                &loop,
                listen_address,
                "net-test-cross-thread-force-close"
            );

            std::atomic<bool>
                force_close_called_from_non_loop_thread{
                    false
                };

            std::atomic<int>
                connected_count{0};

            std::thread force_close_thread;

            server.SetConnectionCallback([&](
                const TcpConnectionPtr& connection
            ) {
                if (!connection->IsConnected()) {
                    return;
                }

                connected_count.fetch_add(
                    1,
                    std::memory_order_relaxed
                );

                force_close_thread = std::thread(
                    [
                        connection,
                        &loop,
                        &force_close_called_from_non_loop_thread
                    ]() {
                        force_close_called_from_non_loop_thread.store(
                            !loop.IsInLoopThread(),
                            std::memory_order_relaxed
                        );

                        connection->ForceClose();
                    }
                );
            });

            server.SetMessageCallback([](
                const TcpConnectionPtr& connection,
                Buffer* buffer
            ) {
                (void)connection;

                buffer->RetrieveAll();
            });

            TINYIMX_EXPECT_TRUE(
                server.Start()
            );

            bool client_saw_eof = false;

            std::thread client_thread([&]() {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100)
                );

                client_saw_eof =
                    WaitForServerShutdown(
                        kTestPort
                    );

                loop.Quit();
            });

            loop.Loop();

            if (client_thread.joinable()) {
                client_thread.join();
            }

            if (force_close_thread.joinable()) {
                force_close_thread.join();
            }

            const std::size_t connection_count =
                server.ConnectionCount();

            server.Stop();

            TINYIMX_EXPECT_TRUE(
                client_saw_eof
            );

            TINYIMX_EXPECT_TRUE(
                force_close_called_from_non_loop_thread.load(
                    std::memory_order_relaxed
                )
            );

            TINYIMX_EXPECT_EQ(
                connected_count.load(
                    std::memory_order_relaxed
                ),
                1
            );

            TINYIMX_EXPECT_EQ(
                connection_count,
                static_cast<std::size_t>(0)
            );
        }
    );

    runner.Add(
    "TcpServer.MultiReactorRoundRobinEchoAndCleanup",
    []() {
        constexpr std::uint16_t kTestPort = 19004;

        constexpr std::size_t kIoThreadCount = 3;

        constexpr std::size_t kClientCount = 6;

        EventLoop base_loop;

        TINYIMX_EXPECT_TRUE(base_loop.IsValid());

        TINYIMX_EXPECT_TRUE(base_loop.IsInLoopThread());

        InetAddress listen_address(
            "127.0.0.1",
            kTestPort
        );

        TINYIMX_EXPECT_TRUE(
            listen_address.IsValid()
        );

        TcpServer server(
            &base_loop,
            listen_address,
            "net-test-multi-reactor",
            kIoThreadCount
        );

        std::mutex callback_mutex;
        std::condition_variable
            callback_condition;

        std::vector<EventLoop*>
            established_loop_sequence;

        established_loop_sequence.reserve(
            kClientCount
        );

        std::set<EventLoop*>
            established_loop_set;

        std::set<EventLoop*>
            message_loop_set;

        std::set<std::thread::id>
            connection_callback_thread_ids;

        std::set<std::thread::id>
            message_callback_thread_ids;

        std::size_t message_count = 0;
        std::size_t disconnected_count = 0;

        bool connection_callbacks_in_owner_loop =
            true;

        bool message_callbacks_in_owner_loop =
            true;

        bool callbacks_outside_base_loop =
            true;

        server.SetConnectionCallback(
            [&](
                const TcpConnectionPtr&
                    connection
            ) {
                EventLoop* io_loop =
                    connection->GetLoop();

                const bool connected =
                    connection->IsConnected();

                {
                    std::lock_guard<std::mutex>
                        lock(callback_mutex);

                    if (io_loop == nullptr ||
                        !io_loop->IsInLoopThread()) {
                        connection_callbacks_in_owner_loop =
                            false;
                    }

                    if (base_loop.IsInLoopThread()) {
                        callbacks_outside_base_loop =
                            false;
                    }

                    connection_callback_thread_ids.insert(
                        std::this_thread::get_id()
                    );

                    if (connected) {
                        established_loop_sequence.push_back(
                            io_loop
                        );

                        established_loop_set.insert(
                            io_loop
                        );
                    } else {
                        ++disconnected_count;
                    }
                }

                callback_condition.notify_all();
            }
        );

        server.SetMessageCallback(
            [&](
                const TcpConnectionPtr&
                    connection,
                Buffer* buffer
            ) {
                const std::string message =
                    buffer->RetrieveAllAsString();

                EventLoop* io_loop =
                    connection->GetLoop();

                {
                    std::lock_guard<std::mutex>
                        lock(callback_mutex);

                    if (io_loop == nullptr ||
                        !io_loop->IsInLoopThread()) {
                        message_callbacks_in_owner_loop =
                            false;
                    }

                    if (base_loop.IsInLoopThread()) {
                        callbacks_outside_base_loop =
                            false;
                    }

                    message_loop_set.insert(
                        io_loop
                    );

                    message_callback_thread_ids.insert(
                        std::this_thread::get_id()
                    );

                    ++message_count;
                }

                callback_condition.notify_all();

                connection->Send(message);
            }
        );

        TINYIMX_EXPECT_TRUE(
            server.Start()
        );

        bool all_clients_ok = true;
        bool cleanup_completed = false;

        std::thread client_driver([&]() {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );

            for (std::size_t i = 0;
                 i < kClientCount;
                 ++i) {
                const std::string request =
                    "multi-reactor-client-"
                    + std::to_string(i);

                std::string response;

                const bool ok =
                    RunEchoClient(
                        kTestPort,
                        request,
                        &response
                    );

                if (!ok ||
                    response != request) {
                    all_clients_ok = false;
                    break;
                }
            }

            if (all_clients_ok) {
                std::unique_lock<std::mutex>
                    lock(callback_mutex);

                cleanup_completed =
                    callback_condition.wait_for(
                        lock,
                        std::chrono::seconds(3),
                        [&]() {
                            return
                                disconnected_count ==
                                kClientCount;
                        }
                    );
            }

            base_loop.Quit();
        });

        base_loop.Loop();

        if (client_driver.joinable()) {
            client_driver.join();
        }

        std::vector<EventLoop*>
            established_sequence_snapshot;

        std::set<EventLoop*>
            established_loop_set_snapshot;

        std::set<EventLoop*>
            message_loop_set_snapshot;

        std::set<std::thread::id>
            connection_thread_ids_snapshot;

        std::set<std::thread::id>
            message_thread_ids_snapshot;

        std::size_t message_count_snapshot = 0;

        std::size_t
            disconnected_count_snapshot = 0;

        bool connection_owner_ok = false;
        bool message_owner_ok = false;
        bool callbacks_not_in_base = false;

        {
            std::lock_guard<std::mutex>
                lock(callback_mutex);

            established_sequence_snapshot =
                established_loop_sequence;

            established_loop_set_snapshot =
                established_loop_set;

            message_loop_set_snapshot =
                message_loop_set;

            connection_thread_ids_snapshot =
                connection_callback_thread_ids;

            message_thread_ids_snapshot =
                message_callback_thread_ids;

            message_count_snapshot =
                message_count;

            disconnected_count_snapshot =
                disconnected_count;

            connection_owner_ok =
                connection_callbacks_in_owner_loop;

            message_owner_ok =
                message_callbacks_in_owner_loop;

            callbacks_not_in_base =
                callbacks_outside_base_loop;
        }

        const std::size_t
            connection_count_before_stop =
                server.ConnectionCount();

        server.Stop();

        bool round_robin_ok = false;

        if (established_sequence_snapshot.size()
            == kClientCount) {
            const bool first_cycle_unique =
                established_sequence_snapshot[0] !=
                    established_sequence_snapshot[1] &&
                established_sequence_snapshot[0] !=
                    established_sequence_snapshot[2] &&
                established_sequence_snapshot[1] !=
                    established_sequence_snapshot[2];

            const bool second_cycle_matches =
                established_sequence_snapshot[3] ==
                    established_sequence_snapshot[0] &&
                established_sequence_snapshot[4] ==
                    established_sequence_snapshot[1] &&
                established_sequence_snapshot[5] ==
                    established_sequence_snapshot[2];

            round_robin_ok =
                first_cycle_unique &&
                second_cycle_matches;
        }

        TINYIMX_EXPECT_TRUE(
            all_clients_ok
        );

        TINYIMX_EXPECT_TRUE(
            cleanup_completed
        );

        TINYIMX_EXPECT_EQ(
            established_sequence_snapshot.size(),
            kClientCount
        );

        TINYIMX_EXPECT_EQ(
            message_count_snapshot,
            kClientCount
        );

        TINYIMX_EXPECT_EQ(
            disconnected_count_snapshot,
            kClientCount
        );

        TINYIMX_EXPECT_TRUE(
            connection_owner_ok
        );

        TINYIMX_EXPECT_TRUE(
            message_owner_ok
        );

        TINYIMX_EXPECT_TRUE(
            callbacks_not_in_base
        );

        TINYIMX_EXPECT_EQ(
            established_loop_set_snapshot.size(),
            kIoThreadCount
        );

        TINYIMX_EXPECT_EQ(
            message_loop_set_snapshot.size(),
            kIoThreadCount
        );

        TINYIMX_EXPECT_TRUE(
            message_loop_set_snapshot ==
            established_loop_set_snapshot
        );

        TINYIMX_EXPECT_EQ(
            connection_thread_ids_snapshot.size(),
            kIoThreadCount
        );

        TINYIMX_EXPECT_EQ(
            message_thread_ids_snapshot.size(),
            kIoThreadCount
        );

        TINYIMX_EXPECT_TRUE(
            round_robin_ok
        );

        TINYIMX_EXPECT_EQ(
            connection_count_before_stop,
            static_cast<std::size_t>(0)
        );
    }
);

runner.Add(
    "TcpServer.ConnectionCountCrossThreadSnapshot",
    []() {
        constexpr std::uint16_t kTestPort =
            19009;

        constexpr std::size_t kIoThreadCount =
            1;

        EventLoop base_loop;

        TINYIMX_EXPECT_TRUE(
            base_loop.IsValid()
        );

        TINYIMX_EXPECT_TRUE(
            base_loop.IsInLoopThread()
        );

        InetAddress listen_address(
            "127.0.0.1",
            kTestPort
        );

        TINYIMX_EXPECT_TRUE(
            listen_address.IsValid()
        );

        TcpServer server(
            &base_loop,
            listen_address,
            "net-test-connection-count",
            kIoThreadCount
        );

        std::mutex callback_mutex;

        bool connected_callback_seen =
            false;

        bool disconnected_callback_seen =
            false;

        bool callbacks_in_owner_loop =
            true;

        bool callbacks_outside_base_loop =
            true;

        std::size_t
            count_when_connected = 0;

        std::size_t
            count_when_disconnected = 1;

        server.SetConnectionCallback(
            [&](
                const TcpConnectionPtr&
                    connection
            ) {
                EventLoop* io_loop =
                    connection->GetLoop();

                const bool connected =
                    connection->IsConnected();

                const std::size_t
                    count_snapshot =
                        server.ConnectionCount();

                {
                    std::lock_guard<std::mutex>
                        lock(callback_mutex);

                    if (io_loop == nullptr ||
                        !io_loop->
                            IsInLoopThread()) {
                        callbacks_in_owner_loop =
                            false;
                    }

                    if (base_loop.
                        IsInLoopThread()) {
                        callbacks_outside_base_loop =
                            false;
                    }

                    if (connected) {
                        connected_callback_seen =
                            true;

                        count_when_connected =
                            count_snapshot;
                    } else {
                        disconnected_callback_seen =
                            true;

                        count_when_disconnected =
                            count_snapshot;
                    }
                }

                if (connected) {
                    connection->ForceClose();
                    return;
                }

                base_loop.Quit();
            }
        );

        server.SetMessageCallback(
            [](
                const TcpConnectionPtr&
                    connection,
                Buffer* buffer
            ) {
                (void)connection;
                buffer->RetrieveAll();
            }
        );

        TINYIMX_EXPECT_TRUE(
            server.Start()
        );

        bool client_connected = false;
        bool peer_closed = false;
        bool watchdog_timed_out = false;

        std::thread client_thread([&]() {
            std::this_thread::sleep_for(
                std::chrono::
                    milliseconds(100)
            );

            const int fd =
                ConnectTestClient(
                    kTestPort
                );

            if (fd < 0) {
                base_loop.Quit();
                return;
            }

            client_connected = true;

            peer_closed =
                WaitForPeerClose(fd);

            ::close(fd);
        });

        const TimerId watchdog_id =
            base_loop.RunAfter(
                std::chrono::seconds(2),
                [&]() {
                    watchdog_timed_out =
                        true;

                    base_loop.Quit();
                }
            );

        TINYIMX_EXPECT_TRUE(
            watchdog_id.IsValid()
        );

        base_loop.Loop();

        if (client_thread.joinable()) {
            client_thread.join();
        }

        bool connected_seen_snapshot =
            false;

        bool disconnected_seen_snapshot =
            false;

        bool owner_loop_snapshot = false;

        bool outside_base_snapshot = false;

        std::size_t
            connected_count_snapshot = 0;

        std::size_t
            disconnected_count_snapshot =
                1;

        {
            std::lock_guard<std::mutex>
                lock(callback_mutex);

            connected_seen_snapshot =
                connected_callback_seen;

            disconnected_seen_snapshot =
                disconnected_callback_seen;

            owner_loop_snapshot =
                callbacks_in_owner_loop;

            outside_base_snapshot =
                callbacks_outside_base_loop;

            connected_count_snapshot =
                count_when_connected;

            disconnected_count_snapshot =
                count_when_disconnected;
        }

        const std::size_t
            final_count_before_stop =
                server.ConnectionCount();

        server.Stop();

        TINYIMX_EXPECT_TRUE(
            client_connected
        );

        TINYIMX_EXPECT_TRUE(
            peer_closed
        );

        TINYIMX_EXPECT_TRUE(
            connected_seen_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            disconnected_seen_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            owner_loop_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            outside_base_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            !watchdog_timed_out
        );

        TINYIMX_EXPECT_EQ(
            connected_count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_EQ(
            disconnected_count_snapshot,
            static_cast<std::size_t>(0)
        );

        TINYIMX_EXPECT_EQ(
            final_count_before_stop,
            static_cast<std::size_t>(0)
        );
    }
);

runner.Add(
    "TcpConnection.WriteCompleteCallback",
    []() {
        constexpr std::uint16_t kTestPort =
            19005;

        EventLoop loop;

        TINYIMX_EXPECT_TRUE(
            loop.IsValid()
        );

        TINYIMX_EXPECT_TRUE(
            loop.IsInLoopThread()
        );

        InetAddress listen_address(
            "127.0.0.1",
            kTestPort
        );

        TINYIMX_EXPECT_TRUE(
            listen_address.IsValid()
        );

        TcpServer server(
            &loop,
            listen_address,
            "net-test-write-complete"
        );

        std::mutex callback_mutex;
        std::condition_variable
            callback_condition;

        std::size_t message_count = 0;
        std::size_t write_complete_count = 0;
        std::size_t disconnected_count = 0;

        bool message_callback_active = false;

        bool write_complete_reentered =
            false;

        bool write_complete_in_owner_loop =
            true;

        server.SetConnectionCallback(
            [&](
                const TcpConnectionPtr&
                    connection
            ) {
                if (connection->IsConnected()) {
                    connection->
                        SetWriteCompleteCallback(
                            [&](
                                const TcpConnectionPtr&
                                    completed_connection
                            ) {
                                EventLoop* io_loop =
                                    completed_connection->
                                        GetLoop();

                                {
                                    std::lock_guard<std::mutex>
                                        lock(callback_mutex);

                                    if (message_callback_active) {
                                        write_complete_reentered =
                                            true;
                                    }

                                    if (io_loop == nullptr ||
                                        !io_loop->
                                            IsInLoopThread()) {
                                        write_complete_in_owner_loop =
                                            false;
                                    }

                                    ++write_complete_count;
                                }

                                callback_condition.
                                    notify_all();
                            }
                        );

                    return;
                }

                {
                    std::lock_guard<std::mutex>
                        lock(callback_mutex);

                    ++disconnected_count;
                }

                callback_condition.notify_all();
            }
        );

        server.SetMessageCallback(
            [&](
                const TcpConnectionPtr&
                    connection,
                Buffer* buffer
            ) {
                const std::string message =
                    buffer->
                        RetrieveAllAsString();

                {
                    std::lock_guard<std::mutex>
                        lock(callback_mutex);

                    ++message_count;

                    message_callback_active =
                        true;
                }

                connection->Send(message);

                {
                    std::lock_guard<std::mutex>
                        lock(callback_mutex);

                    message_callback_active =
                        false;
                }
            }
        );

        TINYIMX_EXPECT_TRUE(
            server.Start()
        );

        bool client_ok = false;
        bool callbacks_completed = false;

        std::string response;

        std::thread client_thread([&]() {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );

            client_ok =
                RunEchoClient(
                    kTestPort,
                    "write-complete-test",
                    &response
                );

            {
                std::unique_lock<std::mutex>
                    lock(callback_mutex);

                callbacks_completed =
                    callback_condition.wait_for(
                        lock,
                        std::chrono::seconds(3),
                        [&]() {
                            return
                                write_complete_count == 1 &&
                                disconnected_count == 1;
                        }
                    );
            }

            loop.Quit();
        });

        loop.Loop();

        if (client_thread.joinable()) {
            client_thread.join();
        }

        std::size_t
            message_count_snapshot = 0;

        std::size_t
            write_complete_count_snapshot = 0;

        std::size_t
            disconnected_count_snapshot = 0;

        bool reentered_snapshot = false;
        bool owner_loop_snapshot = false;

        {
            std::lock_guard<std::mutex>
                lock(callback_mutex);

            message_count_snapshot =
                message_count;

            write_complete_count_snapshot =
                write_complete_count;

            disconnected_count_snapshot =
                disconnected_count;

            reentered_snapshot =
                write_complete_reentered;

            owner_loop_snapshot =
                write_complete_in_owner_loop;
        }

        const std::size_t
            connection_count_before_stop =
                server.ConnectionCount();

        server.Stop();

        TINYIMX_EXPECT_TRUE(
            client_ok
        );

        TINYIMX_EXPECT_TRUE(
            callbacks_completed
        );

        TINYIMX_EXPECT_EQ(
            response,
            std::string(
                "write-complete-test"
            )
        );

        TINYIMX_EXPECT_EQ(
            message_count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_EQ(
            write_complete_count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_EQ(
            disconnected_count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_TRUE(
            owner_loop_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            !reentered_snapshot
        );

        TINYIMX_EXPECT_EQ(
            connection_count_before_stop,
            static_cast<std::size_t>(0)
        );
    }
);

runner.Add(
    "TcpConnection.HighWaterMarkCallback",
    []() {
        constexpr std::uint16_t kTestPort =
            19006;

        constexpr std::size_t kHighWaterMark =
            64U * 1024U;

        constexpr std::size_t kPayloadSize =
            32U * 1024U * 1024U;

        EventLoop loop;

        TINYIMX_EXPECT_TRUE(
            loop.IsValid()
        );

        TINYIMX_EXPECT_TRUE(
            loop.IsInLoopThread()
        );

        InetAddress listen_address(
            "127.0.0.1",
            kTestPort
        );

        TINYIMX_EXPECT_TRUE(
            listen_address.IsValid()
        );

        TcpServer server(
            &loop,
            listen_address,
            "net-test-high-water-mark"
        );

        const std::string large_payload(
            kPayloadSize,
            'H'
        );

        std::mutex callback_mutex;
        std::condition_variable
            callback_condition;

        std::size_t message_count = 0;
        std::size_t high_water_count = 0;
        std::size_t disconnected_count = 0;

        std::size_t
            observed_buffered_bytes = 0;

        bool high_water_in_owner_loop =
            true;

        server.SetConnectionCallback(
            [&](
                const TcpConnectionPtr&
                    connection
            ) {
                if (connection->IsConnected()) {
                    connection->
                        SetHighWaterMarkCallback(
                            [&](
                                const TcpConnectionPtr&
                                    slow_connection,
                                std::size_t
                                    buffered_bytes
                            ) {
                                EventLoop* io_loop =
                                    slow_connection->
                                        GetLoop();

                                {
                                    std::lock_guard<std::mutex>
                                        lock(callback_mutex);

                                    if (io_loop == nullptr ||
                                        !io_loop->
                                            IsInLoopThread()) {
                                        high_water_in_owner_loop =
                                            false;
                                    }

                                    ++high_water_count;

                                    observed_buffered_bytes =
                                        buffered_bytes;
                                }

                                callback_condition.
                                    notify_all();

                                slow_connection->
                                    ForceClose();
                            },
                            kHighWaterMark
                        );

                    return;
                }

                {
                    std::lock_guard<std::mutex>
                        lock(callback_mutex);

                    ++disconnected_count;
                }

                callback_condition.notify_all();
            }
        );

        server.SetMessageCallback(
            [&](
                const TcpConnectionPtr&
                    connection,
                Buffer* buffer
            ) {
                buffer->RetrieveAll();

                {
                    std::lock_guard<std::mutex>
                        lock(callback_mutex);

                    ++message_count;
                }

                connection->Send(
                    large_payload
                );
            }
        );

        TINYIMX_EXPECT_TRUE(
            server.Start()
        );

        bool client_connected = false;
        bool trigger_sent = false;
        bool callbacks_completed = false;

        std::thread client_thread([&]() {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );

            const int fd =
                ::socket(
                    AF_INET,
                    SOCK_STREAM,
                    0
                );

            if (fd < 0) {
                loop.Quit();
                return;
            }

            int receive_buffer_size = 4096;

            if (::setsockopt(
                    fd,
                    SOL_SOCKET,
                    SO_RCVBUF,
                    &receive_buffer_size,
                    sizeof(receive_buffer_size)
                ) != 0) {
                ::close(fd);
                loop.Quit();
                return;
            }

            sockaddr_in address {};
            address.sin_family = AF_INET;
            address.sin_port =
                htons(kTestPort);

            if (::inet_pton(
                    AF_INET,
                    "127.0.0.1",
                    &address.sin_addr
                ) != 1) {
                ::close(fd);
                loop.Quit();
                return;
            }

            if (::connect(
                    fd,
                    reinterpret_cast<
                        sockaddr*
                    >(&address),
                    static_cast<socklen_t>(
                        sizeof(address)
                    )
                ) != 0) {
                ::close(fd);
                loop.Quit();
                return;
            }

            client_connected = true;

            const std::string trigger =
                "start-large-send";

            std::size_t total_sent = 0;

            while (total_sent <
                   trigger.size()) {
                const ssize_t n =
                    ::send(
                        fd,
                        trigger.data() +
                            total_sent,
                        trigger.size() -
                            total_sent,
                        MSG_NOSIGNAL
                    );

                if (n > 0) {
                    total_sent +=
                        static_cast<std::size_t>(
                            n
                        );

                    continue;
                }

                if (n < 0 &&
                    errno == EINTR) {
                    continue;
                }

                break;
            }

            trigger_sent =
                total_sent ==
                trigger.size();

            if (trigger_sent) {
                std::unique_lock<std::mutex>
                    lock(callback_mutex);

                callbacks_completed =
                    callback_condition.wait_for(
                        lock,
                        std::chrono::seconds(5),
                        [&]() {
                            return
                                high_water_count == 1 &&
                                disconnected_count == 1;
                        }
                    );
            }

            ::close(fd);

            loop.Quit();
        });

        loop.Loop();

        if (client_thread.joinable()) {
            client_thread.join();
        }

        std::size_t
            message_count_snapshot = 0;

        std::size_t
            high_water_count_snapshot = 0;

        std::size_t
            disconnected_count_snapshot = 0;

        std::size_t
            buffered_bytes_snapshot = 0;

        bool owner_loop_snapshot = false;

        {
            std::lock_guard<std::mutex>
                lock(callback_mutex);

            message_count_snapshot =
                message_count;

            high_water_count_snapshot =
                high_water_count;

            disconnected_count_snapshot =
                disconnected_count;

            buffered_bytes_snapshot =
                observed_buffered_bytes;

            owner_loop_snapshot =
                high_water_in_owner_loop;
        }

        const std::size_t
            connection_count_before_stop =
                server.ConnectionCount();

        server.Stop();

        TINYIMX_EXPECT_TRUE(
            client_connected
        );

        TINYIMX_EXPECT_TRUE(
            trigger_sent
        );

        TINYIMX_EXPECT_TRUE(
            callbacks_completed
        );

        TINYIMX_EXPECT_EQ(
            message_count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_EQ(
            high_water_count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_EQ(
            disconnected_count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_TRUE(
            owner_loop_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            buffered_bytes_snapshot >=
            kHighWaterMark
        );

        TINYIMX_EXPECT_TRUE(
            buffered_bytes_snapshot <=
            kPayloadSize
        );

        TINYIMX_EXPECT_EQ(
            connection_count_before_stop,
            static_cast<std::size_t>(0)
        );
    }
);

runner.Add(
    "TcpServer.IdleTimeoutClosesSilentClient",
    []() {
        constexpr std::uint16_t kTestPort =
            19007;

        constexpr std::size_t kIoThreadCount =
            1;

        constexpr auto kIdleTimeout =
            std::chrono::milliseconds(250);

        constexpr auto kCheckInterval =
            std::chrono::milliseconds(50);

        EventLoop base_loop;

        TINYIMX_EXPECT_TRUE(
            base_loop.IsValid()
        );

        TINYIMX_EXPECT_TRUE(
            base_loop.IsInLoopThread()
        );

        InetAddress listen_address(
            "127.0.0.1",
            kTestPort
        );

        TINYIMX_EXPECT_TRUE(
            listen_address.IsValid()
        );

        TcpServer server(
            &base_loop,
            listen_address,
            "net-test-idle-silent",
            kIoThreadCount
        );

        TINYIMX_EXPECT_TRUE(
            server.SetIdleTimeout(
                kIdleTimeout,
                kCheckInterval
            )
        );

        std::mutex callback_mutex;

        std::condition_variable
            callback_condition;

        std::size_t connected_count = 0;
        std::size_t disconnected_count = 0;
        std::size_t message_count = 0;

        bool callbacks_in_owner_loop =
            true;

        bool callbacks_outside_base_loop =
            true;

        server.SetConnectionCallback(
            [&](
                const TcpConnectionPtr&
                    connection
            ) {
                EventLoop* io_loop =
                    connection->GetLoop();

                {
                    std::lock_guard<std::mutex>
                        lock(callback_mutex);

                    if (io_loop == nullptr ||
                        !io_loop->
                            IsInLoopThread()) {
                        callbacks_in_owner_loop =
                            false;
                    }

                    if (base_loop.
                        IsInLoopThread()) {
                        callbacks_outside_base_loop =
                            false;
                    }

                    if (connection->
                        IsConnected()) {
                        ++connected_count;
                    } else {
                        ++disconnected_count;
                    }
                }

                callback_condition.
                    notify_all();
            }
        );

        server.SetMessageCallback(
            [&](
                const TcpConnectionPtr&
                    connection,
                Buffer* buffer
            ) {
                (void)connection;

                buffer->RetrieveAll();

                std::lock_guard<std::mutex>
                    lock(callback_mutex);

                ++message_count;
            }
        );

        TINYIMX_EXPECT_TRUE(
            server.Start()
        );

        bool client_connected = false;
        bool peer_closed = false;
        bool cleanup_completed = false;

        std::chrono::milliseconds
            close_elapsed{0};

        std::thread client_thread([&]() {
            std::this_thread::sleep_for(
                std::chrono::
                    milliseconds(100)
            );

            const int fd =
                ConnectTestClient(
                    kTestPort
                );

            if (fd < 0) {
                base_loop.Quit();
                return;
            }

            client_connected = true;

            const auto wait_started_at =
                std::chrono::steady_clock::
                    now();

            peer_closed =
                WaitForPeerClose(fd);

            close_elapsed =
                std::chrono::duration_cast<
                    std::chrono::milliseconds
                >(
                    std::chrono::
                        steady_clock::now() -
                    wait_started_at
                );

            ::close(fd);

            if (peer_closed) {
                std::unique_lock<std::mutex>
                    lock(callback_mutex);

                cleanup_completed =
                    callback_condition.wait_for(
                        lock,
                        std::chrono::seconds(2),
                        [&]() {
                            return
                                disconnected_count ==
                                1;
                        }
                    );
            }

            base_loop.Quit();
        });

        base_loop.Loop();

        if (client_thread.joinable()) {
            client_thread.join();
        }

        std::size_t
            connected_count_snapshot = 0;

        std::size_t
            disconnected_count_snapshot = 0;

        std::size_t
            message_count_snapshot = 0;

        bool owner_loop_snapshot = false;

        bool outside_base_snapshot = false;

        {
            std::lock_guard<std::mutex>
                lock(callback_mutex);

            connected_count_snapshot =
                connected_count;

            disconnected_count_snapshot =
                disconnected_count;

            message_count_snapshot =
                message_count;

            owner_loop_snapshot =
                callbacks_in_owner_loop;

            outside_base_snapshot =
                callbacks_outside_base_loop;
        }

        const std::size_t
            connection_count_before_stop =
                server.ConnectionCount();

        server.Stop();

        TINYIMX_EXPECT_TRUE(
            client_connected
        );

        TINYIMX_EXPECT_TRUE(
            peer_closed
        );

        TINYIMX_EXPECT_TRUE(
            cleanup_completed
        );

        TINYIMX_EXPECT_EQ(
            connected_count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_EQ(
            disconnected_count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_EQ(
            message_count_snapshot,
            static_cast<std::size_t>(0)
        );

        TINYIMX_EXPECT_TRUE(
            owner_loop_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            outside_base_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            close_elapsed >=
            std::chrono::milliseconds(100)
        );

        TINYIMX_EXPECT_TRUE(
            close_elapsed <
            std::chrono::seconds(2)
        );

        TINYIMX_EXPECT_EQ(
            connection_count_before_stop,
            static_cast<std::size_t>(0)
        );
    }
);

runner.Add(
    "TcpServer.IdleTimeoutRefreshesOnInboundData",
    []() {
        constexpr std::uint16_t kTestPort =
            19008;

        constexpr std::size_t kIoThreadCount =
            1;

        constexpr std::size_t kHeartbeatCount =
            8;

        constexpr auto kIdleTimeout =
            std::chrono::milliseconds(250);

        constexpr auto kCheckInterval =
            std::chrono::milliseconds(50);

        constexpr auto kHeartbeatInterval =
            std::chrono::milliseconds(80);

        EventLoop base_loop;

        TINYIMX_EXPECT_TRUE(
            base_loop.IsValid()
        );

        TINYIMX_EXPECT_TRUE(
            base_loop.IsInLoopThread()
        );

        InetAddress listen_address(
            "127.0.0.1",
            kTestPort
        );

        TINYIMX_EXPECT_TRUE(
            listen_address.IsValid()
        );

        TcpServer server(
            &base_loop,
            listen_address,
            "net-test-idle-heartbeat",
            kIoThreadCount
        );

        TINYIMX_EXPECT_TRUE(
            server.SetIdleTimeout(
                kIdleTimeout,
                kCheckInterval
            )
        );

        std::mutex callback_mutex;

        std::condition_variable
            callback_condition;

        std::size_t connected_count = 0;
        std::size_t disconnected_count = 0;
        std::size_t message_count = 0;

        bool callbacks_in_owner_loop =
            true;

        bool callbacks_outside_base_loop =
            true;

        server.SetConnectionCallback(
            [&](
                const TcpConnectionPtr&
                    connection
            ) {
                EventLoop* io_loop =
                    connection->GetLoop();

                {
                    std::lock_guard<std::mutex>
                        lock(callback_mutex);

                    if (io_loop == nullptr ||
                        !io_loop->
                            IsInLoopThread()) {
                        callbacks_in_owner_loop =
                            false;
                    }

                    if (base_loop.
                        IsInLoopThread()) {
                        callbacks_outside_base_loop =
                            false;
                    }

                    if (connection->
                        IsConnected()) {
                        ++connected_count;
                    } else {
                        ++disconnected_count;
                    }
                }

                callback_condition.
                    notify_all();
            }
        );

        server.SetMessageCallback(
            [&](
                const TcpConnectionPtr&
                    connection,
                Buffer* buffer
            ) {
                const std::string message =
                    buffer->
                        RetrieveAllAsString();

                EventLoop* io_loop =
                    connection->GetLoop();

                {
                    std::lock_guard<std::mutex>
                        lock(callback_mutex);

                    if (io_loop == nullptr ||
                        !io_loop->
                            IsInLoopThread()) {
                        callbacks_in_owner_loop =
                            false;
                    }

                    if (base_loop.
                        IsInLoopThread()) {
                        callbacks_outside_base_loop =
                            false;
                    }

                    ++message_count;
                }

                connection->Send(message);
            }
        );

        TINYIMX_EXPECT_TRUE(
            server.Start()
        );

        bool client_connected = false;
        bool all_heartbeats_ok = true;
        bool peer_closed_after_idle = false;
        bool cleanup_completed = false;

        std::chrono::milliseconds
            heartbeat_phase_elapsed{0};

        std::chrono::milliseconds
            idle_close_elapsed{0};

        std::thread client_thread([&]() {
            std::this_thread::sleep_for(
                std::chrono::
                    milliseconds(100)
            );

            const int fd =
                ConnectTestClient(
                    kTestPort
                );

            if (fd < 0) {
                all_heartbeats_ok = false;
                base_loop.Quit();
                return;
            }

            client_connected = true;

            const auto heartbeat_started_at =
                std::chrono::steady_clock::
                    now();

            for (std::size_t i = 0;
                 i < kHeartbeatCount;
                 ++i) {
                const std::string heartbeat =
                    "idle-heartbeat-"
                    + std::to_string(i);

                if (!SendAllBytes(
                        fd,
                        heartbeat
                    )) {
                    all_heartbeats_ok = false;
                    break;
                }

                if (!ReceiveExactBytes(
                        fd,
                        heartbeat
                    )) {
                    all_heartbeats_ok = false;
                    break;
                }

                if (i + 1 <
                    kHeartbeatCount) {
                    std::this_thread::
                        sleep_for(
                            kHeartbeatInterval
                        );
                }
            }

            heartbeat_phase_elapsed =
                std::chrono::duration_cast<
                    std::chrono::milliseconds
                >(
                    std::chrono::
                        steady_clock::now() -
                    heartbeat_started_at
                );

            const auto idle_started_at =
                std::chrono::steady_clock::
                    now();

            peer_closed_after_idle =
                WaitForPeerClose(fd);

            idle_close_elapsed =
                std::chrono::duration_cast<
                    std::chrono::milliseconds
                >(
                    std::chrono::
                        steady_clock::now() -
                    idle_started_at
                );

            ::close(fd);

            if (peer_closed_after_idle) {
                std::unique_lock<std::mutex>
                    lock(callback_mutex);

                cleanup_completed =
                    callback_condition.wait_for(
                        lock,
                        std::chrono::seconds(2),
                        [&]() {
                            return
                                disconnected_count ==
                                1;
                        }
                    );
            }

            base_loop.Quit();
        });

        base_loop.Loop();

        if (client_thread.joinable()) {
            client_thread.join();
        }

        std::size_t
            connected_count_snapshot = 0;

        std::size_t
            disconnected_count_snapshot = 0;

        std::size_t
            message_count_snapshot = 0;

        bool owner_loop_snapshot = false;

        bool outside_base_snapshot = false;

        {
            std::lock_guard<std::mutex>
                lock(callback_mutex);

            connected_count_snapshot =
                connected_count;

            disconnected_count_snapshot =
                disconnected_count;

            message_count_snapshot =
                message_count;

            owner_loop_snapshot =
                callbacks_in_owner_loop;

            outside_base_snapshot =
                callbacks_outside_base_loop;
        }

        const std::size_t
            connection_count_before_stop =
                server.ConnectionCount();

        server.Stop();

        TINYIMX_EXPECT_TRUE(
            client_connected
        );

        TINYIMX_EXPECT_TRUE(
            all_heartbeats_ok
        );

        TINYIMX_EXPECT_TRUE(
            peer_closed_after_idle
        );

        TINYIMX_EXPECT_TRUE(
            cleanup_completed
        );

        TINYIMX_EXPECT_EQ(
            connected_count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_EQ(
            disconnected_count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_EQ(
            message_count_snapshot,
            kHeartbeatCount
        );

        TINYIMX_EXPECT_TRUE(
            owner_loop_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            outside_base_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            heartbeat_phase_elapsed >
            kIdleTimeout
        );

        TINYIMX_EXPECT_TRUE(
            idle_close_elapsed >=
            std::chrono::milliseconds(100)
        );

        TINYIMX_EXPECT_TRUE(
            idle_close_elapsed <
            std::chrono::seconds(2)
        );

        TINYIMX_EXPECT_EQ(
            connection_count_before_stop,
            static_cast<std::size_t>(0)
        );
    }
);

}

}  // namespace tinyimx::test