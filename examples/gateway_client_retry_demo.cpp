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
        "m12-retry-" +
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


bool AckReceiverDeliveryPacket(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    const tinyimx::Packet& packet
) {
    /*
     * 测试启动前可能存在历史Pending消息。
     *
     * 非Receiver Delivery无需处理。
     */
    if (
        packet.type !=
        tinyimx::MessageType::kChatDelivery
    ) {
        return true;
    }


    if (packet.seq == 0) {
        std::cerr
            << "receiver delivery seq must not be zero\n";

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
            << "deserialize receiver delivery for ACK failed"
            << ", error="
            << error_message
            << ", body="
            << packet.body
            << '\n';

        return false;
    }


    if (delivery.message_id == 0) {
        std::cerr
            << "receiver delivery message_id=0\n";

        return false;
    }


    tinyimx::ReceiverChatDeliveryAck ack;

    ack.message_id =
        delivery.message_id;


    std::string ack_body;

    error_message.clear();


    if (
        !tinyimx::SerializeReceiverChatDeliveryAck(
            ack,
            &ack_body,
            &error_message
        )
    ) {
        std::cerr
            << "serialize receiver delivery ACK failed"
            << ", message_id="
            << delivery.message_id
            << ", error="
            << error_message
            << '\n';

        return false;
    }


    tinyimx::Packet ack_packet;

    ack_packet.type =
        tinyimx::MessageType::kChatDeliveryAck;

    /*
     * ACK必须引用当前Receiver Delivery Attempt。
     */
    ack_packet.seq =
        packet.seq;

    ack_packet.body =
        std::move(ack_body);


    if (
        !SendPacket(
            fd,
            codec,
            ack_packet
        )
    ) {
        std::cerr
            << "send receiver delivery ACK failed"
            << ", message_id="
            << delivery.message_id
            << ", delivery_seq="
            << packet.seq
            << '\n';

        return false;
    }


    std::cout
        << "[SENT] receiver delivery ACK"
        << ", message_id="
        << delivery.message_id
        << ", delivery_seq="
        << packet.seq
        << '\n';


    return true;
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


bool ValidateRejectedLateSuccessRegression() {
    /*
     * ============================================================
     * F7-a Regression:
     *
     * deterministic Rejected 是不可逆Terminal State。
     *
     * 即使之后收到同一个Logical Send的合法success ACK：
     *
     * Rejected
     *   X
     * Accepted
     *
     * 都绝不能发生。
     * ============================================================
     */

    PendingLogicalSend logical_send;

    logical_send.client_message_id =
        "m12-f7-rejected-late-success";

    logical_send.to_user_id = 10002;

    logical_send.text =
        "f7 rejected late success regression";

    logical_send.state =
        ClientSendState::kRejected;

    logical_send.server_message_id = 0;

    logical_send.completion_count = 0;


    /*
     * 构造一条协议层完全合法的late success ACK。
     *
     * 这样测试的不是：
     * malformed packet
     *
     * 而是真正的：
     * valid success evidence arriving after Rejected。
     */
    tinyimx::ClientChatAck ack;

    ack.success = true;

    ack.delivered = true;

    ack.stored_offline = false;

    ack.stored_persistent = true;

    ack.reused = true;

    ack.client_message_id =
        logical_send.client_message_id;

    ack.message_id = 900001;

    ack.from_user_id = 10001;

    ack.to_user_id = 10002;

    ack.reason =
        "synthetic_late_success_after_rejected";


    std::string body;

    std::string error_message;

    if (
        !tinyimx::SerializeClientChatAck(
            ack,
            &body,
            &error_message
        )
    ) {
        std::cerr
            << "[FAIL] F7-a could not serialize "
               "synthetic late success ACK"
            << ", error="
            << error_message
            << '\n';

        return false;
    }


    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::kChatAck;

    /*
     * seq只表示这个synthetic ACK所在的Network Attempt。
     * 对Rejected终态保护本身没有业务意义，
     * 但仍给一个合法非零值。
     */
    packet.seq = 9001;

    packet.body =
        std::move(body);


    bool newly_accepted = false;

    const bool apply_ok =
        ApplySuccessfulAckToLogicalSend(
            packet,
            &logical_send,
            &newly_accepted
        );


    /*
     * 最重要的五个断言：
     *
     * 1. ACK本身合法，所以Apply返回true。
     * 2. 不能产生新的Accepted transition。
     * 3. state仍是Rejected。
     * 4. 不能偷偷补写server_message_id。
     * 5. completion_count不能增加。
     */
    if (
        apply_ok &&
        !newly_accepted &&
        logical_send.state ==
            ClientSendState::kRejected &&
        logical_send.server_message_id == 0 &&
        logical_send.completion_count == 0
    ) {
        std::cout
            << "[PASS] F7-a rejected terminal state "
               "ignored late success ACK"
            << ", state=rejected"
            << ", server_message_id="
            << logical_send.server_message_id
            << ", completion_count="
            << logical_send.completion_count
            << '\n';

        return true;
    }


    std::cerr
        << "[FAIL] F7-a rejected logical send "
           "was changed by late success ACK"
        << ", apply_ok="
        << apply_ok
        << ", newly_accepted="
        << newly_accepted
        << ", state="
        << (
            logical_send.state ==
                    ClientSendState::kRejected
                ? "rejected"
                : "changed"
        )
        << ", server_message_id="
        << logical_send.server_message_id
        << ", completion_count="
        << logical_send.completion_count
        << '\n';

    return false;
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
                !AckReceiverDeliveryPacket(
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
            << "receiver unexpectedly received another packet "
                "after receiver confirmation\n";

        return false;
    }


    return true;
}


}  // namespace


