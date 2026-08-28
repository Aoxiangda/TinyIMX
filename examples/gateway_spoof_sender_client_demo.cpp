#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"
#include "common/protocol/ClientChatProtocol.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <chrono>
#include <optional>

namespace {

using Json = nlohmann::json;

std::string MakeClientMessageId(
    const std::string& prefix,
    std::uint32_t seq
) {
    const auto now =
        std::chrono::system_clock::now()
            .time_since_epoch();

    const auto nanoseconds =
        std::chrono::duration_cast<
            std::chrono::nanoseconds
        >(now).count();


    return
        prefix +
        std::to_string(nanoseconds) +
        "-" +
        std::to_string(seq);
}


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

int ConnectToServer(const std::string& host, std::uint16_t port) {
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


tinyimx::Packet MakeChatMessage(
    std::uint64_t to_user_id,
    const std::string& text,
    std::uint32_t seq,
    const std::string& client_message_id,
    std::optional<std::uint64_t>
        claimed_from_user_id =
            std::nullopt
) {
    tinyimx::ClientChatRequest request;

    request.client_message_id =
        client_message_id;

    request.to_user_id =
        to_user_id;

    request.text =
        text;

    request.claimed_from_user_id =
        claimed_from_user_id;


    std::string body;
    std::string error_message;


    if (
        !tinyimx::
            SerializeClientChatRequest(
                request,
                &body,
                &error_message
            )
    ) {
        std::cerr
            << "serialize client chat request failed"
            << ", error="
            << error_message
            << '\n';

        return {};
    }


    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::kChatMessage;

    packet.seq =
        seq;

    packet.body =
        std::move(body);


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

bool ValidateLoginSuccess(const tinyimx::Packet& packet,
                          std::uint64_t expected_user_id,
                          const std::string& expected_username) {
    if (packet.type != tinyimx::MessageType::kLoginResponse) {
        std::cerr << "expected login_response, got "
                  << tinyimx::MessageTypeToString(packet.type)
                  << '\n';
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        if (!body.value("success", false)) {
            std::cerr << "login failed unexpectedly, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("user_id", 0ULL) != expected_user_id) {
            std::cerr << "user_id mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("username", "") != expected_username) {
            std::cerr << "username mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "parse login response failed: "
                  << e.what() << '\n';
        return false;
    }
}

bool ValidateChatRejected(const tinyimx::Packet& packet,
                          const std::string& expected_reason) {
    if (packet.type != tinyimx::MessageType::kChatAck) {
        std::cerr << "expected chat_ack, got "
                  << tinyimx::MessageTypeToString(packet.type)
                  << '\n';
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        if (body.value("success", true)) {
            std::cerr << "expected success=false, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("reason", "") != expected_reason) {
            std::cerr << "reason mismatch, expected="
                      << expected_reason
                      << ", body=" << packet.body << '\n';
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "parse chat_ack failed: "
                  << e.what() << '\n';
        return false;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;

    if (argc >= 2) {
        host = argv[1];
    }

    if (argc >= 3) {
        port = static_cast<std::uint16_t>(std::stoi(argv[2]));
    }

    std::cout << "========== Gateway Spoof Sender Client Demo ==========\n";

    tinyimx::ProtocolCodec codec;
    bool ok = true;

    const int no_login_fd = ConnectToServer(host, port);
    if (no_login_fd < 0) {
        std::cerr << "connect no-login client failed\n";
        return 1;
    }

    ok = ok &&
         SendPacket(
             no_login_fd,
             codec,
             MakeChatMessage(
                10001,
                "message without login",
                1,
                MakeClientMessageId(
                    "m12-no-login-",
                    1
                )
            )
         );

    tinyimx::Packet no_login_ack;

    ok = ok &&
         WaitForOnePacket(
             no_login_fd,
             codec,
             &no_login_ack
         );

    if (ok) {
        PrintPacket("[no_login_client]", no_login_ack);
        ok = ok &&
             ValidateChatRejected(
                 no_login_ack,
                 "sender_not_logged_in"
             );
    }

    ::close(no_login_fd);

    const int spoof_fd = ConnectToServer(host, port);
    if (spoof_fd < 0) {
        std::cerr << "connect spoof client failed\n";
        return 1;
    }

    ok = ok &&
         SendPacket(
             spoof_fd,
             codec,
             MakeLoginRequest("user10002", "123456", 2)
         );

    tinyimx::Packet login_response;

    ok = ok &&
         WaitForOnePacket(
             spoof_fd,
             codec,
             &login_response
         );

    if (ok) {
        PrintPacket("[spoof_client]", login_response);
        ok = ok &&
             ValidateLoginSuccess(
                 login_response,
                 10002,
                 "user10002"
             );
    }

    ok = ok &&
         SendPacket(
             spoof_fd,
             codec,
            MakeChatMessage(
                10001,
                "fake message from user10001",
                3,
                MakeClientMessageId(
                    "m12-spoof-",
                    3
                ),
                10001
            )
         );

    tinyimx::Packet spoof_ack;

    ok = ok &&
         WaitForOnePacket(
             spoof_fd,
             codec,
             &spoof_ack
         );

    if (ok) {
        PrintPacket("[spoof_client]", spoof_ack);
        ok = ok &&
             ValidateChatRejected(
                 spoof_ack,
                 "from_user_mismatch"
             );
    }

    ::close(spoof_fd);

    if (!ok) {
        std::cerr << "gateway spoof sender validation failed\n";
        return 1;
    }

    std::cout << "gateway spoof sender validation passed\n";
    std::cout << "======================================================\n";

    return 0;
}