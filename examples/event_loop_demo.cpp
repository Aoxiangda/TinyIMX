#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "common/net/Channel.h"
#include "common/net/EventLoop.h"
#include "common/net/Socket.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "Load config failed: "
                  << config.LastError()  << "\n";
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "Logger init failed\n";
        return 1;
    }

    int pipe_fd[2] = {-1, -1};

    if (::pipe(pipe_fd) != 0) {
        std::cerr << "pipe failed: "
                  << std::strerror(errno) << "\n";
        return 1;
    }

    tinyimx::Socket::SetNonBlocking(pipe_fd[0]);
    tinyimx::Socket::SetCloseOnExec(pipe_fd[0]);
    tinyimx::Socket::SetCloseOnExec(pipe_fd[1]);

    tinyimx::EventLoop loop;

    if (!loop.IsValid()) {
        std::cerr << "event loop is invalid\n";
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        return 1;
    }

    tinyimx::Channel read_channel(&loop, pipe_fd[0]);

    std::atomic<int> message_count{0};

    read_channel.SetReadCallback([&]() {
        char buffer[256];

        while (true) {
            const ssize_t n = ::read(
                pipe_fd[0],
                buffer,
                sizeof(buffer)
            );

            if (n > 0) {
                std::string message(buffer, static_cast<std::size_t>(n));
                std::cout << "[event_loop_demo] read message: "
                          << message;

                const int count =
                    message_count.fetch_add(1, std::memory_order_relaxed) + 1;

                if (count >= 3) {
                    read_channel.DisableAll();
                    loop.Quit();
                    return;
                }

                continue;
            }

            if (n == 0) {
                std::cout << "[event_loop_demo] pipe closed\n";
                read_channel.DisableAll();
                loop.Quit();
                return;
            }

            if (errno == EINTR) {
                continue;
            }

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }

            std::cerr << "[event_loop_demo] read failed: "
                      << std::strerror(errno) << '\n';

            read_channel.DisableAll();
            loop.Quit();
            return;
        }
    });

    read_channel.SetErrorCallback([&]() {
        std::cerr << "[event_loop_demo] channel error\n";
        read_channel.DisableAll();
        loop.Quit();
    });

    read_channel.EnableReading();

    std::thread writer_thread([&]() {
        for (int i = 1; i <= 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));

            const std::string message =
                "message-" + std::to_string(i) + "\n";

            const ssize_t n = ::write(
                pipe_fd[1],
                message.data(),
                message.size()
            );

            if (n < 0) {
                std::cerr << "[event_loop_demo] write failed: "
                          << std::strerror(errno) << '\n';
                loop.Quit();
                return;
            }
        }
    });

    std::cout << "========== EventLoop Demo ==========\n";
    std::cout << "EventLoop is waiting for pipe readable events...\n";

    loop.Loop();

    if (writer_thread.joinable()) {
        writer_thread.join();
    }

    loop.RemoveChannel(&read_channel);

    ::close(pipe_fd[0]);
    ::close(pipe_fd[1]);

    std::cout << "EventLoop demo finished\n";
    std::cout << "====================================\n";

    tinyimx::Logger::Instance().Shutdown();

    return 0;
}