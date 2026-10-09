#include "common/net/Buffer.h"
#include "common/protocol/ClientChatProtocol.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

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
        "m12-reconnect-dedup-" +
        std::to_string(nanoseconds);
}

bool SetSocketTimeout(
    int fd,
    int seconds
) {
    timeval timeout {};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    ::setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        static_cast<socklen_t>(
            sizeof(timeout)
        )
    );

    ::setsockopt(
        fd,
        SOL_SOCKET,
        SO_SNDTIMEO,
        &timeout,
        static_cast<socklen_t>(
            sizeof(timeout)
        )
    );

    return true;
}

bool SetTcpNoDelay(int fd) {
    int value = 1;

    return
        ::setsockopt(
            fd,
            IPPROTO_TCP,
            TCP_NODELAY,
            &value,
            static_cast<socklen_t>(
                sizeof(value)
            )
        ) == 0;
}

int ConnectToServer(
    const std::string& host,
    std::uint16_t port
) {
    const int fd =
        ::socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (fd < 0) {
        return -1;
    }

    SetSocketTimeout(fd, 5);
    SetTcpNoDelay(fd);

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (
        ::inet_pton(
            AF_INET,
            host.c_str(),
            &address.sin_addr
        ) != 1
    ) {
        ::close(fd);
        return -1;
    }

    if (
        ::connect(
            fd,
            reinterpret_cast<
                sockaddr*
            >(&address),
            static_cast<socklen_t>(
                sizeof(address)
            )
        ) != 0
    ) {
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

    while (
        sent_total <
        data.size()
    ) {
        const ssize_t n =
            ::send(
                fd,
                data.data() +
                    sent_total,
                data.size() -
                    sent_total,
                MSG_NOSIGNAL
            );

        if (n > 0) {
            sent_total +=
                static_cast<std::size_t>(
                    n
                );

            continue;
        }

        if (
            n < 0 &&
            errno == EINTR
        ) {
            continue;
        }

        return false;
    }

    return true;
}

tinyimx::Packet MakeLoginRequest(
    const std::string& username,
    const std::string& password,
    std::uint32_t seq
) {
    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::
            kLoginRequest;

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
    const std::string&
        client_message_id
) {
    tinyimx::ClientChatRequest request;

    request.client_message_id =
        client_message_id;

    request.to_user_id = to;
    request.text = text;

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
            << "serialize chat request failed"
            << ", error="
            << error_message
            << '\n';

        return {};
    }

    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::
            kChatMessage;

    packet.seq = seq;
    packet.body = std::move(body);

    return packet;
}

tinyimx::Packet
MakeReceiverChatDeliveryAck(
    std::uint64_t message_id,
    std::uint32_t delivery_seq
) {
    tinyimx::ReceiverChatDeliveryAck ack;
    ack.message_id = message_id;

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
            << "serialize receiver ACK failed"
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

    packet.seq = delivery_seq;
    packet.body = std::move(body);

    return packet;
}

