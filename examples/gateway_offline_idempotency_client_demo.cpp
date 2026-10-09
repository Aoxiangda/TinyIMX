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
        "m12-offline-" +
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

bool WaitForPackets(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    tinyimx::Buffer* input_buffer,
    std::size_t expected_count,
    std::vector<tinyimx::Packet>* packets
) {
    if (
        input_buffer == nullptr ||
        packets == nullptr
    ) {
        return false;
    }


    while (packets->size() < expected_count) {
        char temp[4096];

        const ssize_t n = ::recv(fd, temp, sizeof(temp), 0);

        if (n > 0) {
            input_buffer->Append(temp, static_cast<std::size_t>(n));

            const tinyimx::DecodeResult result =
                codec.Decode(input_buffer);

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


bool ValidateLocalChatAck(
    const tinyimx::Packet& packet,
    const std::string& expected_client_message_id,
    bool expected_reused,
    const std::string& expected_reason,
    std::uint64_t expected_message_id,
    std::uint64_t* actual_message_id,
    std::int64_t* private_unread,
    std::int64_t* total_unread
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
            << "deserialize local chat ack failed"
            << ", error="
            << error_message
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (!ack.success) {
        std::cerr
            << "local chat ack success=false"
            << ", reason="
            << ack.reason
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (!ack.delivered) {
        std::cerr
            << "local chat ack delivered=false"
            << ", reason="
            << ack.reason
            << '\n';

        return false;
    }


    if (!ack.stored_persistent) {
        std::cerr
            << "local chat must be persisted"
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (ack.stored_offline) {
        std::cerr
            << "online local delivery "
               "must not be stored_offline"
            << ", body="
            << packet.body
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
            << "local chat ack message_id=0\n";

        return false;
    }


    /*
     * 第一次expected_message_id=0：
     * 读取Server生成的message_id。
     *
     * Retry：
     * 必须与第一次完全相同。
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
            << "local ack identity mismatch"
            << ", from="
            << ack.from_user_id
            << ", to="
            << ack.to_user_id
            << '\n';

        return false;
    }


    if (
        ack.reason !=
        expected_reason
    ) {
        std::cerr
            << "local ack reason mismatch"
            << ", expected="
            << expected_reason
            << ", actual="
            << ack.reason
            << '\n';

        return false;
    }


    if (actual_message_id != nullptr) {
        *actual_message_id =
            ack.message_id;
    }


    if (private_unread != nullptr) {
        *private_unread =
            ack.receiver_private_unread;
    }


    if (total_unread != nullptr) {
        *total_unread =
            ack.receiver_total_unread;
    }


    return true;
}


bool ValidateOfflineChatAck(
    const tinyimx::Packet& packet,
    const std::string& expected_client_message_id,
    bool expected_reused,
    std::uint64_t expected_message_id,
    std::uint64_t* actual_message_id,
    std::int64_t* private_unread,
    std::int64_t* total_unread
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
            << "deserialize offline chat ack failed"
            << ", error="
            << error_message
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    /*
     * Offline不代表发送失败。
     *
     * Server已经可靠持久化接受了
     * 这条Logical Message。
     */
    if (!ack.success) {
        std::cerr
            << "offline chat ack success=false"
            << ", reason="
            << ack.reason
            << '\n';

        return false;
    }


    /*
     * Receiver当前没有Online Session，
     * 所以这里绝不能声称Delivered。
     */
    if (ack.delivered) {
        std::cerr
            << "offline message unexpectedly "
               "reported delivered"
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (!ack.stored_persistent) {
        std::cerr
            << "offline reliable message "
               "was not persisted"
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (!ack.stored_offline) {
        std::cerr
            << "offline reliable message "
               "must be marked stored_offline"
            << ", body="
            << packet.body
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
            << "offline ack message_id=0\n";

        return false;
    }


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
            << "offline ack identity mismatch"
            << ", from="
            << ack.from_user_id
            << ", to="
            << ack.to_user_id
            << '\n';

        return false;
    }


    if (
        ack.reason !=
        "target_user_offline"
    ) {
        std::cerr
            << "offline reason mismatch"
            << ", expected=target_user_offline"
            << ", actual="
            << ack.reason
            << '\n';

        return false;
    }


    if (actual_message_id != nullptr) {
        *actual_message_id =
            ack.message_id;
    }


    if (private_unread != nullptr) {
        *private_unread =
            ack.receiver_private_unread;
    }


    if (total_unread != nullptr) {
        *total_unread =
            ack.receiver_total_unread;
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
    std::string host =
        "127.0.0.1";

    std::uint16_t port =
        9001;


    if (argc >= 2) {
        host =
            argv[1];
    }


    if (argc >= 3) {
        port =
            static_cast<std::uint16_t>(
                std::stoi(
                    argv[2]
                )
            );
    }


    std::cout
        << "========== TinyIMX Offline "
           "Idempotency Demo ==========\n";


    tinyimx::ProtocolCodec codec;

    tinyimx::Buffer
        sender_input_buffer;


    bool ok = true;


    /*
     * ============================================================
     * 1. 只连接Sender。
     *
     * Receiver user10002本轮必须保持Offline。
     * ============================================================
     */
    const int sender_fd =
        ConnectToServer(
            host,
            port
        );


    if (sender_fd < 0) {
        std::cerr
            << "connect sender failed"
            << ", host="
            << host
            << ", port="
            << port
            << '\n';

        return 1;
    }


    if (
        !SendPacket(
            sender_fd,
            codec,
            MakeLoginRequest(
                "user10001",
                "123456",
                1
            )
        )
    ) {
        ::close(sender_fd);
        return 1;
    }


    std::vector<tinyimx::Packet>
        login_packets;


    if (
        !WaitForPackets(
            sender_fd,
            codec,
            &sender_input_buffer,
            1,
            &login_packets
        )
    ) {
        ::close(sender_fd);
        return 1;
    }


    if (login_packets.empty()) {
        std::cerr
            << "sender login response missing\n";

        ::close(sender_fd);
        return 1;
    }


    PrintPacket(
        "[user_a@gateway-a]",
        login_packets.front()
    );


    /*
     * ============================================================
     * 2. 一个Logical Client Send
     * ============================================================
     */
    const std::string
        client_message_id =
            MakeClientMessageId();


    /*
     * 每轮使用独特Text，
     * 为下一阶段Receiver上线后的
     * Offline Recovery验证做准备。
     */
    const std::string
        message_text =
            "m12 offline idempotency " +
            client_message_id;


    std::cout
        << "client_message_id="
        << client_message_id
        << '\n';


    /*
     * ============================================================
     * 3. 第一次请求
     *
     * Receiver Offline：
     *
     * Created
     * ↓
     * Pending
     * ↓
     * Unread +1
     * ============================================================
     */
    if (
        !SendPacket(
            sender_fd,
            codec,
            MakeChatMessage(
                10002,
                2,
                message_text,
                client_message_id
            )
        )
    ) {
        ::close(sender_fd);
        return 1;
    }


    std::vector<tinyimx::Packet>
        first_ack_packets;


    if (
        !WaitForPackets(
            sender_fd,
            codec,
            &sender_input_buffer,
            1,
            &first_ack_packets
        )
    ) {
        ::close(sender_fd);
        return 1;
    }


    if (first_ack_packets.empty()) {
        std::cerr
            << "first offline ack missing\n";

        ::close(sender_fd);
        return 1;
    }


    PrintPacket(
        "[user_a first]",
        first_ack_packets.front()
    );


    std::uint64_t
        server_message_id = 0;

    std::int64_t
        first_private_unread = 0;

    std::int64_t
        first_total_unread = 0;


    if (
        !ValidateOfflineChatAck(
            first_ack_packets.front(),
            client_message_id,

            false,

            0,

            &server_message_id,
            &first_private_unread,
            &first_total_unread
        )
    ) {
        ok = false;
    }


    /*
     * ============================================================
     * 4. Client Retry
     *
     * seq变化：
     * 2 → 3
     *
     * client_message_id不变。
     * ============================================================
     */
    if (
        !SendPacket(
            sender_fd,
            codec,
            MakeChatMessage(
                10002,
                3,
                message_text,
                client_message_id
            )
        )
    ) {
        ::close(sender_fd);
        return 1;
    }


    std::vector<tinyimx::Packet>
        retry_ack_packets;


    if (
        !WaitForPackets(
            sender_fd,
            codec,
            &sender_input_buffer,
            1,
            &retry_ack_packets
        )
    ) {
        ::close(sender_fd);
        return 1;
    }


    if (retry_ack_packets.empty()) {
        std::cerr
            << "retry offline ack missing\n";

        ::close(sender_fd);
        return 1;
    }


    PrintPacket(
        "[user_a retry]",
        retry_ack_packets.front()
    );


    std::uint64_t
        retry_message_id = 0;

    std::int64_t
        retry_private_unread = 0;

    std::int64_t
        retry_total_unread = 0;


    if (
        !ValidateOfflineChatAck(
            retry_ack_packets.front(),
            client_message_id,

            true,

            server_message_id,

            &retry_message_id,
            &retry_private_unread,
            &retry_total_unread
        )
    ) {
        ok = false;
    }


    /*
     * ============================================================
     * 5. Stable Server Message ID
     * ============================================================
     */
    if (
        server_message_id != 0 &&
        server_message_id ==
            retry_message_id
    ) {
        std::cout
            << "[PASS] stable offline server message_id"
            << ", message_id="
            << server_message_id
            << '\n';
    } else {
        std::cerr
            << "[FAIL] unstable offline server message_id"
            << ", first="
            << server_message_id
            << ", retry="
            << retry_message_id
            << '\n';

        ok = false;
    }


    /*
     * ============================================================
     * 6. Retry不能产生第二次Unread副作用
     * ============================================================
     */
    if (
        first_private_unread ==
            retry_private_unread &&
        first_total_unread ==
            retry_total_unread
    ) {
        std::cout
            << "[PASS] offline retry unread unchanged"
            << ", private="
            << retry_private_unread
            << ", total="
            << retry_total_unread
            << '\n';
    } else {
        std::cerr
            << "[FAIL] offline retry changed unread"
            << ", first_private="
            << first_private_unread
            << ", retry_private="
            << retry_private_unread
            << ", first_total="
            << first_total_unread
            << ", retry_total="
            << retry_total_unread
            << '\n';

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


    ::close(sender_fd);


    if (!ok) {
        std::cerr
            << "offline idempotency "
               "validation failed\n";

        return 1;
    }


    std::cout
        << "offline idempotency "
           "validation passed\n"
        << "================================================\n";


    return 0;
}
