#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "common/logging/LogMacros.h"
#include "common/net/InetAddress.h"
#include "common/net/Socket.h"

#include <chrono>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        return 1;
    }

    tinyimx::InetAddress listen_address(
        config.ServerHost(),
        config.ServerPort()
    );

    if (!listen_address.IsValid()) {
        LOG_ERROR("invalid listen address"
                  << ", host=" << config.ServerHost()
                  << ", port=" << config.ServerPort());
        return 1;
    }

    tinyimx::Socket listen_socket;

    if (!listen_socket.CreateTcp()) {
        return 1;
    }

    if (!listen_socket.SetReuseAddr(true)) {
        return 1;
    }

    listen_socket.SetReusePort(true);

    if (!listen_socket.Bind(listen_address)) {
        return 1;
    }

    if (!listen_socket.Listen(config.Server().backlog)) {
        return 1;
    }

    LOG_INFO("socket basic demo started"
             << ", listen=" << listen_address.ToString()
             << ", fd=" << listen_socket.Fd());

    LOG_INFO("waiting for one client connection, try: nc 127.0.0.1 "
             << config.ServerPort());

    const auto start_time = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(60);

    while (std::chrono::steady_clock::now() - start_time < timeout) {
        tinyimx::InetAddress peer_address;

        const int client_fd = listen_socket.Accept(&peer_address);
        if (client_fd >= 0) {
            tinyimx::Socket client_socket(client_fd);

            LOG_INFO("client accepted"
                     << ", peer=" << peer_address.ToString()
                     << ", client_fd=" << client_fd);

            const std::string greeting =
                "TinyIMX socket_basic_demo connected\n";

            const ssize_t sent = ::send(
                client_socket.Fd(),
                greeting.data(),
                greeting.size(),
                MSG_NOSIGNAL
            );

            if (sent < 0) {
                LOG_ERROR("send greeting failed"
                          << ", error=" << std::strerror(errno));
            } else {
                LOG_INFO("greeting sent"
                         << ", bytes=" << sent);
            }

            LOG_INFO("socket basic demo finished");
            tinyimx::Logger::Instance().Shutdown();
            return 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_WARN("socket basic demo timeout, no client connected");

    tinyimx::Logger::Instance().Shutdown();
    return 0;
}