bool SendPacket(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    const tinyimx::Packet& packet
) {
    tinyimx::Buffer output;
    std::string error_message;

    if (
        !codec.Encode(
            packet,
            &output,
            &error_message
        )
    ) {
        std::cerr
            << "encode packet failed"
            << ", error="
            << error_message
            << '\n';

        return false;
    }

    return
        SendAll(
            fd,
            output.
                RetrieveAllAsString()
        );
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

    while (
        packets->size() <
        expected_count
    ) {
        const tinyimx::DecodeResult
            buffered_result =
                codec.Decode(
                    input_buffer
                );

        if (
            buffered_result.status ==
            tinyimx::DecodeStatus::kOk
        ) {
            for (
                const auto& packet :
                    buffered_result.packets
            ) {
                packets->push_back(
                    packet
                );
            }

            if (
                packets->size() >=
                expected_count
            ) {
                return true;
            }
        } else if (
            buffered_result.status !=
            tinyimx::DecodeStatus::
                kNeedMoreData
        ) {
            std::cerr
                << "decode buffered data failed"
                << ", status="
                << tinyimx::
                    DecodeStatusToString(
                        buffered_result.status
                    )
                << ", error="
                << buffered_result.
                    error_message
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
                static_cast<std::size_t>(
                    n
                )
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

void PrintPacket(
    const std::string& tag,
    const tinyimx::Packet& packet
) {
    std::cout
        << tag
        << " packet:"
        << " type="
        << tinyimx::
            MessageTypeToString(
                packet.type
            )
        << " seq="
        << packet.seq
        << " body="
        << packet.body
        << '\n';
}

bool ValidateReceiverDelivery(
    const tinyimx::Packet& packet,
    const std::string& expected_text,
    std::uint64_t expected_message_id,
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
            << tinyimx::
                MessageTypeToString(
                    packet.type
                )
            << '\n';

        return false;
    }

    if (packet.seq == 0) {
        std::cerr
            << "delivery seq must not be zero\n";

        return false;
    }

    tinyimx::ServerChatDelivery delivery;
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
            << "deserialize delivery failed"
            << ", error="
            << error_message
            << '\n';

        return false;
    }

    if (
        delivery.message_id == 0 ||
        delivery.from_user_id != 10001 ||
        delivery.to_user_id != 10002 ||
        delivery.text !=
            expected_text
    ) {
        std::cerr
            << "delivery business fields mismatch"
            << ", body="
            << packet.body
            << '\n';

        return false;
    }

    if (
        expected_message_id != 0 &&
        delivery.message_id !=
            expected_message_id
    ) {
        std::cerr
            << "stable message_id mismatch"
            << ", expected="
            << expected_message_id
            << ", actual="
            << delivery.message_id
            << '\n';

        return false;
    }

    if (
        actual_message_id !=
        nullptr
    ) {
        *actual_message_id =
            delivery.message_id;
    }

    if (
        actual_delivery_seq !=
        nullptr
    ) {
        *actual_delivery_seq =
            packet.seq;
    }

    return true;
}

bool ValidateSenderAck(
    const tinyimx::Packet& packet,
    const std::string&
        expected_client_message_id,
    bool expected_reused,
    bool expected_delivered,
    const std::string& expected_reason,
    std::uint64_t expected_message_id,
    std::uint64_t* actual_message_id
) {
    if (
        packet.type !=
        tinyimx::MessageType::
            kChatAck
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
            << "deserialize sender ACK failed"
            << ", error="
            << error_message
            << '\n';

        return false;
    }

    if (
        !ack.success ||
        !ack.stored_persistent ||
        ack.stored_offline ||
        ack.client_message_id !=
            expected_client_message_id ||
        ack.message_id == 0 ||
        ack.from_user_id != 10001 ||
        ack.to_user_id != 10002 ||
        ack.reused !=
            expected_reused ||
        ack.delivered !=
            expected_delivered ||
        ack.reason !=
            expected_reason
    ) {
        std::cerr
            << "sender ACK semantic mismatch"
            << ", expected_reused="
            << expected_reused
            << ", expected_delivered="
            << expected_delivered
            << ", expected_reason="
            << expected_reason
            << ", body="
            << packet.body
            << '\n';

        return false;
    }

    if (
        expected_message_id != 0 &&
        ack.message_id !=
            expected_message_id
    ) {
        std::cerr
            << "sender ACK message_id mismatch"
            << ", expected="
            << expected_message_id
            << ", actual="
            << ack.message_id
            << '\n';

        return false;
    }

    if (
        actual_message_id !=
        nullptr
    ) {
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
    descriptor.fd = fd;
    descriptor.events = POLLIN;

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
        return false;
    }

    if (
        descriptor.revents &
        POLLIN
    ) {
        std::cerr
            << "receiver unexpectedly got "
               "another delivery\n";

        return false;
    }

    return true;
}

/*
 * Demo级Receiver业务幂等状态。
 *
 * 真实生产客户端需要把message_id与业务落地状态
 * 做持久化，再发送ACK，才能跨客户端进程崩溃恢复。
 *
 * 本测试只证明：
 * TCP断线/重连时，同一客户端进程可以按稳定message_id
 * 抑制重复业务副作用，同时仍然ACK新的网络Attempt。
 */
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

/*
 * Reconnect登录时：
 *
 * LoginResponse和Pending Replay可能在同一次recv内到达。
 * 按目标server message_id寻找本次重放。
 */
bool WaitForReconnectReplay(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    tinyimx::Buffer* input_buffer,
    std::uint64_t expected_message_id,
    const std::string& expected_text,
    tinyimx::Packet* delivery_packet,
    std::uint32_t* delivery_seq
) {
    if (
        input_buffer == nullptr ||
        delivery_packet == nullptr ||
        delivery_seq == nullptr ||
        expected_message_id == 0
    ) {
        return false;
    }

    bool login_seen = false;
    bool target_seen = false;

    std::vector<tinyimx::Packet>
        packets;

    std::size_t scanned = 0;

    for (
        std::size_t round = 0;
        round < 32 &&
        (!login_seen || !target_seen);
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
            const auto& packet =
                packets[scanned++];

            PrintPacket(
                "[receiver reconnect]",
                packet
            );

            if (
                packet.type ==
                tinyimx::MessageType::
                    kLoginResponse
            ) {
                login_seen = true;
                continue;
            }

            if (
                packet.type !=
                tinyimx::MessageType::
                    kChatDelivery
            ) {
                continue;
            }

            std::uint64_t message_id = 0;
            std::uint32_t seq = 0;

            if (
                !ValidateReceiverDelivery(
                    packet,
                    expected_text,
                    0,
                    &message_id,
                    &seq
                )
            ) {
                continue;
            }

            if (
                message_id !=
                expected_message_id
            ) {
                std::cout
                    << "[INFO] ignored unrelated "
                       "pending replay"
                    << ", message_id="
                    << message_id
                    << ", expected="
                    << expected_message_id
                    << '\n';

                continue;
            }

            *delivery_packet = packet;
            *delivery_seq = seq;
            target_seen = true;
        }
    }

    return
        login_seen &&
        target_seen;
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
        host = argv[1];
    }

    if (argc >= 3) {
        const int parsed_port =
            std::stoi(
                argv[2]
            );

        if (
            parsed_port <= 0 ||
            parsed_port > 65535
        ) {
            std::cerr
                << "invalid port\n";

            return 1;
        }

        port =
            static_cast<
                std::uint16_t
            >(parsed_port);
    }

    std::cout
        << "========== TinyIMX F5 "
           "Receiver Reconnect/Dedup Demo ==========\n";

    tinyimx::ProtocolCodec codec;

    tinyimx::Buffer
        sender_input_buffer;

    tinyimx::Buffer
        receiver_first_input_buffer;

    ReceiverBusinessDeduplicator
        receiver_business_dedup;

    bool ok = true;

    /*
     * ============================================================
     * 1. Receiver与Sender都登录Gateway A。
     * ============================================================
     */
    int receiver_fd =
        ConnectToServer(
            host,
            port
        );

    if (receiver_fd < 0) {
        std::cerr
            << "connect receiver failed\n";

        return 1;
    }

    if (
        !SendPacket(
            receiver_fd,
            codec,
            MakeLoginRequest(
                "user10002",
                "123456",
                1
            )
        )
    ) {
        ::close(receiver_fd);
        return 1;
    }

    std::vector<tinyimx::Packet>
        receiver_login_packets;

    if (
        !WaitForPackets(
            receiver_fd,
            codec,
            &receiver_first_input_buffer,
            1,
            &receiver_login_packets
        )
    ) {
        ::close(receiver_fd);
        return 1;
    }

    PrintPacket(
        "[receiver first login]",
        receiver_login_packets.front()
    );

    const int sender_fd =
        ConnectToServer(
            host,
            port
        );

    if (sender_fd < 0) {
        ::close(receiver_fd);
        return 1;
    }

    if (
        !SendPacket(
            sender_fd,
            codec,
            MakeLoginRequest(
                "user10001",
                "123456",
                2
            )
        )
    ) {
        ::close(sender_fd);
        ::close(receiver_fd);
        return 1;
    }

    std::vector<tinyimx::Packet>
        sender_login_packets;

    if (
        !WaitForPackets(
            sender_fd,
            codec,
            &sender_input_buffer,
            1,
            &sender_login_packets
        )
    ) {
        ::close(sender_fd);
        ::close(receiver_fd);
        return 1;
    }

    PrintPacket(
        "[sender login]",
        sender_login_packets.front()
    );

    /*
     * ============================================================
     * 2. Sender创建一次Logical Send。
     * ============================================================
     */
    const std::string
        client_message_id =
            MakeClientMessageId();

    const std::string
        message_text =
            "receiver reconnect dedup validation";

    std::cout
        << "client_message_id="
        << client_message_id
        << '\n';

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
        ::close(receiver_fd);
        return 1;
    }

    /*
     * ============================================================
     * 3. Receiver得到M/D1并执行一次业务副作用。
     *
     * 故意不发送ACK。
     * ============================================================
     */
    std::vector<tinyimx::Packet>
        first_delivery_packets;

    if (
        !WaitForPackets(
            receiver_fd,
            codec,
            &receiver_first_input_buffer,
            1,
            &first_delivery_packets
        )
    ) {
        ::close(sender_fd);
        ::close(receiver_fd);
        return 1;
    }

    PrintPacket(
        "[receiver D1]",
        first_delivery_packets.front()
    );

    std::uint64_t
        server_message_id = 0;

    std::uint32_t
        first_delivery_seq = 0;

    if (
        !ValidateReceiverDelivery(
            first_delivery_packets.front(),
            message_text,
            0,
            &server_message_id,
            &first_delivery_seq
        )
    ) {
        ok = false;
    }

    if (
        receiver_business_dedup.
            ApplyOnce(
                server_message_id
            )
    ) {
        std::cout
            << "[APPLY] receiver business effect"
            << ", message_id="
            << server_message_id
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

    /*
     * Sender第一次ACK仍然只表示Server承担责任。
     */
    std::vector<tinyimx::Packet>
        first_sender_ack_packets;

    if (
        !WaitForPackets(
            sender_fd,
            codec,
            &sender_input_buffer,
            1,
            &first_sender_ack_packets
        )
    ) {
        ::close(sender_fd);
        ::close(receiver_fd);
        return 1;
    }

    PrintPacket(
        "[sender first ACK]",
        first_sender_ack_packets.front()
    );

    std::uint64_t
        ack_message_id = 0;

    if (
        !ValidateSenderAck(
            first_sender_ack_packets.front(),
            client_message_id,
            false,
            false,
            "local_delivery_awaiting_receiver_ack",
            server_message_id,
            &ack_message_id
        )
    ) {
        ok = false;
    }

    /*
     * ============================================================
     * 4. 模拟最关键故障窗口：
     *
     * Receiver已经应用M一次，
     * 但ACK尚未发出就断线。
     *
     * Gateway绝不能因此确认M。
     * ============================================================
     */
    std::cout
        << "[SIMULATED DISCONNECT BEFORE ACK]"
        << " message_id="
        << server_message_id
        << ", delivery_seq="
        << first_delivery_seq
        << '\n';

    ::shutdown(
        receiver_fd,
        SHUT_RDWR
    );

    ::close(
        receiver_fd
    );

    receiver_fd = -1;

    /*
     * 等待超过1500ms ACK timeout。
     *
     * 预期Gateway Timer触发时找不到当前Receiver Session，
     * 因此只暂停Runtime Retry，绝不能伪造确认。
     */
    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            1900
        )
    );

    /*
     * ============================================================
     * 5. Receiver重新连接。
     *
     * DB仍然Pending，所以登录必须重放同一个M。
     * 新的网络Attempt必须使用fresh D2。
     * ============================================================
     */
    receiver_fd =
        ConnectToServer(
            host,
            port
        );

    if (receiver_fd < 0) {
        ::close(sender_fd);
        return 1;
    }

    tinyimx::Buffer
        receiver_reconnect_input_buffer;

    if (
        !SendPacket(
            receiver_fd,
            codec,
            MakeLoginRequest(
                "user10002",
                "123456",
                4
            )
        )
    ) {
        ::close(sender_fd);
        ::close(receiver_fd);
        return 1;
    }

    tinyimx::Packet
        reconnect_delivery_packet;

    std::uint32_t
        reconnect_delivery_seq = 0;

    if (
        !WaitForReconnectReplay(
            receiver_fd,
            codec,
            &receiver_reconnect_input_buffer,
            server_message_id,
            message_text,
            &reconnect_delivery_packet,
            &reconnect_delivery_seq
        )
    ) {
        std::cerr
            << "[FAIL] reconnect did not replay Pending message"
            << ", message_id="
            << server_message_id
            << '\n';

        ::close(sender_fd);
        ::close(receiver_fd);
        return 1;
    }

    std::uint64_t
        reconnect_message_id = 0;

    std::uint32_t
        validated_reconnect_seq = 0;

    if (
        !ValidateReceiverDelivery(
            reconnect_delivery_packet,
            message_text,
            server_message_id,
            &reconnect_message_id,
            &validated_reconnect_seq
        )
    ) {
        ok = false;
    }

    if (
        reconnect_message_id ==
            server_message_id &&
        reconnect_delivery_seq ==
            validated_reconnect_seq &&
        reconnect_delivery_seq != 0 &&
        reconnect_delivery_seq !=
            first_delivery_seq
    ) {
        std::cout
            << "[PASS] reconnect replay kept stable M "
               "and allocated fresh D"
            << ", message_id="
            << reconnect_message_id
            << ", first_delivery_seq="
            << first_delivery_seq
            << ", reconnect_delivery_seq="
            << reconnect_delivery_seq
            << '\n';
    } else {
        std::cerr
            << "[FAIL] reconnect delivery identity invalid"
            << ", M1="
            << server_message_id
            << ", M2="
            << reconnect_message_id
            << ", D1="
            << first_delivery_seq
            << ", D2="
            << reconnect_delivery_seq
            << '\n';

        ok = false;
    }

    /*
     * ============================================================
     * 6. Receiver业务幂等：
     *
     * M已经在D1时执行过业务副作用。
     * D2只是同一个M的重放。
     *
     * 因此：
     *
     * 不重复Apply；
     * 但必须ACK D2。
     * ============================================================
     */
    if (
        receiver_business_dedup.
            ApplyOnce(
                reconnect_message_id
            )
    ) {
        std::cerr
            << "[FAIL] duplicate M reapplied "
               "receiver business effect\n";

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

    if (
        !SendPacket(
            receiver_fd,
            codec,
            MakeReceiverChatDeliveryAck(
                reconnect_message_id,
                reconnect_delivery_seq
            )
        )
    ) {
        std::cerr
            << "send reconnect delivery ACK failed\n";

        ::close(sender_fd);
        ::close(receiver_fd);
        return 1;
    }

    std::cout
        << "[SENT] ACK duplicate replay attempt"
        << ", message_id="
        << reconnect_message_id
        << ", delivery_seq="
        << reconnect_delivery_seq
        << '\n';

    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            300
        )
    );

    /*
     * ============================================================
     * 7. Sender相同Logical Send重试。
     *
     * Receiver已经确认：
     *
     * reused=true
     * delivered=true
     * same M
     * ============================================================
     */
    if (
        !SendPacket(
            sender_fd,
            codec,
            MakeChatMessage(
                10002,
                5,
                message_text,
                client_message_id
            )
        )
    ) {
        ::close(sender_fd);
        ::close(receiver_fd);
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
        ::close(receiver_fd);
        return 1;
    }

    PrintPacket(
        "[sender retry ACK]",
        retry_ack_packets.front()
    );

    std::uint64_t
        retry_message_id = 0;

    if (
        !ValidateSenderAck(
            retry_ack_packets.front(),
            client_message_id,
            true,
            true,
            "local_already_receiver_confirmed",
            server_message_id,
            &retry_message_id
        )
    ) {
        ok = false;
    }

    if (
        retry_message_id ==
            server_message_id
    ) {
        std::cout
            << "[PASS] sender retry reused stable message_id"
            << ", message_id="
            << retry_message_id
            << '\n';
    } else {
        ok = false;
    }

    if (
        receiver_business_dedup.
            BusinessApplyCount() != 1
    ) {
        std::cerr
            << "[FAIL] receiver business effect count"
            << ", expected=1"
            << ", actual="
            << receiver_business_dedup.
                BusinessApplyCount()
            << '\n';

        ok = false;
    } else {
        std::cout
            << "[PASS] receiver business effect "
               "business apply count remained one in demo"
            << ", apply_count=1\n";
    }

    if (
        ExpectNoPacketWithin(
            receiver_fd,
            1800
        )
    ) {
        std::cout
            << "[PASS] no extra receiver delivery "
               "after confirmation\n";
    } else {
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
        << first_delivery_seq
        << '\n';

    std::cout
        << "verification_reconnect_delivery_seq="
        << reconnect_delivery_seq
        << '\n';

    ::close(sender_fd);
    ::close(receiver_fd);

    if (!ok) {
        std::cerr
            << "F5 reconnect/dedup validation failed\n";

        return 1;
    }

    std::cout
        << "F5 reconnect/dedup validation passed\n"
        << "=================================================\n";

    return 0;
}
