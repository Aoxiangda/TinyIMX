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
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

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

bool SetTcpNoDelay(
    int fd
) {
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
            << "serialize client chat request failed"
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
            << "encode failed: "
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

/*
 * TCP是byte stream。
 *
 * 每个Connection必须持久保存自己的Input Buffer；
 * 不能像旧Demo那样每次Wait都创建临时Buffer，
 * 否则函数返回时可能丢失半包。
 */
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
        /*
         * 先尝试消费Buffer里之前留下的数据。
         */
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
    std::uint32_t* delivery_seq
) {
    if (
        packet.type !=
        tinyimx::MessageType::
            kChatDelivery
    ) {
        return false;
    }

    if (packet.seq == 0) {
        std::cerr
            << "receiver delivery seq must not be zero\n";

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
            << "deserialize receiver delivery failed"
            << ", error="
            << error_message
            << '\n';

        return false;
    }

    if (
        delivery.from_user_id != 10001 ||
        delivery.to_user_id != 10002 ||
        delivery.text !=
            expected_text ||
        delivery.message_id == 0
    ) {
        std::cerr
            << "receiver delivery business fields mismatch"
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
            << "offline replay message_id mismatch"
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
        delivery_seq !=
        nullptr
    ) {
        *delivery_seq =
            packet.seq;
    }

    return true;
}

bool ValidateSenderChatAck(
    const tinyimx::Packet& packet,
    const std::string&
        expected_client_message_id,
    bool expected_reused,
    bool expected_delivered,
    bool expected_stored_offline,
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
            << "deserialize chat ack failed"
            << ", error="
            << error_message
            << '\n';

        return false;
    }

    if (!ack.success) {
        std::cerr
            << "chat ack success=false"
            << ", reason="
            << ack.reason
            << '\n';

        return false;
    }

    if (
        ack.client_message_id !=
            expected_client_message_id ||
        ack.message_id == 0 ||
        ack.from_user_id != 10001 ||
        ack.to_user_id != 10002 ||
        ack.reused != expected_reused ||
        ack.delivered !=
            expected_delivered ||
        ack.stored_offline !=
            expected_stored_offline ||
        !ack.stored_persistent ||
        ack.reason !=
            expected_reason
    ) {
        std::cerr
            << "chat ack semantic mismatch"
            << ", expected_reused="
            << expected_reused
            << ", expected_delivered="
            << expected_delivered
            << ", expected_stored_offline="
            << expected_stored_offline
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
            << "sender ack message_id mismatch"
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
               "another packet after sender retry\n";

        return false;
    }

    return true;
}

/*
 * Receiver登录时可能存在历史Pending消息。
 *
 * 因此不能假设：
 *
 *   user_b_packets[1]
 *
 * 一定就是本次新消息。
 *
 * 这里按稳定server message_id找到目标Replay。
 */
