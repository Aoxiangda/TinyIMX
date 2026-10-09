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

tinyimx::Packet MakeHistoryRequest(std::uint64_t peer_user_id,
                                    std::uint64_t before_message_id,
                                    std::size_t limit,
                                    std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kHistoryRequest;
    packet.seq = seq;
    packet.body =
        Json{
            {"peer_user_id", peer_user_id},
            {"before_message_id", before_message_id},
            {"limit", limit}
        }.dump();

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

bool ValidateHistorySuccess(const tinyimx::Packet& packet,
                            std::uint64_t expected_user_id,
                            std::uint64_t expected_peer_user_id,
                            std::uint64_t expected_before_message_id,
                            std::size_t expected_limit,
                            Json* messages_out,
                            bool* has_more_out) {
    if (packet.type != tinyimx::MessageType::kHistoryResponse) {
        std::cerr << "expected history_response, got "
                  << tinyimx::MessageTypeToString(packet.type)
                  << '\n';
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        if (!body.value("success", false)) {
            std::cerr << "expected history success=true, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("user_id", 0ULL) != expected_user_id) {
            std::cerr << "user_id mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("peer_user_id", 0ULL) != expected_peer_user_id) {
            std::cerr << "peer_user_id mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("before_message_id", 0ULL) !=
            expected_before_message_id) {
            std::cerr << "before_message_id mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("limit", 0ULL) != expected_limit) {
            std::cerr << "limit mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (!body.contains("messages") || !body.at("messages").is_array()) {
            std::cerr << "messages should be array, body="
                      << packet.body << '\n';
            return false;
        }

        if (messages_out != nullptr) {
            *messages_out = body.at("messages");
        }

        if (has_more_out != nullptr) {
            *has_more_out = body.value("has_more", false);
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "parse history response failed: "
                  << e.what() << '\n';
        return false;
    }
}

bool ValidateHistoryRejected(const tinyimx::Packet& packet,
                             const std::string& expected_reason) {
    if (packet.type != tinyimx::MessageType::kHistoryResponse) {
        std::cerr << "expected history_response, got "
                  << tinyimx::MessageTypeToString(packet.type)
                  << '\n';
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        if (body.value("success", true)) {
            std::cerr << "expected history success=false, body="
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
        std::cerr << "parse rejected history response failed: "
                  << e.what() << '\n';
        return false;
    }
}

bool IsAscendingByMessageId(const Json& messages) {
    std::uint64_t previous_id = 0;

    for (const auto& message : messages) {
        const std::uint64_t current_id =
            message.value("message_id", 0ULL);

        if (current_id == 0) {
            return false;
        }

        if (previous_id != 0 && current_id <= previous_id) {
            return false;
        }

        previous_id = current_id;
    }

    return true;
}

bool ContainsMessageId(const Json& messages,
                       std::uint64_t message_id) {
    for (const auto& message : messages) {
        if (message.value("message_id", 0ULL) == message_id) {
            return true;
        }
    }

    return false;
}

bool AllMessagesLessThan(const Json& messages,
                         std::uint64_t boundary_message_id) {
    for (const auto& message : messages) {
        const std::uint64_t message_id =
            message.value("message_id", 0ULL);

        if (message_id >= boundary_message_id) {
            return false;
        }
    }

    return true;
}

std::uint64_t OldestMessageId(const Json& messages) {
    if (messages.empty()) {
        return 0;
    }

    return messages.front().value("message_id", 0ULL);
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

    std::cout << "========== Gateway History Client Demo ==========\n";

    const int fd = ConnectToServer(host, port);

    if (fd < 0) {
        std::cerr << "connect failed\n";
        return 1;
    }

    tinyimx::ProtocolCodec codec;
    bool ok = true;

    ok = ok &&
         SendPacket(
             fd,
             codec,
             MakeLoginRequest("user10001", "123456", 1)
         );

    tinyimx::Packet login_response;

    ok = ok &&
         WaitForOnePacket(fd, codec, &login_response);

    if (ok) {
        PrintPacket("[history_client]", login_response);

        ok = ok &&
             ValidateLoginSuccess(
                 login_response,
                 10001,
                 "user10001"
             );
    }

    ok = ok &&
         SendPacket(
             fd,
             codec,
             MakeHistoryRequest(10002, 0, 10, 2)
         );

    tinyimx::Packet first_history_response;
    Json first_messages;
    bool first_has_more = false;

    ok = ok &&
         WaitForOnePacket(fd, codec, &first_history_response);

    if (ok) {
        PrintPacket("[history_client]", first_history_response);

        ok = ok &&
             ValidateHistorySuccess(
                 first_history_response,
                 10001,
                 10002,
                 0,
                 10,
                 &first_messages,
                 &first_has_more
             );
    }

    if (ok && first_messages.empty()) {
        std::cerr << "first history messages should not be empty\n";
        ok = false;
    }

    if (ok && !IsAscendingByMessageId(first_messages)) {
        std::cerr << "first history messages should be ascending\n";
        ok = false;
    }

    const std::uint64_t oldest_message_id =
        ok ? OldestMessageId(first_messages) : 0;

    if (ok && oldest_message_id == 0) {
        std::cerr << "oldest_message_id should not be 0\n";
        ok = false;
    }

    if (ok) {
        ok = ok &&
             SendPacket(
                 fd,
                 codec,
                 MakeHistoryRequest(
                     10002,
                     oldest_message_id,
                     10,
                     3
                 )
             );
    }

    tinyimx::Packet second_history_response;
    Json second_messages;
    bool second_has_more = false;

    if (ok) {
        ok = ok &&
             WaitForOnePacket(fd, codec, &second_history_response);
    }

    if (ok) {
        PrintPacket("[history_client]", second_history_response);

        ok = ok &&
             ValidateHistorySuccess(
                 second_history_response,
                 10001,
                 10002,
                 oldest_message_id,
                 10,
                 &second_messages,
                 &second_has_more
             );
    }

    if (ok && ContainsMessageId(second_messages, oldest_message_id)) {
        std::cerr << "second page should not contain boundary message_id="
                  << oldest_message_id << '\n';
        ok = false;
    }

    if (ok && !AllMessagesLessThan(second_messages, oldest_message_id)) {
        std::cerr << "second page messages should all be less than boundary="
                  << oldest_message_id << '\n';
        ok = false;
    }

    if (ok && !IsAscendingByMessageId(second_messages)) {
        std::cerr << "second history messages should be ascending\n";
        ok = false;
    }

    if (ok) {
        ok = ok &&
             SendPacket(
                 fd,
                 codec,
                 MakeHistoryRequest(900000001ULL, 0, 10, 4)
             );
    }

    tinyimx::Packet not_friend_response;

    if (ok) {
        ok = ok &&
             WaitForOnePacket(fd, codec, &not_friend_response);
    }

    if (ok) {
        PrintPacket("[history_client]", not_friend_response);

        ok = ok &&
             ValidateHistoryRejected(
                 not_friend_response,
                 "not_friend"
             );
    }

    ::close(fd);

    if (!ok) {
        std::cerr << "gateway history validation failed\n";
        return 1;
    }

    std::cout << "gateway history validation passed\n";
    std::cout << "=================================================\n";

    return 0;
}