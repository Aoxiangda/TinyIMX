#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "common/logging/LogMacros.h"
#include "common/net/Channel.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/Socket.h"
#include "common/net/TcpConnection.h"

#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

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

        tinyimx::Socket listen_socket;

        if (!listen_socket.CreateTcp()) {
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        listen_socket.SetReuseAddr(true);
        listen_socket.SetReusePort(true);

        if (!listen_socket.Bind(listen_address)) {
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        if (!listen_socket.Listen(config.Server().backlog)) {
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        tinyimx::EventLoop loop;

        if (!loop.IsValid()) {
            LOG_ERROR("event loop is invalid");
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        tinyimx::Channel accept_channel(&loop, listen_socket.Fd());

        tinyimx::TcpConnectionPtr connection;

        accept_channel.SetReadCallback([&]() {
            tinyimx::InetAddress peer_address;

            const int conn_fd = listen_socket.Accept(&peer_address);
            if (conn_fd < 0) {
                return;
            }

            if (connection) {
                LOG_WARN("only one connection is supported in this demo"
                         << ", new_peer=" << peer_address.ToString());
                ::close(conn_fd);
                return;
            }

            const std::string connection_name =
                "tcp-connection-demo-" + peer_address.ToString();

            connection = std::make_shared<tinyimx::TcpConnection>(
                &loop,
                connection_name,
                conn_fd,
                listen_address,
                peer_address
            );

            connection->SetConnectionCallback([](
                const tinyimx::TcpConnectionPtr& conn
            ) {
                if (conn->IsConnected()) {
                    std::cout << "[tcp_connection_demo] connected: "
                              << conn->PeerAddress().ToString() << '\n';
                }
            });

            connection->SetMessageCallback([](
                const tinyimx::TcpConnectionPtr& conn,
                tinyimx::Buffer* buffer
            ) {
                const std::string message =
                    buffer->RetrieveAllAsString();

                std::cout << "[tcp_connection_demo] received: "
                          << message;

                conn->Send(message);
            });

            connection->SetCloseCallback([&](
                const tinyimx::TcpConnectionPtr& conn
            ) {
                std::cout << "[tcp_connection_demo] closed: "
                          << conn->PeerAddress().ToString() << '\n';

                conn->ConnectDestroyed();
                connection.reset();

                accept_channel.DisableAll();
                loop.Quit();
            });

            connection->ConnectEstablished();
        });

        accept_channel.SetErrorCallback([&]() {
            LOG_ERROR("accept channel error");
            accept_channel.DisableAll();
            loop.Quit();
        });

        accept_channel.EnableReading();

        std::cout << "========== TcpConnection Demo ==========\n";
        std::cout << "Listening on " << listen_address.ToString() << '\n';
        std::cout << "Try: nc 127.0.0.1 "
                  << config.ServerPort() << '\n';
        std::cout << "Type any message, server will echo it.\n";
        std::cout << "Press Ctrl-D in nc to close client.\n";

        LOG_INFO("tcp connection demo started"
                 << ", listen=" << listen_address.ToString());

        loop.Loop();

        if (connection) {
            connection->ConnectDestroyed();
            connection.reset();
        }

        loop.RemoveChannel(&accept_channel);

        std::cout << "TcpConnection demo finished\n";
        std::cout << "========================================\n";
    }

    tinyimx::Logger::Instance().Shutdown();

    return 0;
}