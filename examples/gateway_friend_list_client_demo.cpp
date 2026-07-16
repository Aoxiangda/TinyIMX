#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {

using Json = nlohmann::json;

bool SetSocketTimeout(int fd, int seconds) {
    timeval timeout {};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    ::setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        static_cast<socklen_t>(sizeof(timeout))
    );

    ::setsockopt(
        fd,
        SOL_SOCKET,
        SO_SNDTIMEO,
        &timeout,
        static_cast<socklen_t>(sizeof(timeout))
    );

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

int ConnectToServer(
    const std::string& host,
    std::uint16_t port
) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) {
        return -1;
    }

    SetSocketTimeout(fd, 5);
    SetTcpNoDelay(fd);

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(
            AF_INET,
            host.c_str(),
            &address.sin_addr) != 1) {
        ::close(fd);
        return -1;
    }

    if (::connect(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            static_cast<socklen_t>(sizeof(address))) != 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

bool SendAll(
    int fd,
    const std::string& data
) {
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

bool SendPacket(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    const tinyimx::Packet& packet
) {
    tinyimx::Buffer output;
    std::string error;

    if (!codec.Encode(packet, &output, &error)) {
        std::cerr << "encode failed: "
                  << error << '\n';
        return false;
    }

    return SendAll(
        fd,
        output.RetrieveAllAsString()
    );
}

bool WaitForOnePacket(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    tinyimx::Packet* packet
) {
    if (packet == nullptr) {
        return false;
    }

    tinyimx::Buffer input_buffer;

    while (true) {
        char temp[4096];

        const ssize_t n =
            ::recv(fd, temp, sizeof(temp), 0);

        if (n > 0) {
            input_buffer.Append(
                temp,
                static_cast<std::size_t>(n)
            );

            const tinyimx::DecodeResult result =
                codec.Decode(&input_buffer);

            if (result.status ==
                tinyimx::DecodeStatus::kNeedMoreData) {
                continue;
            }

            if (result.status !=
                tinyimx::DecodeStatus::kOk) {
                std::cerr
                    << "decode failed: "
                    << tinyimx::DecodeStatusToString(
                           result.status
                       )
                    << ", error="
                    << result.error_message
                    << '\n';

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

        std::cerr
            << "recv failed: "
            << std::strerror(errno)
            << '\n';

        return false;
    }
}

tinyimx::Packet MakeLoginRequest(
    const std::string& username,
    const std::string& password,
    std::uint32_t seq
) {
    tinyimx::Packet packet;
    packet.type =
        tinyimx::MessageType::kLoginRequest;
    packet.seq = seq;

    packet.body =
        Json{
            {"username", username},
            {"password", password}
        }.dump();

    return packet;
}

tinyimx::Packet MakeFriendListRequest(
    std::size_t limit,
    std::uint32_t seq
) {
    tinyimx::Packet packet;
    packet.type =
        tinyimx::MessageType::kFriendListRequest;
    packet.seq = seq;

    packet.body =
        Json{
            {"limit", limit}
        }.dump();

    return packet;
}

void PrintPacket(
    const std::string& tag,
    const tinyimx::Packet& packet
) {
    std::cout
        << tag
        << " packet:"
        << " type="
        << tinyimx::MessageTypeToString(packet.type)
        << " seq=" << packet.seq
        << " body=" << packet.body
        << '\n';
}

bool ValidateLoginSuccess(
    const tinyimx::Packet& packet,
    std::uint64_t expected_user_id,
    const std::string& expected_username
) {
    if (packet.type !=
        tinyimx::MessageType::kLoginResponse) {
        std::cerr
            << "expected login_response, got "
            << tinyimx::MessageTypeToString(packet.type)
            << '\n';

        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        if (!body.value("success", false)) {
            std::cerr
                << "login failed unexpectedly, body="
                << packet.body
                << '\n';

            return false;
        }

        if (body.value("user_id", 0ULL) !=
            expected_user_id) {
            std::cerr
                << "login user_id mismatch, body="
                << packet.body
                << '\n';

            return false;
        }

        if (body.value("username", "") !=
            expected_username) {
            std::cerr
                << "login username mismatch, body="
                << packet.body
                << '\n';

            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr
            << "parse login response failed: "
            << e.what()
            << '\n';

        return false;
    }
}

bool ValidateFriendListRejected(
    const tinyimx::Packet& packet,
    const std::string& expected_reason
) {
    if (packet.type !=
        tinyimx::MessageType::kFriendListResponse) {
        std::cerr
            << "expected friend_list_response, got "
            << tinyimx::MessageTypeToString(packet.type)
            << '\n';

        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        if (body.value("success", true)) {
            std::cerr
                << "expected success=false, body="
                << packet.body
                << '\n';

            return false;
        }

        if (body.value("reason", "") !=
            expected_reason) {
            std::cerr
                << "reason mismatch, expected="
                << expected_reason
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        if (!body.contains("friends") ||
            !body.at("friends").is_array()) {
            std::cerr
                << "rejected response friends should be array, body="
                << packet.body
                << '\n';

            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr
            << "parse rejected friend list response failed: "
            << e.what()
            << '\n';

        return false;
    }
}

bool ContainsFriend(
    const Json& friends,
    std::uint64_t friend_user_id
) {
    for (const auto& friend_item : friends) {
        if (friend_item.value(
                "friend_user_id",
                0ULL) == friend_user_id) {
            return true;
        }
    }

    return false;
}

bool ValidateFriendFields(
    const Json& friends
) {
    for (const auto& friend_item : friends) {
        if (!friend_item.contains("friend_user_id") ||
            !friend_item.contains("username") ||
            !friend_item.contains("nickname") ||
            !friend_item.contains("avatar_url") ||
            !friend_item.contains("user_status") ||
            !friend_item.contains("relation_status") ||
            !friend_item.contains("relation_created_at") ||
            !friend_item.contains("relation_updated_at")) {
            return false;
        }

        if (!friend_item.at(
                "friend_user_id").is_number_unsigned()) {
            return false;
        }

        if (!friend_item.at("username").is_string() ||
            !friend_item.at("nickname").is_string() ||
            !friend_item.at("avatar_url").is_string()) {
            return false;
        }

        if (!friend_item.at(
                "relation_status").is_number_unsigned() &&
            !friend_item.at(
                "relation_status").is_number_integer()) {
            return false;
        }
    }

    return true;
}

bool ValidateFriendListSuccess(
    const tinyimx::Packet& packet,
    std::uint64_t expected_user_id,
    std::size_t expected_limit,
    Json* friends_out
) {
    if (packet.type !=
        tinyimx::MessageType::kFriendListResponse) {
        std::cerr
            << "expected friend_list_response, got "
            << tinyimx::MessageTypeToString(packet.type)
            << '\n';

        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        if (!body.value("success", false)) {
            std::cerr
                << "expected success=true, body="
                << packet.body
                << '\n';

            return false;
        }

        if (body.value("user_id", 0ULL) !=
            expected_user_id) {
            std::cerr
                << "user_id mismatch, body="
                << packet.body
                << '\n';

            return false;
        }

        if (body.value("limit", 0ULL) !=
            expected_limit) {
            std::cerr
                << "limit mismatch, body="
                << packet.body
                << '\n';

            return false;
        }

        if (!body.contains("has_more") ||
            !body.at("has_more").is_boolean()) {
            std::cerr
                << "has_more should be boolean, body="
                << packet.body
                << '\n';

            return false;
        }

        if (!body.contains("friends") ||
            !body.at("friends").is_array()) {
            std::cerr
                << "friends should be array, body="
                << packet.body
                << '\n';

            return false;
        }

        const Json& friends = body.at("friends");

        if (friends.empty()) {
            std::cerr
                << "friends should not be empty, body="
                << packet.body
                << '\n';

            return false;
        }

        if (friends.size() > expected_limit) {
            std::cerr
                << "friend count exceeds limit, body="
                << packet.body
                << '\n';

            return false;
        }

        if (!ValidateFriendFields(friends)) {
            std::cerr
                << "friend fields invalid, body="
                << packet.body
                << '\n';

            return false;
        }

        if (!ContainsFriend(friends, 10002)) {
            std::cerr
                << "friend list should contain user10002, body="
                << packet.body
                << '\n';

            return false;
        }

        if (ContainsFriend(friends, 10003)) {
            std::cerr
                << "blocked user10003 should not appear, body="
                << packet.body
                << '\n';

            return false;
        }

        if (ContainsFriend(friends, 10004)) {
            std::cerr
                << "non-friend user10004 should not appear, body="
                << packet.body
                << '\n';

            return false;
        }

        for (const auto& friend_item : friends) {
            if (friend_item.value(
                    "friend_user_id",
                    0ULL) != 10002) {
                continue;
            }

            if (friend_item.value(
                    "username",
                    "") != "user10002") {
                std::cerr
                    << "user10002 username mismatch, body="
                    << packet.body
                    << '\n';

                return false;
            }

            if (friend_item.value(
                    "relation_status",
                    0U) != 1U) {
                std::cerr
                    << "user10002 relation_status should be 1, body="
                    << packet.body
                    << '\n';

                return false;
            }
        }

        if (friends_out != nullptr) {
            *friends_out = friends;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr
            << "parse friend list response failed: "
            << e.what()
            << '\n';

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
        port = static_cast<std::uint16_t>(
            std::stoi(argv[2])
        );
    }

    std::cout
        << "========== Gateway Friend List Client Demo ==========\n";

    tinyimx::ProtocolCodec codec;
    bool ok = true;

    // 第一部分：未登录连接不能拉取好友列表。
    const int no_login_fd =
        ConnectToServer(host, port);

    if (no_login_fd < 0) {
        std::cerr
            << "connect no-login client failed\n";
        return 1;
    }

    ok = ok &&
         SendPacket(
             no_login_fd,
             codec,
             MakeFriendListRequest(20, 1)
         );

    tinyimx::Packet no_login_response;

    ok = ok &&
         WaitForOnePacket(
             no_login_fd,
             codec,
             &no_login_response
         );

    if (ok) {
        PrintPacket(
            "[no_login_client]",
            no_login_response
        );

        ok = ok &&
             ValidateFriendListRejected(
                 no_login_response,
                 "not_logged_in"
             );
    }

    ::close(no_login_fd);

    // 第二部分：登录后查询自己的好友列表。
    const int fd =
        ConnectToServer(host, port);

    if (fd < 0) {
        std::cerr
            << "connect friend-list client failed\n";
        return 1;
    }

    ok = ok &&
         SendPacket(
             fd,
             codec,
             MakeLoginRequest(
                 "user10001",
                 "123456",
                 2
             )
         );

    tinyimx::Packet login_response;

    ok = ok &&
         WaitForOnePacket(
             fd,
             codec,
             &login_response
         );

    if (ok) {
        PrintPacket(
            "[friend_list_client]",
            login_response
        );

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
             MakeFriendListRequest(20, 3)
         );

    tinyimx::Packet friend_list_response;
    Json friends;

    ok = ok &&
         WaitForOnePacket(
             fd,
             codec,
             &friend_list_response
         );

    if (ok) {
        PrintPacket(
            "[friend_list_client]",
            friend_list_response
        );

        ok = ok &&
             ValidateFriendListSuccess(
                 friend_list_response,
                 10001,
                 20,
                 &friends
             );
    }

    ::close(fd);

    if (!ok) {
        std::cerr
            << "gateway friend list validation failed\n";
        return 1;
    }

    std::cout
        << "gateway friend list validation passed\n"
        << "====================================================\n";

    return 0;
}