bool WaitForTargetOfflineReplay(
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
        received_packets;

    std::size_t scanned = 0;

    for (
        std::size_t round = 0;
        round < 32 &&
        (!login_found || !delivery_found);
        ++round
    ) {
        const std::size_t
            required_count =
                received_packets.size() + 1;

        if (
            !WaitForPackets(
                fd,
                codec,
                input_buffer,
                required_count,
                &received_packets
            )
        ) {
            return false;
        }

        while (
            scanned <
            received_packets.size()
        ) {
            const tinyimx::Packet&
                packet =
                    received_packets[
                        scanned++
                    ];

            PrintPacket(
                "[user_b]",
                packet
            );

            if (
                !login_found &&
                packet.type ==
                    tinyimx::
                        MessageType::
                            kLoginResponse
            ) {
                *login_response =
                    packet;

                login_found = true;

                continue;
            }

            if (
                packet.type !=
                    tinyimx::
                        MessageType::
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
                    0,
                    &actual_message_id,
                    &actual_delivery_seq
                )
            ) {
                /*
                 * 可能是历史Pending消息，
                 * 不把它当成当前目标失败。
                 */
                continue;
            }

            if (
                actual_message_id !=
                    expected_message_id
            ) {
                std::cout
                    << "[INFO] ignored historical "
                       "offline replay"
                    << ", message_id="
                    << actual_message_id
                    << ", expected="
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
    std::string host =
        "127.0.0.1";

    std::uint16_t port =
        9001;

    if (argc >= 2) {
        host = argv[1];
    }

    if (argc >= 3) {
        const int parsed_port =
            std::stoi(argv[2]);

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
        << "========== TinyIMX F4-b2 "
           "Offline Replay Receiver ACK Demo ==========\n";

    tinyimx::ProtocolCodec codec;

    tinyimx::Buffer
        user_a_input_buffer;

    tinyimx::Buffer
        user_b_input_buffer;

    bool ok = true;

    /*
     * ============================================================
     * 1. Sender 10001登录。
     *
     * Receiver 10002此时故意保持离线。
     * ============================================================
     */
    const int user_a_fd =
        ConnectToServer(
            host,
            port
        );

    if (user_a_fd < 0) {
        std::cerr
            << "connect user A failed\n";

        return 1;
    }

    if (
        !SendPacket(
            user_a_fd,
            codec,
            MakeLoginRequest(
                "user10001",
                "123456",
                1
            )
        )
    ) {
        ::close(user_a_fd);
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
        return 1;
    }

    PrintPacket(
        "[user_a]",
        user_a_login_packets.front()
    );

    if (
        user_a_login_packets.front().
            type !=
        tinyimx::MessageType::
            kLoginResponse
    ) {
        std::cerr
            << "user A login response invalid\n";

        ::close(user_a_fd);
        return 1;
    }

    const std::string
        client_message_id =
            MakeClientMessageId(
                "m12-offline-f4b2-"
            );

    const std::string
        message_text =
            "offline replay receiver ack validation";

    std::cout
        << "client_message_id="
        << client_message_id
        << '\n';

    /*
     * ============================================================
     * 2. Receiver离线时发送。
     *
     * 预期：
     *
     * success=true
     * delivered=false
     * stored_offline=true
     * DB=Pending
     * ============================================================
     */
    if (
        !SendPacket(
            user_a_fd,
            codec,
            MakeChatMessage(
                10002,
                2,
                message_text,
                client_message_id
            )
        )
    ) {
        ::close(user_a_fd);
        return 1;
    }

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
        return 1;
    }

    PrintPacket(
        "[user_a offline send]",
        first_ack_packets.front()
    );

    std::uint64_t
        server_message_id = 0;

    if (
        !ValidateSenderChatAck(
            first_ack_packets.front(),
            client_message_id,
            false,
            false,
            true,
            "target_user_offline",
            0,
            &server_message_id
        )
    ) {
        ok = false;
    }

    if (server_message_id == 0) {
        std::cerr
            << "[FAIL] offline send server message_id=0\n";

        ok = false;
    } else {
        std::cout
            << "[PASS] offline send persisted stable message"
            << ", message_id="
            << server_message_id
            << '\n';
    }

    /*
     * ============================================================
     * 3. Receiver 10002登录。
     *
     * Login会触发PushPersistentOfflineMessages。
     *
     * 旧实现：
     * Send -> DB Delivered
     *
     * F4-b2新实现：
     * Send -> DB仍Pending
     * ============================================================
     */
    const int user_b_fd =
        ConnectToServer(
            host,
            port
        );

    if (user_b_fd < 0) {
        std::cerr
            << "connect user B failed\n";

        ::close(user_a_fd);
        return 1;
    }

    if (
        !SendPacket(
            user_b_fd,
            codec,
            MakeLoginRequest(
                "user10002",
                "123456",
                3
            )
        )
    ) {
        ::close(user_b_fd);
        ::close(user_a_fd);
        return 1;
    }

    tinyimx::Packet
        user_b_login_response;

    tinyimx::Packet
        replay_packet;

    std::uint32_t
        replay_delivery_seq = 0;

    if (
        !WaitForTargetOfflineReplay(
            user_b_fd,
            codec,
            &user_b_input_buffer,
            server_message_id,
            message_text,
            &user_b_login_response,
            &replay_packet,
            &replay_delivery_seq
        )
    ) {
        std::cerr
            << "[FAIL] target offline replay not received"
            << ", message_id="
            << server_message_id
            << '\n';

        ::close(user_b_fd);
        ::close(user_a_fd);

        return 1;
    }

    if (
        user_b_login_response.type !=
        tinyimx::MessageType::
            kLoginResponse
    ) {
        std::cerr
            << "[FAIL] receiver login response missing\n";

        ok = false;
    }

    std::uint64_t
        receiver_message_id = 0;

    std::uint32_t
        validated_delivery_seq = 0;

    if (
        !ValidateReceiverDelivery(
            replay_packet,
            message_text,
            server_message_id,
            &receiver_message_id,
            &validated_delivery_seq
        )
    ) {
        ok = false;
    }

    if (
        receiver_message_id ==
            server_message_id &&
        replay_delivery_seq ==
            validated_delivery_seq &&
        replay_delivery_seq != 0
    ) {
        std::cout
            << "[PASS] offline replay reused stable message_id"
            << ", message_id="
            << receiver_message_id
            << ", delivery_seq="
            << replay_delivery_seq
            << '\n';
    } else {
        std::cerr
            << "[FAIL] offline replay identity mismatch"
            << ", sender_message_id="
            << server_message_id
            << ", receiver_message_id="
            << receiver_message_id
            << ", delivery_seq="
            << replay_delivery_seq
            << '\n';

        ok = false;
    }

    /*
     * ============================================================
     * 4. Receiver真实ACK。
     *
     * 只有这里才允许：
     *
     * DB Pending -> ReceiverConfirmed
     * ============================================================
     */
    if (
        !SendPacket(
            user_b_fd,
            codec,
            MakeReceiverChatDeliveryAck(
                server_message_id,
                replay_delivery_seq
            )
        )
    ) {
        std::cerr
            << "send offline replay receiver ack failed\n";

        ::close(user_b_fd);
        ::close(user_a_fd);

        return 1;
    }

    std::cout
        << "[SENT] offline replay receiver delivery ack"
        << ", message_id="
        << server_message_id
        << ", delivery_seq="
        << replay_delivery_seq
        << '\n';

    /*
     * ACK没有ACK-of-ACK。
     *
     * 给Gateway完成MarkReceiverConfirmed一个短窗口，
     * 然后用Sender Retry从业务侧再次读取持久状态。
     */
    std::this_thread::sleep_for(
        std::chrono::milliseconds(
            300
        )
    );

    /*
     * ============================================================
     * 5. Sender用相同client_message_id重试。
     *
     * DB已经ReceiverConfirmed。
     *
     * 预期：
     *
     * reused=true
     * delivered=true
     * same server message_id
     * 不再向Receiver重复Push
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
        ::close(user_b_fd);
        ::close(user_a_fd);

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
        ::close(user_b_fd);
        ::close(user_a_fd);

        return 1;
    }

    PrintPacket(
        "[user_a retry]",
        retry_ack_packets.front()
    );

    std::uint64_t
        retry_message_id = 0;

    if (
        !ValidateSenderChatAck(
            retry_ack_packets.front(),
            client_message_id,
            true,
            true,
            false,
            "local_already_receiver_confirmed",
            server_message_id,
            &retry_message_id
        )
    ) {
        ok = false;
    }

    if (
        retry_message_id ==
            server_message_id &&
        retry_message_id != 0
    ) {
        std::cout
            << "[PASS] sender retry reused stable "
               "receiver-confirmed message"
            << ", message_id="
            << retry_message_id
            << '\n';
    } else {
        std::cerr
            << "[FAIL] sender retry message_id changed"
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
            1800
        )
    ) {
        std::cout
            << "[PASS] receiver duplicate offline "
               "replay suppressed after sender retry\n";
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

    ::close(user_b_fd);
    ::close(user_a_fd);

    if (!ok) {
        std::cerr
            << "gateway F4-b2 offline replay "
               "validation failed\n";

        return 1;
    }

    std::cout
        << "gateway F4-b2 offline replay "
           "validation passed\n"
        << "=================================================\n";

    return 0;
}
