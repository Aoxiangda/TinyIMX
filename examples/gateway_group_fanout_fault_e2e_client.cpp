#include "common/net/Buffer.h"
#include "common/protocol/GroupMessageDeliveryProtocol.h"
#include "common/protocol/ProtocolCodec.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {
using Json = nlohmann::json;

class ScopedFd {
public:
    explicit ScopedFd(int fd = -1) : fd_(fd) {}
    ~ScopedFd() { if (fd_ >= 0) ::close(fd_); }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    ScopedFd(ScopedFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    int Get() const noexcept { return fd_; }
    bool Valid() const noexcept { return fd_ >= 0; }
private:
    int fd_{-1};
};

struct Client {
    ScopedFd fd;
    tinyimx::Buffer input;
    std::deque<tinyimx::Packet> pending;
};

bool ConfigureSocket(int fd, int timeout_seconds = 35) {
    timeval timeout{};
    timeout.tv_sec = timeout_seconds;
    int one = 1;
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
           ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0 &&
           ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == 0;
}

ScopedFd Connect(const std::string& host, std::uint16_t port) {
    ScopedFd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd.Valid() || !ConfigureSocket(fd.Get())) return ScopedFd{};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) return ScopedFd{};
    if (::connect(fd.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        return ScopedFd{};
    }
    return fd;
}

