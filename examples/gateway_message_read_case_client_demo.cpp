#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace {

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kTestUserId = 10001;
constexpr std::uint64_t kPeerUserId = 10002;
constexpr const char* kTestUsername = "user10001";
constexpr const char* kTestPassword = "123456";

enum class ReadStatus {
    kPacket = 0,
    kTimeout,
    kClosed,
    kError,
};

class ScopedFd final {
public:
    ScopedFd() = default;
    explicit ScopedFd(int fd) : fd_(fd) {}
    ~ScopedFd() { Reset(); }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    ScopedFd(ScopedFd&& other) noexcept
        : fd_(std::exchange(other.fd_, -1)) {}

    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this != &other) {
            Reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    int Get() const noexcept { return fd_; }
    explicit operator bool() const noexcept { return fd_ >= 0; }

    void Reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_{-1};
};

struct PacketReader {
    tinyimx::Buffer input;
    std::deque<tinyimx::Packet> pending;
};

bool ConfigureSocket(int fd, int timeout_seconds) {
    timeval timeout{};
    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    if (::setsockopt(
            fd,
            SOL_SOCKET,
            SO_RCVTIMEO,
            &timeout,
            static_cast<socklen_t>(sizeof(timeout))) != 0) {
        return false;
    }

    if (::setsockopt(
            fd,
            SOL_SOCKET,
            SO_SNDTIMEO,
            &timeout,
            static_cast<socklen_t>(sizeof(timeout))) != 0) {
        return false;
    }

    int no_delay = 1;
    return ::setsockopt(
               fd,
               IPPROTO_TCP,
               TCP_NODELAY,
               &no_delay,
               static_cast<socklen_t>(sizeof(no_delay))) == 0;
}

