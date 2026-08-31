#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>

namespace {

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

bool SetSocketTimeout(int fd, int seconds) {
    timeval timeout{};
    timeout.tv_sec = seconds;

    return
        ::setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            static_cast<socklen_t>(sizeof(timeout))
        ) == 0 &&
        ::setsockopt(
            fd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            static_cast<socklen_t>(sizeof(timeout))
        ) == 0;
}

bool SetTcpNoDelay(int fd) {
    int enabled = 1;
    return ::setsockopt(
               fd,
               IPPROTO_TCP,
               TCP_NODELAY,
               &enabled,
               static_cast<socklen_t>(sizeof(enabled))
           ) == 0;
}

int ConnectToServer(const std::string& host, std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    SetSocketTimeout(fd, 5);
    SetTcpNoDelay(fd);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
        ::connect(
            fd,
            reinterpret_cast<sockaddr*>(&address),
            static_cast<socklen_t>(sizeof(address))
        ) != 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

bool SendAll(int fd, const std::string& data) {
    std::size_t offset = 0;

    while (offset < data.size()) {
        const ssize_t n = ::send(
            fd,
            data.data() + offset,
            data.size() - offset,
            MSG_NOSIGNAL
        );

        if (n > 0) {
            offset += static_cast<std::size_t>(n);
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
}

bool SendPacket(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    const tinyimx::Packet& packet
) {
    tinyimx::Buffer output;
    std::string error;

    if (!codec.Encode(packet, &output, &error)) {
        std::cerr << "encode failed: " << error << '\n';
        return false;
    }

    return SendAll(fd, output.RetrieveAllAsString());
}

tinyimx::Packet MakeLoginRequest(std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kLoginRequest;
    packet.seq = seq;
    packet.body = Json{
        {"username", "user10001"},
        {"password", "123456"}
    }.dump();
    return packet;
}

tinyimx::Packet MakeHistoryRequest(std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kHistoryRequest;
    packet.seq = seq;
    packet.body = Json{
        {"peer_user_id", 10002},
        {"before_message_id", 0},
        {"limit", 10}
    }.dump();
    return packet;
}

tinyimx::Packet MakeHeartbeatRequest(std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kHeartbeat;
    packet.seq = seq;
    packet.body = Json{{"ping", true}}.dump();
    return packet;
}

class PacketReader final {
public:
    explicit PacketReader(const tinyimx::ProtocolCodec& codec)
        : codec_(codec) {
    }

    bool ReadOne(
        int fd,
        tinyimx::Packet* output,
        std::chrono::milliseconds timeout
    ) {
        if (output == nullptr) {
            return false;
        }

        const auto deadline = Clock::now() + timeout;

        while (Clock::now() < deadline) {
            if (!pending_.empty()) {
                *output = std::move(pending_.front());
                pending_.pop_front();
                return true;
            }

            const auto decoded = codec_.Decode(&input_);
            if (decoded.status == tinyimx::DecodeStatus::kOk) {
                for (const auto& packet : decoded.packets) {
                    pending_.push_back(packet);
                }

                if (!pending_.empty()) {
                    continue;
                }
            } else if (decoded.status != tinyimx::DecodeStatus::kNeedMoreData) {
                std::cerr
                    << "decode failed: "
                    << tinyimx::DecodeStatusToString(decoded.status)
                    << ", error=" << decoded.error_message << '\n';
                return false;
            }

            const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - Clock::now()
            );
            if (remain.count() <= 0) {
                break;
            }

            pollfd descriptor{};
            descriptor.fd = fd;
            descriptor.events = POLLIN;

            int result = 0;
            do {
                result = ::poll(
                    &descriptor,
                    1,
                    static_cast<int>(remain.count())
                );
            } while (result < 0 && errno == EINTR);

            if (result <= 0) {
                return false;
            }

            if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                return false;
            }

            if (!(descriptor.revents & POLLIN)) {
                continue;
            }

            char temp[8192];
            const ssize_t n = ::recv(fd, temp, sizeof(temp), 0);
            if (n <= 0) {
                return false;
            }

            input_.Append(temp, static_cast<std::size_t>(n));
        }

        return false;
    }

private:
    const tinyimx::ProtocolCodec& codec_;
    tinyimx::Buffer input_;
    std::deque<tinyimx::Packet> pending_;
};

bool ValidateLogin(const tinyimx::Packet& packet) {
    if (packet.type != tinyimx::MessageType::kLoginResponse) {
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);
        return
            body.value("success", false) &&
            body.value("user_id", std::uint64_t{0}) == 10001;
    } catch (...) {
        return false;
    }
}

struct Observation {
    std::size_t history_overload_count{0};
    std::size_t history_success_count{0};
    bool heartbeat_seen{false};
    bool first_slow_history_seen{false};
    Clock::time_point heartbeat_received_at{};
    Clock::time_point first_slow_history_received_at{};
};

