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

bool ConfigureSocket(int fd) {
    timeval timeout{};
    timeout.tv_sec = 8;
    int one = 1;
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
           ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0 &&
           ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == 0;
}

ScopedFd Connect(const std::string& host, std::uint16_t port) {
    ScopedFd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd.Valid() || !ConfigureSocket(fd.Get())) return {};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) return {};
    if (::connect(fd.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) return {};
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
    if (!client || !client->fd.Valid()) return false;
    tinyimx::Buffer output;
    std::string error;
    if (!codec.Encode(packet, &output, &error)) {
        std::cerr << "encode failed: " << error << '\n';
        return false;
    }
    return SendAll(client->fd.Get(), output.RetrieveAllAsString());
}

bool ReadPacket(Client* client, const tinyimx::ProtocolCodec& codec, tinyimx::Packet* output) {
    if (!client || !output) return false;
    if (!client->pending.empty()) {
        *output = std::move(client->pending.front());
        client->pending.pop_front();
        return true;
    }
    for (;;) {
        char buffer[8192];
        const auto n = ::recv(client->fd.Get(), buffer, sizeof(buffer), 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        client->input.Append(buffer, static_cast<std::size_t>(n));
        const auto decoded = codec.Decode(&client->input);
        if (decoded.status == tinyimx::DecodeStatus::kNeedMoreData) continue;
        if (decoded.status != tinyimx::DecodeStatus::kOk || decoded.packets.empty()) return false;
        for (std::size_t i = 1; i < decoded.packets.size(); ++i) {
            client->pending.push_back(decoded.packets[i]);
        }
        *output = decoded.packets.front();
        return true;
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
    std::uint64_t* user_id = nullptr
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
        const auto resolved_user_id = body.value("user_id", std::uint64_t{0});
        if (resolved_user_id == 0) {
            std::cerr << "login response missing user_id for " << username << '\n';
            return false;
        }
        if (user_id) *user_id = resolved_user_id;
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
        {"client_operation_id", Unique("m17b2-create-")},
        {"name", Unique("m17b2-fanout-")},
        {"join_policy", "open"},
        {"max_members", 20}
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

bool JoinGroup(
    Client* client,
    const tinyimx::ProtocolCodec& codec,
    std::uint64_t group_id,
    const char* operation_prefix
) {
    tinyimx::Packet request;
    request.type = tinyimx::MessageType::kJoinGroupRequest;
    request.seq = 2;
    request.body = Json{
        {"client_operation_id", Unique(operation_prefix)},
        {"group_id", group_id}
    }.dump();
    if (!SendPacket(client, codec, request)) return false;
    tinyimx::Packet response;
    if (!ReadExpected(client, codec, tinyimx::MessageType::kJoinGroupResponse, 2, &response)) return false;
    try {
        const auto body = Json::parse(response.body);
        if (!body.value("success", false)) {
            std::cerr << "join rejected: " << response.body << '\n';
            return false;
        }
        return true;
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
        {"client_message_id", Unique("m17b2-message-")},
        {"message_type", 1},
        {"content", content},
        // Must be ignored by the Gateway; sender identity comes from Session.
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
    std::uint64_t expected_group_id,
    std::uint64_t expected_message_id,
    const std::string& expected_content,
    ReceivedGroupDelivery* output
) {
    for (;;) {
        tinyimx::Packet packet;
        if (!ReadPacket(client, codec, &packet)) return false;
        if (packet.type != tinyimx::MessageType::kGroupMessageDelivery) continue;
        tinyimx::GroupMessageDelivery delivery;
        std::string error;
        if (!tinyimx::DeserializeGroupMessageDelivery(packet.body, &delivery, &error)) {
            std::cerr << "invalid group delivery: " << error << '\n';
            return false;
        }
        if (delivery.group_id != expected_group_id ||
            delivery.message_id != expected_message_id ||
            delivery.content != expected_content || packet.seq == 0) {
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

bool SendGroupDeliveryAck(
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
    tinyimx::Packet ack_packet;
    ack_packet.type = tinyimx::MessageType::kGroupMessageDeliveryAck;
    ack_packet.seq = delivery_seq;
    ack_packet.body = std::move(body);
    return SendPacket(client, codec, ack_packet);
}

bool ReceiveAndAck(
    Client* client,
    const tinyimx::ProtocolCodec& codec,
    std::uint64_t expected_group_id,
    std::uint64_t expected_message_id,
    const std::string& expected_content,
    std::uint64_t* from_user_id
) {
    ReceivedGroupDelivery received;
    if (!ReceiveGroupDelivery(
            client, codec, expected_group_id, expected_message_id,
            expected_content, &received)) {
        return false;
    }
    if (from_user_id) *from_user_id = received.delivery.from_user_id;
    return SendGroupDeliveryAck(
        client, codec, received.delivery.message_id, received.packet.seq);
}

bool SenderReceivedUnexpectedDelivery(Client* sender, const tinyimx::ProtocolCodec& codec) {
    pollfd pfd{};
    pfd.fd = sender->fd.Get();
    pfd.events = POLLIN;
    const int ready = ::poll(&pfd, 1, 500);
    if (ready <= 0 || !(pfd.revents & POLLIN)) return false;
    tinyimx::Packet packet;
    if (!ReadPacket(sender, codec, &packet)) return false;
    return packet.type == tinyimx::MessageType::kGroupMessageDelivery;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 9) {
        std::cerr << "usage: gateway_group_offline_replay_e2e_client "
                  << "<host-a> <port-a> <host-b> <port-b> "
                  << "<sender-username> <local-recipient-username> "
                  << "<remote-recipient-username> <password>\n";
        return 2;
    }

    int port_a = 0;
    int port_b = 0;
    try {
        port_a = std::stoi(argv[2]);
        port_b = std::stoi(argv[4]);
    } catch (...) {
        return 2;
    }
    if (port_a <= 0 || port_a > 65535 || port_b <= 0 || port_b > 65535) {
        return 2;
    }

    Client sender{Connect(argv[1], static_cast<std::uint16_t>(port_a))};
    Client local_receiver{Connect(argv[1], static_cast<std::uint16_t>(port_a))};
    Client remote_receiver{Connect(argv[3], static_cast<std::uint16_t>(port_b))};
    if (!sender.fd.Valid() || !local_receiver.fd.Valid() || !remote_receiver.fd.Valid()) {
        std::cerr << "failed to connect M17-B3 clients\n";
        return 1;
    }

    tinyimx::ProtocolCodec codec;
    std::uint64_t sender_user_id = 0;
    std::uint64_t local_user_id = 0;
    std::uint64_t remote_user_id = 0;
    if (!Login(&sender, codec, argv[5], argv[8], &sender_user_id) ||
        !Login(&local_receiver, codec, argv[6], argv[8], &local_user_id) ||
        !Login(&remote_receiver, codec, argv[7], argv[8], &remote_user_id)) {
        return 1;
    }

    const auto group_id = CreateGroup(&sender, codec);
    if (!group_id.has_value()) return 1;
    if (!JoinGroup(&local_receiver, codec, *group_id, "m17b3-join-local-") ||
        !JoinGroup(&remote_receiver, codec, *group_id, "m17b3-join-remote-")) {
        return 1;
    }

    // B3-A + B3-B scenario 1:
    // local recipient is offline at send time, reconnects, receives durable
    // DEFERRED_OFFLINE replay, intentionally drops the first ACK, observes a
    // fresh-seq retry, then sends a late + duplicate ACK for the first attempt.
    local_receiver.fd = ScopedFd{};
    std::this_thread::sleep_for(std::chrono::milliseconds(750));

    const std::string replay_content = Unique("m17-b3-offline-replay-");
    const auto replay_message_id = SendGroupMessage(
        &sender, codec, *group_id, replay_content);
    if (!replay_message_id.has_value()) return 1;

    if (!ReceiveAndAck(&remote_receiver, codec, *group_id, *replay_message_id,
                       replay_content, nullptr)) {
        std::cerr << "online peer did not receive B3 replay fixture message\n";
        return 1;
    }

    Client reconnected_local{Connect(argv[1], static_cast<std::uint16_t>(port_a))};
    if (!reconnected_local.fd.Valid() ||
        !Login(&reconnected_local, codec, argv[6], argv[8], nullptr)) {
        std::cerr << "offline recipient failed to reconnect on Gateway-A\n";
        return 1;
    }

    ReceivedGroupDelivery first_replay;
    if (!ReceiveGroupDelivery(&reconnected_local, codec, *group_id,
                              *replay_message_id, replay_content, &first_replay)) {
        std::cerr << "DEFERRED_OFFLINE message was not replayed after login\n";
        return 1;
    }

    ReceivedGroupDelivery retry_replay;
    if (!ReceiveGroupDelivery(&reconnected_local, codec, *group_id,
                              *replay_message_id, replay_content, &retry_replay)) {
        std::cerr << "group replay did not recover from dropped first ACK\n";
        return 1;
    }
    if (retry_replay.packet.seq == first_replay.packet.seq) {
        std::cerr << "group offline replay retry reused delivery seq\n";
        return 1;
    }

    if (!SendGroupDeliveryAck(&reconnected_local, codec, *replay_message_id,
                              first_replay.packet.seq) ||
        !SendGroupDeliveryAck(&reconnected_local, codec, *replay_message_id,
                              first_replay.packet.seq)) {
        std::cerr << "late/duplicate ACK submission failed for offline replay\n";
        return 1;
    }

    // B3-B scenario 2: the remote recipient was previously on Gateway-B, goes
    // offline before send, then logs into Gateway-A. Recovery must follow the
    // current authenticated session instead of a stale route hint.
    remote_receiver.fd = ScopedFd{};
    std::this_thread::sleep_for(std::chrono::milliseconds(750));

    const std::string movement_content = Unique("m17-b3-gateway-move-");
    const auto movement_message_id = SendGroupMessage(
        &sender, codec, *group_id, movement_content);
    if (!movement_message_id.has_value()) return 1;

    if (!ReceiveAndAck(&reconnected_local, codec, *group_id, *movement_message_id,
                       movement_content, nullptr)) {
        std::cerr << "online local peer missed movement fixture message\n";
        return 1;
    }

    Client moved_remote{Connect(argv[1], static_cast<std::uint16_t>(port_a))};
    if (!moved_remote.fd.Valid() ||
        !Login(&moved_remote, codec, argv[7], argv[8], nullptr)) {
        std::cerr << "remote recipient failed to move from Gateway-B to Gateway-A\n";
        return 1;
    }

    ReceivedGroupDelivery moved_replay;
    if (!ReceiveGroupDelivery(&moved_remote, codec, *group_id,
                              *movement_message_id, movement_content, &moved_replay)) {
        std::cerr << "offline replay did not follow current Gateway session\n";
        return 1;
    }
    if (!SendGroupDeliveryAck(&moved_remote, codec, *movement_message_id,
                              moved_replay.packet.seq)) {
        std::cerr << "movement replay ACK failed\n";
        return 1;
    }

    if (SenderReceivedUnexpectedDelivery(&sender, codec)) {
        std::cerr << "sender unexpectedly received its own group message\n";
        return 1;
    }

    std::cout << "[PASS] M17-B3 offline recipient reconnect receives durable replay\n";
    std::cout << "[PASS] M17-B3 ACK loss produces fresh-seq retry; late/duplicate ACK is safe\n";
    std::cout << "[PASS] M17-B3 replay follows authenticated Gateway movement\n";
    std::cout << "M17_B3_RESULT group_id=" << *group_id
              << " sender_user_id=" << sender_user_id
              << " local_user_id=" << local_user_id
              << " remote_user_id=" << remote_user_id
              << " replay_message_id=" << *replay_message_id
              << " movement_message_id=" << *movement_message_id
              << " first_replay_seq=" << first_replay.packet.seq
              << " retry_replay_seq=" << retry_replay.packet.seq
              << " moved_replay_seq=" << moved_replay.packet.seq
              << '\n';
    return 0;
}
