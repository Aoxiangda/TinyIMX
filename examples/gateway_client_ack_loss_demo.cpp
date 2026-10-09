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


/*
 * 一次Logical Client Send在客户端侧的生命周期。
 *
 * 注意：
 * Accepted != Delivered。
 *
 * Accepted：
 * Server已经可靠接受这个Logical Send，
 * Client不应该继续重新提交。
 *
 * Uncertain：
 * 多次网络Attempt都没有获得明确结果，
 * 但不能断言Server一定没有处理。
 */
enum class ClientSendState {
    kCreated = 0,
    kInFlight,
    kRetryWait,
    kAccepted,
    kRejected,
    kUncertain
};


/*
 * 一次用户点击“发送”对应一个PendingLogicalSend。
 *
 * 整个对象生命周期：
 *
 * client_message_id 永远不变
 *
 * 每次Retry：
 * current_seq变化
 * attempt_count增加
 */
struct PendingLogicalSend {
    std::string client_message_id;

    std::uint64_t to_user_id{0};

    std::string text;

    /*
     * 当前Network Attempt使用的Packet.seq。
     */
    std::uint32_t current_seq{0};

    /*
     * 已经真正发出的Attempt数量。
     */
    std::size_t attempt_count{0};

    ClientSendState state{
        ClientSendState::kCreated
    };

    /*
     * Server第一次成功接受Logical Send以后
     * 返回的稳定业务message_id。
     */
    std::uint64_t server_message_id{0};

    /*
    * Logical Send真正从非终态
    * 进入Accepted的次数。
    *
    * 正确情况下永远只能是：
    *
    * 0 → 1
    *
    * Late ACK、Retry ACK即使都到达，
    * 也不能变成2。
    */
    std::size_t completion_count{0};

};


