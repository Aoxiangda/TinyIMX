#include "common/net/Buffer.h"
#include "common/protocol/ClientChatProtocol.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
namespace {

using Json = nlohmann::json;

std::string MakeClientMessageId() {
    const auto now =
        std::chrono::system_clock::now()
            .time_since_epoch();

    const auto nanoseconds =
        std::chrono::duration_cast<
            std::chrono::nanoseconds
        >(now).count();


    return
        "m12-cross-" +
        std::to_string(nanoseconds);
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

bool ValidateReadResponseBody(
    const std::string& body,
    std::uint64_t expected_user_id,
    std::uint64_t expected_peer_user_id
) {
    try {
        const Json json = Json::parse(body);

        if (!json.value("success", false)) {
            std::cerr << "read_response success is false, body="
                      << body << '\n';
            return false;
        }

        if (json.value("user_id", 0ULL) != expected_user_id) {
            std::cerr << "read_response user_id mismatch, body="
                      << body << '\n';
            return false;
        }

        if (json.value("peer_user_id", 0ULL) != expected_peer_user_id) {
            std::cerr << "read_response peer_user_id mismatch, body="
                      << body << '\n';
            return false;
        }

        if (json.value("private_unread", -1) != 0) {
            std::cerr << "read_response private_unread is not 0, body="
                      << body << '\n';
            return false;
        }

        if (json.value("total_unread", -1) != 0) {
            std::cerr << "read_response total_unread is not 0, body="
                      << body << '\n';
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "parse read_response failed: "
                  << e.what()
                  << ", body=" << body << '\n';
        return false;
    }
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
            Json{
                {"user_id", user_id},
                {"token", "demo-token"}
            }.dump();

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

tinyimx::Packet MakeChatMessage(
    std::uint64_t to,
    std::uint32_t seq,
    const std::string& text,
    const std::string& client_message_id
) {
    tinyimx::ClientChatRequest request;

    request.client_message_id =
        client_message_id;

    request.to_user_id =
        to;

    request.text =
        text;


    std::string body;
    std::string error_message;


    if (
        !tinyimx::SerializeClientChatRequest(
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

tinyimx::Packet MakeReadRequest(std::uint64_t peer_user_id,
                                 std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kReadRequest;
    packet.seq = seq;
    packet.body =
        Json{
            {"peer_user_id", peer_user_id}
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

bool ValidateChatAck(
    const tinyimx::Packet& packet,
    const std::string& expected_client_message_id,
    bool expected_reused,
    std::uint64_t expected_message_id,
    std::uint64_t* actual_message_id
) {
    if (
        packet.type !=
        tinyimx::MessageType::kChatAck
    ) {
        std::cerr
            << "expected chat_ack, actual="
            << tinyimx::MessageTypeToString(
                   packet.type
               )
            << '\n';

        return false;
    }


    tinyimx::ClientChatAck ack;

    std::string error_message;


    if (
        !tinyimx::DeserializeClientChatAck(
            packet.body,
            &ack,
            &error_message
        )
    ) {
        std::cerr
            << "deserialize chat ack failed"
            << ", error="
            << error_message
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (!ack.success) {
        std::cerr
            << "chat ack success=false"
            << ", reason="
            << ack.reason
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (!ack.delivered) {
        std::cerr
            << "chat ack delivered=false"
            << ", reason="
            << ack.reason
            << '\n';

        return false;
    }


    if (
        ack.client_message_id !=
        expected_client_message_id
    ) {
        std::cerr
            << "client_message_id mismatch"
            << ", expected="
            << expected_client_message_id
            << ", actual="
            << ack.client_message_id
            << '\n';

        return false;
    }


    if (
        ack.reused !=
        expected_reused
    ) {
        std::cerr
            << "reused mismatch"
            << ", expected="
            << expected_reused
            << ", actual="
            << ack.reused
            << '\n';

        return false;
    }


    if (ack.message_id == 0) {
        std::cerr
            << "chat ack message_id=0\n";

        return false;
    }


    /*
     * 第一次调用expected_message_id传0，
     * 只负责取出server message_id。
     *
     * 第二次必须与第一次完全一致。
     */
    if (
        expected_message_id != 0 &&
        ack.message_id !=
            expected_message_id
    ) {
        std::cerr
            << "server message_id mismatch"
            << ", expected="
            << expected_message_id
            << ", actual="
            << ack.message_id
            << '\n';

        return false;
    }


    if (
        ack.from_user_id != 10001 ||
        ack.to_user_id != 10002
    ) {
        std::cerr
            << "chat ack user identity mismatch"
            << ", from="
            << ack.from_user_id
            << ", to="
            << ack.to_user_id
            << '\n';

        return false;
    }


    if (
        ack.remote_gateway_id !=
        "gateway-b"
    ) {
        std::cerr
            << "remote gateway mismatch"
            << ", actual="
            << ack.remote_gateway_id
            << '\n';

        return false;
    }


    if (
        ack.reason !=
        "remote_delivered"
    ) {
        std::cerr
            << "unexpected ack reason="
            << ack.reason
            << '\n';

        return false;
    }


    if (actual_message_id != nullptr) {
        *actual_message_id =
            ack.message_id;
    }


    return true;
}

bool ExpectNoPacketWithin(
    int fd,
    int timeout_ms
) {
    pollfd descriptor {};

    descriptor.fd =
        fd;

    descriptor.events =
        POLLIN;


    int result = 0;


    do {
        result =
            ::poll(
                &descriptor,
                1,
                timeout_ms
            );
    } while (
        result < 0 &&
        errno == EINTR
    );


    if (result == 0) {
        /*
         * timeout：
         * 没有任何可读数据，
         * 这正是我们期待的结果。
         */
        return true;
    }


    if (result < 0) {
        std::cerr
            << "poll failed: "
            << std::strerror(errno)
            << '\n';

        return false;
    }


    if (
        descriptor.revents &
        (
            POLLERR |
            POLLHUP |
            POLLNVAL
        )
    ) {
        std::cerr
            << "receiver socket became invalid"
            << ", revents="
            << descriptor.revents
            << '\n';

        return false;
    }


    if (
        descriptor.revents &
        POLLIN
    ) {
        std::cerr
            << "receiver unexpectedly received "
               "another packet after client retry\n";

        return false;
    }


    return true;
}


}  // namespace

int main(
    int argc,
    char* argv[]
) {
    std::string host = "127.0.0.1";

    std::uint16_t gateway_a_port = 9001;

    std::uint16_t gateway_b_port = 9002;

    if (argc >= 2) {
        host = argv[1];
    }

    if (argc >= 3) {
        gateway_a_port =
            static_cast<std::uint16_t>(
                std::stoi(argv[2])
            );
    }

    if (argc >= 4) {
        gateway_b_port =
            static_cast<std::uint16_t>(
                std::stoi(argv[3])
            );
    }


    std::cout
        << "========== TinyIMX "
           "Cross Gateway Client "
           "Demo ==========\n";


    tinyimx::ProtocolCodec codec;


    /*
     * user10002 → Gateway B
     */
    const int user_b_fd =
        ConnectToServer(
            host,
            gateway_b_port
        );

    if (user_b_fd < 0) {
        std::cerr
            << "connect user B to "
               "gateway B failed\n";

        return 1;
    }


    /*
     * user10001 → Gateway A
     */
    const int user_a_fd =
        ConnectToServer(
            host,
            gateway_a_port
        );

    if (user_a_fd < 0) {
        std::cerr
            << "connect user A to "
               "gateway A failed\n";

        ::close(user_b_fd);

        return 1;
    }


    /*
     * 1. B登录gateway-b。
     */
    if (
        !SendPacket(
            user_b_fd,
            codec,
            MakeLoginRequest(
                "user10002",
                "123456",
                1
            )
        )
    ) {
        return 1;
    }

    std::vector<tinyimx::Packet>
        user_b_login_packets;

    if (
        !WaitForPackets(
            user_b_fd,
            codec,
            1,
            &user_b_login_packets
        )
    ) {
        return 1;
    }

    PrintPacket(
        "[user_b@gateway-b]",
        user_b_login_packets.front()
    );


    /*
     * 2. A登录gateway-a。
     */
    if (
        !SendPacket(
            user_a_fd,
            codec,
            MakeLoginRequest(
                "user10001",
                "123456",
                2
            )
        )
    ) {
        return 1;
    }

    std::vector<tinyimx::Packet>
        user_a_login_packets;

    if (
        !WaitForPackets(
            user_a_fd,
            codec,
            1,
            &user_a_login_packets
        )
    ) {
        return 1;
    }

    PrintPacket(
        "[user_a@gateway-a]",
        user_a_login_packets.front()
    );


    /*
     * 给Discovery一个刷新窗口。
     *
     * 用户在线状态本身Redis立即可见，
     * 这里主要等待Gateway A拿到
     * gateway-b最新Discovery snapshot。
     */
    std::this_thread::sleep_for(
        std::chrono::seconds(4)
    );

    const std::string
        client_message_id =
            MakeClientMessageId();


    const std::string message_text =
        "hello across gateways";


    std::cout
        << "client_message_id="
        << client_message_id
        << '\n';


    /*
     * 3. A → B跨Gateway发消息。
     */
    if (
        !SendPacket(
            user_a_fd,
            codec,
            MakeChatMessage(
                10002,
                3,
                message_text,
                client_message_id
            )
        )
    ) {
        return 1;
    }


    /*
     * user10002必须真的收到消息。
     */
    std::vector<tinyimx::Packet>
        user_b_chat_packets;

    if (
        !WaitForPackets(
            user_b_fd,
            codec,
            1,
            &user_b_chat_packets
        )
    ) {
        return 1;
    }

    PrintPacket(
        "[user_b@gateway-b]",
        user_b_chat_packets.front()
    );


    /*
     * user10001收到Gateway A最终ACK。
     */
    std::vector<tinyimx::Packet>
        user_a_ack_packets;

    if (
        !WaitForPackets(
            user_a_fd,
            codec,
            1,
            &user_a_ack_packets
        )
    ) {
        return 1;
    }

    PrintPacket(
        "[user_a@gateway-a]",
        user_a_ack_packets.front()
    );


    bool ok = true;

    ok =
        ok &&
        user_b_chat_packets.front().type ==
            tinyimx::MessageType::
                kChatMessage;

    ok =
        ok &&
        user_a_ack_packets.front().type ==
            tinyimx::MessageType::
                kChatAck;


    try {
        const Json chat_body =
            Json::parse(
                user_b_chat_packets.
                    front().body
            );

        ok =
            ok &&
            chat_body.value(
                "from",
                0ULL
            ) == 10001;

        ok =
            ok &&
            chat_body.value(
                "to",
                0ULL
            ) == 10002;

        ok =
            ok &&
            chat_body.value(
                "text",
                ""
            ) ==
                message_text;


        const Json ack_body =
            Json::parse(
                user_a_ack_packets.
                    front().body
            );

        ok =
            ok &&
            ack_body.value(
                "success",
                false
            );

        ok =
            ok &&
            ack_body.value(
                "delivered",
                false
            );

        ok =
            ok &&
            ack_body.value(
                "remote_gateway_id",
                ""
            ) ==
                "gateway-b";

        ok =
            ok &&
            ack_body.value(
                "reason",
                ""
            ) ==
                "remote_delivered";
    } catch (
        const std::exception& error
    ) {
        std::cerr
            << "parse validation packet "
               "failed: "
            << error.what()
            << '\n';

        ok = false;
    }


    std::uint64_t
    server_message_id = 0;


    if (
        !ValidateChatAck(
            user_a_ack_packets.front(),
            client_message_id,

            /*
            * 第一次必须是新建。
            */
            false,

            /*
            * 第一次还不知道message_id，
            * 所以传0。
            */
            0,

            &server_message_id
        )
    ) {
        ok = false;
    }


        /*
    * =====================================================
    * 4. Client Retry
    *
    * seq变化：
    * 3 -> 4
    *
    * client_message_id保持完全不变。
    * =====================================================
    */
    if (
        !SendPacket(
            user_a_fd,
            codec,
            MakeChatMessage(
                10002,
                4,
                message_text,
                client_message_id
            )
        )
    ) {
        return 1;
    }



    std::vector<tinyimx::Packet>
        retry_ack_packets;


    if (
        !WaitForPackets(
            user_a_fd,
            codec,
            1,
            &retry_ack_packets
        )
    ) {
        return 1;
    }


    PrintPacket(
        "[user_a@gateway-a retry]",
        retry_ack_packets.front()
    );


    std::uint64_t
        retry_message_id = 0;


    if (
        !ValidateChatAck(
            retry_ack_packets.front(),
            client_message_id,

            /*
            * Retry必须识别为Reused。
            */
            true,

            /*
            * 必须复用第一次server ID。
            */
            server_message_id,

            &retry_message_id
        )
    ) {
        ok = false;
    }


    if (
        server_message_id ==
            retry_message_id &&
        server_message_id != 0
    ) {
        std::cout
            << "[PASS] stable server message_id"
            << ", message_id="
            << server_message_id
            << '\n';
    } else {
        std::cerr
            << "[FAIL] unstable server message_id"
            << ", first="
            << server_message_id
            << ", retry="
            << retry_message_id
            << '\n';

        ok = false;
    }


    if (
        ExpectNoPacketWithin(
            user_b_fd,
            1500
        )
    ) {
        std::cout
            << "[PASS] receiver duplicate "
            "delivery suppressed\n";
    } else {
        std::cerr
            << "[FAIL] receiver received "
            "duplicate chat message\n";

        ok = false;
    }


    std::cout
        << "verification_client_message_id="
        << client_message_id
        << '\n';


    std::cout
        << "verification_server_message_id="
        << server_message_id
        << '\n';

    ::close(user_a_fd);
    ::close(user_b_fd);


    if (!ok) {
        std::cerr
            << "cross gateway "
               "validation failed\n";

        return 1;
    }


    std::cout
        << "cross gateway "
           "validation passed\n"
        << "================================"
           "================\n";

    return 0;
}