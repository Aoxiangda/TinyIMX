#include "tests/concurrency/TestFramework.h"

#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/TcpConnection.h"
#include "gateway/SessionManager.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <utility>

namespace tinyimx::test {
namespace {

TcpConnectionPtr MakeTestConnection(
    EventLoop* loop,
    std::string name,
    std::uint16_t local_port,
    std::uint16_t peer_port
) {
    if (loop == nullptr) {
        return nullptr;
    }

    const int socket_fd =
        ::socket(
            AF_INET,
            SOCK_STREAM |
                SOCK_NONBLOCK |
                SOCK_CLOEXEC,
            IPPROTO_TCP
        );

    if (socket_fd < 0) {
        return nullptr;
    }

    const InetAddress local_address(
        "127.0.0.1",
        local_port
    );

    const InetAddress peer_address(
        "127.0.0.1",
        peer_port
    );

    return std::make_shared<
        TcpConnection
    >(
        loop,
        std::move(name),
        socket_fd,
        local_address,
        peer_address
    );
}

}  // namespace

void RegisterSessionManagerTests(
    TestRunner& runner
) {
    runner.Add(
        "SessionManager.CurrentConnectionUnbinds",
        []() {
            constexpr UserId kUserId =
                10001;

            EventLoop loop;

            TINYIMX_EXPECT_TRUE(
                loop.IsValid()
            );

            const TcpConnectionPtr
                connection =
                    MakeTestConnection(
                        &loop,
                        "session-current",
                        21001,
                        22001
                    );

            TINYIMX_EXPECT_TRUE(
                connection != nullptr
            );

            SessionManager manager;

            TINYIMX_EXPECT_TRUE(
                manager.Bind(
                    kUserId,
                    connection
                )
            );

            TINYIMX_EXPECT_EQ(
                manager.OnlineCount(),
                static_cast<std::size_t>(1)
            );

            const auto before_unbind_user =
                manager.FindUserByConnection(
                    connection
                );

            TINYIMX_EXPECT_TRUE(
                before_unbind_user.
                    has_value()
            );

            TINYIMX_EXPECT_EQ(
                before_unbind_user.value(),
                kUserId
            );

            const SessionUnbindResult
                result =
                    manager.UnbindIfCurrent(
                        connection
                    );

            TINYIMX_EXPECT_TRUE(
                result.unbound
            );

            TINYIMX_EXPECT_EQ(
                result.user_id,
                kUserId
            );

            TINYIMX_EXPECT_TRUE(
                manager.FindConnection(
                    kUserId
                ) == nullptr
            );

            TINYIMX_EXPECT_TRUE(
                !manager.
                    FindUserByConnection(
                        connection
                    ).has_value()
            );

            TINYIMX_EXPECT_EQ(
                manager.OnlineCount(),
                static_cast<std::size_t>(0)
            );

            const SessionUnbindResult
                second_result =
                    manager.UnbindIfCurrent(
                        connection
                    );

            TINYIMX_EXPECT_TRUE(
                !second_result.unbound
            );

            TINYIMX_EXPECT_EQ(
                second_result.user_id,
                static_cast<UserId>(0)
            );
        }
    );

    runner.Add(
        "SessionManager.ReplacedConnectionCannotUnbindCurrent",
        []() {
            constexpr UserId kUserId =
                10002;

            EventLoop loop;

            TINYIMX_EXPECT_TRUE(
                loop.IsValid()
            );

            const TcpConnectionPtr
                old_connection =
                    MakeTestConnection(
                        &loop,
                        "session-old",
                        21002,
                        22002
                    );

            const TcpConnectionPtr
                new_connection =
                    MakeTestConnection(
                        &loop,
                        "session-new",
                        21003,
                        22003
                    );

            TINYIMX_EXPECT_TRUE(
                old_connection != nullptr
            );

            TINYIMX_EXPECT_TRUE(
                new_connection != nullptr
            );

            SessionManager manager;

            TINYIMX_EXPECT_TRUE(
                manager.Bind(
                    kUserId,
                    old_connection
                )
            );

            const SessionBindResult
                replace_result =
                    manager.BindOrReplace(
                        kUserId,
                        new_connection
                    );

            TINYIMX_EXPECT_TRUE(
                replace_result.success
            );

            TINYIMX_EXPECT_TRUE(
                replace_result.replaced
            );

            TINYIMX_EXPECT_TRUE(
                replace_result.old_connection ==
                old_connection
            );

            const SessionUnbindResult
                stale_unbind_result =
                    manager.UnbindIfCurrent(
                        old_connection
                    );

            TINYIMX_EXPECT_TRUE(
                !stale_unbind_result.unbound
            );

            TINYIMX_EXPECT_TRUE(
                manager.FindConnection(
                    kUserId
                ) == new_connection
            );

            TINYIMX_EXPECT_TRUE(
                !manager.
                    FindUserByConnection(
                        old_connection
                    ).has_value()
            );

            const auto current_user =
                manager.FindUserByConnection(
                    new_connection
                );

            TINYIMX_EXPECT_TRUE(
                current_user.has_value()
            );

            TINYIMX_EXPECT_EQ(
                current_user.value(),
                kUserId
            );

            TINYIMX_EXPECT_EQ(
                manager.OnlineCount(),
                static_cast<std::size_t>(1)
            );

            const SessionUnbindResult
                current_unbind_result =
                    manager.UnbindIfCurrent(
                        new_connection
                    );

            TINYIMX_EXPECT_TRUE(
                current_unbind_result.unbound
            );

            TINYIMX_EXPECT_EQ(
                current_unbind_result.user_id,
                kUserId
            );

            TINYIMX_EXPECT_EQ(
                manager.OnlineCount(),
                static_cast<std::size_t>(0)
            );
        }
    );

    runner.Add(
        "SessionManager.ConcurrentReplaceAndUnbindKeepsConsistency",
        []() {
            constexpr UserId kUserId =
                10003;

            constexpr std::size_t
                kIterations = 2000;

            EventLoop loop;

            TINYIMX_EXPECT_TRUE(
                loop.IsValid()
            );

            const TcpConnectionPtr
                connection_a =
                    MakeTestConnection(
                        &loop,
                        "session-concurrent-a",
                        21004,
                        22004
                    );

            const TcpConnectionPtr
                connection_b =
                    MakeTestConnection(
                        &loop,
                        "session-concurrent-b",
                        21005,
                        22005
                    );

            TINYIMX_EXPECT_TRUE(
                connection_a != nullptr
            );

            TINYIMX_EXPECT_TRUE(
                connection_b != nullptr
            );

            SessionManager manager;

            TINYIMX_EXPECT_TRUE(
                manager.Bind(
                    kUserId,
                    connection_a
                )
            );

            std::atomic<bool>
                start{false};

            std::thread replace_thread(
                [&]() {
                    while (!start.load(
                        std::memory_order_acquire
                    )) {
                        std::this_thread::
                            yield();
                    }

                    for (std::size_t i = 0;
                         i < kIterations;
                         ++i) {
                        const TcpConnectionPtr&
                            selected =
                                (i % 2 == 0)
                                    ? connection_a
                                    : connection_b;

                        manager.BindOrReplace(
                            kUserId,
                            selected
                        );
                    }
                }
            );

            std::thread unbind_thread(
                [&]() {
                    while (!start.load(
                        std::memory_order_acquire
                    )) {
                        std::this_thread::
                            yield();
                    }

                    for (std::size_t i = 0;
                         i < kIterations;
                         ++i) {
                        const TcpConnectionPtr&
                            selected =
                                (i % 2 == 0)
                                    ? connection_b
                                    : connection_a;

                        manager.UnbindIfCurrent(
                            selected
                        );
                    }
                }
            );

            start.store(
                true,
                std::memory_order_release
            );

            replace_thread.join();
            unbind_thread.join();

            const SessionBindResult
                final_bind_result =
                    manager.BindOrReplace(
                        kUserId,
                        connection_b
                    );

            TINYIMX_EXPECT_TRUE(
                final_bind_result.success
            );

            const SessionUnbindResult
                stale_result =
                    manager.UnbindIfCurrent(
                        connection_a
                    );

            TINYIMX_EXPECT_TRUE(
                !stale_result.unbound
            );

            TINYIMX_EXPECT_TRUE(
                manager.FindConnection(
                    kUserId
                ) == connection_b
            );

            TINYIMX_EXPECT_TRUE(
                !manager.
                    FindUserByConnection(
                        connection_a
                    ).has_value()
            );

            const auto user_by_connection =
                manager.FindUserByConnection(
                    connection_b
                );

            TINYIMX_EXPECT_TRUE(
                user_by_connection.has_value()
            );

            TINYIMX_EXPECT_EQ(
                user_by_connection.value(),
                kUserId
            );

            TINYIMX_EXPECT_EQ(
                manager.OnlineCount(),
                static_cast<std::size_t>(1)
            );
        }
    );

        runner.Add(
        "SessionManager.SessionEpochFencesReplacement",
        []() {
            constexpr UserId kUserId =
                10004;

            EventLoop loop;

            TINYIMX_EXPECT_TRUE(
                loop.IsValid()
            );

            const TcpConnectionPtr
                connection_a =
                    MakeTestConnection(
                        &loop,
                        "session-epoch-a",
                        21006,
                        22006
                    );

            const TcpConnectionPtr
                connection_b =
                    MakeTestConnection(
                        &loop,
                        "session-epoch-b",
                        21007,
                        22007
                    );

            TINYIMX_EXPECT_TRUE(
                connection_a != nullptr
            );

            TINYIMX_EXPECT_TRUE(
                connection_b != nullptr
            );

            SessionManager manager;

            /*
             * 第一代Session：
             *
             * user10004
             *   ->
             * connection_a
             *   ->
             * epoch1
             */
            const SessionBindResult
                first_bind =
                    manager.BindOrReplace(
                        kUserId,
                        connection_a
                    );

            TINYIMX_EXPECT_TRUE(
                first_bind.success
            );

            TINYIMX_EXPECT_TRUE(
                first_bind.epoch != 0
            );

            TINYIMX_EXPECT_TRUE(
                manager.IsCurrent(
                    kUserId,
                    first_bind.epoch,
                    connection_a
                )
            );

            /*
             * 再确认：
             *
             * connection_a反查到的Snapshot
             * 与Bind产生的epoch一致。
             */
            const auto first_snapshot =
                manager.FindSessionByConnection(
                    connection_a
                );

            TINYIMX_EXPECT_TRUE(
                first_snapshot.has_value()
            );

            TINYIMX_EXPECT_EQ(
                first_snapshot->user_id,
                kUserId
            );

            TINYIMX_EXPECT_EQ(
                first_snapshot->epoch,
                first_bind.epoch
            );

            /*
             * 第二代Session：
             *
             * 同一个User，
             * 换成connection_b。
             */
            const SessionBindResult
                second_bind =
                    manager.BindOrReplace(
                        kUserId,
                        connection_b
                    );

            TINYIMX_EXPECT_TRUE(
                second_bind.success
            );

            TINYIMX_EXPECT_TRUE(
                second_bind.epoch != 0
            );

            /*
             * 新Session必须产生新generation。
             */
            TINYIMX_EXPECT_TRUE(
                second_bind.epoch !=
                first_bind.epoch
            );

            TINYIMX_EXPECT_TRUE(
                second_bind.replaced
            );

            TINYIMX_EXPECT_TRUE(
                second_bind.old_connection ==
                connection_a
            );

            /*
             * 核心Invariant：
             *
             * 旧 connection + 旧 epoch
             * 已经不是current。
             */
            TINYIMX_EXPECT_TRUE(
                !manager.IsCurrent(
                    kUserId,
                    first_bind.epoch,
                    connection_a
                )
            );

            /*
             * 新 connection + 新 epoch
             * 才是current。
             */
            TINYIMX_EXPECT_TRUE(
                manager.IsCurrent(
                    kUserId,
                    second_bind.epoch,
                    connection_b
                )
            );

            /*
             * 旧connection的reverse mapping
             * 也必须被清理。
             */
            TINYIMX_EXPECT_TRUE(
                !manager.
                    FindSessionByConnection(
                        connection_a
                    ).has_value()
            );

            const auto second_snapshot =
                manager.FindSessionByConnection(
                    connection_b
                );

            TINYIMX_EXPECT_TRUE(
                second_snapshot.has_value()
            );

            TINYIMX_EXPECT_EQ(
                second_snapshot->user_id,
                kUserId
            );

            TINYIMX_EXPECT_EQ(
                second_snapshot->epoch,
                second_bind.epoch
            );

            TINYIMX_EXPECT_EQ(
                manager.OnlineCount(),
                static_cast<std::size_t>(1)
            );
        }
    );

        runner.Add(
        "SessionManager.RebindSameConnectionAdvancesEpoch",
        []() {
            constexpr UserId kUserId =
                10005;

            EventLoop loop;

            TINYIMX_EXPECT_TRUE(
                loop.IsValid()
            );

            const TcpConnectionPtr
                connection =
                    MakeTestConnection(
                        &loop,
                        "session-rebind-same",
                        21008,
                        22008
                    );

            TINYIMX_EXPECT_TRUE(
                connection != nullptr
            );

            SessionManager manager;

            /*
             * 第一次Bind：
             *
             * connection
             * epoch1
             */
            const SessionBindResult
                first_bind =
                    manager.BindOrReplace(
                        kUserId,
                        connection
                    );

            TINYIMX_EXPECT_TRUE(
                first_bind.success
            );

            TINYIMX_EXPECT_TRUE(
                first_bind.epoch != 0
            );

            TINYIMX_EXPECT_TRUE(
                manager.IsCurrent(
                    kUserId,
                    first_bind.epoch,
                    connection
                )
            );

            /*
             * 同一个TCP connection再次Bind。
             *
             * 注意：
             * pointer完全没有变化。
             */
            const SessionBindResult
                second_bind =
                    manager.BindOrReplace(
                        kUserId,
                        connection
                    );

            TINYIMX_EXPECT_TRUE(
                second_bind.success
            );

            TINYIMX_EXPECT_TRUE(
                second_bind.epoch != 0
            );

            /*
             * 核心：
             *
             * Connection相同，
             * 但Session generation必须变化。
             */
            TINYIMX_EXPECT_TRUE(
                second_bind.epoch !=
                first_bind.epoch
            );

            /*
             * 旧epoch已经失效。
             *
             * 这就是仅检查connection pointer
             * 无法提供的能力。
             */
            TINYIMX_EXPECT_TRUE(
                !manager.IsCurrent(
                    kUserId,
                    first_bind.epoch,
                    connection
                )
            );

            /*
             * 新epoch才是current。
             */
            TINYIMX_EXPECT_TRUE(
                manager.IsCurrent(
                    kUserId,
                    second_bind.epoch,
                    connection
                )
            );

            const auto snapshot =
                manager.FindSessionByConnection(
                    connection
                );

            TINYIMX_EXPECT_TRUE(
                snapshot.has_value()
            );

            TINYIMX_EXPECT_EQ(
                snapshot->user_id,
                kUserId
            );

            TINYIMX_EXPECT_EQ(
                snapshot->epoch,
                second_bind.epoch
            );

            /*
             * 同一connection重新Bind
             * 仍然只有一个Online Session。
             */
            TINYIMX_EXPECT_EQ(
                manager.OnlineCount(),
                static_cast<std::size_t>(1)
            );
        }
    );
}

}  // namespace tinyimx::test