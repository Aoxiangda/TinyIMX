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
#include <vector>
#include <chrono>
#include <thread>

namespace {

using Json = nlohmann::json;

std::string MakeClientMessageId(
    const std::string& prefix
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
        std::to_string(
            nanoseconds
        );
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
        !tinyimx::
            SerializeClientChatRequest(
                request,
                &body,
                &error_message
            )
    ) {
        std::cerr
            << "serialize client chat "
               "request failed"
            << ", error="
            << error_message
            << '\n';

        return {};
    }


    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::
            kChatMessage;

    packet.seq =
        seq;

    packet.body =
        body;


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
        !tinyimx::SerializeReceiverChatDeliveryAck(
            ack,
            &body,
            &error_message
        )
    ) {
        std::cerr
            << "serialize receiver delivery ACK failed"
            << ", error="
            << error_message
            << '\n';

        return {};
    }

    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::kChatDeliveryAck;

    /*
     * Receiver ACK引用真实Delivery Attempt D。
     */
    packet.seq =
        delivery_seq;

    packet.body =
        body;

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
        /*
         * 先尝试解码上一次recv留下来的完整Packet。
         *
         * Buffer生命周期与TCP Connection一致，
         * 因此半包不会因为一次Wait函数返回而丢失。
         */
        const tinyimx::DecodeResult buffered_result =
            codec.Decode(input_buffer);

        if (
            buffered_result.status ==
            tinyimx::DecodeStatus::kOk
        ) {
            for (
                const auto& packet :
                    buffered_result.packets
            ) {
                packets->push_back(packet);
            }

            if (
                packets->size() >=
                expected_count
            ) {
                break;
            }

            continue;
        }

        if (
            buffered_result.status !=
            tinyimx::DecodeStatus::kNeedMoreData
        ) {
            std::cerr
                << "decode failed: "
                << tinyimx::DecodeStatusToString(
                       buffered_result.status
                   )
                << ", error="
                << buffered_result.error_message
                << '\n';

            return false;
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
                << "server closed connection\n";

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

    return true;
}


