#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <optional>
#include <string>

namespace {

using Json = nlohmann::json;

class ScopedFd {
public:
    explicit ScopedFd(int fd = -1) : fd_(fd) {}
    ~ScopedFd() { if (fd_ >= 0) ::close(fd_); }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    ScopedFd(ScopedFd&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                ::close(fd_);
            }

            fd_ = other.fd_;
            other.fd_ = -1;
        }

        return *this;
    }

    [[nodiscard]] int Get() const noexcept { return fd_; }
    [[nodiscard]] bool Valid() const noexcept { return fd_ >= 0; }
private:
    int fd_{-1};
};

struct PacketReader {
    tinyimx::Buffer input;
    std::deque<tinyimx::Packet> pending;
};

std::optional<std::pair<tinyimx::MessageType, tinyimx::MessageType>> OperationTypes(
    const std::string& operation
) {
    using tinyimx::MessageType;
    if (operation == "begin") return {{MessageType::kBeginFileUploadRequest, MessageType::kBeginFileUploadResponse}};
    if (operation == "get") return {{MessageType::kGetFileUploadSessionRequest, MessageType::kGetFileUploadSessionResponse}};
    if (operation == "cancel") return {{MessageType::kCancelFileUploadRequest, MessageType::kCancelFileUploadResponse}};
    return std::nullopt;
}

bool ConfigureSocket(int fd) {
    timeval timeout{}; timeout.tv_sec = 8;
    int one = 1;
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
           ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0 &&
           ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == 0;
}

ScopedFd Connect(const std::string& host, std::uint16_t port) {
    ScopedFd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd.Valid() || !ConfigureSocket(fd.Get())) return ScopedFd{};
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) return ScopedFd{};
    if (::connect(fd.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) return ScopedFd{};
    return fd;
}

bool SendAll(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = ::send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
        if (n > 0) { offset += static_cast<std::size_t>(n); continue; }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

bool SendPacket(int fd, const tinyimx::ProtocolCodec& codec, const tinyimx::Packet& packet) {
    tinyimx::Buffer buffer;
    std::string error;
    if (!codec.Encode(packet, &buffer, &error)) {
        std::cerr << "encode failed: " << error << '\n';
        return false;
    }
    return SendAll(fd, buffer.RetrieveAllAsString());
}

bool ReadPacket(int fd, const tinyimx::ProtocolCodec& codec, PacketReader* reader, tinyimx::Packet* output) {
    if (!reader || !output) return false;
    if (!reader->pending.empty()) {
        *output = std::move(reader->pending.front()); reader->pending.pop_front(); return true;
    }
    for (;;) {
        char temp[4096];
        const ssize_t n = ::recv(fd, temp, sizeof(temp), 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        reader->input.Append(temp, static_cast<std::size_t>(n));
        const auto result = codec.Decode(&reader->input);
        if (result.status == tinyimx::DecodeStatus::kNeedMoreData) continue;
        if (result.status != tinyimx::DecodeStatus::kOk || result.packets.empty()) return false;
        for (std::size_t i = 1; i < result.packets.size(); ++i) reader->pending.push_back(result.packets[i]);
        *output = result.packets.front(); return true;
    }
}

bool ReadExpected(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    PacketReader* reader,
    tinyimx::MessageType type,
    std::uint32_t seq,
    tinyimx::Packet* output
) {
    for (;;) {
        tinyimx::Packet packet;
        if (!ReadPacket(fd, codec, reader, &packet)) return false;
        if (packet.type == type && packet.seq == seq) {
            if (output) *output = std::move(packet);
            return true;
        }
    }
}

bool Login(
    int fd,
    const tinyimx::ProtocolCodec& codec,
    PacketReader* reader,
    const std::string& username,
    const std::string& password
) {
    tinyimx::Packet request;
    request.type = tinyimx::MessageType::kLoginRequest;
    request.seq = 1;
    request.body = Json{{"username",username},{"password",password}}.dump();
    if (!SendPacket(fd, codec, request)) return false;
    tinyimx::Packet response;
    if (!ReadExpected(fd, codec, reader, tinyimx::MessageType::kLoginResponse, 1, &response)) return false;
    try {
        const auto body = Json::parse(response.body);
        if (!body.value("success", false)) {
            std::cerr << "login rejected: " << response.body << '\n';
            return false;
        }
    } catch (...) { return false; }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 7) {
        std::cerr << "usage: gateway_file_client_demo <host> <port> <username> <password> <operation> '<json-body>'\n";
        return 2;
    }

    const std::string host = argv[1];
    const int port_value = std::stoi(argv[2]);
    if (port_value <= 0 || port_value > 65535) return 2;
    const std::string username = argv[3];
    const std::string password = argv[4];
    const std::string operation = argv[5];
    const auto types = OperationTypes(operation);
    if (!types) {
        std::cerr << "unsupported operation\n";
        return 2;
    }

    Json body;
    try { body = Json::parse(argv[6]); }
    catch (const std::exception& e) { std::cerr << "invalid json: " << e.what() << '\n'; return 2; }
    if (!body.is_object()) return 2;

    auto fd = Connect(host, static_cast<std::uint16_t>(port_value));
    if (!fd.Valid()) {
        std::cerr << "connect failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    tinyimx::ProtocolCodec codec;
    PacketReader reader;
    if (!Login(fd.Get(), codec, &reader, username, password)) return 1;

    tinyimx::Packet request;
    request.type = types->first;
    request.seq = 2;
    request.body = body.dump();
    if (!SendPacket(fd.Get(), codec, request)) return 1;

    tinyimx::Packet response;
    if (!ReadExpected(fd.Get(), codec, &reader, types->second, 2, &response)) {
        std::cerr << "file response not received\n";
        return 1;
    }

    std::cout << response.body << '\n';
    return 0;
}
