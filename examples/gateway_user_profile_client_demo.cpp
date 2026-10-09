#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace {

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kTestUserId = 10001;
constexpr const char* kTestUsername = "user10001";
constexpr const char* kTestPassword = "123456";

class ScopedFd {
public:
    explicit ScopedFd(int fd = -1) : fd_(fd) {}

    ~ScopedFd() {
        Reset();
    }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    ScopedFd(ScopedFd&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this != &other) {
            Reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int Get() const noexcept { return fd_; }
    [[nodiscard]] bool Valid() const noexcept { return fd_ >= 0; }

    void Reset(int new_fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = new_fd;
    }

private:
    int fd_{-1};
};

struct PacketReader {
    tinyimx::Buffer input;
    std::deque<tinyimx::Packet> pending;
};

enum class ReadStatus {
    kPacket,
    kClosed,
    kTimeout,
    kError
};

bool SetSocketTimeout(int fd, int seconds) {
    timeval timeout {};
    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

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
    int value = 1;
    return ::setsockopt(
               fd,
               IPPROTO_TCP,
               TCP_NODELAY,
               &value,
               static_cast<socklen_t>(sizeof(value))
           ) == 0;
}

ScopedFd ConnectToServer(
    const std::string& host,
    std::uint16_t port
) {
    ScopedFd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd.Valid()) {
        return fd;
    }

    if (!SetSocketTimeout(fd.Get(), 6) ||
        !SetTcpNoDelay(fd.Get())) {
        fd.Reset();
        return fd;
    }

    sockaddr_in address {};
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
            static_cast<socklen_t>(sizeof(address))
        ) != 0) {
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
            reader->input.Append(
                temporary,
                static_cast<std::size_t>(n)
            );

            const tinyimx::DecodeResult result =
                codec.Decode(&reader->input);

            if (result.status ==
                tinyimx::DecodeStatus::kNeedMoreData) {
                continue;
            }

            if (result.status != tinyimx::DecodeStatus::kOk) {
                std::cerr
                    << "decode failed: "
                    << tinyimx::DecodeStatusToString(result.status)
                    << ", error=" << result.error_message
                    << '\n';
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

        std::cerr
            << "recv failed: "
            << std::strerror(errno)
            << '\n';
        return ReadStatus::kError;
    }
}

void PrintPacket(
    const std::string& tag,
    const tinyimx::Packet& packet
) {
    std::cout
        << tag
        << " packet: type="
        << tinyimx::MessageTypeToString(packet.type)
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
        {"password", kTestPassword}
    }.dump();
    return packet;
}

