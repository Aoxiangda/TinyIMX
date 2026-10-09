#include "common/config/Config.h"
#include "common/logging/LogMacros.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr int kBacklog = 128;
constexpr int kBufferSize = 4096;
constexpr int kMaxEvents = 1024;

bool SetNonBlocking(int fd) {
    int old_flags = fcntl(fd, F_GETFL, 0);
    if (old_flags < 0) {
        LOG_ERROR("fcntl F_GETFL failed, fd=" << fd
                  << " error=" << std::strerror(errno));
        return false;
    }

    int new_flags = old_flags | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, new_flags) < 0) {
        LOG_ERROR("fcntl F_SETFL O_NONBLOCK failed, fd=" << fd
                  << " error=" << std::strerror(errno));
        return false;
    }

    return true;
}

bool SetReuseAddr(int fd) {
    int opt = 1;
    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (ret < 0) {
        LOG_ERROR("setsockopt SO_REUSEADDR failed, fd=" << fd
                  << " error=" << std::strerror(errno));
        return false;
    }

    return true;
}

bool FillServerAddress(const std::string& host,
                       uint16_t port,
                       sockaddr_in* server_addr) {
    if (server_addr == nullptr) {
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
        LOG_ERROR("invalid server host: " << host);
        return false;
    }

    return true;
}

int CreateListenSocket(const std::string& host, uint16_t port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERROR("socket failed: " << std::strerror(errno));
        return -1;
    }

    if (!SetReuseAddr(listen_fd)) {
        close(listen_fd);
        return -1;
    }

    if (!SetNonBlocking(listen_fd)) {
        close(listen_fd);
        return -1;
    }

    sockaddr_in server_addr{};
    if (!FillServerAddress(host, port, &server_addr)) {
        close(listen_fd);
        return -1;
    }

    int ret = bind(listen_fd,
                   reinterpret_cast<sockaddr*>(&server_addr),
                   sizeof(server_addr));
    if (ret < 0) {
        LOG_ERROR("bind failed: " << std::strerror(errno));
        close(listen_fd);
        return -1;
    }

    ret = listen(listen_fd, kBacklog);
    if (ret < 0) {
        LOG_ERROR("listen failed: " << std::strerror(errno));
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

bool AddFdToEpoll(int epoll_fd, int fd, uint32_t events) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;

    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    if (ret < 0) {
        LOG_ERROR("epoll_ctl ADD failed, fd=" << fd
                  << " error=" << std::strerror(errno));
        return false;
    }

    return true;
}

bool DeleteFdFromEpoll(int epoll_fd, int fd) {
    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    if (ret < 0) {
        LOG_ERROR("epoll_ctl DEL failed, fd=" << fd
                  << " error=" << std::strerror(errno));
        return false;
    }

    return true;
}

void CloseClient(int epoll_fd,
                 int client_fd,
                 std::unordered_map<int, std::string>* client_addresses) {
    DeleteFdFromEpoll(epoll_fd, client_fd);

    if (client_addresses != nullptr) {
        auto iter = client_addresses->find(client_fd);
        if (iter != client_addresses->end()) {
            LOG_INFO("client disconnected, fd=" << client_fd
                     << " addr=" << iter->second);
            client_addresses->erase(iter);
        } else {
            LOG_INFO("client disconnected, fd=" << client_fd);
        }
    }

    close(client_fd);
}

void AcceptNewClients(int epoll_fd,
                      int listen_fd,
                      std::unordered_map<int, std::string>* client_addresses) {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(
            listen_fd,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_len
        );

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 非阻塞 listen_fd 已经没有更多新连接可接收。
                break;
            }

            LOG_ERROR("accept failed: " << std::strerror(errno));
            break;
        }

        if (!SetNonBlocking(client_fd)) {
            close(client_fd);
            continue;
        }

        if (!AddFdToEpoll(epoll_fd, client_fd, EPOLLIN | EPOLLRDHUP)) {
            close(client_fd);
            continue;
        }

        char client_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_addr.sin_port);

        std::string address = std::string(client_ip) + ":" + std::to_string(client_port);

        if (client_addresses != nullptr) {
            (*client_addresses)[client_fd] = address;
        }

        LOG_INFO("client connected, fd=" << client_fd
                 << " addr=" << address);
    }
}

void HandleClientRead(int epoll_fd,
                      int client_fd,
                      std::unordered_map<int, std::string>* client_addresses) {
    char buffer[kBufferSize];

    while (true) {
        std::memset(buffer, 0, sizeof(buffer));

        ssize_t recv_size = recv(client_fd, buffer, sizeof(buffer), 0);
        if (recv_size > 0) {
            std::string request(buffer, static_cast<std::size_t>(recv_size));
            LOG_INFO("received from client fd=" << client_fd
                     << " data=" << request);

            std::string response = "echo: " + request;

            ssize_t send_size = send(client_fd, response.data(), response.size(), 0);
            if (send_size < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    LOG_WARN("send would block, fd=" << client_fd);
                    return;
                }

                LOG_ERROR("send failed, fd=" << client_fd
                          << " error=" << std::strerror(errno));
                CloseClient(epoll_fd, client_fd, client_addresses);
                return;
            }

            LOG_INFO("send response to client fd=" << client_fd
                     << " bytes=" << send_size);
            continue;
        }

        if (recv_size == 0) {
            CloseClient(epoll_fd, client_fd, client_addresses);
            return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 当前 fd 的数据已经读完。
            return;
        }

        if (errno == EINTR) {
            // 被信号中断，继续读。
            continue;
        }

        LOG_ERROR("recv failed, fd=" << client_fd
                  << " error=" << std::strerror(errno));
        CloseClient(epoll_fd, client_fd, client_addresses);
        return;
    }
}

}  // namespace

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

    int listen_fd = CreateListenSocket(host, port);
    if (listen_fd < 0) {
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LOG_ERROR("epoll_create1 failed: " << std::strerror(errno));
        close(listen_fd);
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!AddFdToEpoll(epoll_fd, listen_fd, EPOLLIN)) {
        close(epoll_fd);
        close(listen_fd);
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    std::unordered_map<int, std::string> client_addresses;
    epoll_event events[kMaxEvents];

    LOG_INFO("epoll echo server listening on " << host << ":" << port);

    while (true) {
        int event_count = epoll_wait(epoll_fd, events, kMaxEvents, -1);
        if (event_count < 0) {
            if (errno == EINTR) {
                continue;
            }

            LOG_ERROR("epoll_wait failed: " << std::strerror(errno));
            break;
        }

        for (int i = 0; i < event_count; ++i) {
            int fd = events[i].data.fd;
            uint32_t event_mask = events[i].events;

            if (fd == listen_fd) {
                AcceptNewClients(epoll_fd, listen_fd, &client_addresses);
                continue;
            }

            if ((event_mask & EPOLLERR) || (event_mask & EPOLLHUP) ||
                (event_mask & EPOLLRDHUP)) {
                CloseClient(epoll_fd, fd, &client_addresses);
                continue;
            }

            if (event_mask & EPOLLIN) {
                HandleClientRead(epoll_fd, fd, &client_addresses);
            }
        }
    }

    for (const auto& item : client_addresses) {
        close(item.first);
    }

    close(epoll_fd);
    close(listen_fd);

    tinyimx::Logger::Instance().Shutdown();
    return 0;
}