bool CollectOverloadResponses(
    int fd,
    PacketReader* reader,
    std::uint32_t heartbeat_seq,
    Clock::time_point heartbeat_sent_at,
    Observation* observation
) {
    if (reader == nullptr || observation == nullptr) {
        return false;
    }

    const auto deadline = Clock::now() + std::chrono::seconds(8);

    while (Clock::now() < deadline) {
        tinyimx::Packet packet;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - Clock::now()
        );

        if (!reader->ReadOne(fd, &packet, remaining)) {
            break;
        }

        const auto received_at = Clock::now();

        if (packet.type == tinyimx::MessageType::kHeartbeat &&
            packet.seq == heartbeat_seq) {
            observation->heartbeat_seen = true;
            observation->heartbeat_received_at = received_at;
        } else if (packet.type == tinyimx::MessageType::kHistoryResponse) {
            try {
                const Json body = Json::parse(packet.body);
                const std::string reason = body.value("reason", std::string{});

                if (reason == "business_runtime_overloaded") {
                    ++observation->history_overload_count;
                } else if (body.value("success", false)) {
                    ++observation->history_success_count;
                    if (!observation->first_slow_history_seen) {
                        observation->first_slow_history_seen = true;
                        observation->first_slow_history_received_at = received_at;
                    }
                }
            } catch (...) {
                std::cerr << "invalid history response json\n";
                return false;
            }
        }

        if (observation->heartbeat_seen &&
            observation->history_overload_count > 0 &&
            observation->first_slow_history_seen) {
            const auto heartbeat_latency =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    observation->heartbeat_received_at - heartbeat_sent_at
                );

            const bool heartbeat_before_history =
                observation->heartbeat_received_at <
                observation->first_slow_history_received_at;

            if (heartbeat_before_history && heartbeat_latency < std::chrono::milliseconds(400)) {
                return true;
            }
        }
    }

    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9001;

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        const int parsed = std::stoi(argv[2]);
        if (parsed <= 0 || parsed > 65535) {
            std::cerr << "invalid port\n";
            return 1;
        }
        port = static_cast<std::uint16_t>(parsed);
    }

    std::cout << "========== TinyIMX M13-C2 Gateway Runtime Overload Demo ==========" << '\n';

    const int fd = ConnectToServer(host, port);
    if (fd < 0) {
        std::cerr << "[FAIL] connect gateway failed\n";
        return 1;
    }

    tinyimx::ProtocolCodec codec;
    PacketReader reader(codec);

    constexpr std::uint32_t kLoginSeq = 1;
    if (!SendPacket(fd, codec, MakeLoginRequest(kLoginSeq))) {
        ::close(fd);
        return 1;
    }

    tinyimx::Packet login_response;
    if (!reader.ReadOne(fd, &login_response, std::chrono::seconds(5)) ||
        !ValidateLogin(login_response)) {
        std::cerr << "[FAIL] login failed\n";
        ::close(fd);
        return 1;
    }

    constexpr std::size_t kHistoryRequests = 12;
    constexpr std::uint32_t kFirstHistorySeq = 100;

    for (std::size_t i = 0; i < kHistoryRequests; ++i) {
        if (!SendPacket(
                fd,
                codec,
                MakeHistoryRequest(
                    kFirstHistorySeq + static_cast<std::uint32_t>(i)
                )
            )) {
            std::cerr << "[FAIL] send history burst failed\n";
            ::close(fd);
            return 1;
        }
    }

    constexpr std::uint32_t kHeartbeatSeq = 900;
    const auto heartbeat_sent_at = Clock::now();
    if (!SendPacket(fd, codec, MakeHeartbeatRequest(kHeartbeatSeq))) {
        std::cerr << "[FAIL] send heartbeat failed\n";
        ::close(fd);
        return 1;
    }

    Observation observation;
    const bool collected = CollectOverloadResponses(
        fd,
        &reader,
        kHeartbeatSeq,
        heartbeat_sent_at,
        &observation
    );

    ::close(fd);

    long long heartbeat_latency_ms = -1;
    bool heartbeat_before_history = false;

    if (observation.heartbeat_seen) {
        heartbeat_latency_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                observation.heartbeat_received_at - heartbeat_sent_at
            ).count();
    }

    if (observation.heartbeat_seen && observation.first_slow_history_seen) {
        heartbeat_before_history =
            observation.heartbeat_received_at <
            observation.first_slow_history_received_at;
    }

    const bool heartbeat_not_blocked =
        observation.heartbeat_seen &&
        heartbeat_before_history &&
        heartbeat_latency_ms >= 0 &&
        heartbeat_latency_ms < 400;

    const bool runtime_overload_observed =
        observation.history_overload_count > 0;

    std::cout
        << "history_overload_count=" << observation.history_overload_count << '\n'
        << "history_success_count=" << observation.history_success_count << '\n'
        << "heartbeat_latency_ms=" << heartbeat_latency_ms << '\n'
        << "heartbeat_before_slow_history=" << (heartbeat_before_history ? 1 : 0) << '\n'
        << "heartbeat_not_blocked=" << (heartbeat_not_blocked ? 1 : 0) << '\n'
        << "runtime_overload_observed=" << (runtime_overload_observed ? 1 : 0) << '\n';

    if (!collected || !runtime_overload_observed || !heartbeat_not_blocked) {
        std::cerr
            << "[FAIL] Gateway overload isolation validation failed"
            << ", collected=" << collected
            << ", overload=" << observation.history_overload_count
            << ", heartbeat_latency_ms=" << heartbeat_latency_ms
            << '\n';
        return 1;
    }

    std::cout
        << "[PASS] Gateway shed Business Runtime overload without blocking heartbeat\n"
        << "====================================================================\n";

    return 0;
}
