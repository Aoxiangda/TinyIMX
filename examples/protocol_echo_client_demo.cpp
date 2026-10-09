#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool SetSocketTimeout(int fd, int seconds) {
    timeval timeout {};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    if (::setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            static_cast<socklen_t>(sizeof(timeout))
        ) != 0) {
        return false;
    }

    if (::setsockopt(
            fd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            static_cast<socklen_t>(sizeof(timeout))
        ) != 0) {
        return false;
    }

    return true;
}

bool SetTcpNoDelay(int fd) {
    int value = 1;

    return ::setsockopt(
               fd,
               IPPROTO_TCP,
               TCP_NODELAY,
               &value,
               static_cast<socklen_t>(sizeof(value))
           ) == 0;
}

int ConnectToServer(const std::string& host, uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    SetSocketTimeout(fd, 5);
    SetTcpNoDelay(fd);

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        ::close(fd);
        return -1;
    }

    if (::connect(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            static_cast<socklen_t>(sizeof(address))
        ) != 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

bool SendAll(int fd, const std::string& data) {
    std::size_t sent_total = 0;

    while (sent_total < data.size()) {
        const ssize_t n = ::send(
            fd,
            data.data() + sent_total,
            data.size() - sent_total,
            MSG_NOSIGNAL
        );

        if (n > 0) {
            sent_total += static_cast<std::size_t>(n);
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
}

tinyimx::Packet MakeLoginRequest() {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kLoginRequest;
    packet.seq = 1;
    packet.body = R"({"user_id":10001,"token":"demo-token"})";
    return packet;
}

tinyimx::Packet MakeChatMessage() {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kChatMessage;
    packet.seq = 2;
    packet.body = R"({"from":10001,"to":10002,"text":"hello TinyIMX"})";
    return packet;
}

bool WaitForPackets(int fd,
                    const tinyimx::ProtocolCodec& codec,
                    std::size_t expected_count,
                    std::vector<tinyimx::Packet>* packets) {
    if (packets == nullptr) {
        return false;
    }

    tinyimx::Buffer input_buffer;

    while (packets->size() < expected_count) {
        char temp[4096];

        const ssize_t n = ::recv(fd, temp, sizeof(temp), 0);

        if (n > 0) {
            input_buffer.Append(temp, static_cast<std::size_t>(n));

            const tinyimx::DecodeResult result =
                codec.Decode(&input_buffer);

            if (result.status == tinyimx::DecodeStatus::kNeedMoreData) {
                continue;
            }

            if (result.status != tinyimx::DecodeStatus::kOk) {
                std::cerr << "decode failed: "
                          << tinyimx::DecodeStatusToString(result.status)
                          << ", error=" << result.error_message << '\n';
                return false;
            }

            for (const auto& packet : result.packets) {
                packets->push_back(packet);
            }

            continue;
        }

        if (n == 0) {
            std::cerr << "server closed connection\n";
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        std::cerr << "recv failed: "
                  << std::strerror(errno) << '\n';
        return false;
    }

    return true;
}

void PrintPacket(const tinyimx::Packet& packet) {
    std::cout << "packet:"
              << " type=" << tinyimx::MessageTypeToString(packet.type)
              << " seq=" << packet.seq
              << " flags=" << packet.flags
              << " body=" << packet.body
              << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    uint16_t port = 9000;

    if (argc >= 2) {
        host = argv[1];
    }

    if (argc >= 3) {
        const int parsed_port = std::stoi(argv[2]);
        if (parsed_port <= 0 || parsed_port > 65535) {
            std::cerr << "invalid port\n";
            return 1;
        }
        port = static_cast<uint16_t>(parsed_port);
    }

    tinyimx::ProtocolCodec codec;

    const int fd = ConnectToServer(host, port);
    if (fd < 0) {
        std::cerr << "connect failed: "
                  << host << ":" << port << '\n';
        return 1;
    }

    std::cout << "========== Protocol Echo Client Demo ==========\n";
    std::cout << "connected to " << host << ":" << port << '\n';

    tinyimx::Buffer output;
    std::string error;

    if (!codec.Encode(MakeLoginRequest(), &output, &error)) {
        std::cerr << "encode login request failed: "
                  << error << '\n';
        ::close(fd);
        return 1;
    }

    if (!codec.Encode(MakeChatMessage(), &output, &error)) {
        std::cerr << "encode chat message failed: "
                  << error << '\n';
        ::close(fd);
        return 1;
    }

    const std::string request_bytes =
        output.RetrieveAllAsString();

    if (!SendAll(fd, request_bytes)) {
        std::cerr << "send request failed\n";
        ::close(fd);
        return 1;
    }

    std::vector<tinyimx::Packet> packets;

    if (!WaitForPackets(fd, codec, 2, &packets)) {
        ::close(fd);
        return 1;
    }

    for (const auto& packet : packets) {
        PrintPacket(packet);
    }

    bool ok = true;

    if (packets.size() != 2) {
        ok = false;
    } else {
        ok = ok &&
             packets[0].type == tinyimx::MessageType::kLoginResponse &&
             packets[0].seq == 1;

        ok = ok &&
             packets[1].type == tinyimx::MessageType::kChatAck &&
             packets[1].seq == 2;
    }

    ::close(fd);

    if (!ok) {
        std::cerr << "protocol echo client validation failed\n";
        return 1;
    }

    std::cout << "protocol echo client validation passed\n";
    std::cout << "===============================================\n";

    return 0;
}