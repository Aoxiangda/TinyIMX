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
        "m12-local-" +
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


tinyimx::Packet MakeReceiverChatDeliveryAck(
    std::uint64_t message_id,
    std::uint32_t delivery_seq
) {
    tinyimx::ReceiverChatDeliveryAck ack;

    ack.message_id =
        message_id;


    std::string body;
    std::string error_message;


    if (
        !tinyimx::
            SerializeReceiverChatDeliveryAck(
                ack,
                &body,
                &error_message
            )
    ) {
        std::cerr
            << "serialize receiver delivery ack failed"
            << ", message_id="
            << message_id
            << ", delivery_seq="
            << delivery_seq
            << ", error="
            << error_message
            << '\n';

        return {};
    }


    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::
            kChatDeliveryAck;

    packet.seq =
        delivery_seq;

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
    bool expected_delivered,
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


    if (
        ack.delivered !=
        expected_delivered
    ) {
        std::cerr
            << "local chat ack delivered mismatch"
            << ", expected="
            << expected_delivered
            << ", actual="
            << ack.delivered
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


bool ValidateReceiverChat(
    const tinyimx::Packet& packet,
    const std::string& expected_text,
    std::uint64_t* actual_message_id,
    std::uint32_t* actual_delivery_seq
) {
    /*
     * Receiver现在必须收到独立的
     * Gateway -> Receiver Delivery协议，
     * 而不是Sender侧kChatMessage。
     */
    if (
        packet.type !=
        tinyimx::MessageType::kChatDelivery
    ) {
        std::cerr
            << "expected receiver chat_delivery"
            << ", actual="
            << tinyimx::MessageTypeToString(
                   packet.type
               )
            << '\n';

        return false;
    }


    if (packet.seq == 0) {
        std::cerr
            << "receiver delivery seq must "
               "not be zero\n";

        return false;
    }


    tinyimx::ServerChatDelivery
        delivery;

    std::string error_message;


    if (
        !tinyimx::
            DeserializeServerChatDelivery(
                packet.body,
                &delivery,
                &error_message
            )
    ) {
        std::cerr
            << "deserialize receiver "
               "chat delivery failed"
            << ", error="
            << error_message
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (
        delivery.from_user_id !=
            10001
    ) {
        std::cerr
            << "receiver from mismatch"
            << ", actual="
            << delivery.from_user_id
            << '\n';

        return false;
    }


    if (
        delivery.to_user_id !=
            10002
    ) {
        std::cerr
            << "receiver to mismatch"
            << ", actual="
            << delivery.to_user_id
            << '\n';

        return false;
    }


    if (
        delivery.text !=
            expected_text
    ) {
        std::cerr
            << "receiver text mismatch"
            << ", actual="
            << delivery.text
            << '\n';

        return false;
    }


    if (delivery.message_id == 0) {
        std::cerr
            << "receiver delivery message_id=0\n";

        return false;
    }


    if (actual_message_id != nullptr) {
        *actual_message_id =
            delivery.message_id;
    }


    if (actual_delivery_seq != nullptr) {
        *actual_delivery_seq =
            packet.seq;
    }


    return true;
}


bool DrainPacketsUntilQuiet(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    tinyimx::Buffer* input_buffer,
    int quiet_timeout_ms
) {
    if (input_buffer == nullptr) {
        return false;
    }


    for (;;) {
        /*
         * 先处理之前recv留下来的完整Packet。
         *
         * 注意：
         * input_buffer本身不会销毁，
         * 半包也会继续保留。
         */
        const tinyimx::DecodeResult
            decode_result =
                codec.Decode(
                    input_buffer
                );


        if (
            decode_result.status ==
            tinyimx::DecodeStatus::kOk
        ) {
            for (
                const auto& packet :
                    decode_result.packets
            ) {
                PrintPacket(
                    "[user_b pre-test drain]",
                    packet
                );
            }


            continue;
        }


        if (
            decode_result.status !=
            tinyimx::DecodeStatus::
                kNeedMoreData
        ) {
            std::cerr
                << "decode while draining failed"
                << ", status="
                << tinyimx::
                    DecodeStatusToString(
                        decode_result.status
                    )
                << ", error="
                << decode_result.error_message
                << '\n';

            return false;
        }


        pollfd descriptor {};

        descriptor.fd =
            fd;

        descriptor.events =
            POLLIN;


        int poll_result = 0;


        do {
            poll_result =
                ::poll(
                    &descriptor,
                    1,
                    quiet_timeout_ms
                );
        } while (
            poll_result < 0 &&
            errno == EINTR
        );


        /*
         * 一整个quiet window没有新数据。
         *
         * Login引起的历史Offline Push
         * 已经全部处理完。
         */
        if (poll_result == 0) {
            return true;
        }


        if (poll_result < 0) {
            std::cerr
                << "poll while draining failed: "
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
                << "receiver socket invalid "
                   "while draining"
                << ", revents="
                << descriptor.revents
                << '\n';

            return false;
        }


        if (
            !(descriptor.revents & POLLIN)
        ) {
            continue;
        }


        char temp[4096];


        const ssize_t n =
            ::recv(
                fd,
                temp,
                sizeof(temp),
                0
            );


        if (n > 0) {
            input_buffer->Append(
                temp,
                static_cast<std::size_t>(n)
            );

            continue;
        }


        if (n == 0) {
            std::cerr
                << "receiver connection closed "
                   "while draining\n";

            return false;
        }


        if (errno == EINTR) {
            continue;
        }


        std::cerr
            << "recv while draining failed: "
            << std::strerror(errno)
            << '\n';

        return false;
    }
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
        << "========== TinyIMX Same Gateway "
           "Sequential Idempotency Demo ==========\n";


    tinyimx::ProtocolCodec codec;


    /*
     * 每个TCP连接独立维护Input Buffer。
     *
     * TCP是byte stream，
     * 半包不能因为一次Wait结束而丢失。
     */
    tinyimx::Buffer
        user_a_input_buffer;

    tinyimx::Buffer
        user_b_input_buffer;


    bool ok = true;


    /*
     * ============================================================
     * 1. Receiver先连接Gateway A
     * ============================================================
     */
    const int user_b_fd =
        ConnectToServer(
            host,
            port
        );


    if (user_b_fd < 0) {
        std::cerr
            << "connect user_b failed"
            << ", host="
            << host
            << ", port="
            << port
            << '\n';

        return 1;
    }


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
        ::close(user_b_fd);
        return 1;
    }


    std::vector<tinyimx::Packet>
        user_b_login_packets;


    if (
        !WaitForPackets(
            user_b_fd,
            codec,
            &user_b_input_buffer,
            1,
            &user_b_login_packets
        )
    ) {
        ::close(user_b_fd);
        return 1;
    }


    if (user_b_login_packets.empty()) {
        std::cerr
            << "user_b login response missing\n";

        ::close(user_b_fd);
        return 1;
    }


    PrintPacket(
        "[user_b@gateway-a]",
        user_b_login_packets.front()
    );


    /*
     * 同一次recv中可能同时解出：
     *
     * LoginResponse
     * +
     * Persistent Offline Messages
     *
     * LoginResponse之后的Packet
     * 全部属于本轮测试前历史数据。
     */
    for (
        std::size_t index = 1;
        index <
            user_b_login_packets.size();
        ++index
    ) {
        PrintPacket(
            "[user_b pre-test drain]",
            user_b_login_packets[index]
        );
    }


    if (
        !DrainPacketsUntilQuiet(
            user_b_fd,
            codec,
            &user_b_input_buffer,
            500
        )
    ) {
        std::cerr
            << "failed to drain "
               "receiver login backlog\n";

        ::close(user_b_fd);
        return 1;
    }


    std::cout
        << "[PASS] receiver login backlog drained\n";


    /*
     * ============================================================
     * 2. Sender也连接同一个Gateway A
     * ============================================================
     */
    const int user_a_fd =
        ConnectToServer(
            host,
            port
        );


    if (user_a_fd < 0) {
        std::cerr
            << "connect user_a failed"
            << ", host="
            << host
            << ", port="
            << port
            << '\n';

        ::close(user_b_fd);
        return 1;
    }


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
        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    std::vector<tinyimx::Packet>
        user_a_login_packets;


    if (
        !WaitForPackets(
            user_a_fd,
            codec,
            &user_a_input_buffer,
            1,
            &user_a_login_packets
        )
    ) {
        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    if (user_a_login_packets.empty()) {
        std::cerr
            << "user_a login response missing\n";

        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    PrintPacket(
        "[user_a@gateway-a]",
        user_a_login_packets.front()
    );


    /*
     * ============================================================
     * 3. 构造一次Logical Send
     * ============================================================
     */
    const std::string
        client_message_id =
            MakeClientMessageId();


    const std::string
        message_text =
            "m12 same gateway sequential idempotency";


    std::cout
        << "client_message_id="
        << client_message_id
        << '\n';


    /*
     * ============================================================
     * 4. 第一次发送
     *
     * Packet.seq = 3
     * client_message_id = C100
     * ============================================================
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
        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    /*
    * 当前F4阶段Local Delivery路径：
    *
    * Build Delivery
    *      ↓
    * Encode
    *      ↓
    * Register Receiver Attempt
    *      ↓
    * Submit Send
    *      ↓
    * DB保持Pending
    *      ↓
    * Receiver ACK
    *      ↓
    * Pending -> ReceiverConfirmed
    *
    * Sender ACK中的success表示Server已承担持久化责任；
    * delivered仅表示Receiver应用层是否已经确认。
    */
    std::vector<tinyimx::Packet>
        receiver_chat_packets;


    if (
        !WaitForPackets(
            user_b_fd,
            codec,
            &user_b_input_buffer,
            1,
            &receiver_chat_packets
        )
    ) {
        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    if (receiver_chat_packets.empty()) {
        std::cerr
            << "receiver did not get first chat\n";

        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    /*
     * 如果一次recv已经拿到了两个Chat，
     * 这里就直接暴露Duplicate。
     */
    if (
        receiver_chat_packets.size() != 1
    ) {
        std::cerr
            << "[FAIL] receiver got unexpected "
               "packet count on first delivery"
            << ", count="
            << receiver_chat_packets.size()
            << '\n';

        ok = false;
    }


    PrintPacket(
        "[user_b@gateway-a]",
        receiver_chat_packets.front()
    );

    std::uint64_t
        receiver_server_message_id = 0;

    std::uint32_t
        receiver_delivery_seq = 0;

    if (
        !ValidateReceiverChat(
            receiver_chat_packets.front(),
            message_text,
            &receiver_server_message_id,
            &receiver_delivery_seq
        )
    ) {
        ok = false;
    }

    std::cout
        << "receiver_delivery_seq="
        << receiver_delivery_seq
        << ", receiver_message_id="
        << receiver_server_message_id
        << '\n';

    /*
    * ============================================================
    * 5-a. Wrong Receiver Identity
    *
    * user10001是Sender。
    *
    * M真正的Receiver是user10002。
    *
    * 即使：
    *
    * message_id正确
    * delivery_seq正确
    *
    * user10001也绝对不能确认M。
    * ============================================================
    */
    if (
        !SendPacket(
            user_a_fd,
            codec,
            MakeReceiverChatDeliveryAck(
                receiver_server_message_id,
                receiver_delivery_seq
            )
        )
    ) {
        std::cerr
            << "send forged receiver ack failed\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    std::cout
        << "[SENT] forged receiver ack"
        << ", authenticated_user=10001"
        << ", message_id="
        << receiver_server_message_id
        << ", delivery_seq="
        << receiver_delivery_seq
        << '\n';


    std::this_thread::sleep_for(
        std::chrono::milliseconds(50)
    );


    /*
    * ============================================================
    * 5-b. Unknown Delivery Attempt
    *
    * Receiver身份正确：
    *     user10002
    *
    * message_id正确：
    *     M
    *
    * 但Packet.seq不是Gateway真实发送过的D。
    *
    * 必须被Tracker拒绝为UnknownAttempt。
    * ============================================================
    */
    std::uint32_t
        invalid_delivery_seq =
            receiver_delivery_seq +
            1000000U;


    if (
        invalid_delivery_seq == 0 ||
        invalid_delivery_seq ==
            receiver_delivery_seq
    ) {
        invalid_delivery_seq =
            receiver_delivery_seq ^
            0x40000000U;
    }


    if (
        invalid_delivery_seq == 0 ||
        invalid_delivery_seq ==
            receiver_delivery_seq
    ) {
        invalid_delivery_seq = 1;
    }


    if (invalid_delivery_seq == 0) {
        /*
        * uint32 wrap。
        *
        * Receiver真实seq保证非0，
        * 所以1仍然与原seq不同。
        */
        invalid_delivery_seq = 1;
    }


    if (
        !SendPacket(
            user_b_fd,
            codec,
            MakeReceiverChatDeliveryAck(
                receiver_server_message_id,
                invalid_delivery_seq
            )
        )
    ) {
        std::cerr
            << "send unknown-attempt ack failed\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    std::cout
        << "[SENT] receiver ack with "
        "unknown delivery attempt"
        << ", message_id="
        << receiver_server_message_id
        << ", fake_delivery_seq="
        << invalid_delivery_seq
        << '\n';


    std::this_thread::sleep_for(
        std::chrono::milliseconds(50)
    );

    /*
    * ============================================================
    * 5-c. Simulate Receiver ACK Loss
    *
    * Receiver已经收到：
    *
    *     M / D1
    *
    * 但这里故意不发送：
    *
    *     ACK(M,D1)
    *
    * Gateway必须在receiver_ack_timeout以后：
    *
    *     same M
    *     fresh D2
    *
    * 自动重试。
    * ============================================================
    */

    const std::uint64_t
        first_delivery_message_id =
            receiver_server_message_id;

    const std::uint32_t
        first_delivery_seq =
            receiver_delivery_seq;


    std::cout
        << "[SIMULATED ACK LOSS]"
        << " withholding receiver ACK"
        << ", message_id="
        << first_delivery_message_id
        << ", delivery_seq="
        << first_delivery_seq
        << '\n';



   /*
    * ============================================================
    * 5-d. Wait For ACK-Timeout Retry
    *
    * 默认ACK timeout目前是1500ms。
    *
    * Socket recv timeout本身已有5秒，
    * 所以直接等待下一条Receiver Packet。
    * ============================================================
    */

    std::vector<tinyimx::Packet>
        receiver_retry_packets;


    if (
        !WaitForPackets(
            user_b_fd,
            codec,
            &user_a_input_buffer,
            1,
            &receiver_retry_packets
        )
    ) {
        std::cerr
            << "[FAIL] receiver did not get "
            "ACK-timeout retry delivery\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    if (receiver_retry_packets.empty()) {
        std::cerr
            << "[FAIL] receiver retry packet missing\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    PrintPacket(
        "[user_b@gateway-a retry delivery]",
        receiver_retry_packets.front()
    );

    std::uint64_t
        retry_delivery_message_id = 0;

    std::uint32_t
        retry_delivery_seq = 0;


    if (
        !ValidateReceiverChat(
            receiver_retry_packets.front(),
            message_text,
            &retry_delivery_message_id,
            &retry_delivery_seq
        )
    ) {
        std::cerr
            << "[FAIL] retry receiver delivery invalid\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }

    if (
        retry_delivery_message_id !=
            first_delivery_message_id
    ) {
        std::cerr
            << "[FAIL] ACK-timeout retry changed "
            "stable message_id"
            << ", first="
            << first_delivery_message_id
            << ", retry="
            << retry_delivery_message_id
            << '\n';

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    if (
        retry_delivery_seq == 0 ||
        retry_delivery_seq ==
            first_delivery_seq
    ) {
        std::cerr
            << "[FAIL] ACK-timeout retry did not "
            "allocate fresh delivery seq"
            << ", first="
            << first_delivery_seq
            << ", retry="
            << retry_delivery_seq
            << '\n';

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    std::cout
        << "[PASS] ACK-timeout retry kept "
        "stable message_id and allocated "
        "fresh delivery seq"
        << ", message_id="
        << retry_delivery_message_id
        << ", first_delivery_seq="
        << first_delivery_seq
        << ", retry_delivery_seq="
        << retry_delivery_seq
        << '\n';

    /*
    * ============================================================
    * 5-e. Late ACK For Old Attempt
    *
    * 此时：
    *
    * current attempt = D2
    *
    * 但Receiver发送的是：
    *
    *     ACK(M,D1)
    *
    * 因为D1是真实历史Attempt，
    * 所以必须允许其完成M。
    * ============================================================
    */

    if (
        !SendPacket(
            user_b_fd,
            codec,
            MakeReceiverChatDeliveryAck(
                first_delivery_message_id,
                first_delivery_seq
            )
        )
    ) {
        std::cerr
            << "send late old receiver ack failed\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    std::cout
        << "[SENT] late ACK for old "
        "receiver delivery attempt"
        << ", message_id="
        << first_delivery_message_id
        << ", old_delivery_seq="
        << first_delivery_seq
        << ", current_delivery_seq="
        << retry_delivery_seq
        << '\n';


    std::this_thread::sleep_for(
        std::chrono::milliseconds(100)
    );


    /*
    * ============================================================
    * 5-f. Current Attempt ACK After Late Old ACK
    *
    * M已经被旧D1 ACK确认。
    *
    * 因此现在：
    *
    *     ACK(M,D2)
    *
    * 必须成为Duplicate。
    * ============================================================
    */

    if (
        !SendPacket(
            user_b_fd,
            codec,
            MakeReceiverChatDeliveryAck(
                retry_delivery_message_id,
                retry_delivery_seq
            )
        )
    ) {
        std::cerr
            << "send current retry delivery ack failed\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    std::cout
        << "[SENT] ACK for current retry attempt "
        "after late old ACK confirmation"
        << ", message_id="
        << retry_delivery_message_id
        << ", delivery_seq="
        << retry_delivery_seq
        << '\n';

    /*
    * ============================================================
    * 5-g. Confirm Retry Timer Stops After Late ACK
    *
    * D2自己的Timer已经存在。
    *
    * M被ACK(D1)确认以后，
    * D2 Timer到期必须看到：
    *
    *     confirmed=true
    *
    * 然后直接退出，
    * 不再产生D3。
    * ============================================================
    */

    if (
        ExpectNoPacketWithin(
            user_b_fd,
            2200
        )
    ) {
        std::cout
            << "[PASS] late ACK stopped "
            "future receiver retries"
            << ", message_id="
            << first_delivery_message_id
            << '\n';
    } else {
        std::cerr
            << "[FAIL] receiver got unexpected "
            "third delivery after message "
            "was confirmed"
            << ", message_id="
            << first_delivery_message_id
            << '\n';

        ok = false;
    }
    /*
     * ============================================================
     * 5. 第一次Sender ACK
     * ============================================================
     */
    std::vector<tinyimx::Packet>
        first_ack_packets;


    if (
        !WaitForPackets(
            user_a_fd,
            codec,
            &user_a_input_buffer,
            1,
            &first_ack_packets
        )
    ) {
        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    if (first_ack_packets.empty()) {
        std::cerr
            << "first ack missing\n";

        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    PrintPacket(
        "[user_a@gateway-a]",
        first_ack_packets.front()
    );


    std::uint64_t
        server_message_id = 0;

    std::int64_t
        first_private_unread = 0;

    std::int64_t
        first_total_unread = 0;


    if (
        !ValidateLocalChatAck(
            first_ack_packets.front(),
            client_message_id,

            /*
            * 第一次必须Created。
            */
            false,

            /*
            * Sender ACK生成时，
            * Receiver应用层ACK尚未确认。
            */
            false,

            "local_delivery_awaiting_receiver_ack",

            /*
            * 第一次不知道Server ID。
            */
            0,

            &server_message_id,
            &first_private_unread,
            &first_total_unread
        )
    ) {
        ok = false;
    }

    /*
    * Receiver Delivery与Sender ACK虽然属于
    * 不同Protocol Domain，
    *
    * 但是必须引用同一个稳定Server Message。
    */
    if (
        receiver_server_message_id != 0 &&
        server_message_id != 0 &&
        receiver_server_message_id ==
            server_message_id
    ) {
        std::cout
            << "[PASS] receiver delivery and "
            "sender ack share stable message_id"
            << ", message_id="
            << server_message_id
            << ", delivery_seq="
            << receiver_delivery_seq
            << '\n';
    } else {
        std::cerr
            << "[FAIL] receiver delivery "
            "message_id mismatch"
            << ", receiver_message_id="
            << receiver_server_message_id
            << ", sender_ack_message_id="
            << server_message_id
            << '\n';

        ok = false;
    }


    /*
     * ============================================================
     * 6. Client Retry
     *
     * 网络Attempt变化：
     * seq 3 -> 4
     *
     * Logical Send不变：
     * client_message_id保持相同。
     * ============================================================
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
        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    std::vector<tinyimx::Packet>
        retry_ack_packets;


    if (
        !WaitForPackets(
            user_a_fd,
            codec,
            &user_a_input_buffer,
            1,
            &retry_ack_packets
        )
    ) {
        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    if (retry_ack_packets.empty()) {
        std::cerr
            << "retry ack missing\n";

        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    PrintPacket(
        "[user_a@gateway-a retry]",
        retry_ack_packets.front()
    );


    std::uint64_t
        retry_message_id = 0;

    std::int64_t
        retry_private_unread = 0;

    std::int64_t
        retry_total_unread = 0;


    if (
        !ValidateLocalChatAck(
            retry_ack_packets.front(),
            client_message_id,

            /*
            * 第二次必须Reused。
            */
            true,

            /*
            * 此时Receiver已经通过ACK确认，
            * 所以delivered=true。
            */
            true,

            "local_already_receiver_confirmed",

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
     * 7. Stable Server Message ID
     * ============================================================
     */
    if (
        server_message_id != 0 &&
        server_message_id ==
            retry_message_id
    ) {
        std::cout
            << "[PASS] stable local server message_id"
            << ", message_id="
            << server_message_id
            << '\n';
    } else {
        std::cerr
            << "[FAIL] unstable local server message_id"
            << ", first="
            << server_message_id
            << ", retry="
            << retry_message_id
            << '\n';

        ok = false;
    }


    /*
     * ============================================================
     * 8. Retry不能重复增加Unread
     * ============================================================
     */
    if (
        first_private_unread ==
            retry_private_unread &&
        first_total_unread ==
            retry_total_unread
    ) {
        std::cout
            << "[PASS] retry unread unchanged"
            << ", private="
            << retry_private_unread
            << ", total="
            << retry_total_unread
            << '\n';
    } else {
        std::cerr
            << "[FAIL] retry changed unread"
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


    /*
     * ============================================================
     * 9. Receiver绝不能看到第二条Chat
     * ============================================================
     */
    if (
        ExpectNoPacketWithin(
            user_b_fd,
            1500
        )
    ) {
        std::cout
            << "[PASS] local receiver duplicate "
               "delivery suppressed\n";
    } else {
        std::cerr
            << "[FAIL] local receiver received "
               "duplicate chat\n";

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
            << "same gateway sequential "
               "idempotency validation failed\n";

        return 1;
    }


    std::cout
        << "same gateway sequential "
           "idempotency validation passed\n"
        << "================================================\n";


    return 0;
}
