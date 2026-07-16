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
#include <nlohmann/json.hpp>

namespace {

using Json = nlohmann::json;

bool SetSocketTimeout(int fd, int seconds) {
    timeval timeout {};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                 &timeout, static_cast<socklen_t>(sizeof(timeout)));

    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                 &timeout, static_cast<socklen_t>(sizeof(timeout)));

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

    if (::connect(fd,
                  reinterpret_cast<sockaddr*>(&address),
                  static_cast<socklen_t>(sizeof(address))) != 0) {
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
/*
    tinyimx::Packet MakeLoginRequest(std::uint64_t user_id,
                                  std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kLoginRequest;
    packet.seq = seq;
    packet.body =
        std::string(R"({"user_id":)") +
        std::to_string(user_id) +
        R"(,"token":"demo-token"})";

    return packet;
}

*/

tinyimx::Packet MakeLoginRequest(const std::string& username,
                                  const std::string& password,
                                  std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kLoginRequest;
    packet.seq = seq;
    packet.body =
        Json{
            {"username", username},
            {"password", password}
        }.dump();

    return packet;
}
/*
    tinyimx::Packet MakeChatMessage(std::uint64_t from,
                                    std::uint64_t to,
                                    std::uint32_t seq,
                                    const std::string& text) {
        tinyimx::Packet packet;
        packet.type = tinyimx::MessageType::kChatMessage;
        packet.seq = seq;
        packet.body =
            std::string(R"({"from":)") +
            std::to_string(from) +
            R"(,"to":)" +
            std::to_string(to) +
            R"(,"text":")" +
            text +
            R"("})";

        return packet;
    }
*/

tinyimx::Packet MakeChatMessage(std::uint64_t to,
                                 std::uint32_t seq,
                                 const std::string& text) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kChatMessage;
    packet.seq = seq;
    packet.body =
        Json{
            {"to", to},
            {"text", text}
        }.dump();

    return packet;
}

bool SendPacket(int fd,
                const tinyimx::ProtocolCodec& codec,
                const tinyimx::Packet& packet) {
    tinyimx::Buffer output;
    std::string error;

    if (!codec.Encode(packet, &output, &error)) {
        std::cerr << "encode failed: " << error << '\n';
        return false;
    }

    return SendAll(fd, output.RetrieveAllAsString());
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

            if (result.status ==
                tinyimx::DecodeStatus::kNeedMoreData) {
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

void PrintPacket(const std::string& tag,
                 const tinyimx::Packet& packet) {
    std::cout << tag
              << " packet:"
              << " type=" << tinyimx::MessageTypeToString(packet.type)
              << " seq=" << packet.seq
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

    const int user_a_fd = ConnectToServer(host, port);
    if (user_a_fd < 0) {
        std::cerr << "connect user A failed\n";
        return 1;
    }

    std::cout << "========== Gateway Offline Client Demo ==========\n";

    if (!SendPacket(
            user_a_fd,
            codec,
            MakeLoginRequest("user10001", "123456", 1))) {
        return 1;
    }

    std::vector<tinyimx::Packet> user_a_login_packets;
    if (!WaitForPackets(user_a_fd, codec, 1, &user_a_login_packets)) {
        return 1;
    }

    PrintPacket("[user_a]", user_a_login_packets[0]);

    if (!SendPacket(
            user_a_fd,
            codec,
            MakeChatMessage(
                10002,
                2,
                "offline hello from user 10001"))) {
        return 1;
    }

    std::vector<tinyimx::Packet> user_a_ack_packets;
    if (!WaitForPackets(user_a_fd, codec, 1, &user_a_ack_packets)) {
        return 1;
    }

    PrintPacket("[user_a]", user_a_ack_packets[0]);

    const int user_b_fd = ConnectToServer(host, port);
    if (user_b_fd < 0) {
        std::cerr << "connect user B failed\n";
        ::close(user_a_fd);
        return 1;
    }

    if (!SendPacket(
            user_b_fd,
            codec,
            MakeLoginRequest("user10002", "123456", 3))) {
        return 1;
    }

    std::vector<tinyimx::Packet> user_b_packets;
    if (!WaitForPackets(user_b_fd, codec, 2, &user_b_packets)) {
        return 1;
    }

    PrintPacket("[user_b]", user_b_packets[0]);
    PrintPacket("[user_b]", user_b_packets[1]);

    bool ok = true;

    ok = ok &&
         user_a_login_packets[0].type ==
             tinyimx::MessageType::kLoginResponse;

    ok = ok &&
         user_a_ack_packets[0].type ==
             tinyimx::MessageType::kChatAck;

    ok = ok &&
         user_b_packets[0].type ==
             tinyimx::MessageType::kLoginResponse;

    ok = ok &&
         user_b_packets[1].type ==
             tinyimx::MessageType::kChatMessage;

    ::close(user_b_fd);
    ::close(user_a_fd);

    if (!ok) {
        std::cerr << "gateway offline client validation failed\n";
        return 1;
    }

    std::cout << "gateway offline client validation passed\n";
    std::cout << "=================================================\n";

    return 0;
}