#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr int kBufferSize = 4096;

bool FillServerAddress(const std::string& server_ip,
                       uint16_t server_port,
                       sockaddr_in* server_addr) {
    if (server_addr == nullptr) {
        return false;
    }

    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons(server_port);

    int ret = inet_pton(AF_INET, server_ip.c_str(), &server_addr->sin_addr);
    if (ret <= 0) {
        std::cerr << "invalid server ip: " << server_ip << std::endl;
        return false;
    }

    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string server_ip = "127.0.0.1";
    uint16_t server_port = 9000;
    std::string message = "hello TinyIMX";

    if (argc >= 2) {
        server_ip = argv[1];
    }

    if (argc >= 3) {
        server_port = static_cast<uint16_t>(std::stoi(argv[2]));
    }

    if (argc >= 4) {
        message = argv[3];
    }

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << std::endl;
        return 1;
    }

    sockaddr_in server_addr{};
    if (!FillServerAddress(server_ip, server_port, &server_addr)) {
        close(client_fd);
        return 1;
    }

    int ret = connect(client_fd,
                      reinterpret_cast<sockaddr*>(&server_addr),
                      sizeof(server_addr));
    if (ret < 0) {
        std::cerr << "connect failed: " << std::strerror(errno) << std::endl;
        close(client_fd);
        return 1;
    }

    std::cout << "connected to server "
              << server_ip << ":" << server_port << std::endl;

    ssize_t send_size = send(client_fd, message.data(), message.size(), 0);
    if (send_size < 0) {
        std::cerr << "send failed: " << std::strerror(errno) << std::endl;
        close(client_fd);
        return 1;
    }

    std::cout << "send: " << message << std::endl;

    char buffer[kBufferSize] = {0};
    ssize_t recv_size = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (recv_size < 0) {
        std::cerr << "recv failed: " << std::strerror(errno) << std::endl;
        close(client_fd);
        return 1;
    }

    if (recv_size == 0) {
        std::cout << "server closed connection" << std::endl;
        close(client_fd);
        return 0;
    }

    std::string response(buffer, static_cast<std::size_t>(recv_size));
    std::cout << "recv: " << response << std::endl;

    close(client_fd);
    return 0;
}