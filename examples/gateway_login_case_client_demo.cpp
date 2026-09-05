#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

namespace {

using Json = nlohmann::json;

void SetSocketTimeout(int fd, int seconds) {
    timeval timeout{};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                 &timeout, static_cast<socklen_t>(sizeof(timeout)));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                 &timeout, static_cast<socklen_t>(sizeof(timeout)));
}

int ConnectToServer(const std::string& host, std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    SetSocketTimeout(fd, 8);

    int no_delay = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                 &no_delay, static_cast<socklen_t>(sizeof(no_delay)));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
        ::connect(fd,
                  reinterpret_cast<sockaddr*>(&address),
                  static_cast<socklen_t>(sizeof(address))) != 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

bool SendAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(
            fd,
            data.data() + sent,
            data.size() - sent,
            MSG_NOSIGNAL
        );
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

bool WaitForPacket(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    tinyimx::Packet* packet
) {
    if (packet == nullptr) {
        return false;
    }

    tinyimx::Buffer input;
    while (true) {
        char temp[4096];
        const ssize_t n = ::recv(fd, temp, sizeof(temp), 0);
        if (n > 0) {
            input.Append(temp, static_cast<std::size_t>(n));
            const auto result = codec.Decode(&input);
            if (result.status == tinyimx::DecodeStatus::kNeedMoreData) {
                continue;
            }
            if (result.status != tinyimx::DecodeStatus::kOk ||
                result.packets.empty()) {
                return false;
            }
            *packet = result.packets.front();
            return true;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
}

bool ParseExpectedSuccess(const std::string& value, bool* output) {
    if (output == nullptr) {
        return false;
    }
    if (value == "1" || value == "true") {
        *output = true;
        return true;
    }
    if (value == "0" || value == "false") {
        *output = false;
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 7) {
        std::cerr
            << "usage: " << argv[0]
            << " <host> <port> <username> <password>"
               " <expected_success:0|1> <expected_reason|- >\n";
        return 2;
    }

    const std::string host = argv[1];
    const auto port = static_cast<std::uint16_t>(std::stoi(argv[2]));
    const std::string username = argv[3];
    const std::string password = argv[4];
    bool expected_success = false;
    if (!ParseExpectedSuccess(argv[5], &expected_success)) {
        std::cerr << "invalid expected_success: " << argv[5] << '\n';
        return 2;
    }
    const std::string expected_reason =
        std::string(argv[6]) == "-" ? std::string{} : std::string(argv[6]);

    const int fd = ConnectToServer(host, port);
    if (fd < 0) {
        std::cerr << "connect failed\n";
        return 1;
    }

    tinyimx::ProtocolCodec codec;
    tinyimx::Packet request;
    request.type = tinyimx::MessageType::kLoginRequest;
    request.seq = 1;
    request.body = Json{
        {"username", username},
        {"password", password}
    }.dump();

    tinyimx::Buffer encoded;
    std::string encode_error;
    if (!codec.Encode(request, &encoded, &encode_error) ||
        !SendAll(fd, encoded.RetrieveAllAsString())) {
        std::cerr << "send failed: " << encode_error << '\n';
        ::close(fd);
        return 1;
    }

    tinyimx::Packet response;
    if (!WaitForPacket(fd, codec, &response)) {
        std::cerr << "wait response failed\n";
        ::close(fd);
        return 1;
    }
    ::close(fd);

    std::cout << "response type="
              << tinyimx::MessageTypeToString(response.type)
              << " body=" << response.body << '\n';

    if (response.type != tinyimx::MessageType::kLoginResponse) {
        std::cerr << "expected login_response\n";
        return 1;
    }

    try {
        const Json body = Json::parse(response.body);
        const bool success = body.value("success", false);
        const std::string reason = body.value("reason", "");

        if (success != expected_success) {
            std::cerr << "success mismatch\n";
            return 1;
        }
        if (reason != expected_reason) {
            std::cerr << "reason mismatch expected=" << expected_reason
                      << " got=" << reason << '\n';
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "invalid response json: " << e.what() << '\n';
        return 1;
    }

    std::cout << "gateway login case validation passed\n";
    return 0;
}
