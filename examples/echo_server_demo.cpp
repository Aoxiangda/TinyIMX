#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "common/logging/LogMacros.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/TcpServer.h"

#include <csignal>
#include <iostream>
#include <memory>
#include <string>

namespace {

tinyimx::EventLoop* g_loop = nullptr;

void HandleSignal(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        if (g_loop != nullptr) {
            g_loop->Quit();
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "load config failed: "
                  << config.LastError() << '\n';
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "logger init failed\n";
        return 1;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    {
        tinyimx::InetAddress listen_address(
            config.ServerHost(),
            config.ServerPort()
        );

        if (!listen_address.IsValid()) {
            LOG_ERROR("invalid listen address"
                      << ", host=" << config.ServerHost()
                      << ", port=" << config.ServerPort());

            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        tinyimx::EventLoop loop;
        g_loop = &loop;

        if (!loop.IsValid()) {
            LOG_ERROR("event loop is invalid");
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        tinyimx::TcpServer server(
            &loop,
            listen_address,
            "echo-server"
        );

        server.SetConnectionCallback([](
            const tinyimx::TcpConnectionPtr& connection
        ) {
            if (connection->IsConnected()) {
                std::cout << "[echo_server] connected: "
                          << connection->PeerAddress().ToString()
                          << '\n';
            }
        });

        server.SetMessageCallback([](
            const tinyimx::TcpConnectionPtr& connection,
            tinyimx::Buffer* buffer
        ) {
            const std::string message =
                buffer->RetrieveAllAsString();

            std::cout << "[echo_server] received from "
                      << connection->PeerAddress().ToString()
                      << ": "
                      << message;

            connection->Send(message);
        });

        if (!server.Start()) {
            LOG_ERROR("echo server start failed");
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        std::cout << "========== Echo Server Demo ==========\n";
        std::cout << "Listening on " << listen_address.ToString() << '\n';
        std::cout << "Try:\n";
        std::cout << "  nc 127.0.0.1 "
                  << config.ServerPort() << '\n';
        std::cout << "Multiple clients are supported.\n";
        std::cout << "Press Ctrl-C to stop server.\n";

        LOG_INFO("echo server demo started"
                 << ", listen=" << listen_address.ToString());

        loop.Loop();

        server.Stop();

        std::cout << "Echo server demo stopped\n";
        std::cout << "======================================\n";

        g_loop = nullptr;
    }

    tinyimx::Logger::Instance().Shutdown();

    return 0;
}