bool SendAll(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const auto n = ::send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
        if (n > 0) { offset += static_cast<std::size_t>(n); continue; }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

bool SendPacket(Client* client, const tinyimx::ProtocolCodec& codec, const tinyimx::Packet& packet) {
    if (client == nullptr || !client->fd.Valid()) return false;
    tinyimx::Buffer output;
    std::string error;
    if (!codec.Encode(packet, &output, &error)) return false;
    return SendAll(client->fd.Get(), output.RetrieveAllAsString());
}

bool ReadPacket(Client* client, const tinyimx::ProtocolCodec& codec, tinyimx::Packet* packet) {
    if (client == nullptr || packet == nullptr || !client->fd.Valid()) return false;
    for (;;) {
        if (!client->pending.empty()) {
            *packet = std::move(client->pending.front());
            client->pending.pop_front();
            return true;
        }
        auto decoded = codec.Decode(&client->input);

        if (decoded.status == tinyimx::DecodeStatus::kOk) {
            for (auto& decoded_packet : decoded.packets) {
                client->pending.push_back(std::move(decoded_packet));
            }

            if (!client->pending.empty()) {
                continue;
            }
        } else if (
            decoded.status != tinyimx::DecodeStatus::kNeedMoreData
        ) {
            std::cerr
                << "decode failed: "
                << tinyimx::DecodeStatusToString(decoded.status)
                << ", error=" << decoded.error_message
                << '\n';
            return false;
        }
        char buffer[8192];
        const auto n = ::recv(client->fd.Get(), buffer, sizeof(buffer), 0);
        if (n > 0) {
            client->input.Append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
}

bool ReadExpected(
    Client* client,
    const tinyimx::ProtocolCodec& codec,
    tinyimx::MessageType type,
    std::uint32_t seq,
    tinyimx::Packet* output
) {
    for (;;) {
        tinyimx::Packet packet;
        if (!ReadPacket(client, codec, &packet)) return false;
        if (packet.type == type && packet.seq == seq) {
            if (output) *output = std::move(packet);
            return true;
        }
    }
}

bool Login(
    Client* client,
    const tinyimx::ProtocolCodec& codec,
    const std::string& username,
    const std::string& password,
    std::uint64_t* user_id
) {
    tinyimx::Packet request;
    request.type = tinyimx::MessageType::kLoginRequest;
    request.seq = 1;
    request.body = Json{{"username", username}, {"password", password}}.dump();
    if (!SendPacket(client, codec, request)) return false;
    tinyimx::Packet response;
    if (!ReadExpected(client, codec, tinyimx::MessageType::kLoginResponse, 1, &response)) return false;
    try {
        const auto body = Json::parse(response.body);
        if (!body.value("success", false)) {
            std::cerr << "login rejected for " << username << ": " << response.body << '\n';
            return false;
        }
        const auto resolved = body.value("user_id", std::uint64_t{0});
        if (resolved == 0) return false;
        if (user_id) *user_id = resolved;
        return true;
    } catch (...) {
        return false;
    }
}

std::string Unique(const char* prefix) {
    const auto value = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::string(prefix) + std::to_string(static_cast<long long>(value));
}

std::optional<std::uint64_t> CreateGroup(Client* sender, const tinyimx::ProtocolCodec& codec) {
    tinyimx::Packet request;
    request.type = tinyimx::MessageType::kCreateGroupRequest;
    request.seq = 2;
    request.body = Json{
        {"client_operation_id", Unique("m17b2-fault-create-")},
        {"name", Unique("m17b2-fault-")},
        {"join_policy", "open"},
        {"max_members", 8}
    }.dump();
    if (!SendPacket(sender, codec, request)) return std::nullopt;
    tinyimx::Packet response;
    if (!ReadExpected(sender, codec, tinyimx::MessageType::kCreateGroupResponse, 2, &response)) {
        return std::nullopt;
    }
    try {
        const auto body = Json::parse(response.body);
        if (!body.value("success", false) || !body.contains("group") ||
            !body["group"].contains("group_id")) {
            std::cerr << "create group rejected: " << response.body << '\n';
            return std::nullopt;
        }
        return body["group"]["group_id"].get<std::uint64_t>();
    } catch (...) {
        return std::nullopt;
    }
}

bool JoinGroup(Client* client, const tinyimx::ProtocolCodec& codec, std::uint64_t group_id) {
    tinyimx::Packet request;
    request.type = tinyimx::MessageType::kJoinGroupRequest;
    request.seq = 2;
    request.body = Json{
        {"client_operation_id", Unique("m17b2-fault-join-")},
        {"group_id", group_id}
    }.dump();
    if (!SendPacket(client, codec, request)) return false;
    tinyimx::Packet response;
    if (!ReadExpected(client, codec, tinyimx::MessageType::kJoinGroupResponse, 2, &response)) return false;
    try {
        const auto body = Json::parse(response.body);
        return body.value("success", false);
    } catch (...) {
        return false;
    }
}

std::optional<std::uint64_t> SendGroupMessage(
    Client* sender,
    const tinyimx::ProtocolCodec& codec,
    std::uint64_t group_id,
    const std::string& content
) {
    tinyimx::Packet request;
    request.type = tinyimx::MessageType::kGroupMessageSendRequest;
    request.seq = 3;
    request.body = Json{
        {"group_id", group_id},
        {"client_message_id", Unique("m17b2-fault-message-")},
        {"message_type", 1},
        {"content", content},
        {"from_user_id", 999999},
        {"actor_user_id", 999999}
    }.dump();
    if (!SendPacket(sender, codec, request)) return std::nullopt;
    tinyimx::Packet response;
    if (!ReadExpected(sender, codec, tinyimx::MessageType::kGroupMessageSendResponse, 3, &response)) {
        return std::nullopt;
    }
    try {
        const auto body = Json::parse(response.body);
        if (!body.value("success", false) || !body.contains("message_id")) {
            std::cerr << "group send rejected: " << response.body << '\n';
            return std::nullopt;
        }
        return body["message_id"].get<std::uint64_t>();
    } catch (...) {
        return std::nullopt;
    }
}

struct ReceivedGroupDelivery {
    tinyimx::Packet packet;
    tinyimx::GroupMessageDelivery delivery;
};

bool ReceiveGroupDelivery(
    Client* client,
    const tinyimx::ProtocolCodec& codec,
    std::uint64_t group_id,
    std::uint64_t message_id,
    const std::string& content,
    ReceivedGroupDelivery* output
) {
    for (;;) {
        tinyimx::Packet packet;
        if (!ReadPacket(client, codec, &packet)) return false;
        if (packet.type != tinyimx::MessageType::kGroupMessageDelivery) continue;
        tinyimx::GroupMessageDelivery delivery;
        std::string error;
        if (!tinyimx::DeserializeGroupMessageDelivery(packet.body, &delivery, &error)) return false;
        if (delivery.group_id != group_id || delivery.message_id != message_id ||
            delivery.content != content || packet.seq == 0) {
            std::cerr << "unexpected group delivery identity\n";
            return false;
        }
        if (output) {
            output->packet = std::move(packet);
            output->delivery = std::move(delivery);
        }
        return true;
    }
}

bool SendAck(
    Client* client,
    const tinyimx::ProtocolCodec& codec,
    std::uint64_t message_id,
    std::uint32_t delivery_seq
) {
    tinyimx::GroupMessageDeliveryAck ack;
    ack.message_id = message_id;
    std::string body;
    std::string error;
    if (!tinyimx::SerializeGroupMessageDeliveryAck(ack, &body, &error)) return false;
    tinyimx::Packet packet;
    packet.type = tinyimx::MessageType::kGroupMessageDeliveryAck;
    packet.seq = delivery_seq;
    packet.body = std::move(body);
    return SendPacket(client, codec, packet);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 9) {
        std::cerr << "usage: gateway_group_fanout_fault_e2e_client "
                  << "<peer-loss|crash-claim> <host-a> <port-a> <host-b> <port-b> "
                  << "<sender-username> <remote-recipient-username> <password>\n";
        return 2;
    }
    const std::string mode = argv[1];
    if (mode != "peer-loss" && mode != "crash-claim") return 2;
    int port_a = 0;
    int port_b = 0;
    try {
        port_a = std::stoi(argv[3]);
        port_b = std::stoi(argv[5]);
    } catch (...) {
        return 2;
    }
    if (port_a <= 0 || port_a > 65535 || port_b <= 0 || port_b > 65535) return 2;

    Client sender{Connect(argv[2], static_cast<std::uint16_t>(port_a))};
    Client remote{Connect(argv[4], static_cast<std::uint16_t>(port_b))};
    if (!sender.fd.Valid() || !remote.fd.Valid()) {
        std::cerr << "failed to connect fault-gate clients\n";
        return 1;
    }

    tinyimx::ProtocolCodec codec;
    std::uint64_t sender_user_id = 0;
    std::uint64_t remote_user_id = 0;
    if (!Login(&sender, codec, argv[6], argv[8], &sender_user_id) ||
        !Login(&remote, codec, argv[7], argv[8], &remote_user_id)) {
        return 1;
    }
    const auto group_id = CreateGroup(&sender, codec);
    if (!group_id.has_value() || !JoinGroup(&remote, codec, *group_id)) return 1;

    const std::string content = Unique(mode == "peer-loss"
        ? "m17-b2-peer-loss-" : "m17-b2-crash-claim-");
    const auto message_id = SendGroupMessage(&sender, codec, *group_id, content);
    if (!message_id.has_value()) return 1;

    std::cout << "M17_B2_FAULT_FIXTURE mode=" << mode
              << " group_id=" << *group_id
              << " message_id=" << *message_id
              << " sender_user_id=" << sender_user_id
              << " recipient_user_id=" << remote_user_id
              << std::endl;

    ReceivedGroupDelivery first;
    if (!ReceiveGroupDelivery(&remote, codec, *group_id, *message_id, content, &first)) {
        std::cerr << "first durable group delivery missing\n";
        return 1;
    }

    if (mode == "peer-loss") {
        ReceivedGroupDelivery retry;
        if (!ReceiveGroupDelivery(&remote, codec, *group_id, *message_id, content, &retry)) {
            std::cerr << "peer-response-loss did not converge to durable retry\n";
            return 1;
        }
        if (retry.packet.seq == first.packet.seq) {
            std::cerr << "peer-response-loss retry reused delivery attempt sequence\n";
            return 1;
        }
        if (!SendAck(&remote, codec, *message_id, retry.packet.seq)) return 1;
        std::cout << "[PASS] M17-B2 peer-response-loss durable retry reached receiver"
                  << " message_id=" << *message_id
                  << " first_seq=" << first.packet.seq
                  << " retry_seq=" << retry.packet.seq << '\n';
        return 0;
    }

    // crash-claim mode receives only after the crashed coordinator's lease has
    // expired and another coordinator has reclaimed the same durable row. The
    // shell harness kills gateway-a while its post-claim fault pause is active.
    if (!SendAck(&remote, codec, *message_id, first.packet.seq)) return 1;
    std::cout << "[PASS] M17-B2 crashed-coordinator lease takeover reached receiver"
              << " message_id=" << *message_id
              << " delivery_seq=" << first.packet.seq << '\n';
    return 0;
}
