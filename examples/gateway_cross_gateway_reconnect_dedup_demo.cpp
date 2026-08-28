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
#include <unordered_set>
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
            << ", message_id=" << message_id
            << ", delivery_seq=" << delivery_seq
            << ", error=" << error_message
            << '\n';
        return {};
    }

    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kChatDeliveryAck;
    packet.seq = delivery_seq;
    packet.body = std::move(body);
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


bool ValidateReceiverDelivery(
    const tinyimx::Packet& packet,
    const std::string& expected_text,
    std::uint64_t* actual_message_id,
    std::uint32_t* actual_delivery_seq
) {
    if (
        packet.type !=
        tinyimx::MessageType::
            kChatDelivery
    ) {
        std::cerr
            << "expected chat_delivery"
            << ", actual="
            << tinyimx::MessageTypeToString(
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
            << "deserialize cross gateway "
               "receiver delivery failed"
            << ", error="
            << error_message
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (
        delivery.message_id == 0
    ) {
        std::cerr
            << "receiver message_id=0\n";

        return false;
    }


    if (
        delivery.from_user_id !=
            10001 ||
        delivery.to_user_id !=
            10002
    ) {
        std::cerr
            << "receiver delivery "
               "user identity mismatch"
            << ", from="
            << delivery.from_user_id
            << ", to="
            << delivery.to_user_id
            << '\n';

        return false;
    }


    if (
        delivery.text !=
            expected_text
    ) {
        std::cerr
            << "receiver delivery "
               "text mismatch"
            << ", actual="
            << delivery.text
            << '\n';

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



bool ValidateChatAck(
    const tinyimx::Packet& packet,
    const std::string& expected_client_message_id,
    bool expected_reused,
    bool expected_delivered,
    const std::string& expected_reason,
    std::uint64_t expected_message_id,
    std::uint64_t* actual_message_id
) {
    if (packet.type != tinyimx::MessageType::kChatAck) {
        std::cerr << "expected chat_ack, actual="
                  << tinyimx::MessageTypeToString(packet.type) << '\n';
        return false;
    }

    tinyimx::ClientChatAck ack;
    std::string error_message;

    if (!tinyimx::DeserializeClientChatAck(
            packet.body, &ack, &error_message)) {
        std::cerr << "deserialize chat ack failed"
                  << ", error=" << error_message
                  << ", body=" << packet.body << '\n';
        return false;
    }

    if (!ack.success) {
        std::cerr << "chat ack success=false"
                  << ", reason=" << ack.reason
                  << ", body=" << packet.body << '\n';
        return false;
    }

    if (ack.delivered != expected_delivered) {
        std::cerr << "chat ack delivered mismatch"
                  << ", expected=" << expected_delivered
                  << ", actual=" << ack.delivered
                  << ", reason=" << ack.reason << '\n';
        return false;
    }

    if (!ack.stored_persistent) {
        std::cerr << "cross gateway chat must be persisted"
                  << ", body=" << packet.body << '\n';
        return false;
    }

    if (ack.stored_offline) {
        std::cerr << "online cross gateway delivery must not be stored_offline"
                  << ", body=" << packet.body << '\n';
        return false;
    }

    if (ack.client_message_id != expected_client_message_id) {
        std::cerr << "client_message_id mismatch"
                  << ", expected=" << expected_client_message_id
                  << ", actual=" << ack.client_message_id << '\n';
        return false;
    }

    if (ack.reused != expected_reused) {
        std::cerr << "reused mismatch"
                  << ", expected=" << expected_reused
                  << ", actual=" << ack.reused << '\n';
        return false;
    }

    if (ack.message_id == 0) {
        std::cerr << "chat ack message_id=0\n";
        return false;
    }

    if (expected_message_id != 0 &&
        ack.message_id != expected_message_id) {
        std::cerr << "server message_id mismatch"
                  << ", expected=" << expected_message_id
                  << ", actual=" << ack.message_id << '\n';
        return false;
    }

    if (ack.from_user_id != 10001 ||
        ack.to_user_id != 10002) {
        std::cerr << "chat ack user identity mismatch"
                  << ", from=" << ack.from_user_id
                  << ", to=" << ack.to_user_id << '\n';
        return false;
    }

    if (ack.remote_gateway_id != "gateway-b") {
        std::cerr << "remote gateway mismatch"
                  << ", actual=" << ack.remote_gateway_id << '\n';
        return false;
    }

    if (ack.reason != expected_reason) {
        std::cerr << "unexpected ack reason"
                  << ", expected=" << expected_reason
                  << ", actual=" << ack.reason << '\n';
        return false;
    }

    if (actual_message_id != nullptr) {
        *actual_message_id = ack.message_id;
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

class ReceiverBusinessDeduplicator final {
public:
    bool ApplyOnce(
        std::uint64_t message_id
    ) {
        if (message_id == 0) {
            return false;
        }

        const auto [iterator, inserted] =
            applied_message_ids_.insert(
                message_id
            );

        (void)iterator;

        if (inserted) {
            ++business_apply_count_;
        }

        return inserted;
    }

    std::size_t BusinessApplyCount() const {
        return business_apply_count_;
    }

private:
    std::unordered_set<std::uint64_t>
        applied_message_ids_;

    std::size_t
        business_apply_count_{0};
};


bool WaitForReconnectReplay(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    tinyimx::Buffer* input_buffer,
    std::uint64_t expected_message_id,
    const std::string& expected_text,
    tinyimx::Packet* login_response,
    tinyimx::Packet* delivery_packet,
    std::uint32_t* delivery_seq
) {
    if (
        input_buffer == nullptr ||
        login_response == nullptr ||
        delivery_packet == nullptr ||
        delivery_seq == nullptr ||
        expected_message_id == 0
    ) {
        return false;
    }


    bool login_found = false;
    bool delivery_found = false;


    std::vector<tinyimx::Packet>
        packets;


    std::size_t scanned = 0;


    /*
     * LoginResponse和Offline Replay
     * 可能一次recv同时到达。
     *
     * 所以不能假设：
     *
     * packets[0] = login
     * packets[1] = target delivery
     *
     * 必须按MessageType + message_id寻找。
     */
    for (
        std::size_t round = 0;
        round < 32 &&
        (!login_found || !delivery_found);
        ++round
    ) {
        const std::size_t
            required_count =
                packets.size() + 1;


        if (
            !WaitForPackets(
                fd,
                codec,
                input_buffer,
                required_count,
                &packets
            )
        ) {
            return false;
        }


        while (
            scanned <
            packets.size()
        ) {
            const tinyimx::Packet&
                packet =
                    packets[scanned++];


            PrintPacket(
                "[user_b reconnect@gateway-b]",
                packet
            );


            if (
                packet.type ==
                    tinyimx::MessageType::
                        kLoginResponse
            ) {
                *login_response =
                    packet;

                login_found = true;

                continue;
            }


            if (
                packet.type !=
                    tinyimx::MessageType::
                        kChatDelivery
            ) {
                continue;
            }


            std::uint64_t
                actual_message_id = 0;

            std::uint32_t
                actual_delivery_seq = 0;


            if (
                !ValidateReceiverDelivery(
                    packet,
                    expected_text,
                    &actual_message_id,
                    &actual_delivery_seq
                )
            ) {
                continue;
            }


            /*
             * 登录时可能还有历史Pending消息。
             *
             * 我们只寻找本次测试的稳定M。
             */
            if (
                actual_message_id !=
                    expected_message_id
            ) {
                std::cout
                    << "[INFO] ignored unrelated "
                       "pending replay"
                    << ", actual_message_id="
                    << actual_message_id
                    << ", expected_message_id="
                    << expected_message_id
                    << '\n';

                continue;
            }


            *delivery_packet =
                packet;

            *delivery_seq =
                actual_delivery_seq;

            delivery_found = true;
        }
    }


    return
        login_found &&
        delivery_found;
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
        << "========== TinyIMX F6 "
            "Cross Gateway Reconnect/Dedup "
            "Demo ==========\n";


    tinyimx::ProtocolCodec codec;

    /*
    * TCP是字节流。
    *
    * 每个连接必须保存自己尚未完成解码的数据，
    * 不能每次WaitForPackets重新创建Buffer。
    */
    tinyimx::Buffer
        user_a_input_buffer;

    tinyimx::Buffer
        user_b_input_buffer;
    /*
     * user10002 → Gateway B
     */
    int user_b_fd =
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
                4
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
            &user_b_input_buffer,
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
    * 同一次recv()中可能已经同时解出了：
    *
    * LoginResponse
    * +
    * Offline Messages
    *
    * LoginResponse之后的包都属于本轮测试前历史数据。
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
            << "failed to drain receiver "
            "login backlog\n";

        return 1;
    }


    std::cout
        << "[PASS] receiver login backlog drained\n";

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
            &user_a_input_buffer,
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
            &user_b_input_buffer,
            1,
            &user_b_chat_packets
        )
    ) {
        return 1;
    }

    bool ok = true;


    ReceiverBusinessDeduplicator
        receiver_business_dedup;


    PrintPacket(
        "[user_b@gateway-b]",
        user_b_chat_packets.front()
    );


    std::uint64_t
        receiver_server_message_id = 0;

    std::uint32_t
        receiver_delivery_seq = 0;


    if (
        !ValidateReceiverDelivery(
            user_b_chat_packets.front(),
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
     * user10001收到Gateway A最终ACK。
     */


    if (
        receiver_business_dedup.
            ApplyOnce(
                receiver_server_message_id
            )
    ) {
        std::cout
            << "[APPLY] receiver business effect"
            << ", message_id="
            << receiver_server_message_id
            << ", apply_count="
            << receiver_business_dedup.
                BusinessApplyCount()
            << '\n';
    } else {
        std::cerr
            << "[FAIL] first receiver delivery "
            "was unexpectedly duplicate\n";

        ok = false;
    }

    std::vector<tinyimx::Packet>
        user_a_ack_packets;

    if (
        !WaitForPackets(
            user_a_fd,
            codec,
            &user_a_input_buffer,
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


    std::uint64_t
        server_message_id = 0;


    if (
        !ValidateChatAck(
            user_a_ack_packets.front(),
            client_message_id,
            false,
            false,
            "remote_delivery_awaiting_receiver_ack",
            0,
            &server_message_id
        )
    ) {
        ok = false;
    }

    if (
        receiver_server_message_id != 0 &&
        server_message_id != 0 &&
        receiver_server_message_id ==
            server_message_id
    ) {
        std::cout
            << "[PASS] cross gateway "
            "receiver delivery and "
            "sender ack share stable "
            "message_id"
            << ", message_id="
            << server_message_id
            << ", delivery_seq="
            << receiver_delivery_seq
            << '\n';
    } else {
        std::cerr
            << "[FAIL] cross gateway "
            "receiver message_id mismatch"
            << ", receiver_message_id="
            << receiver_server_message_id
            << ", sender_ack_message_id="
            << server_message_id
            << '\n';

        ok = false;
    }


    /*
     * 4. Receiver -> Gateway B application ACK.
     *
     * Before this ACK, shared MySQL must remain Pending.
     * Only this ACK may advance:
     *
     * Pending -> ReceiverConfirmed
     */
    std::cout
        << "[SIMULATED DISCONNECT BEFORE ACK]"
        << ", message_id="
        << receiver_server_message_id
        << ", delivery_seq="
        << receiver_delivery_seq
        << '\n';


    ::shutdown(
        user_b_fd,
        SHUT_RDWR
    );


    ::close(
        user_b_fd
    );


    user_b_fd = -1;


    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            1900
        )
    );

    /*
    * ============================================================
    * 5. Receiver重新连接Gateway B
    * ============================================================
    */
    user_b_fd =
        ConnectToServer(
            host,
            gateway_b_port
        );


    if (user_b_fd < 0) {
        std::cerr
            << "reconnect user B to "
            "gateway B failed\n";

        ::close(user_a_fd);

        return 1;
    }


    /*
    * 新TCP连接必须使用新的Input Buffer。
    *
    * 旧Connection的Buffer不能跨TCP连接复用。
    */
    tinyimx::Buffer user_b_reconnect_input_buffer;

    if (
        !SendPacket(
            user_b_fd,
            codec,
            MakeLoginRequest(
                "user10002",
                "123456",
                4
            )
        )
    ) {
        std::cerr
            << "send reconnect login failed\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }

    tinyimx::Packet
        reconnect_login_response;


    tinyimx::Packet
        reconnect_delivery_packet;


    std::uint32_t
        reconnect_delivery_seq = 0;


    if (
        !WaitForReconnectReplay(
            user_b_fd,
            codec,
            &user_b_reconnect_input_buffer,
            receiver_server_message_id,
            message_text,
            &reconnect_login_response,
            &reconnect_delivery_packet,
            &reconnect_delivery_seq
        )
    ) {
        std::cerr
            << "[FAIL] cross gateway reconnect "
            "did not replay pending message"
            << ", message_id="
            << receiver_server_message_id
            << '\n';

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    std::uint64_t
        reconnect_message_id = 0;


    std::uint32_t
        validated_reconnect_delivery_seq = 0;


    if (
        !ValidateReceiverDelivery(
            reconnect_delivery_packet,
            message_text,
            &reconnect_message_id,
            &validated_reconnect_delivery_seq
        )
    ) {
        std::cerr
            << "[FAIL] reconnect receiver "
            "delivery invalid\n";

        ok = false;
    }


    if (
        reconnect_message_id ==
            receiver_server_message_id &&
        reconnect_delivery_seq ==
            validated_reconnect_delivery_seq &&
        reconnect_delivery_seq != 0 &&
        reconnect_delivery_seq !=
            receiver_delivery_seq
    ) {
        std::cout
            << "[PASS] cross gateway reconnect "
            "replay kept stable M and "
            "allocated fresh D"
            << ", message_id="
            << reconnect_message_id
            << ", first_delivery_seq="
            << receiver_delivery_seq
            << ", reconnect_delivery_seq="
            << reconnect_delivery_seq
            << '\n';
    } else {
        std::cerr
            << "[FAIL] reconnect delivery "
            "identity mismatch"
            << ", first_message_id="
            << receiver_server_message_id
            << ", reconnect_message_id="
            << reconnect_message_id
            << ", first_delivery_seq="
            << receiver_delivery_seq
            << ", reconnect_delivery_seq="
            << reconnect_delivery_seq
            << '\n';

        ok = false;
    }



    /*
    * ============================================================
    * 6. Receiver按稳定message_id幂等消费
    * ============================================================
    *
    * M已经在D1执行过业务。
    *
    * D2只是同一个M的重放。
    */
    if (
        receiver_business_dedup.
            ApplyOnce(
                reconnect_message_id
            )
    ) {
        std::cerr
            << "[FAIL] duplicate message "
            "reapplied receiver business effect"
            << ", message_id="
            << reconnect_message_id
            << '\n';

        ok = false;
    } else {
        std::cout
            << "[PASS] receiver dedup suppressed "
            "duplicate business effect"
            << ", message_id="
            << reconnect_message_id
            << ", apply_count="
            << receiver_business_dedup.
                BusinessApplyCount()
            << '\n';
    }


    /*
    * Duplicate M不能重新产生业务副作用，
    * 但当前Delivery Attempt D2必须ACK。
    */
    if (
        !SendPacket(
            user_b_fd,
            codec,
            MakeReceiverChatDeliveryAck(
                reconnect_message_id,
                reconnect_delivery_seq
            )
        )
    ) {
        std::cerr
            << "send reconnect receiver "
            "delivery ack failed\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    std::cout
        << "[SENT] reconnect receiver ACK"
        << ", message_id="
        << reconnect_message_id
        << ", delivery_seq="
        << reconnect_delivery_seq
        << '\n';


    /*
    * 给Gateway B完成：
    *
    * Pending -> ReceiverConfirmed
    *
    * 一个很短的处理窗口。
    */
    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            300
        )
    );
        /*
    * =====================================================
    * 7. Sender Client Retry
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
                5,
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
            &user_a_input_buffer,
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
            true,
            true,
            "remote_receiver_confirmed",
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
        receiver_business_dedup.
            BusinessApplyCount() == 1
    ) {
        std::cout
            << "[PASS] receiver business effect "
            "executed once across reconnect"
            << ", apply_count=1\n";
    } else {
        std::cerr
            << "[FAIL] receiver business effect "
            "count mismatch"
            << ", expected=1"
            << ", actual="
            << receiver_business_dedup.
                BusinessApplyCount()
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

    std::cout
        << "verification_first_delivery_seq="
        << receiver_delivery_seq
        << '\n';


    std::cout
        << "verification_reconnect_delivery_seq="
        << reconnect_delivery_seq
        << '\n';


    std::cout
        << "verification_receiver_apply_count="
        << receiver_business_dedup.
            BusinessApplyCount()
        << '\n';


    ::close(user_a_fd);
    ::close(user_b_fd);


    if (!ok) {
        std::cerr
            << "F6 cross gateway reconnect/dedup "
            "validation failed\n";

        return 1;
    }


    std::cout
        << "F6 cross gateway reconnect/dedup "
        "validation passed\n"
        << "================================"
           "================\n";

    return 0;
}