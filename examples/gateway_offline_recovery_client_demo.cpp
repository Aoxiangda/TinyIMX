#include "common/net/Buffer.h"
#include "common/protocol/ClientChatProtocol.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>

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
/*
    tinyimx::Packet MakeLoginRequest(std::uint64_t user_id,
                                  std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kLoginRequest;
    packet.seq = seq;
    packet.body =
        std::string(R"({"user_id":)") +
        std::to_string(user_id) +
        R"(,"token":"demo-token"})";

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


tinyimx::Packet MakeReceiverChatDeliveryAck(
    std::uint64_t message_id,
    std::uint32_t delivery_seq
) {
    tinyimx::ReceiverChatDeliveryAck ack;
    ack.message_id = message_id;

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
        tinyimx::MessageType::kChatDeliveryAck;

    /*
     * Packet.seq is the real Receiver Delivery Attempt D.
     * body.message_id is the stable durable business M.
     */
    packet.seq = delivery_seq;
    packet.body = std::move(body);

    return packet;
}


tinyimx::Packet MakeChatMessage(std::uint64_t to,
                                 std::uint32_t seq,
                                 const std::string& text) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kChatMessage;
    packet.seq = seq;
    packet.body =
        Json{
            {"to", to},
            {"text", text}
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
         * First consume any partial/complete data preserved from the
         * previous call on this same TCP connection.
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
                return true;
            }
        } else if (
            buffered_result.status !=
            tinyimx::DecodeStatus::kNeedMoreData
        ) {
            std::cerr
                << "decode buffered data failed: "
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

            const tinyimx::DecodeResult result =
                codec.Decode(input_buffer);

            if (
                result.status ==
                tinyimx::DecodeStatus::kNeedMoreData
            ) {
                continue;
            }

            if (
                result.status !=
                tinyimx::DecodeStatus::kOk
            ) {
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

            for (
                const auto& packet :
                    result.packets
            ) {
                packets->push_back(packet);
            }

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

bool IsTargetOfflineMessage(
    const tinyimx::Packet& packet,
    const std::string& expected_text,
    std::uint64_t expected_server_message_id,
    std::uint32_t* actual_delivery_seq
) {
    /*
     * Persistent Offline Replay现在也必须
     * 使用正式Receiver Delivery协议。
     */
    if (
        packet.type !=
        tinyimx::MessageType::
            kChatDelivery
    ) {
        return false;
    }


    if (packet.seq == 0) {
        std::cerr
            << "offline delivery seq is zero\n";

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
            << "deserialize persistent "
               "offline chat delivery failed"
            << ", error="
            << error_message
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (
        delivery.message_id !=
            expected_server_message_id
    ) {
        return false;
    }


    if (
        delivery.from_user_id != 10001 ||
        delivery.to_user_id != 10002 ||
        delivery.text != expected_text
    ) {
        return false;
    }


    if (actual_delivery_seq != nullptr) {
        *actual_delivery_seq =
            packet.seq;
    }


    return true;
}


std::size_t CountTargetOfflineMessages(
    const std::vector<tinyimx::Packet>& packets,
    const std::string& expected_text,
    std::uint64_t expected_server_message_id,
    std::uint32_t* actual_delivery_seq
) {
    std::size_t count = 0;


    if (actual_delivery_seq != nullptr) {
        *actual_delivery_seq =
            0;
    }


    for (
        const auto& packet :
            packets
    ) {
        std::uint32_t
            current_delivery_seq = 0;


        if (
            !IsTargetOfflineMessage(
                packet,
                expected_text,
                expected_server_message_id,
                &current_delivery_seq
            )
        ) {
            continue;
        }


        ++count;


        if (
            actual_delivery_seq != nullptr &&
            *actual_delivery_seq == 0
        ) {
            *actual_delivery_seq =
                current_delivery_seq;
        }
    }


    return count;
}


int main(
    int argc,
    char* argv[]
) {
    if (argc < 5) {
        std::cerr
            << "usage: "
            << argv[0]
            << " <host> <port> "
               "<client_message_id> "
               "<server_message_id>\n";

        return 1;
    }


    const std::string host =
        argv[1];

    const std::uint16_t port =
        static_cast<std::uint16_t>(
            std::stoi(argv[2])
        );

    const std::string client_message_id =
        argv[3];

    const std::uint64_t
        expected_server_message_id =
            std::stoull(argv[4]);


    const std::string expected_text =
        "m12 offline idempotency " +
        client_message_id;


    tinyimx::ProtocolCodec codec;

    /*
     * TCP is a byte stream. Keep one persistent input buffer per
     * connection so a half packet cannot be lost across Wait calls.
     */
    tinyimx::Buffer first_input_buffer;
    tinyimx::Buffer second_input_buffer;


    std::cout
        << "========== TinyIMX Persistent "
           "Offline Recovery Demo ==========\n"
        << "client_message_id="
        << client_message_id
        << '\n'
        << "expected_server_message_id="
        << expected_server_message_id
        << '\n';


    /*
     * ============================================================
     * 第一次Receiver登录
     * ============================================================
     */
    const int first_fd =
        ConnectToServer(
            host,
            port
        );


    if (first_fd < 0) {
        std::cerr
            << "first receiver connect failed\n";

        return 1;
    }


    if (
        !SendPacket(
            first_fd,
            codec,
            MakeLoginRequest(
                "user10002",
                "123456",
                1
            )
        )
    ) {
        ::close(first_fd);
        return 1;
    }


    /*
     * 先收LoginResponse。
     *
     * 注意：
     * WaitForPackets一次recv可能顺带解析出
     * 后面的offline chat，所以vector里可能>1。
     */
    std::vector<tinyimx::Packet>
        first_packets;


    if (
        !WaitForPackets(
            first_fd,
            codec,
            &first_input_buffer,
            1,
            &first_packets
        )
    ) {
        ::close(first_fd);
        return 1;
    }


    if (first_packets.empty()) {
        std::cerr
            << "first login response missing\n";

        ::close(first_fd);
        return 1;
    }


    PrintPacket(
        "[first login]",
        first_packets.front()
    );


    if (
        first_packets.front().type !=
        tinyimx::MessageType::kLoginResponse
    ) {
        std::cerr
            << "first packet is not login_response\n";

        ::close(first_fd);
        return 1;
    }


    std::size_t offline_count = 0;


    try {
        const Json login_body =
            Json::parse(
                first_packets.front().body
            );

        if (
            !login_body.value(
                "success",
                false
            )
        ) {
            std::cerr
                << "first receiver login failed\n";

            ::close(first_fd);
            return 1;
        }


        offline_count =
            login_body.value(
                "offline_count",
                0ULL
            );

    } catch (
        const std::exception& e
    ) {
        std::cerr
            << "parse first login response failed: "
            << e.what()
            << '\n';

        ::close(first_fd);
        return 1;
    }


    /*
     * 如果第一次recv只拿到了LoginResponse，
     * 继续等剩余offline_count条。
     *
     * 如果第一次recv已经顺带拿到了，
     * first_packets.size()可能已经够了。
     */
    const std::size_t
        expected_first_packet_count =
            1 + offline_count;


    if (
        first_packets.size() <
        expected_first_packet_count
    ) {
        if (
            !WaitForPackets(
                first_fd,
                codec,
                &first_input_buffer,
                expected_first_packet_count,
                &first_packets
            )
        ) {
            ::close(first_fd);
            return 1;
        }
    }


    for (
        std::size_t i = 1;
        i < first_packets.size();
        ++i
    ) {
        PrintPacket(
            "[first recovery]",
            first_packets[i]
        );
    }


    std::uint32_t
        first_recovery_delivery_seq = 0;


    const std::size_t
        first_target_count =
            CountTargetOfflineMessages(
                first_packets,
                expected_text,
                expected_server_message_id,
                &first_recovery_delivery_seq
            );

    if (first_target_count != 1) {
        std::cerr
            << "[FAIL] target offline message "
               "recovery count mismatch"
            << ", expected=1"
            << ", actual="
            << first_target_count
            << '\n';

        ::close(first_fd);
        return 1;
    }


    if (first_recovery_delivery_seq == 0) {
        std::cerr
            << "[FAIL] recovered offline "
            "delivery seq is zero\n";

        ::close(first_fd);

        return 1;
    }


    std::cout
        << "[PASS] target persistent offline "
        "message recovered once"
        << ", server_message_id="
        << expected_server_message_id
        << ", delivery_seq="
        << first_recovery_delivery_seq
        << '\n';


    /*
     * Final M12 semantics:
     *
     * Receiving kChatDelivery is NOT durable delivery confirmation.
     * Only the Receiver application ACK may advance:
     *
     * Pending -> ReceiverConfirmed
     *
     * This ACK is the exact step the old recovery demo was missing.
     */
    if (
        !SendPacket(
            first_fd,
            codec,
            MakeReceiverChatDeliveryAck(
                expected_server_message_id,
                first_recovery_delivery_seq
            )
        )
    ) {
        std::cerr
            << "[FAIL] first recovery receiver ACK failed"
            << ", message_id="
            << expected_server_message_id
            << ", delivery_seq="
            << first_recovery_delivery_seq
            << '\n';

        ::close(first_fd);
        return 1;
    }


    std::cout
        << "[SENT] first recovery receiver ACK"
        << ", message_id="
        << expected_server_message_id
        << ", delivery_seq="
        << first_recovery_delivery_seq
        << '\n';


    /*
     * kChatDeliveryAck is one-way. Give Gateway enough time to
     * persist ReceiverConfirmed before closing and re-login.
     */
    std::this_thread::sleep_for(
        std::chrono::milliseconds(500)
    );


    ::close(first_fd);


    /*
     * 给Gateway一点时间完成：
     *
     * Connection Close
     * Session Unbind
     * Redis Online Route清理
     */
    std::this_thread::sleep_for(
        std::chrono::milliseconds(300)
    );


    /*
     * ============================================================
     * 第二次Receiver重新登录
     * ============================================================
     */
    const int second_fd =
        ConnectToServer(
            host,
            port
        );


    if (second_fd < 0) {
        std::cerr
            << "second receiver connect failed\n";

        return 1;
    }


    if (
        !SendPacket(
            second_fd,
            codec,
            MakeLoginRequest(
                "user10002",
                "123456",
                2
            )
        )
    ) {
        ::close(second_fd);
        return 1;
    }


    std::vector<tinyimx::Packet>
        second_packets;


    if (
        !WaitForPackets(
            second_fd,
            codec,
            &second_input_buffer,
            1,
            &second_packets
        )
    ) {
        ::close(second_fd);
        return 1;
    }


    if (second_packets.empty()) {
        std::cerr
            << "second login response missing\n";

        ::close(second_fd);
        return 1;
    }


    PrintPacket(
        "[second login]",
        second_packets.front()
    );


    std::size_t second_offline_count = 0;


    try {
        const Json second_login_body =
            Json::parse(
                second_packets.front().body
            );


        if (
            !second_login_body.value(
                "success",
                false
            )
        ) {
            std::cerr
                << "second receiver login failed\n";

            ::close(second_fd);
            return 1;
        }


        second_offline_count =
            second_login_body.value(
                "offline_count",
                0ULL
            );

    } catch (
        const std::exception& e
    ) {
        std::cerr
            << "parse second login response failed: "
            << e.what()
            << '\n';

        ::close(second_fd);
        return 1;
    }


    const std::size_t
        expected_second_packet_count =
            1 + second_offline_count;


    if (
        second_packets.size() <
        expected_second_packet_count
    ) {
        if (
            !WaitForPackets(
                second_fd,
                codec,
                &second_input_buffer,
                expected_second_packet_count,
                &second_packets
            )
        ) {
            ::close(second_fd);
            return 1;
        }
    }


    for (
        std::size_t i = 1;
        i < second_packets.size();
        ++i
    ) {
        PrintPacket(
            "[second recovery]",
            second_packets[i]
        );
    }


    const std::size_t
        second_target_count =
            CountTargetOfflineMessages(
                second_packets,
                expected_text,
                expected_server_message_id,
                nullptr
            );


    if (second_target_count != 0) {
        std::cerr
            << "[FAIL] receiver-confirmed offline message "
               "was replayed"
            << ", count="
            << second_target_count
            << '\n';

        ::close(second_fd);
        return 1;
    }


    std::cout
        << "[PASS] receiver-confirmed offline message "
           "not replayed after re-login\n";


    std::cout
        << "verification_client_message_id="
        << client_message_id
        << '\n';

    std::cout
        << "verification_server_message_id="
        << expected_server_message_id
        << '\n';

    std::cout
        << "verification_first_delivery_seq="
        << first_recovery_delivery_seq
        << '\n';


    ::close(second_fd);


    std::cout
        << "persistent offline recovery "
           "validation passed\n"
        << "================================================\n";


    return 0;
}