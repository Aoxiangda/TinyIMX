#include "common/net/Buffer.h"

#include "common/protocol/GatewayPeerProtocol.h"
#include "common/protocol/Packet.h"
#include "common/protocol/ProtocolCodec.h"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>


namespace {

using Json = nlohmann::json;


constexpr std::uint64_t
    kFromUserId = 10001;

constexpr std::uint64_t
    kToUserId = 10002;


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
        std::cerr
            << "socket failed: "
            << std::strerror(errno)
            << '\n';

        return -1;
    }


    sockaddr_in address{};

    address.sin_family =
        AF_INET;

    address.sin_port =
        htons(port);


    if (
        ::inet_pton(
            AF_INET,
            host.c_str(),
            &address.sin_addr
        ) != 1
    ) {
        std::cerr
            << "invalid host: "
            << host
            << '\n';

        ::close(fd);

        return -1;
    }


    if (
        ::connect(
            fd,
            reinterpret_cast<
                sockaddr*
            >(&address),
            sizeof(address)
        ) != 0
    ) {
        std::cerr
            << "connect failed: "
            << std::strerror(errno)
            << '\n';

        ::close(fd);

        return -1;
    }


    return fd;
}


bool SendAll(
    int fd,
    const char* data,
    std::size_t size
) {
    std::size_t sent = 0;


    while (sent < size) {
        const ssize_t result =
            ::send(
                fd,
                data + sent,
                size - sent,
                0
            );


        if (result > 0) {
            sent +=
                static_cast<std::size_t>(
                    result
                );

            continue;
        }


        if (
            result < 0 &&
            errno == EINTR
        ) {
            continue;
        }


        return false;
    }


    return true;
}


bool SendPacket(
    int fd,
    tinyimx::ProtocolCodec& codec,
    const tinyimx::Packet& packet
) {
    tinyimx::Buffer output;

    std::string error;


    if (
        !codec.Encode(
            packet,
            &output,
            &error
        )
    ) {
        std::cerr
            << "encode failed: "
            << error
            << '\n';

        return false;
    }


    const std::string bytes =
        output.RetrieveAllAsString();


    if (
        !SendAll(
            fd,
            bytes.data(),
            bytes.size()
        )
    ) {
        std::cerr
            << "send failed: "
            << std::strerror(errno)
            << '\n';

        return false;
    }


    return true;
}


bool WaitForOnePacket(
    int fd,
    tinyimx::ProtocolCodec& codec,
    tinyimx::Packet* packet,
    int timeout_ms = 3000
) {
    if (packet == nullptr) {
        return false;
    }


    tinyimx::Buffer input;


    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(
            timeout_ms
        );


    while (true) {
        const auto now =
            std::chrono::steady_clock::now();


        if (now >= deadline) {
            return false;
        }


        const auto remaining =
            std::chrono::
                duration_cast<
                    std::chrono::milliseconds
                >(
                    deadline - now
                );


        pollfd descriptor{};

        descriptor.fd =
            fd;

        descriptor.events =
            POLLIN;


        const int poll_result =
            ::poll(
                &descriptor,
                1,
                static_cast<int>(
                    remaining.count()
                )
            );


        if (poll_result == 0) {
            return false;
        }


        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }

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


        char buffer[4096];


        const ssize_t received =
            ::recv(
                fd,
                buffer,
                sizeof(buffer),
                0
            );


        if (received <= 0) {
            return false;
        }


        input.Append(
            buffer,
            static_cast<std::size_t>(
                received
            )
        );


        const tinyimx::DecodeResult
            decoded =
                codec.Decode(
                    &input
                );


        if (
            decoded.status ==
            tinyimx::DecodeStatus::
                kNeedMoreData
        ) {
            continue;
        }


        if (
            decoded.status !=
            tinyimx::DecodeStatus::
                kOk
        ) {
            std::cerr
                << "decode failed: "
                << tinyimx::
                    DecodeStatusToString(
                        decoded.status
                    )
                << ", error="
                << decoded.error_message
                << '\n';

            return false;
        }


        if (decoded.packets.empty()) {
            continue;
        }


        *packet =
            decoded.packets.front();

        return true;
    }
}


bool ExpectNoPacket(
    int fd,
    int timeout_ms
) {
    pollfd descriptor{};

    descriptor.fd =
        fd;

    descriptor.events =
        POLLIN;


    while (true) {
        const int result =
            ::poll(
                &descriptor,
                1,
                timeout_ms
            );


        if (result == 0) {
            return true;
        }


        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }

            return false;
        }


        return false;
    }
}


