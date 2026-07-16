#include "common/config/Config.h"
#include "common/logging/LogMacros.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr int kBacklog = 128;
constexpr int kBufferSize = 4096;

bool SetReuseAddr(int fd) {
    int opt = 1;
    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (ret < 0) {
        LOG_ERROR("setsockopt SO_REUSEADDR failed: " << std::strerror(errno));
        return false;
    }
    return true;
}

}  // namespace

bool FillServerAddress(const std::string& host, uint16_t port, sockaddr_in* server_addr) {
    if (server_addr == nullptr) {
        LOG_ERROR("server_addr is null");
        return false;
    }

    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons(port);

    if (host == "0.0.0.0") {
        server_addr->sin_addr.s_addr = INADDR_ANY;
        return true;
    }

    int ret = inet_pton(AF_INET, host.c_str(), &server_addr->sin_addr);

    if (ret <= 0) {
        if (ret == 0) {
            LOG_ERROR("invalid server host: " << host);
        } else {
            LOG_ERROR("inet_pton failed: " << std::strerror(errno));
        }
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    std::string config_path = "config/server.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "failed to load config: " << config_path << std::endl;
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(
            config.LogLevel(),
            config.LogFile(),
            config.LogConsole())) {
        std::cerr << "failed to initialize logger" << std::endl;
        return 1;
    }

    const std::string host = config.ServerHost();
    const uint16_t port = config.ServerPort();

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERROR("socket failed: " << std::strerror(errno));
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!SetReuseAddr(listen_fd)) {
        close(listen_fd);
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    sockaddr_in server_addr{};
    if (!FillServerAddress(host, port, &server_addr)) {
        close(listen_fd);
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    int ret = bind(listen_fd,
                   reinterpret_cast<sockaddr*>(&server_addr),
                   sizeof(server_addr));
    if (ret < 0) {
        LOG_ERROR("bind failed: " << std::strerror(errno));
        close(listen_fd);
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    ret = listen(listen_fd, kBacklog);
    if (ret < 0) {
        LOG_ERROR("listen failed: " << std::strerror(errno));
        close(listen_fd);
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    LOG_INFO("tcp echo server listening on " << host << ":" << port);

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(
            listen_fd,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len
        );

        if (client_fd < 0) {
            LOG_ERROR("accept failed: " << std::strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_addr.sin_port);

        LOG_INFO("client connected, fd=" << client_fd
                 << " addr=" << client_ip << ":" << client_port);

        char buffer[kBufferSize] = {0};

        ssize_t recv_size = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (recv_size < 0) {
            LOG_ERROR("recv failed, fd=" << client_fd
                      << " error=" << std::strerror(errno));
            close(client_fd);
            continue;
        }

        if (recv_size == 0) {
            LOG_INFO("client closed connection before sending data, fd="
                     << client_fd);
            close(client_fd);
            continue;
        }

        std::string request(buffer, static_cast<std::size_t>(recv_size));
        LOG_INFO("received from client fd=" << client_fd
                 << " data=" << request);

        std::string response = "echo: " + request;

        ssize_t send_size = send(client_fd, response.data(), response.size(), 0);
        if (send_size < 0) {
            LOG_ERROR("send failed, fd=" << client_fd
                      << " error=" << std::strerror(errno));
        } else {
            LOG_INFO("send response to client fd=" << client_fd
                     << " bytes=" << send_size);
        }

        close(client_fd);
        LOG_INFO("client disconnected, fd=" << client_fd);
    }

    close(listen_fd);
    tinyimx::Logger::Instance().Shutdown();

    return 0;
}