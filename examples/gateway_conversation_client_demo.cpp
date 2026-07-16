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

tinyimx::Packet MakeConversationListRequest(std::size_t limit,
                                             std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kConversationListRequest;
    packet.seq = seq;
    packet.body =
        Json{
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

bool ValidateConversationRejected(const tinyimx::Packet& packet,
                                  const std::string& expected_reason) {
    if (packet.type != tinyimx::MessageType::kConversationListResponse) {
        std::cerr << "expected conversation_list_response, got "
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
        std::cerr << "parse rejected conversation response failed: "
                  << e.what() << '\n';
        return false;
    }
}

bool IsDescendingByLastMessageId(const Json& conversations) {
    std::uint64_t previous_id = 0;

    for (const auto& conversation : conversations) {
        const std::uint64_t current_id =
            conversation.value("last_message_id", 0ULL);

        if (current_id == 0) {
            return false;
        }

        if (previous_id != 0 && current_id >= previous_id) {
            return false;
        }

        previous_id = current_id;
    }

    return true;
}

bool ContainsPeerConversation(const Json& conversations,
                              std::uint64_t peer_user_id) {
    for (const auto& conversation : conversations) {
        if (conversation.value("peer_user_id", 0ULL) == peer_user_id) {
            return true;
        }
    }

    return false;
}

bool ValidateConversationFields(const Json& conversations) {
    for (const auto& conversation : conversations) {
        if (!conversation.contains("peer_user_id") ||
            !conversation.contains("last_message_id") ||
            !conversation.contains("last_from") ||
            !conversation.contains("last_to") ||
            !conversation.contains("last_content") ||
            !conversation.contains("last_created_at") ||
            !conversation.contains("last_delivery_status") ||
            !conversation.contains("unread_count")) {
            return false;
        }

        if (!conversation.at("unread_count").is_number_integer() &&
            !conversation.at("unread_count").is_number_unsigned()) {
            return false;
        }
    }

    return true;
}

bool ValidateConversationSuccess(const tinyimx::Packet& packet,
                                 std::uint64_t expected_user_id,
                                 std::size_t expected_limit,
                                 Json* conversations_out) {
    if (packet.type != tinyimx::MessageType::kConversationListResponse) {
        std::cerr << "expected conversation_list_response, got "
                  << tinyimx::MessageTypeToString(packet.type)
                  << '\n';
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        if (!body.value("success", false)) {
            std::cerr << "expected success=true, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("user_id", 0ULL) != expected_user_id) {
            std::cerr << "user_id mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("limit", 0ULL) != expected_limit) {
            std::cerr << "limit mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (!body.contains("conversations") ||
            !body.at("conversations").is_array()) {
            std::cerr << "conversations should be array, body="
                      << packet.body << '\n';
            return false;
        }

        const Json& conversations = body.at("conversations");

        if (conversations.empty()) {
            std::cerr << "conversations should not be empty, body="
                      << packet.body << '\n';
            return false;
        }

        if (!ValidateConversationFields(conversations)) {
            std::cerr << "conversation fields invalid, body="
                      << packet.body << '\n';
            return false;
        }

        if (!IsDescendingByLastMessageId(conversations)) {
            std::cerr << "conversations should be ordered by last_message_id desc, body="
                      << packet.body << '\n';
            return false;
        }

        if (!ContainsPeerConversation(conversations, 10002)) {
            std::cerr << "conversations should contain peer_user_id=10002, body="
                      << packet.body << '\n';
            return false;
        }

        if (conversations_out != nullptr) {
            *conversations_out = conversations;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "parse conversation response failed: "
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

    std::cout << "========== Gateway Conversation Client Demo ==========\n";

    tinyimx::ProtocolCodec codec;
    bool ok = true;

    const int no_login_fd = ConnectToServer(host, port);
    if (no_login_fd < 0) {
        std::cerr << "connect no_login client failed\n";
        return 1;
    }

    ok = ok &&
         SendPacket(
             no_login_fd,
             codec,
             MakeConversationListRequest(20, 1)
         );

    tinyimx::Packet no_login_response;

    ok = ok &&
         WaitForOnePacket(
             no_login_fd,
             codec,
             &no_login_response
         );

    if (ok) {
        PrintPacket("[no_login_client]", no_login_response);

        ok = ok &&
             ValidateConversationRejected(
                 no_login_response,
                 "not_logged_in"
             );
    }

    ::close(no_login_fd);

    const int fd = ConnectToServer(host, port);
    if (fd < 0) {
        std::cerr << "connect login client failed\n";
        return 1;
    }

    ok = ok &&
         SendPacket(
             fd,
             codec,
             MakeLoginRequest("user10001", "123456", 2)
         );

    tinyimx::Packet login_response;

    ok = ok &&
         WaitForOnePacket(
             fd,
             codec,
             &login_response
         );

    if (ok) {
        PrintPacket("[conversation_client]", login_response);

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
             MakeConversationListRequest(20, 3)
         );

    tinyimx::Packet conversation_response;
    Json conversations;

    ok = ok &&
         WaitForOnePacket(
             fd,
             codec,
             &conversation_response
         );

    if (ok) {
        PrintPacket("[conversation_client]", conversation_response);

        ok = ok &&
             ValidateConversationSuccess(
                 conversation_response,
                 10001,
                 20,
                 &conversations
             );
    }

    ::close(fd);

    if (!ok) {
        std::cerr << "gateway conversation validation failed\n";
        return 1;
    }

    std::cout << "gateway conversation validation passed\n";
    std::cout << "======================================================\n";

    return 0;
}