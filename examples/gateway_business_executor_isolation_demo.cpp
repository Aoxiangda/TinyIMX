#include "common/net/Buffer.h"
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
#include <chrono>

namespace {

using Json = nlohmann::json;

using Clock =
    std::chrono::steady_clock;


struct IsolationObservation {
    bool heartbeat_seen{false};
    bool history_seen{false};

    tinyimx::Packet
        heartbeat_packet;

    tinyimx::Packet
        history_packet;

    Clock::time_point
        heartbeat_received_at{};

    Clock::time_point
        history_received_at{};
};


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

int ConnectToServer(const std::string& host, std::uint16_t port) {
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

void PrintPacket(const std::string& tag,
                 const tinyimx::Packet& packet) {
    std::cout << tag
              << " packet:"
              << " type=" << tinyimx::MessageTypeToString(packet.type)
              << " seq=" << packet.seq
              << " body=" << packet.body
              << '\n';
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

bool WaitForOnePacket(int fd,
                      const tinyimx::ProtocolCodec& codec,
                      tinyimx::Packet* packet) {
    if (packet == nullptr) {
        return false;
    }

    tinyimx::Buffer input_buffer;

    while (true) {
        char temp[4096];

        const ssize_t n = ::recv(fd, temp, sizeof(temp), 0);

        if (n > 0) {
            input_buffer.Append(temp, static_cast<std::size_t>(n));

            const tinyimx::DecodeResult result =
                codec.Decode(&input_buffer);

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

            if (result.packets.empty()) {
                continue;
            }

            *packet = result.packets.front();
            return true;
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
}


bool WaitForIsolationResponses(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    std::uint32_t heartbeat_seq,
    std::uint32_t history_seq,
    IsolationObservation* observation
) {
    if (observation == nullptr) {
        return false;
    }


    tinyimx::Buffer input_buffer;


    while (
        !observation->heartbeat_seen ||
        !observation->history_seen
    ) {
        char temp[4096];


        const ssize_t n =
            ::recv(
                fd,
                temp,
                sizeof(temp),
                0
            );


        if (n > 0) {
            input_buffer.Append(
                temp,
                static_cast<
                    std::size_t
                >(n)
            );


            const tinyimx::DecodeResult
                result =
                    codec.Decode(
                        &input_buffer
                    );


            if (
                result.status ==
                tinyimx::
                    DecodeStatus::
                        kNeedMoreData
            ) {
                continue;
            }


            if (
                result.status !=
                tinyimx::
                    DecodeStatus::kOk
            ) {
                std::cerr
                    << "decode isolation "
                       "responses failed"
                    << ", status="
                    << tinyimx::
                        DecodeStatusToString(
                            result.status
                        )
                    << ", error="
                    << result.
                        error_message
                    << '\n';

                return false;
            }


            const auto received_at =
                Clock::now();


            for (
                const auto& packet :
                result.packets
            ) {
                if (
                    packet.type ==
                        tinyimx::
                            MessageType::
                                kHeartbeat &&
                    packet.seq ==
                        heartbeat_seq &&
                    !observation->
                        heartbeat_seen
                ) {
                    observation->
                        heartbeat_seen =
                            true;

                    observation->
                        heartbeat_packet =
                            packet;

                    observation->
                        heartbeat_received_at =
                            received_at;

                    continue;
                }


                if (
                    packet.type ==
                        tinyimx::
                            MessageType::
                                kHistoryResponse &&
                    packet.seq ==
                        history_seq &&
                    !observation->
                        history_seen
                ) {
                    observation->
                        history_seen =
                            true;

                    observation->
                        history_packet =
                            packet;

                    observation->
                        history_received_at =
                            received_at;

                    continue;
                }


                PrintPacket(
                    "[isolation-extra]",
                    packet
                );
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
            << "recv isolation response failed"
            << ", error="
            << std::strerror(errno)
            << '\n';

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

tinyimx::Packet MakeHistoryRequest(std::uint64_t peer_user_id,
                                    std::uint64_t before_message_id,
                                    std::size_t limit,
                                    std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kHistoryRequest;
    packet.seq = seq;
    packet.body =
        Json{
            {"peer_user_id", peer_user_id},
            {"before_message_id", before_message_id},
            {"limit", limit}
        }.dump();

    return packet;
}

tinyimx::Packet MakeHeartbeatRequest(
    std::uint32_t seq
) {
    tinyimx::Packet packet;

    packet.type =
        tinyimx::MessageType::
            kHeartbeat;

    packet.seq =
        seq;

    packet.body =
        Json{
            {"ping", true}
        }.dump();

    return packet;
}


bool ValidateLoginSuccess(const tinyimx::Packet& packet,
                          std::uint64_t expected_user_id,
                          const std::string& expected_username) {
    if (packet.type != tinyimx::MessageType::kLoginResponse) {
        std::cerr << "expected login_response, got "
                  << tinyimx::MessageTypeToString(packet.type)
                  << '\n';
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        if (!body.value("success", false)) {
            std::cerr << "login failed unexpectedly, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("user_id", 0ULL) != expected_user_id) {
            std::cerr << "user_id mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("username", "") != expected_username) {
            std::cerr << "username mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "parse login response failed: "
                  << e.what() << '\n';
        return false;
    }
}


bool ValidateHeartbeatResponse(
    const tinyimx::Packet& packet,
    std::uint32_t expected_seq
) {
    if (
        packet.type !=
        tinyimx::MessageType::
            kHeartbeat
    ) {
        std::cerr
            << "expected heartbeat response"
            << ", actual="
            << tinyimx::
                MessageTypeToString(
                    packet.type
                )
            << '\n';

        return false;
    }


    if (
        packet.seq !=
        expected_seq
    ) {
        std::cerr
            << "heartbeat seq mismatch"
            << ", expected="
            << expected_seq
            << ", actual="
            << packet.seq
            << '\n';

        return false;
    }


    try {
        const Json body =
            Json::parse(
                packet.body
            );


        if (
            !body.value(
                "pong",
                false
            )
        ) {
            std::cerr
                << "heartbeat pong missing"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }
    }
    catch (
        const std::exception& e
    ) {
        std::cerr
            << "parse heartbeat response "
               "failed: "
            << e.what()
            << '\n';

        return false;
    }


    return true;
}


bool ValidateHistorySuccess(const tinyimx::Packet& packet,
                            std::uint64_t expected_user_id,
                            std::uint64_t expected_peer_user_id,
                            std::uint64_t expected_before_message_id,
                            std::size_t expected_limit,
                            Json* messages_out,
                            bool* has_more_out) {
    if (packet.type != tinyimx::MessageType::kHistoryResponse) {
        std::cerr << "expected history_response, got "
                  << tinyimx::MessageTypeToString(packet.type)
                  << '\n';
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        if (!body.value("success", false)) {
            std::cerr << "expected history success=true, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("user_id", 0ULL) != expected_user_id) {
            std::cerr << "user_id mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("peer_user_id", 0ULL) != expected_peer_user_id) {
            std::cerr << "peer_user_id mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("before_message_id", 0ULL) !=
            expected_before_message_id) {
            std::cerr << "before_message_id mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("limit", 0ULL) != expected_limit) {
            std::cerr << "limit mismatch, body="
                      << packet.body << '\n';
            return false;
        }

        if (!body.contains("messages") || !body.at("messages").is_array()) {
            std::cerr << "messages should be array, body="
                      << packet.body << '\n';
            return false;
        }

        if (messages_out != nullptr) {
            *messages_out = body.at("messages");
        }

        if (has_more_out != nullptr) {
            *has_more_out = body.value("has_more", false);
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "parse history response failed: "
                  << e.what() << '\n';
        return false;
    }
}

bool ValidateHistoryRejected(const tinyimx::Packet& packet,
                             const std::string& expected_reason) {
    if (packet.type != tinyimx::MessageType::kHistoryResponse) {
        std::cerr << "expected history_response, got "
                  << tinyimx::MessageTypeToString(packet.type)
                  << '\n';
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);

        if (body.value("success", true)) {
            std::cerr << "expected history success=false, body="
                      << packet.body << '\n';
            return false;
        }

        if (body.value("reason", "") != expected_reason) {
            std::cerr << "reason mismatch, expected="
                      << expected_reason
                      << ", body=" << packet.body << '\n';
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "parse rejected history response failed: "
                  << e.what() << '\n';
        return false;
    }
}

bool IsAscendingByMessageId(const Json& messages) {
    std::uint64_t previous_id = 0;

    for (const auto& message : messages) {
        const std::uint64_t current_id =
            message.value("message_id", 0ULL);

        if (current_id == 0) {
            return false;
        }

        if (previous_id != 0 && current_id <= previous_id) {
            return false;
        }

        previous_id = current_id;
    }

    return true;
}

bool ContainsMessageId(const Json& messages,
                       std::uint64_t message_id) {
    for (const auto& message : messages) {
        if (message.value("message_id", 0ULL) == message_id) {
            return true;
        }
    }

    return false;
}

bool AllMessagesLessThan(const Json& messages,
                         std::uint64_t boundary_message_id) {
    for (const auto& message : messages) {
        const std::uint64_t message_id =
            message.value("message_id", 0ULL);

        if (message_id >= boundary_message_id) {
            return false;
        }
    }

    return true;
}

std::uint64_t OldestMessageId(const Json& messages) {
    if (messages.empty()) {
        return 0;
    }

    return messages.front().value("message_id", 0ULL);
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
        port =
            static_cast<
                std::uint16_t
            >(
                std::stoi(
                    argv[2]
                )
            );
    }


    std::cout
        << "========== "
           "M13 Business Executor "
           "Isolation Demo ==========\n";


    const int fd =
        ConnectToServer(
            host,
            port
        );


    if (fd < 0) {
        std::cerr
            << "connect failed\n";

        return 1;
    }


    tinyimx::ProtocolCodec codec;


    /*
     * ------------------------------------------------------------
     * 1. Login
     * ------------------------------------------------------------
     */
    if (
        !SendPacket(
            fd,
            codec,
            MakeLoginRequest(
                "user10001",
                "123456",
                1
            )
        )
    ) {
        ::close(fd);
        return 1;
    }


    tinyimx::Packet login_response;


    if (
        !WaitForOnePacket(
            fd,
            codec,
            &login_response
        ) ||
        !ValidateLoginSuccess(
            login_response,
            10001,
            "user10001"
        )
    ) {
        ::close(fd);
        return 1;
    }


    PrintPacket(
        "[isolation]",
        login_response
    );


    constexpr std::uint32_t
        kHistorySeq = 2;

    constexpr std::uint32_t
        kHeartbeatSeq = 3;


    /*
     * ------------------------------------------------------------
     * 2. 同一个Connection连续发送：
     *
     * History → Heartbeat
     *
     * History Worker会被Gateway故障注入阻塞500ms。
     * ------------------------------------------------------------
     */
    const auto started_at =
        Clock::now();


    if (
        !SendPacket(
            fd,
            codec,
            MakeHistoryRequest(
                10002,
                0,
                10,
                kHistorySeq
            )
        )
    ) {
        ::close(fd);
        return 1;
    }


    if (
        !SendPacket(
            fd,
            codec,
            MakeHeartbeatRequest(
                kHeartbeatSeq
            )
        )
    ) {
        ::close(fd);
        return 1;
    }


    IsolationObservation
        observation;


    if (
        !WaitForIsolationResponses(
            fd,
            codec,
            kHeartbeatSeq,
            kHistorySeq,
            &observation
        )
    ) {
        ::close(fd);
        return 1;
    }


    ::close(fd);


    /*
     * ------------------------------------------------------------
     * 3. Functional validation
     * ------------------------------------------------------------
     */
    if (
        !ValidateHeartbeatResponse(
            observation.
                heartbeat_packet,
            kHeartbeatSeq
        )
    ) {
        return 1;
    }


    Json messages;
    bool has_more = false;


    if (
        !ValidateHistorySuccess(
            observation.
                history_packet,
            10001,
            10002,
            0,
            10,
            &messages,
            &has_more
        )
    ) {
        return 1;
    }


    const auto heartbeat_latency =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(
            observation.
                heartbeat_received_at -
            started_at
        );


    const auto history_latency =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(
            observation.
                history_received_at -
            started_at
        );


    std::cout
        << "heartbeat_latency_ms="
        << heartbeat_latency.count()
        << '\n';

    std::cout
        << "history_latency_ms="
        << history_latency.count()
        << '\n';


    /*
     * ------------------------------------------------------------
     * 4. M13 isolation acceptance
     * ------------------------------------------------------------
     */
    const bool
        heartbeat_before_history =
            observation.
                heartbeat_received_at <
            observation.
                history_received_at;


    const bool
        heartbeat_not_blocked =
            heartbeat_latency <
            std::chrono::
                milliseconds(250);


    /*
     * 注入500ms时，
     * History不应该提前完成。
     *
     * 留50ms容差用于Debug/虚拟机测试环境。
     */
    const bool
        history_was_slow =
            history_latency >=
            std::chrono::
                milliseconds(450);


    std::cout
        << "heartbeat_before_history="
        << heartbeat_before_history
        << '\n';

    std::cout
        << "heartbeat_not_blocked="
        << heartbeat_not_blocked
        << '\n';

    std::cout
        << "history_was_slow="
        << history_was_slow
        << '\n';


    if (
        !heartbeat_before_history ||
        !heartbeat_not_blocked ||
        !history_was_slow
    ) {
        std::cerr
            << "[FAIL] M13 business executor "
               "did not isolate slow history "
               "work from same-connection "
               "heartbeat\n";

        return 1;
    }


    std::cout
        << "[PASS] M13 slow history business "
           "work did not block same-connection "
           "heartbeat\n";

    std::cout
        << "==========================================\n";


    return 0;
}