bool ValidateReceiverDelivery(
    const tinyimx::Packet& packet,
    const std::string& expected_text,
    std::uint64_t* message_id,
    std::uint32_t* delivery_seq
) {
    if (
        packet.type !=
        tinyimx::MessageType::
            kChatDelivery
    ) {
        std::cerr
            << "expected receiver "
               "chat_delivery"
            << ", actual="
            << tinyimx::
                MessageTypeToString(
                    packet.type
                )
            << '\n';

        return false;
    }


    if (packet.seq == 0) {
        std::cerr
            << "receiver delivery seq "
               "must not be zero\n";

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
               "delivery failed"
            << ", error="
            << error_message
            << '\n';

        return false;
    }


    if (
        delivery.from_user_id != 10001 ||
        delivery.to_user_id != 10002 ||
        delivery.text != expected_text ||
        delivery.message_id == 0
    ) {
        std::cerr
            << "receiver delivery "
               "business fields mismatch"
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (message_id != nullptr) {
        *message_id =
            delivery.message_id;
    }


    if (delivery_seq != nullptr) {
        *delivery_seq =
            packet.seq;
    }


    return true;
}

bool ValidateSenderChatAck(
    const tinyimx::Packet& packet,
    const std::string&
        expected_client_message_id,
    bool expected_delivered,
    bool expected_stored_offline,
    const std::string& expected_reason,
    std::uint64_t* message_id
) {
    if (
        packet.type !=
        tinyimx::MessageType::kChatAck
    ) {
        std::cerr
            << "expected chat_ack"
            << ", actual="
            << tinyimx::
                MessageTypeToString(
                    packet.type
                )
            << '\n';

        return false;
    }


    tinyimx::ClientChatAck ack;

    std::string error_message;


    if (
        !tinyimx::
            DeserializeClientChatAck(
                packet.body,
                &ack,
                &error_message
            )
    ) {
        std::cerr
            << "deserialize chat ack failed"
            << ", error="
            << error_message
            << '\n';

        return false;
    }


    if (
        !ack.success ||
        ack.client_message_id !=
            expected_client_message_id ||
        ack.message_id == 0 ||
        ack.from_user_id != 10001 ||
        ack.to_user_id != 10002 ||
        ack.delivered !=
            expected_delivered ||
        ack.stored_offline !=
            expected_stored_offline ||
        !ack.stored_persistent ||
        ack.reused ||
        ack.reason !=
            expected_reason
    ) {
        std::cerr
            << "chat ack semantic mismatch"
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (message_id != nullptr) {
        *message_id =
            ack.message_id;
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

    /*
     * 每条TCP byte stream拥有独立、长生命周期Buffer。
     *
     * TCP半包不能因为一次WaitForPackets返回而丢失。
     */
    tinyimx::Buffer user_a_input_buffer;
    tinyimx::Buffer user_b_input_buffer;

    const int user_b_fd = ConnectToServer(host, port);
    if (user_b_fd < 0) {
        std::cerr << "connect user B failed\n";
        return 1;
    }

    const int user_a_fd = ConnectToServer(host, port);
    if (user_a_fd < 0) {
        std::cerr << "connect user A failed\n";
        ::close(user_b_fd);
        return 1;
    }

    std::cout << "========== Gateway Session Client Demo ==========\n";

    if (!SendPacket(
            user_b_fd,
            codec,
            MakeLoginRequest("user10002", "123456", 1))){
        return 1;
    }

    std::vector<tinyimx::Packet> user_b_packets;
    if (!WaitForPackets(
            user_b_fd,
            codec,
            &user_b_input_buffer,
            1,
            &user_b_packets)) {
        return 1;
    }

    PrintPacket("[user_b]", user_b_packets[0]);

    if (!SendPacket(
            user_a_fd,
            codec,
            MakeLoginRequest("user10001", "123456", 2))){
        return 1;
    }

    std::vector<tinyimx::Packet> user_a_packets;
    if (!WaitForPackets(
            user_a_fd,
            codec,
            &user_a_input_buffer,
            1,
            &user_a_packets)) {
        return 1;
    }

    PrintPacket("[user_a]", user_a_packets[0]);

    const std::string
        client_message_id =
            MakeClientMessageId(
                "m12-legacy-session-"
            );


    const std::string
        message_text =
            "hello from user 10001";


    std::cout
        << "client_message_id="
        << client_message_id
        << '\n';


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

    std::vector<tinyimx::Packet> user_b_chat_packets;
    if (!WaitForPackets(
            user_b_fd,
            codec,
            &user_b_input_buffer,
            1,
            &user_b_chat_packets)) {
        return 1;
    }

    PrintPacket("[user_b]", user_b_chat_packets[0]);

    std::vector<tinyimx::Packet> user_a_ack_packets;
    if (!WaitForPackets(
            user_a_fd,
            codec,
            &user_a_input_buffer,
            1,
            &user_a_ack_packets)) {
        return 1;
    }


    PrintPacket("[user_a]", user_a_ack_packets[0]);

    bool ok = true;

    std::uint64_t
        receiver_message_id = 0;

    std::uint32_t
        receiver_delivery_seq = 0;

    std::uint64_t
        sender_ack_message_id = 0;


    /*
     * 先验证Receiver真正观察到的M / D。
     */
    if (
        !ValidateReceiverDelivery(
            user_b_chat_packets[0],
            message_text,
            &receiver_message_id,
            &receiver_delivery_seq
        )
    ) {
        ok = false;
    }


    /*
     * Sender第一次ChatAck发生在Receiver application ACK之前。
     *
     * 所以：
     *
     * delivered=false
     * reason=local_delivery_awaiting_receiver_ack
     */
    if (
        !ValidateSenderChatAck(
            user_a_ack_packets[0],
            client_message_id,
            false,
            false,
            "local_delivery_awaiting_receiver_ack",
            &sender_ack_message_id
        )
    ) {
        ok = false;
    }


    if (
        receiver_message_id != 0 &&
        receiver_message_id ==
            sender_ack_message_id
    ) {
        std::cout
            << "[PASS] session receiver "
            "delivery and sender ack "
            "share stable message_id"
            << ", message_id="
            << receiver_message_id
            << ", delivery_seq="
            << receiver_delivery_seq
            << '\n';
    } else {
        std::cerr
            << "[FAIL] session message_id "
            "mismatch"
            << ", receiver="
            << receiver_message_id
            << ", sender_ack="
            << sender_ack_message_id
            << '\n';

        ok = false;
    }


    /*
     * 只有Receiver application ACK可以推进：
     *
     * Pending -> ReceiverConfirmed
     */
    if (
        ok &&
        !SendPacket(
            user_b_fd,
            codec,
            MakeReceiverChatDeliveryAck(
                receiver_message_id,
                receiver_delivery_seq
            )
        )
    ) {
        std::cerr
            << "[FAIL] session receiver delivery ACK failed\n";

        ok = false;
    }


    if (ok) {
        std::cout
            << "[SENT] session receiver delivery ACK"
            << ", message_id="
            << receiver_message_id
            << ", delivery_seq="
            << receiver_delivery_seq
            << '\n';

        /*
         * Receiver ACK本身没有ACK-of-ACK。
         *
         * 给Gateway一个短窗口完成durable ReceiverConfirmed，
         * 然后再做Read。
         */
        std::this_thread::sleep_for(
            std::chrono::milliseconds(300)
        );
    }


    if (
        ok &&
        !SendPacket(
            user_b_fd,
            codec,
            MakeReadRequest(
                10001,
                4
            )
        )
    ) {
        std::cerr
            << "[FAIL] send read request failed\n";

        ok = false;
    }


    std::vector<tinyimx::Packet>
        user_b_read_packets;


    if (
        ok &&
        !WaitForPackets(
            user_b_fd,
            codec,
            &user_b_input_buffer,
            1,
            &user_b_read_packets
        )
    ) {
        ok = false;
    }


    if (
        ok &&
        !user_b_read_packets.empty()
    ) {
        PrintPacket(
            "[user_b]",
            user_b_read_packets[0]
        );
    }


    ok = ok &&
         user_b_packets[0].type ==
             tinyimx::MessageType::kLoginResponse;

    ok = ok &&
         user_a_packets[0].type ==
             tinyimx::MessageType::kLoginResponse;

    ok = ok &&
        !user_b_read_packets.empty() &&
        user_b_read_packets[0].type ==
            tinyimx::MessageType::kReadResponse;

    ok = ok &&
        !user_b_read_packets.empty() &&
        ValidateReadResponseBody(
            user_b_read_packets[0].body,
            10002,
            10001);

    ::close(user_a_fd);
    ::close(user_b_fd);

    if (!ok) {
        std::cerr << "gateway session client validation failed\n";
        return 1;
    }

    std::cout
        << "[PASS] session happy path "
           "Pending -> ReceiverConfirmed -> Read\n";
    std::cout << "gateway session client validation passed\n";
    std::cout << "=================================================\n";

    return 0;
}