tinyimx::Packet MakeLoginRequest(
    std::uint32_t seq
) {
    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::
            kLoginRequest;

    packet.seq =
        seq;

    packet.body =
        Json{
            {"username", "user10002"},
            {"password", "123456"}
        }.dump();


    return packet;
}


bool ValidateLoginResponse(
    const tinyimx::Packet& packet
) {
    if (
        packet.type !=
        tinyimx::MessageType::
            kLoginResponse
    ) {
        std::cerr
            << "expected login_response\n";

        return false;
    }


    try {
        const Json body =
            Json::parse(
                packet.body
            );


        if (
            !body.value(
                "success",
                false
            )
        ) {
            std::cerr
                << "login failed: "
                << packet.body
                << '\n';

            return false;
        }


        if (
            body.value(
                "user_id",
                0ULL
            ) !=
            kToUserId
        ) {
            std::cerr
                << "unexpected login user\n";

            return false;
        }


        /*
         * 为了保证后面的“No Packet”
         * 只针对本次Replay，
         * 当前测试要求没有Pending离线消息
         * 在登录阶段被推送。
         */
        if (
            body.value(
                "offline_count",
                0ULL
            ) != 0
        ) {
            std::cerr
                << "user10002 has pending "
                   "offline messages; "
                   "durable replay test "
                   "requires offline_count=0"
                << '\n';

            return false;
        }


        return true;

    } catch (
        const std::exception& e
    ) {
        std::cerr
            << "parse login response failed: "
            << e.what()
            << '\n';

        return false;
    }
}


bool BuildForwardPacket(
    const std::string&
        source_gateway_id,

    const std::string&
        source_lease_token,

    std::uint64_t message_id,

    const std::string&
        message_text,

    std::uint32_t rpc_seq,

    tinyimx::Packet* packet
) {
    if (
        packet == nullptr ||
        message_id == 0
    ) {
        return false;
    }


    tinyimx::
    GatewayForwardChatRequest request;


    request.message_id =
        message_id;

    request.source_gateway_id =
        source_gateway_id;

    request.source_lease_token =
        source_lease_token;

    request.from_user_id =
        kFromUserId;

    request.to_user_id =
        kToUserId;


    /*
     * 必须与最初由Gateway A
     * 保存进MySQL的content完全一致。
     */
    request.message_body =
        Json{
            {"from", kFromUserId},
            {"text", message_text},
            {"to", kToUserId}
        }.dump();


    std::string body;
    std::string error;


    if (
        !tinyimx::
            SerializeGatewayForwardChatRequest(
                request,
                &body,
                &error
            )
    ) {
        std::cerr
            << "serialize forward request "
               "failed: "
            << error
            << '\n';

        return false;
    }


    packet->type =
        tinyimx::MessageType::
            kGatewayForwardChatRequest;

    packet->seq =
        rpc_seq;

    packet->body =
        std::move(body);


    return true;
}