ScopedFd ConnectToServer(
    const std::string& host,
    std::uint16_t port,
    int timeout_seconds = 8
) {
    ScopedFd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd) {
        return fd;
    }

    if (!ConfigureSocket(fd.Get(), timeout_seconds)) {
        fd.Reset();
        return fd;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (::inet_pton(
            AF_INET,
            host.c_str(),
            &address.sin_addr
        ) != 1) {
        fd.Reset();
        return fd;
    }

    if (::connect(
            fd.Get(),
            reinterpret_cast<sockaddr*>(&address),
            static_cast<socklen_t>(sizeof(address))) != 0) {
        fd.Reset();
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

ReadStatus ReadNextPacket(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    PacketReader* reader,
    tinyimx::Packet* packet
) {
    if (reader == nullptr || packet == nullptr) {
        return ReadStatus::kError;
    }

    if (!reader->pending.empty()) {
        *packet = std::move(reader->pending.front());
        reader->pending.pop_front();
        return ReadStatus::kPacket;
    }

    while (true) {
        char temporary[4096];
        const ssize_t n = ::recv(fd, temporary, sizeof(temporary), 0);

        if (n > 0) {
            reader->input.Append(temporary, static_cast<std::size_t>(n));
            const tinyimx::DecodeResult result = codec.Decode(&reader->input);

            if (result.status == tinyimx::DecodeStatus::kNeedMoreData) {
                continue;
            }
            if (result.status != tinyimx::DecodeStatus::kOk) {
                std::cerr
                    << "decode failed: "
                    << tinyimx::DecodeStatusToString(result.status)
                    << ", error=" << result.error_message << '\n';
                return ReadStatus::kError;
            }
            if (result.packets.empty()) {
                continue;
            }

            for (std::size_t i = 1; i < result.packets.size(); ++i) {
                reader->pending.push_back(result.packets[i]);
            }
            *packet = result.packets.front();
            return ReadStatus::kPacket;
        }

        if (n == 0) {
            return ReadStatus::kClosed;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return ReadStatus::kTimeout;
        }

        std::cerr << "recv failed: " << std::strerror(errno) << '\n';
        return ReadStatus::kError;
    }
}

void PrintPacket(const std::string& tag, const tinyimx::Packet& packet) {
    std::cout
        << tag
        << " packet: type=" << tinyimx::MessageTypeToString(packet.type)
        << " seq=" << packet.seq
        << " body=" << packet.body
        << std::endl;
}

tinyimx::Packet MakeLoginRequest(std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kLoginRequest;
    packet.seq = seq;
    packet.body = Json{
        {"username", kTestUsername},
        {"password", kTestPassword},
    }.dump();
    return packet;
}

tinyimx::Packet MakeHistoryRequest(std::uint32_t seq) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kHistoryRequest;
    packet.seq = seq;
    packet.body = Json{
        {"peer_user_id", kPeerUserId},
        {"before_message_id", 0},
        {"limit", 10},
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

bool ReadExpected(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    PacketReader* reader,
    tinyimx::MessageType expected_type,
    std::uint32_t expected_seq,
    tinyimx::Packet* output
) {
    while (true) {
        tinyimx::Packet packet;
        const ReadStatus status = ReadNextPacket(fd, codec, reader, &packet);
        if (status != ReadStatus::kPacket) {
            std::cerr
                << "expected " << tinyimx::MessageTypeToString(expected_type)
                << " seq=" << expected_seq
                << ", read_status=" << static_cast<int>(status) << '\n';
            return false;
        }

        PrintPacket("[recv]", packet);
        if (packet.type == expected_type && packet.seq == expected_seq) {
            if (output != nullptr) {
                *output = std::move(packet);
            }
            return true;
        }
    }
}

bool Login(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    PacketReader* reader,
    std::uint32_t seq
) {
    if (!SendPacket(fd, codec, MakeLoginRequest(seq))) {
        return false;
    }

    tinyimx::Packet response;
    if (!ReadExpected(
            fd,
            codec,
            reader,
            tinyimx::MessageType::kLoginResponse,
            seq,
            &response)) {
        return false;
    }

    try {
        const Json body = Json::parse(response.body);
        return body.value("success", false) &&
               body.value("user_id", 0ULL) == kTestUserId &&
               body.value("username", "") == kTestUsername;
    } catch (...) {
        return false;
    }
}

bool ValidateFailure(
    const tinyimx::Packet& packet,
    const std::string& expected_reason
) {
    if (packet.type != tinyimx::MessageType::kHistoryResponse) {
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);
        if (body.value("success", true)) {
            return false;
        }
        if (body.value("reason", "") != expected_reason) {
            std::cerr
                << "history failure reason mismatch, expected="
                << expected_reason << ", body=" << packet.body << '\n';
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool ValidateHeartbeat(const tinyimx::Packet& packet) {
    if (packet.type != tinyimx::MessageType::kHeartbeat) {
        return false;
    }
    try {
        const Json body = Json::parse(packet.body);
        return body.value("pong", false);
    } catch (...) {
        return false;
    }
}

bool WaitForGateFile(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    const auto deadline = Clock::now() + std::chrono::seconds(12);
    while (Clock::now() < deadline) {
        std::error_code error;
        if (std::filesystem::exists(path, error) && !error) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

bool RunUnavailable(
    const std::string& host,
    std::uint16_t port,
    const std::string& gate_file
) {
    tinyimx::ProtocolCodec codec;
    ScopedFd fd = ConnectToServer(host, port);
    if (!fd) {
        std::cerr << "connect failed\n";
        return false;
    }

    PacketReader reader;
    if (!Login(fd.Get(), codec, &reader, 10)) {
        std::cerr << "login failed\n";
        return false;
    }

    std::cout << "MESSAGE_READ_READY" << std::endl;
    if (!WaitForGateFile(gate_file)) {
        std::cerr << "gate file timeout: " << gate_file << '\n';
        return false;
    }

    if (!SendPacket(fd.Get(), codec, MakeHistoryRequest(11))) {
        return false;
    }

    tinyimx::Packet response;
    if (!ReadExpected(
            fd.Get(),
            codec,
            &reader,
            tinyimx::MessageType::kHistoryResponse,
            11,
            &response)) {
        return false;
    }

    if (!ValidateFailure(response, "message_service_unavailable")) {
        return false;
    }

    if (!SendPacket(fd.Get(), codec, MakeHeartbeatRequest(12))) {
        return false;
    }
    tinyimx::Packet heartbeat;
    if (!ReadExpected(
            fd.Get(), codec, &reader,
            tinyimx::MessageType::kHeartbeat, 12, &heartbeat) ||
        !ValidateHeartbeat(heartbeat)) {
        return false;
    }

    std::cout << "gateway message read unavailable validation passed\n";
    return true;
}

bool RunTimeout(const std::string& host, std::uint16_t port) {
    tinyimx::ProtocolCodec codec;
    ScopedFd fd = ConnectToServer(host, port, 8);
    if (!fd) {
        return false;
    }

    PacketReader reader;
    if (!Login(fd.Get(), codec, &reader, 20)) {
        return false;
    }

    const auto started = Clock::now();
    if (!SendPacket(fd.Get(), codec, MakeHistoryRequest(21)) ||
        !SendPacket(fd.Get(), codec, MakeHeartbeatRequest(22))) {
        return false;
    }

    bool saw_heartbeat = false;
    bool saw_history = false;
    std::int64_t heartbeat_latency_ms = -1;
    std::int64_t history_latency_ms = -1;

    while (!saw_heartbeat || !saw_history) {
        tinyimx::Packet packet;
        const ReadStatus status =
            ReadNextPacket(fd.Get(), codec, &reader, &packet);
        if (status != ReadStatus::kPacket) {
            std::cerr << "timeout case read failed status="
                      << static_cast<int>(status) << '\n';
            return false;
        }

        PrintPacket("[timeout]", packet);
        const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - started
        ).count();

        if (packet.type == tinyimx::MessageType::kHeartbeat &&
            packet.seq == 22) {
            if (!ValidateHeartbeat(packet)) {
                return false;
            }
            saw_heartbeat = true;
            heartbeat_latency_ms = latency;
            continue;
        }

        if (packet.type == tinyimx::MessageType::kHistoryResponse &&
            packet.seq == 21) {
            if (!ValidateFailure(packet, "message_service_timeout")) {
                return false;
            }
            saw_history = true;
            history_latency_ms = latency;
        }
    }

    std::cout << "heartbeat_latency_ms=" << heartbeat_latency_ms << '\n';
    std::cout << "history_latency_ms=" << history_latency_ms << '\n';

    if (heartbeat_latency_ms < 0 || heartbeat_latency_ms >= 1000) {
        std::cerr << "heartbeat should remain Reactor-fast\n";
        return false;
    }
    if (history_latency_ms < 2000 || history_latency_ms > 4500) {
        std::cerr << "history timeout should follow M13 E2E budget\n";
        return false;
    }
    if (heartbeat_latency_ms >= history_latency_ms) {
        std::cerr << "heartbeat should arrive before slow history completion\n";
        return false;
    }

    std::cout << "gateway message read deadline/isolation validation passed\n";
    return true;
}

bool RunStale(const std::string& host, std::uint16_t port) {
    tinyimx::ProtocolCodec codec;

    ScopedFd first = ConnectToServer(host, port, 6);
    if (!first) {
        return false;
    }
    PacketReader first_reader;
    if (!Login(first.Get(), codec, &first_reader, 30)) {
        return false;
    }

    if (!SendPacket(first.Get(), codec, MakeHistoryRequest(31))) {
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    ScopedFd second = ConnectToServer(host, port, 6);
    if (!second) {
        return false;
    }
    PacketReader second_reader;
    if (!Login(second.Get(), codec, &second_reader, 32)) {
        return false;
    }

    // The old connection must be fenced/replaced, and in particular it must
    // never receive the stale History completion for seq=31.
    bool saw_replaced = false;
    const auto old_deadline = Clock::now() + std::chrono::seconds(4);
    while (Clock::now() < old_deadline) {
        tinyimx::Packet packet;
        const ReadStatus status =
            ReadNextPacket(first.Get(), codec, &first_reader, &packet);

        if (status == ReadStatus::kClosed || status == ReadStatus::kTimeout) {
            break;
        }
        if (status == ReadStatus::kError) {
            return false;
        }

        PrintPacket("[stale-first]", packet);
        if (packet.type == tinyimx::MessageType::kHistoryResponse &&
            packet.seq == 31) {
            std::cerr << "stale Session received History completion\n";
            return false;
        }

        if (packet.type == tinyimx::MessageType::kError) {
            try {
                const Json body = Json::parse(packet.body);
                if (body.value("reason", "") == "login_replaced") {
                    saw_replaced = true;
                }
            } catch (...) {
                return false;
            }
        }
    }

    if (!saw_replaced) {
        std::cerr << "old connection did not observe login_replaced\n";
        return false;
    }

    if (!SendPacket(second.Get(), codec, MakeHeartbeatRequest(33))) {
        return false;
    }
    tinyimx::Packet heartbeat;
    if (!ReadExpected(
            second.Get(), codec, &second_reader,
            tinyimx::MessageType::kHeartbeat, 33, &heartbeat) ||
        !ValidateHeartbeat(heartbeat)) {
        return false;
    }

    std::cout << "gateway message read stale-session fence validation passed\n";
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr
            << "usage: " << argv[0]
            << " <host> <port> <unavailable|timeout|stale> [gate_file]\n";
        return 2;
    }

    const std::string host = argv[1];
    const auto port = static_cast<std::uint16_t>(std::stoul(argv[2]));
    const std::string mode = argv[3];

    bool ok = false;
    if (mode == "unavailable") {
        if (argc < 5) {
            std::cerr << "unavailable mode requires gate_file\n";
            return 2;
        }
        ok = RunUnavailable(host, port, argv[4]);
    } else if (mode == "timeout") {
        ok = RunTimeout(host, port);
    } else if (mode == "stale") {
        ok = RunStale(host, port);
    } else {
        std::cerr << "unknown mode: " << mode << '\n';
        return 2;
    }

    return ok ? 0 : 1;
}