tinyimx::Packet MakeProfileRequest(
    std::uint32_t seq,
    Json body = Json::object()
) {
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kUserProfileRequest;
    packet.seq = seq;
    packet.body = body.dump();
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
        const ReadStatus status =
            ReadNextPacket(fd, codec, reader, &packet);

        if (status != ReadStatus::kPacket) {
            std::cerr
                << "expected "
                << tinyimx::MessageTypeToString(expected_type)
                << " seq=" << expected_seq
                << ", read_status="
                << static_cast<int>(status)
                << '\n';
            return false;
        }

        PrintPacket("[recv]", packet);

        if (packet.type == expected_type &&
            packet.seq == expected_seq) {
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
    std::uint32_t seq,
    std::uint64_t* user_id,
    std::string* username
) {
    if (!SendPacket(fd, codec, MakeLoginRequest(seq))) {
        std::cerr << "send login failed\n";
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
        if (!body.value("success", false)) {
            std::cerr << "login failed: " << response.body << '\n';
            return false;
        }

        const std::uint64_t actual_user_id =
            body.value("user_id", 0ULL);
        const std::string actual_username =
            body.value("username", "");

        if (actual_user_id != kTestUserId ||
            actual_username != kTestUsername) {
            std::cerr << "unexpected login identity: "
                      << response.body << '\n';
            return false;
        }

        if (user_id != nullptr) {
            *user_id = actual_user_id;
        }
        if (username != nullptr) {
            *username = actual_username;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr
            << "parse login response failed: "
            << e.what() << '\n';
        return false;
    }
}

bool ValidateProfileFailure(
    const tinyimx::Packet& packet,
    const std::string& expected_reason
) {
    if (packet.type != tinyimx::MessageType::kUserProfileResponse) {
        std::cerr << "expected user_profile_response\n";
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);
        if (body.value("success", true)) {
            std::cerr
                << "profile unexpectedly succeeded: "
                << packet.body << '\n';
            return false;
        }
        if (body.value("reason", "") != expected_reason) {
            std::cerr
                << "profile reason mismatch, expected="
                << expected_reason
                << ", body=" << packet.body
                << '\n';
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr
            << "parse profile failure failed: "
            << e.what() << '\n';
        return false;
    }
}

bool ValidateProfileSuccess(
    const tinyimx::Packet& packet,
    std::uint64_t expected_user_id,
    const std::string& expected_username
) {
    if (packet.type != tinyimx::MessageType::kUserProfileResponse) {
        std::cerr << "expected user_profile_response\n";
        return false;
    }

    try {
        const Json body = Json::parse(packet.body);
        if (!body.value("success", false) ||
            !body.contains("profile") ||
            !body.at("profile").is_object()) {
            std::cerr
                << "invalid profile success body: "
                << packet.body << '\n';
            return false;
        }

        const Json& profile = body.at("profile");
        if (profile.value("user_id", 0ULL) != expected_user_id ||
            profile.value("username", "") != expected_username) {
            std::cerr
                << "profile identity mismatch: "
                << packet.body << '\n';
            return false;
        }

        for (const char* field : {
                 "nickname",
                 "avatar_url",
                 "user_status"
             }) {
            if (!profile.contains(field)) {
                std::cerr
                    << "profile missing field=" << field
                    << ", body=" << packet.body << '\n';
                return false;
            }
        }

        for (const char* forbidden : {
                 "password",
                 "password_hash",
                 "password_salt",
                 "online",
                 "gateway_id",
                 "session_epoch",
                 "route",
                 "remote_address"
             }) {
            if (profile.contains(forbidden)) {
                std::cerr
                    << "profile leaked forbidden field="
                    << forbidden
                    << ", body=" << packet.body
                    << '\n';
                return false;
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr
            << "parse profile success failed: "
            << e.what() << '\n';
        return false;
    }
}

bool RunHappy(const std::string& host, std::uint16_t port) {
    tinyimx::ProtocolCodec codec;

    // Case A: an unauthenticated connection must never reach UserService.
    {
        ScopedFd fd = ConnectToServer(host, port);
        if (!fd.Valid()) {
            std::cerr << "not-logged-in connect failed\n";
            return false;
        }
        PacketReader reader;
        constexpr std::uint32_t kSeq = 1;
        if (!SendPacket(fd.Get(), codec, MakeProfileRequest(kSeq))) {
            return false;
        }
        tinyimx::Packet response;
        if (!ReadExpected(
                fd.Get(), codec, &reader,
                tinyimx::MessageType::kUserProfileResponse,
                kSeq, &response) ||
            !ValidateProfileFailure(response, "not_logged_in")) {
            return false;
        }
    }

    ScopedFd fd = ConnectToServer(host, port);
    if (!fd.Valid()) {
        std::cerr << "authenticated connect failed\n";
        return false;
    }

    PacketReader reader;
    std::uint64_t user_id = 0;
    std::string username;
    if (!Login(fd.Get(), codec, &reader, 10, &user_id, &username)) {
        return false;
    }

    // Case B: the real authenticated self-profile vertical slice.
    constexpr std::uint32_t kProfileSeq = 11;
    if (!SendPacket(fd.Get(), codec, MakeProfileRequest(kProfileSeq))) {
        return false;
    }
    tinyimx::Packet profile_response;
    if (!ReadExpected(
            fd.Get(), codec, &reader,
            tinyimx::MessageType::kUserProfileResponse,
            kProfileSeq, &profile_response) ||
        !ValidateProfileSuccess(profile_response, user_id, username)) {
        return false;
    }

    // Case C: a client cannot spoof/choose another actor through JSON.
    constexpr std::uint32_t kSpoofSeq = 12;
    if (!SendPacket(
            fd.Get(), codec,
            MakeProfileRequest(
                kSpoofSeq,
                Json{{"user_id", user_id + 1}}
            ))) {
        return false;
    }
    tinyimx::Packet spoof_response;
    if (!ReadExpected(
            fd.Get(), codec, &reader,
            tinyimx::MessageType::kUserProfileResponse,
            kSpoofSeq, &spoof_response) ||
        !ValidateProfileFailure(
            spoof_response,
            "target_user_id_not_supported")) {
        return false;
    }

    std::cout
        << "gateway user profile happy-path validation passed"
        << std::endl;
    return true;
}

bool RunUnavailable(
    const std::string& host,
    std::uint16_t port,
    int pre_profile_delay_ms
) {
    tinyimx::ProtocolCodec codec;
    ScopedFd fd = ConnectToServer(host, port);
    if (!fd.Valid()) {
        return false;
    }
    PacketReader reader;
    if (!Login(fd.Get(), codec, &reader, 20, nullptr, nullptr)) {
        return false;
    }

    std::cout << "PROFILE_READY" << std::endl;
    std::this_thread::sleep_for(
        std::chrono::milliseconds(pre_profile_delay_ms)
    );

    constexpr std::uint32_t kSeq = 21;
    if (!SendPacket(fd.Get(), codec, MakeProfileRequest(kSeq))) {
        return false;
    }
    tinyimx::Packet response;
    if (!ReadExpected(
            fd.Get(), codec, &reader,
            tinyimx::MessageType::kUserProfileResponse,
            kSeq, &response) ||
        !ValidateProfileFailure(response, "profile_unavailable")) {
        return false;
    }

    std::cout
        << "gateway user profile unavailable validation passed"
        << std::endl;
    return true;
}

bool RunTimeoutAndHeartbeat(
    const std::string& host,
    std::uint16_t port
) {
    tinyimx::ProtocolCodec codec;
    ScopedFd fd = ConnectToServer(host, port);
    if (!fd.Valid()) {
        return false;
    }
    PacketReader reader;
    if (!Login(fd.Get(), codec, &reader, 30, nullptr, nullptr)) {
        return false;
    }

    constexpr std::uint32_t kProfileSeq = 31;
    constexpr std::uint32_t kHeartbeatSeq = 32;

    const auto begin = Clock::now();
    if (!SendPacket(fd.Get(), codec, MakeProfileRequest(kProfileSeq)) ||
        !SendPacket(fd.Get(), codec, MakeHeartbeatRequest(kHeartbeatSeq))) {
        return false;
    }

    bool heartbeat_ok = false;
    bool profile_ok = false;
    long long heartbeat_ms = -1;
    long long profile_ms = -1;

    while (!heartbeat_ok || !profile_ok) {
        tinyimx::Packet packet;
        const ReadStatus status =
            ReadNextPacket(fd.Get(), codec, &reader, &packet);
        if (status != ReadStatus::kPacket) {
            std::cerr
                << "timeout/isolation response stream ended, status="
                << static_cast<int>(status) << '\n';
            return false;
        }

        PrintPacket("[timeout]", packet);
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - begin
            ).count();

        if (packet.type == tinyimx::MessageType::kHeartbeat &&
            packet.seq == kHeartbeatSeq) {
            try {
                const Json body = Json::parse(packet.body);
                if (!body.value("pong", false)) {
                    return false;
                }
            } catch (...) {
                return false;
            }
            heartbeat_ok = true;
            heartbeat_ms = elapsed_ms;
            continue;
        }

        if (packet.type == tinyimx::MessageType::kUserProfileResponse &&
            packet.seq == kProfileSeq) {
            if (!ValidateProfileFailure(packet, "profile_timeout")) {
                return false;
            }
            profile_ok = true;
            profile_ms = elapsed_ms;
        }
    }

    std::cout << "heartbeat_latency_ms=" << heartbeat_ms << std::endl;
    std::cout << "profile_latency_ms=" << profile_ms << std::endl;

    if (heartbeat_ms < 0 || heartbeat_ms > 1000) {
        std::cerr << "heartbeat was blocked by slow profile RPC\n";
        return false;
    }
    if (profile_ms <= heartbeat_ms) {
        std::cerr << "profile timeout completed before heartbeat\n";
        return false;
    }

    std::cout
        << "gateway user profile deadline/isolation validation passed"
        << std::endl;
    return true;
}

bool RunStaleSessionFence(
    const std::string& host,
    std::uint16_t port
) {
    tinyimx::ProtocolCodec codec;

    ScopedFd first = ConnectToServer(host, port);
    if (!first.Valid()) {
        return false;
    }
    PacketReader first_reader;
    if (!Login(first.Get(), codec, &first_reader, 40, nullptr, nullptr)) {
        return false;
    }

    constexpr std::uint32_t kProfileSeq = 41;
    if (!SendPacket(
            first.Get(), codec,
            MakeProfileRequest(kProfileSeq))) {
        return false;
    }

    // Give the first profile RPC enough time to enter UserService's injected
    // delay, then replace its logical Session with a second login.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    ScopedFd second = ConnectToServer(host, port);
    if (!second.Valid()) {
        return false;
    }
    PacketReader second_reader;
    if (!Login(second.Get(), codec, &second_reader, 42, nullptr, nullptr)) {
        return false;
    }

    bool saw_login_replaced = false;
    while (true) {
        tinyimx::Packet packet;
        const ReadStatus status =
            ReadNextPacket(
                first.Get(), codec,
                &first_reader, &packet
            );

        if (status == ReadStatus::kClosed) {
            break;
        }
        if (status == ReadStatus::kTimeout) {
            // A timeout after the replacement notification is acceptable;
            // a stale user_profile_response is not.
            break;
        }
        if (status != ReadStatus::kPacket) {
            return false;
        }

        PrintPacket("[stale-first]", packet);

        if (packet.type == tinyimx::MessageType::kUserProfileResponse &&
            packet.seq == kProfileSeq) {
            std::cerr
                << "stale Session received user_profile_response\n";
            return false;
        }

        if (packet.type == tinyimx::MessageType::kError) {
            try {
                const Json body = Json::parse(packet.body);
                if (body.value("reason", "") == "login_replaced") {
                    saw_login_replaced = true;
                }
            } catch (...) {
                return false;
            }
        }
    }

    if (!saw_login_replaced) {
        std::cerr << "first Session did not observe login_replaced\n";
        return false;
    }

    // Ensure the replacement Session/connection remains healthy without
    // creating another profile completion that would make the stale-case
    // log ambiguous.
    constexpr std::uint32_t kHeartbeatSeq = 43;
    if (!SendPacket(
            second.Get(), codec,
            MakeHeartbeatRequest(kHeartbeatSeq))) {
        return false;
    }

    tinyimx::Packet heartbeat;
    if (!ReadExpected(
            second.Get(), codec, &second_reader,
            tinyimx::MessageType::kHeartbeat,
            kHeartbeatSeq, &heartbeat)) {
        return false;
    }

    try {
        const Json body = Json::parse(heartbeat.body);
        if (!body.value("pong", false)) {
            return false;
        }
    } catch (...) {
        return false;
    }

    std::cout
        << "gateway user profile stale-session fence validation passed"
        << std::endl;
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr
            << "usage: " << argv[0]
            << " <host> <port> <happy|unavailable|timeout|stale>"
            << " [pre_profile_delay_ms]\n";
        return 2;
    }

    const std::string host = argv[1];
    const std::uint16_t port =
        static_cast<std::uint16_t>(std::stoi(argv[2]));
    const std::string mode = argv[3];

    bool ok = false;
    if (mode == "happy") {
        ok = RunHappy(host, port);
    } else if (mode == "unavailable") {
        const int delay_ms = argc >= 5 ? std::stoi(argv[4]) : 1500;
        ok = RunUnavailable(host, port, delay_ms);
    } else if (mode == "timeout") {
        ok = RunTimeoutAndHeartbeat(host, port);
    } else if (mode == "stale") {
        ok = RunStaleSessionFence(host, port);
    } else {
        std::cerr << "unknown mode: " << mode << '\n';
        return 2;
    }

    return ok ? 0 : 1;
}