bool ValidateDuplicateResponse(
    const tinyimx::Packet& packet,
    std::uint64_t expected_message_id
) {
    if (
        packet.type !=
        tinyimx::MessageType::
            kGatewayForwardChatResponse
    ) {
        std::cerr
            << "unexpected response type\n";

        return false;
    }


    tinyimx::
    GatewayForwardChatResponse response;

    std::string error;


    if (
        !tinyimx::
            DeserializeGatewayForwardChatResponse(
                packet.body,
                &response,
                &error
            )
    ) {
        std::cerr
            << "deserialize response failed: "
            << error
            << '\n';

        return false;
    }


    if (!response.Delivered()) {
        std::cerr
            << "expected delivered response"
            << ", status="
            << tinyimx::
                GatewayForwardChatStatusToString(
                    response.status
                )
            << '\n';

        return false;
    }


    if (
        response.message_id !=
        expected_message_id
    ) {
        std::cerr
            << "message_id mismatch\n";

        return false;
    }


    if (
        response.to_user_id !=
        kToUserId
    ) {
        std::cerr
            << "to_user_id mismatch\n";

        return false;
    }


    if (!response.duplicate) {
        std::cerr
            << "durable replay must return "
               "duplicate=true\n";

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


}  // namespace


int main(
    int argc,
    char* argv[]
) {
    if (argc < 7) {
        std::cerr
            << "usage:\n  "
            << argv[0]
            << " <host>"
            << " <gateway-b-port>"
            << " <source-gateway-id>"
            << " <source-lease-token>"
            << " <message-id>"
            << " <message-text>\n";

        return 1;
    }


    const std::string host =
        argv[1];


    const auto gateway_b_port =
        static_cast<std::uint16_t>(
            std::stoi(
                argv[2]
            )
        );


    const std::string
        source_gateway_id =
            argv[3];


    const std::string
        source_lease_token =
            argv[4];


    const std::uint64_t
        message_id =
            std::stoull(
                argv[5]
            );


    const std::string
        message_text =
            argv[6];


    if (message_id == 0) {
        std::cerr
            << "message-id must not be zero\n";

        return 1;
    }


    std::cout
        << "========== TinyIMX "
           "Gateway Durable Replay Demo "
           "==========\n"
        << "gateway-b="
        << host
        << ':'
        << gateway_b_port
        << '\n'
        << "source_gateway="
        << source_gateway_id
        << '\n'
        << "message_id="
        << message_id
        << '\n'
        << "message_text="
        << message_text
        << '\n';


    tinyimx::ProtocolCodec
        user_codec;

    tinyimx::ProtocolCodec
        peer_codec;


    /*
     * 1. user10002登录Gateway B。
     */
    const int user_fd =
        ConnectToServer(
            host,
            gateway_b_port
        );


    if (user_fd < 0) {
        return 1;
    }


    if (
        !SendPacket(
            user_fd,
            user_codec,
            MakeLoginRequest(1)
        )
    ) {
        ::close(user_fd);

        return 1;
    }


    tinyimx::Packet login_response;


    if (
        !WaitForOnePacket(
            user_fd,
            user_codec,
            &login_response
        )
    ) {
        std::cerr
            << "login response timeout\n";

        ::close(user_fd);

        return 1;
    }


    PrintPacket(
        "[user10002 login]",
        login_response
    );


    if (
        !ValidateLoginResponse(
            login_response
        )
    ) {
        ::close(user_fd);

        return 1;
    }


    /*
     * 2. 建立Gateway Peer连接。
     */
    const int peer_fd =
        ConnectToServer(
            host,
            gateway_b_port
        );


    if (peer_fd < 0) {
        ::close(user_fd);

        return 1;
    }


    /*
     * 3. 第一次Replay。
     *
     * Gateway B刚重启时，
     * Memory Dedup应该为空。
     *
     * 因此这一次必须依赖MySQL
     * 识别message_id已经ReceiverConfirmed。
     */
    tinyimx::Packet first_replay;


    if (
        !BuildForwardPacket(
            source_gateway_id,
            source_lease_token,
            message_id,
            message_text,
            7201,
            &first_replay
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    std::cout
        << "\n[first durable replay]"
        << " seq=7201"
        << " message_id="
        << message_id
        << '\n';


    if (
        !SendPacket(
            peer_fd,
            peer_codec,
            first_replay
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    tinyimx::Packet first_response;


    if (
        !WaitForOnePacket(
            peer_fd,
            peer_codec,
            &first_response
        )
    ) {
        std::cerr
            << "first durable replay "
               "response timeout\n";

        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    PrintPacket(
        "[first durable replay response]",
        first_response
    );


    if (
        !ValidateDuplicateResponse(
            first_response,
            message_id
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    if (
        !ExpectNoPacket(
            user_fd,
            600
        )
    ) {
        std::cerr
            << "FAILED: durable replay "
               "caused user delivery\n";

        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    std::cout
        << "[PASS] first durable replay "
           "suppressed by persisted state\n";


    /*
     * 4. 第二次Replay。
     *
     * 第一轮持久化命中后，
     * Gateway B应已经把Memory Dedup
     * 恢复为本地Delivered去重状态。
     *
     * 所以第二轮应该走内存Fast Path。
     */
    tinyimx::Packet second_replay;


    if (
        !BuildForwardPacket(
            source_gateway_id,
            source_lease_token,
            message_id,
            message_text,
            7202,
            &second_replay
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    std::cout
        << "\n[second replay]"
        << " seq=7202"
        << " message_id="
        << message_id
        << '\n';


    if (
        !SendPacket(
            peer_fd,
            peer_codec,
            second_replay
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    tinyimx::Packet second_response;


    if (
        !WaitForOnePacket(
            peer_fd,
            peer_codec,
            &second_response
        )
    ) {
        std::cerr
            << "second replay response "
               "timeout\n";

        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    PrintPacket(
        "[second replay response]",
        second_response
    );


    if (
        !ValidateDuplicateResponse(
            second_response,
            message_id
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    if (
        !ExpectNoPacket(
            user_fd,
            600
        )
    ) {
        std::cerr
            << "FAILED: second replay "
               "caused user delivery\n";

        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    std::cout
        << "[PASS] second replay "
           "suppressed by memory fast path\n"
        << "[PASS] user10002 received "
           "no replayed chat_message\n"
        << "\nGateway durable replay "
           "validation passed\n"
        << "================================"
           "============================\n";


    ::close(peer_fd);
    ::close(user_fd);


    return 0;
}