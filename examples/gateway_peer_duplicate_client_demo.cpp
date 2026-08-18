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
#include <vector>


namespace {

using Json = nlohmann::json;


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


bool WaitForPackets(
    int fd,
    tinyimx::ProtocolCodec& codec,
    std::size_t minimum_count,
    std::vector<tinyimx::Packet>* packets,
    int timeout_ms = 3000
) {
    if (packets == nullptr) {
        return false;
    }


    packets->clear();

    tinyimx::Buffer input;


    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(
            timeout_ms
        );


    while (
        packets->size() <
        minimum_count
    ) {
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


        while (true) {
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
                break;
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


            for (
                const auto& packet :
                decoded.packets
            ) {
                packets->push_back(
                    packet
                );
            }


            /*
             * ProtocolCodec一次Decode已经会
             * 处理当前Buffer中的完整粘包。
             *
             * 剩余不足一包时下一轮会NeedMoreData。
             */
            if (
                input.ReadableBytes() == 0
            ) {
                break;
            }
        }
    }


    return true;
}


/*
 * 第二次重复RPC发送以后，
 * user10002在指定时间内绝对不能
 * 再收到一个packet。
 */
bool ExpectNoPacket(
    int fd,
    int timeout_ms
) {
    pollfd descriptor{};

    descriptor.fd =
        fd;

    descriptor.events =
        POLLIN;


    const int result =
        ::poll(
            &descriptor,
            1,
            timeout_ms
        );


    if (result == 0) {
        /*
         * timeout：
         * 说明没有第二次消息，
         * 这正是我们希望看到的。
         */
        return true;
    }


    if (result < 0) {
        if (errno == EINTR) {
            return ExpectNoPacket(
                fd,
                timeout_ms
            );
        }

        return false;
    }


    /*
     * socket可读：
     * 表示第二次又有数据到达。
     */
    return false;
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


bool BuildForwardPacket(
    const std::string& source_gateway_id,
    const std::string& lease_token,
    std::uint64_t message_id,
    std::uint32_t rpc_seq,
    tinyimx::Packet* packet
) {
    if (packet == nullptr) {
        return false;
    }


    tinyimx::
    GatewayForwardChatRequest request;


    request.message_id =
        message_id;

    request.source_gateway_id =
        source_gateway_id;

    request.source_lease_token =
        lease_token;

    request.from_user_id =
        10001;

    request.to_user_id =
        10002;

    request.message_body =
        Json{
            {"from", 10001},
            {"to", 10002},
            {"text", "duplicate-dedup-test"}
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


void PrintPacket(
    const std::string& prefix,
    const tinyimx::Packet& packet
) {
    std::cout
        << prefix
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


bool ValidateForwardResponse(
    const tinyimx::Packet& packet,
    std::uint64_t expected_message_id,
    bool expected_duplicate
) {
    if (
        packet.type !=
        tinyimx::MessageType::
            kGatewayForwardChatResponse
    ) {
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
            << "response not delivered, "
               "status="
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
        return false;
    }


    if (
        response.to_user_id !=
        10002
    ) {
        return false;
    }


    if (
        response.duplicate !=
        expected_duplicate
    ) {
        return false;
    }


    return true;
}

bool ValidateUnauthorizedResponse(
    const tinyimx::Packet& packet,
    std::uint64_t expected_message_id,
    std::uint32_t expected_seq
) {
    if (
        packet.type !=
        tinyimx::MessageType::
            kGatewayForwardChatResponse
    ) {
        std::cerr
            << "unauthorized response has "
               "unexpected packet type\n";

        return false;
    }


    if (packet.seq != expected_seq) {
        std::cerr
            << "unauthorized response seq "
               "mismatch"
            << ", expected="
            << expected_seq
            << ", actual="
            << packet.seq
            << '\n';

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
            << "deserialize unauthorized "
               "response failed: "
            << error
            << '\n';

        return false;
    }


    if (
        response.status !=
        tinyimx::
            GatewayForwardChatStatus::
                kUnauthorized
    ) {
        std::cerr
            << "expected unauthorized, actual="
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
            << "unauthorized response "
               "message_id mismatch"
            << ", expected="
            << expected_message_id
            << ", actual="
            << response.message_id
            << '\n';

        return false;
    }


    if (response.to_user_id != 10002) {
        std::cerr
            << "unauthorized response "
               "to_user_id mismatch\n";

        return false;
    }


    if (response.duplicate) {
        std::cerr
            << "unauthorized request must not "
               "be reported as duplicate\n";

        return false;
    }


    if (response.error_message.empty()) {
        std::cerr
            << "unauthorized response should "
               "contain error message\n";

        return false;
    }


    return true;
}

}  // namespace


int main(
    int argc,
    char* argv[]
) {
    if (argc < 5) {
        std::cerr
            << "usage:\n"
            << "  "
            << argv[0]
            << " <host>"
            << " <gateway-b-port>"
            << " <source-gateway-id>"
            << " <source-lease-token>\n";

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

    const std::string
        invalid_lease_token =
            source_lease_token +
            "-invalid";

    /*
     * 每次运行自动生成不同的业务message_id，
     * 避免上一次运行留下的进程内Dedup记录
     * 干扰本次实验。
     */
    const std::uint64_t message_id =
        static_cast<std::uint64_t>(
            std::chrono::
                duration_cast<
                    std::chrono::microseconds
                >(
                    std::chrono::
                        system_clock::now().
                            time_since_epoch()
                ).count()
        );


    std::cout
        << "========== TinyIMX "
           "Gateway Peer Duplicate "
           "Delivery Demo ==========\n"
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
        << '\n';


    tinyimx::ProtocolCodec
        user_codec;

    tinyimx::ProtocolCodec
        peer_codec;


    /*
     * 第一条连接：
     * 普通用户10002。
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


    std::vector<tinyimx::Packet>
        login_packets;


    if (
        !WaitForPackets(
            user_fd,
            user_codec,
            1,
            &login_packets
        )
    ) {
        std::cerr
            << "user login response timeout\n";

        ::close(user_fd);
        return 1;
    }


    PrintPacket(
        "[user10002 login]",
        login_packets.front()
    );


    if (
        login_packets.front().type !=
        tinyimx::MessageType::
            kLoginResponse
    ) {
        ::close(user_fd);
        return 1;
    }


    /*
     * 第二条连接：
     * Gateway Peer。
     *
     * 不执行普通用户Login。
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
    * ====================================
    * RPC 0：非法Gateway身份
    *
    * 关键：
    *
    * message_id与后续合法RPC完全相同，
    * 但这里故意使用错误Lease Token。
    *
    * 验证目标：
    *
    * Unauthorized请求绝对不能提前
    * 占用Deduplicator中的message_id。
    * ====================================
    */
    tinyimx::Packet
        unauthorized_request;


    if (
        !BuildForwardPacket(
            source_gateway_id,
            invalid_lease_token,
            message_id,
            7101,
            &unauthorized_request
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    std::cout
        << "\n[unauthorized rpc]"
        << " seq=7101"
        << " message_id="
        << message_id
        << '\n';


    if (
        !SendPacket(
            peer_fd,
            peer_codec,
            unauthorized_request
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    /*
    * Gateway B必须给Peer返回
    * Unauthorized Response。
    */
    std::vector<tinyimx::Packet>
        unauthorized_responses;


    if (
        !WaitForPackets(
            peer_fd,
            peer_codec,
            1,
            &unauthorized_responses
        )
    ) {
        std::cerr
            << "unauthorized rpc response "
            "timeout\n";

        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    PrintPacket(
        "[unauthorized rpc response]",
        unauthorized_responses.front()
    );


    if (
        !ValidateUnauthorizedResponse(
            unauthorized_responses.front(),
            message_id,
            7101
        )
    ) {
        std::cerr
            << "unauthorized rpc response "
            "invalid\n";

        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    /*
    * 非法RPC绝对不能产生客户端副作用。
    */
    if (
        !ExpectNoPacket(
            user_fd,
            400
        )
    ) {
        std::cerr
            << "FAILED: unauthorized RPC "
            "caused user delivery\n";

        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    std::cout
        << "[PASS] unauthorized RPC rejected"
        << " without user delivery\n";
    /*
     * ====================================
     * 第一次RPC
     * message_id相同
     * seq=7001
     * ====================================
     */
    tinyimx::Packet first_request;


    if (
        !BuildForwardPacket(
            source_gateway_id,
            source_lease_token,
            message_id,
            7001,
            &first_request
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);
        return 1;
    }


    std::cout
        << "\n[first rpc]"
        << " seq=7001"
        << " message_id="
        << message_id
        << '\n';


    if (
        !SendPacket(
            peer_fd,
            peer_codec,
            first_request
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);
        return 1;
    }


    /*
     * user10002必须收到第一次消息。
     */
    std::vector<tinyimx::Packet>
        first_user_packets;


    if (
        !WaitForPackets(
            user_fd,
            user_codec,
            1,
            &first_user_packets
        )
    ) {
        std::cerr
            << "first user delivery timeout\n";

        ::close(peer_fd);
        ::close(user_fd);
        return 1;
    }


    PrintPacket(
        "[user10002 first delivery]",
        first_user_packets.front()
    );


    if (
        first_user_packets.front().type !=
        tinyimx::MessageType::
            kChatMessage
    ) {
        std::cerr
            << "first delivery is not "
               "chat_message\n";

        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    /*
     * Peer收到第一次Response。
     */
    std::vector<tinyimx::Packet>
        first_responses;


    if (
        !WaitForPackets(
            peer_fd,
            peer_codec,
            1,
            &first_responses
        )
    ) {
        std::cerr
            << "first rpc response timeout\n";

        ::close(peer_fd);
        ::close(user_fd);
        return 1;
    }


    PrintPacket(
        "[first rpc response]",
        first_responses.front()
    );


    if (
        !ValidateForwardResponse(
            first_responses.front(),
            message_id,
            false
        )
    ) {
        std::cerr
            << "first rpc response invalid\n";

        ::close(peer_fd);
        ::close(user_fd);
        return 1;
    }


    /*
     * ====================================
     * 第二次RPC
     *
     * message_id完全不变
     * seq改成7002
     * ====================================
     */
    tinyimx::Packet second_request;


    if (
        !BuildForwardPacket(
            source_gateway_id,
            source_lease_token,
            message_id,
            7002,
            &second_request
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);
        return 1;
    }


    std::cout
        << "\n[duplicate rpc]"
        << " seq=7002"
        << " message_id="
        << message_id
        << '\n';


    if (
        !SendPacket(
            peer_fd,
            peer_codec,
            second_request
        )
    ) {
        ::close(peer_fd);
        ::close(user_fd);
        return 1;
    }


    /*
     * 第二个RPC仍然应该收到成功Response，
     * 但duplicate=true。
     */
    std::vector<tinyimx::Packet>
        second_responses;


    if (
        !WaitForPackets(
            peer_fd,
            peer_codec,
            1,
            &second_responses
        )
    ) {
        std::cerr
            << "duplicate rpc response "
               "timeout\n";

        ::close(peer_fd);
        ::close(user_fd);
        return 1;
    }


    PrintPacket(
        "[duplicate rpc response]",
        second_responses.front()
    );


    if (
        !ValidateForwardResponse(
            second_responses.front(),
            message_id,
            true
        )
    ) {
        std::cerr
            << "duplicate rpc response "
               "invalid\n";

        ::close(peer_fd);
        ::close(user_fd);
        return 1;
    }


    /*
     * 最关键的验收：
     *
     * 第二次RPC之后等待600ms，
     * user10002不能再收到第二条消息。
     */
    if (
        !ExpectNoPacket(
            user_fd,
            600
        )
    ) {
        std::cerr
            << "FAILED: duplicate RPC "
               "caused second user delivery\n";

        ::close(peer_fd);
        ::close(user_fd);

        return 1;
    }


    std::cout
        << "\n[PASS] first RPC delivered once\n"
        << "[PASS] duplicate RPC returned "
           "duplicate=true\n"
        << "[PASS] user10002 received no "
           "second chat_message\n"
        << "\nGateway peer duplicate "
           "delivery validation passed\n"
        << "================================"
           "============================\n";


    ::close(peer_fd);
    ::close(user_fd);


    return 0;
}