std::string MakeClientMessageId() {
    const auto now =
        std::chrono::system_clock::now()
            .time_since_epoch();

    const auto nanoseconds =
        std::chrono::duration_cast<
            std::chrono::nanoseconds
        >(now).count();


    return
        "m12-ack-loss-" +
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


bool ApplySuccessfulAckToLogicalSend(
    const tinyimx::Packet& packet,
    PendingLogicalSend* logical_send,
    bool* newly_accepted
) {
    if (
        logical_send == nullptr ||
        newly_accepted == nullptr
    ) {
        return false;
    }


    *newly_accepted =
        false;


    if (
        packet.type !=
        tinyimx::MessageType::kChatAck
    ) {
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
            << "apply ack failed to deserialize"
            << ", error="
            << error_message
            << '\n';

        return false;
    }


    /*
     * 这个函数只负责处理：
     *
     * Server已经明确接受Logical Send的ACK。
     */
    if (!ack.success) {
        return false;
    }


    /*
     * 第一层身份：
     *
     * ACK必须属于当前Logical Send。
     */
    if (
        ack.client_message_id !=
        logical_send->client_message_id
    ) {
        std::cerr
            << "late/retry ack logical identity mismatch"
            << ", expected="
            << logical_send->client_message_id
            << ", actual="
            << ack.client_message_id
            << '\n';

        return false;
    }


    if (ack.message_id == 0) {
        return false;
    }

    /*
    * Rejected 是确定性Terminal State。
    *
    * 即使后续收到同一个Logical Send的late success ACK，
    * 也只能忽略，绝不能：
    *
    * Rejected -> Accepted
    *
    * 同时不能补写server_message_id，
    * 不能增加completion_count，
    * 不能清除/改写其他terminal state。
    *
    * 返回true表示：
    * ACK本身已经完成基础协议/Logical Identity校验，
    * 但它没有产生新的状态迁移。
    */
    if (
        logical_send->state ==
        ClientSendState::kRejected
    ) {
        /*
        * 如果此前已经知道稳定Server Message ID，
        * late ACK仍不能偷偷换成另一条业务消息。
        */
        if (
            logical_send->server_message_id != 0 &&
            logical_send->server_message_id !=
                ack.message_id
        ) {
            std::cerr
                << "rejected logical send received "
                "conflicting server message_id"
                << ", expected="
                << logical_send->server_message_id
                << ", actual="
                << ack.message_id
                << '\n';

            return false;
        }

        return true;
    }

    /*
     * 第一次成功ACK：
     *
     * Client第一次获知稳定Server Message ID。
     */
    if (
        logical_send->server_message_id == 0
    ) {
        logical_send->server_message_id =
            ack.message_id;
    }


    /*
     * 后续所有ACK必须继续映射到
     * 同一个Server Message。
     */
    else if (
        logical_send->server_message_id !=
        ack.message_id
    ) {
        std::cerr
            << "ack server message identity changed"
            << ", expected="
            << logical_send->server_message_id
            << ", actual="
            << ack.message_id
            << '\n';

        return false;
    }


    /*
     * 已经Accepted：
     *
     * 说明这是迟到/重复的Terminal ACK。
     *
     * 可以验证，
     * 但不能再次产生Logical Completion。
     */
    if (
        logical_send->state ==
        ClientSendState::kAccepted
    ) {
        return true;
    }


    logical_send->state =
        ClientSendState::kAccepted;

    ++logical_send->completion_count;

    *newly_accepted =
        true;


    return true;
}


bool ValidateReceiverChat(
    const tinyimx::Packet& packet,
    const std::string& expected_text,
    std::uint64_t* actual_message_id = nullptr,
    std::uint32_t* actual_delivery_seq = nullptr
) {
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
            << "receiver message_id "
               "must not be zero\n";

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



bool SendReceiverDeliveryAck(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    std::uint64_t message_id,
    std::uint32_t delivery_seq
) {
    if (
        message_id == 0 ||
        delivery_seq == 0
    ) {
        return false;
    }

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

        return false;
    }

    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::kChatDeliveryAck;

    /*
     * Receiver ACK必须引用真实Delivery Attempt D。
     */
    packet.seq =
        delivery_seq;

    packet.body =
        std::move(body);

    return SendPacket(
        fd,
        codec,
        packet
    );
}


bool AckPreTestReceiverDelivery(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    const tinyimx::Packet& packet
) {
    if (
        packet.type !=
        tinyimx::MessageType::kChatDelivery
    ) {
        return true;
    }

    if (packet.seq == 0) {
        std::cerr
            << "pre-test backlog delivery "
               "seq must not be zero\n";

        return false;
    }

    tinyimx::ServerChatDelivery delivery;
    std::string error_message;

    if (
        !tinyimx::DeserializeServerChatDelivery(
            packet.body,
            &delivery,
            &error_message
        )
    ) {
        std::cerr
            << "deserialize pre-test backlog "
               "delivery failed"
            << ", error="
            << error_message
            << ", body="
            << packet.body
            << '\n';

        return false;
    }

    if (delivery.message_id == 0) {
        std::cerr
            << "pre-test backlog message_id=0\n";

        return false;
    }

    if (
        !SendReceiverDeliveryAck(
            fd,
            codec,
            delivery.message_id,
            packet.seq
        )
    ) {
        std::cerr
            << "send pre-test backlog ACK failed"
            << ", message_id="
            << delivery.message_id
            << ", delivery_seq="
            << packet.seq
            << '\n';

        return false;
    }

    std::cout
        << "[SENT] pre-test backlog receiver ACK"
        << ", message_id="
        << delivery.message_id
        << ", delivery_seq="
        << packet.seq
        << '\n';

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

                if (
                    !AckPreTestReceiverDelivery(
                        fd,
                        codec,
                        packet
                    )
                ) {
                    return false;
                }
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
        << "========== TinyIMX Client "
        "ACK Loss Recovery Demo ==========\n";

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

        if (
            !AckPreTestReceiverDelivery(
                user_b_fd,
                codec,
                user_b_login_packets[index]
            )
        ) {
            std::cerr
                << "failed to ack receiver login backlog\n";

            ::close(user_b_fd);
            return 1;
        }
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
    int user_a_fd =
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
    *
    * 一个PendingLogicalSend
    * 对应用户的一次“发送消息”动作。
    *
    * 后面的所有Retry：
    *
    * client_message_id 不变
    * current_seq        改变
    */
    PendingLogicalSend logical_send;

    logical_send.client_message_id =
        MakeClientMessageId();

    logical_send.to_user_id =
        10002;

    logical_send.text =
        "m12 client automatic retry demo";

    logical_send.current_seq =
        3;

    logical_send.attempt_count =
        0;

    logical_send.state =
        ClientSendState::kCreated;


    /*
    * 下面两个reference暂时保留，
    * 是为了让本阶段后面的旧验证代码
    * 不需要一起大改。
    *
    * 下一阶段真正改Timeout流程时再清理。
    */
    const std::string&
        client_message_id =
            logical_send.client_message_id;

    const std::string&
        message_text =
            logical_send.text;


    std::cout
        << "client_message_id="
        << logical_send.client_message_id
        << '\n';


    /*
     * ============================================================
     * 4. 第一次发送
     *
     * Packet.seq = 3
     * client_message_id = C100
     * ============================================================
     */

     logical_send.state =
        ClientSendState::kInFlight;

    logical_send.current_seq =
        3;

    ++logical_send.attempt_count;


    std::cout
        << "[Attempt #"
        << logical_send.attempt_count
        << "]"
        << " seq="
        << logical_send.current_seq
        << ", client_message_id="
        << logical_send.client_message_id
        << '\n';


    if (
        !SendPacket(
            user_a_fd,
            codec,
           MakeChatMessage(
                logical_send.to_user_id,
                logical_send.current_seq,
                logical_send.text,
                logical_send.client_message_id
            )
        )
    ) {
        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    /*
     * Local Delivery路径：
     *
     * Gateway先提交Receiver Delivery，
     * Sender ACK只表示Server已经接受责任。
     *
     * ReceiverConfirmed必须由Receiver application ACK驱动。
     *
     * 所以这里先读取Receiver Delivery，
     * 再显式ACK(M,D)。
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
        receiver_message_id = 0;

    std::uint32_t
        receiver_delivery_seq = 0;


    if (
        !ValidateReceiverChat(
            receiver_chat_packets.front(),
            message_text,
            &receiver_message_id,
            &receiver_delivery_seq
        )
    ) {
        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    /*
     * 先固定Server Truth：
     *
     * Receiver已经真实收到M/D，
     * 并通过application ACK驱动：
     *
     * Pending -> ReceiverConfirmed
     *
     * 后面只制造Sender ACK不可观察，
     * 不再混入Receiver ACK等待这一第二故障维度。
     */
    if (
        !SendReceiverDeliveryAck(
            user_b_fd,
            codec,
            receiver_message_id,
            receiver_delivery_seq
        )
    ) {
        std::cerr
            << "[FAIL] receiver delivery ACK failed\n";

        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }


    std::cout
        << "[SENT] receiver delivery ACK"
        << ", message_id="
        << receiver_message_id
        << ", delivery_seq="
        << receiver_delivery_seq
        << '\n';


    std::this_thread::sleep_for(
        std::chrono::milliseconds(300)
    );


    /*
     * ============================================================
     * 5. 第一次Sender ACK
     * ============================================================
     */
    /*
    * ============================================================
    * Simulated ACK Timeout
    * ============================================================
    *
    * Gateway实际上已经处理完成，
    * 第一次ACK也很可能已经进入Sender Socket接收缓冲区。
    *
    * 但Client现在故意不调用recv，
    * 模拟：
    *
    * “在超时窗口内没有观察到ACK”。
    */


    /*
    * ============================================================
    * Real ACK Loss Simulation
    *
    * Gateway已经完成：
    *
    * 1. MySQL persistence
    * 2. Receiver push
    * 3. Receiver application ACK
    * 4. durable ReceiverConfirmed
    *
    * 但是Sender没有观察到第一次ChatAck。
    *
    * 模拟真实网络情况：
    *
    * Sender TCP连接断开。
    *
    * ============================================================
    */


    std::cout
        << "[SIMULATED CONNECTION LOSS]"
        << " closing sender connection"
        << ", client_message_id="
        << logical_send.client_message_id
        << '\n';


    ::close(user_a_fd);


    user_a_fd = -1;


    logical_send.state =
        ClientSendState::kRetryWait;


    std::this_thread::sleep_for(
        std::chrono::milliseconds(500)
    );



    /*
    * Reconnect sender
    */

    std::cout
        << "[RECONNECT]"
        << " rebuilding sender connection\n";


    user_a_fd =
        ConnectToServer(
            host,
            port
        );


    if (user_a_fd < 0) {

        std::cerr
            << "reconnect user_a failed\n";

        ::close(user_b_fd);

        return 1;
    }



    std::vector<tinyimx::Packet>
        reconnect_login_packets;


    /*
    * 新TCP Connection必须拥有自己的Stream Buffer。
    *
    * Old connection:
    *     user_a_input_buffer
    *
    * New connection:
    *     reconnected_user_a_input_buffer
    */
    tinyimx::Buffer
        reconnected_user_a_input_buffer;

    if (
        !SendPacket(
            user_a_fd,
            codec,
            MakeLoginRequest(
                "user10001",
                "123456",
                4
            )
        )
    ) {

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }



    if (
        !WaitForPackets(
            user_a_fd,
            codec,
            &reconnected_user_a_input_buffer,
            1,
            &reconnect_login_packets
        )
    ) {

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    if (reconnect_login_packets.empty()) {
        std::cerr
            << "reconnect login response missing\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    PrintPacket(
        "[user_a reconnect login]",
        reconnect_login_packets.front()
    );


    std::cout
        << "[PASS] sender reconnect login completed\n";

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


    /*
    * 新的一次Network Attempt：
    *
    * seq改变，
    * client_message_id绝对不改变。
    */
    logical_send.current_seq =
        5;

    logical_send.state =
        ClientSendState::kInFlight;

    ++logical_send.attempt_count;


    std::cout
        << "[Attempt #"
        << logical_send.attempt_count
        << "]"
        << " seq="
        << logical_send.current_seq
        << ", client_message_id="
        << logical_send.client_message_id
        << '\n';


    if (
        !SendPacket(
            user_a_fd,
            codec,
            MakeChatMessage(
                logical_send.to_user_id,
                logical_send.current_seq,
                logical_send.text,
                logical_send.client_message_id
            )
        )
    ) {
        ::close(user_a_fd);
        ::close(user_b_fd);
        return 1;
    }

    /*
    * ============================================================
    * 7. Retry ACK
    *
    * Attempt #1的ACK已经随着旧TCP Connection永久不可见。
    *
    * 因此新Connection上只应该观察到：
    *
    * Attempt #2 Retry ACK
    * ============================================================
    */
    std::vector<tinyimx::Packet>
        retry_ack_packets;


    if (
        !WaitForPackets(
            user_a_fd,
            codec,
            &reconnected_user_a_input_buffer,
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
            << "retry ACK missing after reconnect\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }


    const tinyimx::Packet&
        retry_ack =
            retry_ack_packets.front();


    PrintPacket(
        "[user_a retry ack after reconnect]",
        retry_ack
    );


    if (
        retry_ack.seq !=
        logical_send.current_seq
    ) {
        std::cerr
            << "[FAIL] retry ACK seq mismatch"
            << ", expected="
            << logical_send.current_seq
            << ", actual="
            << retry_ack.seq
            << '\n';

        ok = false;
    }


    std::uint64_t
        server_message_id = 0;

    std::int64_t
        retry_private_unread = 0;

    std::int64_t
        retry_total_unread = 0;


    /*
    * Client从未观察到第一次ACK。
    *
    * 因此：
    *
    * expected_message_id = 0
    *
    * Server必须告诉Client：
    *
    * reused = true
    * delivered = true
    * reason = local_already_receiver_confirmed
    */
    if (
        !ValidateLocalChatAck(
            retry_ack,
            client_message_id,
            true,
            true,
            "local_already_receiver_confirmed",
            0,
            &server_message_id,
            &retry_private_unread,
            &retry_total_unread
        )
    ) {
        ok = false;
    }


    /*
    * 这是Client第一次真正观察到成功结果。
    *
    * 即使它来自Attempt #2，
    * 完成的依然是同一个Logical Send。
    */
    bool retry_ack_newly_accepted =
        false;


    if (
        !ApplySuccessfulAckToLogicalSend(
            retry_ack,
            &logical_send,
            &retry_ack_newly_accepted
        )
    ) {
        std::cerr
            << "[FAIL] retry ACK could not "
            "complete logical send\n";

        ok = false;
    }


    if (
        retry_ack_newly_accepted &&
        logical_send.state ==
            ClientSendState::kAccepted &&
        logical_send.completion_count == 1 &&
        logical_send.server_message_id ==
            server_message_id
    ) {
        std::cout
            << "[PASS] reconnect retry completed "
            "logical send once"
            << ", message_id="
            << server_message_id
            << ", completion_count="
            << logical_send.completion_count
            << '\n';
    } else {
        std::cerr
            << "[FAIL] reconnect retry logical "
            "completion invalid"
            << ", newly_accepted="
            << retry_ack_newly_accepted
            << ", completion_count="
            << logical_send.completion_count
            << '\n';

        ok = false;
    }

        if (
        receiver_message_id != 0 &&
        receiver_message_id ==
            server_message_id
    ) {
        std::cout
            << "[PASS] receiver delivery and recovered sender ACK "
               "share stable message_id"
            << ", message_id="
            << server_message_id
            << '\n';
    } else {
        std::cerr
            << "[FAIL] receiver/sender stable message_id mismatch"
            << ", receiver_message_id="
            << receiver_message_id
            << ", sender_message_id="
            << server_message_id
            << '\n';

        ok = false;
    }


    /*
     * ============================================================
     * 7. Stable Server Message ID
     * ============================================================
     */
    /*
    * Client第一次观察到的Server Message ID
    * 必须有效，并已经写入Logical Send状态。
    *
    * 第一次Server实际创建的message_id是否与此一致，
    * 后续通过Gateway日志/MySQL共同验证。
    */
    if (
        server_message_id != 0 &&
        logical_send.server_message_id ==
            server_message_id
    ) {
        std::cout
            << "[PASS] recovered stable server message_id"
            << ", message_id="
            << server_message_id
            << '\n';
    } else {
        std::cerr
            << "[FAIL] invalid recovered server message_id"
            << '\n';

        ok = false;
    }


    /*
     * ============================================================
     * 8. Retry不能重复增加Unread
     * ============================================================
     */
    std::cout
        << "verification_retry_private_unread="
        << retry_private_unread
        << '\n';

    std::cout
        << "verification_retry_total_unread="
        << retry_total_unread
        << '\n';
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

    std::cout
        << "verification_attempt_count="
        << logical_send.attempt_count
        << '\n';


    std::cout
        << "verification_completion_count="
        << logical_send.completion_count
        << '\n';

    std::cout
        << "verification_final_state="
        << (
            logical_send.state ==
                    ClientSendState::kAccepted
                ? "accepted"
                : "not_accepted"
        )
        << '\n';
    ::close(user_a_fd);
    ::close(user_b_fd);


    if (!ok) {
        std::cerr
        << "client ACK loss recovery validation failed\n";

        return 1;
    }


    std::cout
        << "client ACK loss recovery validation passed\n"
        << "================================================\n";

    return 0;
}
