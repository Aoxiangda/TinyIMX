#include "common/net/Buffer.h"
#include "common/protocol/ClientChatProtocol.h"
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
#include <chrono>

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
    const std::string& client_message_id
) {
    tinyimx::ClientChatRequest request;

    request.client_message_id =
        client_message_id;

    request.to_user_id =
        to_user_id;

    request.text =
        text;


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
                          const std::string& expected_reason,
                          std::uint64_t expected_from,
                          std::uint64_t expected_to) {
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

        if (body.value("delivered", true)) {
            std::cerr << "expected delivered=false, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("reason", "") != expected_reason) {
            std::cerr << "reason mismatch, expected="
                      << expected_reason
                      << ", body=" << packet.body << '\n';
            return false;
        }

        if (body.value("from", 0ULL) != expected_from ||
            body.value("to", 0ULL) != expected_to) {
            std::cerr << "from/to mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("stored_offline", true)) {
            std::cerr << "expected stored_offline=false, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("stored_persistent", true)) {
            std::cerr << "expected stored_persistent=false, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("receiver_private_unread", 1) != 0 ||
            body.value("receiver_total_unread", 1) != 0) {
            std::cerr << "expected unread count 0, body="
                      << packet.body << '\n';
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "parse chat ack failed: "
                  << e.what() << '\n';
        return false;
    }
}

bool SendAndExpectRejected(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    std::uint64_t to_user_id,
    const std::string& text,
    std::uint32_t seq,
    const std::string& expected_reason
) {
    const std::string client_message_id =
        MakeClientMessageId(
            "m12-relation-",
            seq
        );

    if (
        !SendPacket(
            fd,
            codec,
            MakeChatMessage(
                to_user_id,
                text,
                seq,
                client_message_id
            )
        )
    ) {
        std::cerr
            << "send chat failed\n";

        return false;
    }

    tinyimx::Packet ack;

    if (!WaitForOnePacket(fd, codec, &ack)) {
        std::cerr << "wait chat ack failed\n";
        return false;
    }

    PrintPacket("[relation_client]", ack);

    return ValidateChatRejected(
        ack,
        expected_reason,
        10001,
        to_user_id
    );
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

    std::cout << "========== Gateway Relation Permission Client Demo ==========\n";

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
         WaitForOnePacket(
             fd,
             codec,
             &login_response
         );

    if (ok) {
        PrintPacket("[relation_client]", login_response);

        ok = ok &&
             ValidateLoginSuccess(
                 login_response,
                 10001,
                 "user10001"
             );
    }

    ok = ok &&
         SendAndExpectRejected(
             fd,
             codec,
             10004,
             "not friend message",
             2,
             "not_friend"
         );

    ok = ok &&
         SendAndExpectRejected(
             fd,
             codec,
             10003,
             "blocked message",
             3,
             "blocked_by_self"
         );

    ::close(fd);

    if (!ok) {
        std::cerr << "gateway relation permission validation failed\n";
        return 1;
    }

    std::cout << "gateway relation permission validation passed\n";
    std::cout << "=============================================================\n";

    return 0;
}