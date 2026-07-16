#include "tests/concurrency/TestFramework.h"

#include "common/net/Channel.h"
#include "common/net/EventLoop.h"
#include "common/net/Socket.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>

namespace tinyimx::test {

void RegisterEventLoopTests(TestRunner& runner) {
    runner.Add("EventLoop.PipeReadableEvent", []() {
        int pipe_fd[2] = {-1, -1};

        TINYIMX_EXPECT_EQ(::pipe(pipe_fd), 0);

        Socket::SetNonBlocking(pipe_fd[0]);
        Socket::SetCloseOnExec(pipe_fd[0]);
        Socket::SetCloseOnExec(pipe_fd[1]);

        EventLoop loop;
        TINYIMX_EXPECT_TRUE(loop.IsValid());

        Channel channel(&loop, pipe_fd[0]);

        std::atomic<int> read_count{0};

        channel.SetReadCallback([&]() {
            char buffer[128];

            while (true) {
                const ssize_t n = ::read(
                    pipe_fd[0],
                    buffer,
                    sizeof(buffer)
                );

                if (n > 0) {
                    read_count.fetch_add(1, std::memory_order_relaxed);
                    channel.DisableAll();
                    loop.Quit();
                    return;
                }

                if (n == 0) {
                    channel.DisableAll();
                    loop.Quit();
                    return;
                }

                if (errno == EINTR) {
                    continue;
                }

                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return;
                }

                channel.DisableAll();
                loop.Quit();
                return;
            }
        });

        channel.EnableReading();

        std::thread writer_thread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            const std::string message = "event-loop-test";

            ::write(
                pipe_fd[1],
                message.data(),
                message.size()
            );
        });

        loop.Loop();

        if (writer_thread.joinable()) {
            writer_thread.join();
        }

        loop.RemoveChannel(&channel);

        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);

        TINYIMX_EXPECT_EQ(read_count.load(), 1);
    });
}

}  // namespace tinyimx::test