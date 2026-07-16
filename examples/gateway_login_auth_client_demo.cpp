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
#include <vector>

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

bool WaitForOnePacket(int fd,
                      const tinyimx::ProtocolCodec& codec,
                      tinyimx::Packet* packet) {
    if (packet == nullptr) {
        return false;
    }

    tinyimx::Buffer input_buffer;

    while (true) {
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

            if (result.packets.empty()) {
                continue;
            }

            *packet = result.packets.front();
            return true;
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
}

tinyimx::Packet MakeLoginRequest(const Json& body,
                                  std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kLoginRequest;
    packet.seq = seq;
    packet.body = body.dump();

    return packet;
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

bool ValidateLoginResponse(const tinyimx::Packet& packet,
                           bool expected_success,
                           const std::string& expected_reason) {
    if (packet.type != tinyimx::MessageType::kLoginResponse) {
        std::cerr << "expected login_response, got "
                  << tinyimx::MessageTypeToString(packet.type)
                  << '\n';
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        const bool success = body.value("success", false);
        if (success != expected_success) {
            std::cerr << "login success mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (!expected_reason.empty()) {
            const std::string reason =
                body.value("reason", "");

            if (reason != expected_reason) {
                std::cerr << "login reason mismatch, expected="
                          << expected_reason
                          << ", got=" << reason
                          << ", body=" << packet.body << '\n';
                return false;
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "parse login_response failed: "
                  << e.what()
                  << ", body=" << packet.body << '\n';
        return false;
    }
}

bool RunLoginCase(const std::string& host,
                  uint16_t port,
                  const std::string& case_name,
                  const Json& request_body,
                  bool expected_success,
                  const std::string& expected_reason,
                  std::uint32_t seq) {
    const int fd = ConnectToServer(host, port);
    if (fd < 0) {
        std::cerr << case_name << " connect failed\n";
        return false;
    }

    tinyimx::ProtocolCodec codec;

    if (!SendPacket(
            fd,
            codec,
            MakeLoginRequest(request_body, seq))) {
        std::cerr << case_name << " send login failed\n";
        ::close(fd);
        return false;
    }

    tinyimx::Packet response;
    if (!WaitForOnePacket(fd, codec, &response)) {
        std::cerr << case_name << " wait login response failed\n";
        ::close(fd);
        return false;
    }

    PrintPacket("[" + case_name + "]", response);

    const bool ok =
        ValidateLoginResponse(
            response,
            expected_success,
            expected_reason
        );

    ::close(fd);
    return ok;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    uint16_t port = 9000;

    if (argc >= 2) {
        host = argv[1];
    }

    if (argc >= 3) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }

    std::cout << "========== Gateway Login Auth Client Demo ==========\n";

    bool ok = true;

    ok = ok &&
         RunLoginCase(
             host,
             port,
             "correct_password",
             Json{
                 {"username", "user10001"},
                 {"password", "123456"}
             },
             true,
             "",
             1
         );

    ok = ok &&
         RunLoginCase(
             host,
             port,
             "wrong_password",
             Json{
                 {"username", "user10001"},
                 {"password", "wrong-password"}
             },
             false,
             "wrong_password",
             2
         );

    ok = ok &&
         RunLoginCase(
             host,
             port,
             "user_not_found",
             Json{
                 {"username", "not_exists_user"},
                 {"password", "123456"}
             },
             false,
             "user_not_found",
             3
         );

    ok = ok &&
         RunLoginCase(
             host,
             port,
             "invalid_username",
             Json{
                 {"password", "123456"}
             },
             false,
             "invalid_username",
             4
         );

    ok = ok &&
         RunLoginCase(
             host,
             port,
             "invalid_password",
             Json{
                 {"username", "user10001"}
             },
             false,
             "invalid_password",
             5
         );

    if (!ok) {
        std::cerr << "gateway login auth client validation failed\n";
        return 1;
    }

    std::cout << "gateway login auth client validation passed\n";
    std::cout << "====================================================\n";

    return 0;
}