int main(
    int argc,
    char* argv[]
) {

    /*
     * F7-a纯状态机Regression。
     *
     * 必须先于任何网络操作执行，
     * 这样它失败时不会被Gateway/MySQL/Redis环境噪声掩盖。
     */
    if (
        !ValidateRejectedLateSuccessRegression()
    ) {
        return 1;
    }
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
        "Retry Demo ==========\n";


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
            !AckReceiverDeliveryPacket(
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
     * Gateway先Push Receiver，
     * 再MarkDelivered，
     * 最后ACK Sender。
     *
     * 所以先读取Receiver是合理的。
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


    if (
        !ValidateReceiverChat(
            receiver_chat_packets.front(),
            message_text
        )
    ) {
        ok = false;
    }


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
    constexpr auto kSimulatedAckTimeout =
        std::chrono::milliseconds(500);


    std::this_thread::sleep_for(
        kSimulatedAckTimeout
    );


    logical_send.state =
        ClientSendState::kRetryWait;


    std::cout
        << "[SIMULATED TIMEOUT]"
        << " seq="
        << logical_send.current_seq
        << ", client_message_id="
        << logical_send.client_message_id
        << ", wait_ms="
        << kSimulatedAckTimeout.count()
        << '\n';
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
        4;

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
        "[user_a late ack]",
        first_ack_packets.front()
    );

    std::cout
        << "[PASS] late ACK observed after retry send"
        << ", ack_seq="
        << first_ack_packets.front().seq
        << '\n';

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
            * 第一次Client Logical Send，
            * 不是幂等复用。
            */
            false,

            /*
            * Receiver还没有ACK。
            */
            false,

            "local_delivery_awaiting_receiver_ack",

            0,

            &server_message_id,
            &first_private_unread,
            &first_total_unread
        )
    ) {
        ok = false;
    }


    bool late_ack_newly_accepted =
        false;


    if (
        !ApplySuccessfulAckToLogicalSend(
            first_ack_packets.front(),
            &logical_send,
            &late_ack_newly_accepted
        )
    ) {
        std::cerr
            << "[FAIL] late ACK could not "
            "complete logical send\n";

        ok = false;
    }


    if (
        late_ack_newly_accepted &&
        logical_send.state ==
            ClientSendState::kAccepted &&
        logical_send.completion_count == 1
    ) {
        std::cout
            << "[PASS] late ACK completed logical send"
            << ", ack_seq="
            << first_ack_packets.front().seq
            << ", completion_count="
            << logical_send.completion_count
            << '\n';
    } else {
        std::cerr
            << "[FAIL] late ACK did not create "
            "exactly one logical completion"
            << ", completion_count="
            << logical_send.completion_count
            << '\n';

        ok = false;
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


    /*
    * ============================================================
    * Client Retry的两条Sender ACK现在都已经到达。
    *
    * 到这里为止：
    *
    * Client Retry是否重复创建M、
    * 是否重复Unread、
    * 是否重复Logical Completion，
    *
    * 都可以继续通过已经收到的ACK验证。
    *
    * 现在必须ACK第一次Receiver Delivery，
    * 防止Receiver ACK Timeout状态机在后面的
    * No-Duplicate窗口中合法产生D2。
    * ============================================================
    */
    if (
        !AckReceiverDeliveryPacket(
            user_b_fd,
            codec,
            receiver_chat_packets.front()
        )
    ) {
        std::cerr
            << "failed to ACK current receiver delivery\n";

        ::close(user_a_fd);
        ::close(user_b_fd);

        return 1;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(200)
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
            * same client_message_id，
            * 所以业务消息被复用。
            */
            true,

            /*
            * Receiver尚未确认。
            */
            false,

            "local_delivery_awaiting_receiver_ack",

            server_message_id,

            &retry_message_id,
            &retry_private_unread,
            &retry_total_unread
        )
    ) {
        ok = false;
    }


    bool retry_ack_newly_accepted =
        false;


    if (
        !ApplySuccessfulAckToLogicalSend(
            retry_ack_packets.front(),
            &logical_send,
            &retry_ack_newly_accepted
        )
    ) {
        std::cerr
            << "[FAIL] retry ACK identity validation failed\n";

        ok = false;
    }


    if (
        !retry_ack_newly_accepted &&
        logical_send.state ==
            ClientSendState::kAccepted &&
        logical_send.completion_count == 1
    ) {
        std::cout
            << "[PASS] duplicate terminal ACK "
            "did not re-complete logical send"
            << ", ack_seq="
            << retry_ack_packets.front().seq
            << ", completion_count="
            << logical_send.completion_count
            << '\n';
    } else {
        std::cerr
            << "[FAIL] retry ACK caused duplicate "
            "logical completion"
            << ", newly_accepted="
            << retry_ack_newly_accepted
            << ", completion_count="
            << logical_send.completion_count
            << '\n';

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
    * 9. Receiver ACK之后不得再出现Delivery Retry
    * ============================================================
    *
    * Client Retry本身已经被Gateway Memory/Durable Dedup抑制。
    *
    * 当前Receiver也已经ACK原始Delivery Attempt，
    * 因此Receiver ACK Timeout Timer必须停止。
    *
    * 等待超过1500ms：
    * 不允许再观察到任何Receiver Delivery Packet。
    */
    if (
        ExpectNoPacketWithin(
            user_b_fd,
            1800
        )
    ) {
        std::cout
            << "[PASS] client retry suppressed duplicate "
            "receiver effect and receiver ACK stopped "
            "delivery retry\n";
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
