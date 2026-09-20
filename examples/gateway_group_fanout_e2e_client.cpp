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
        std::cerr << "usage: gateway_group_fanout_e2e_client "
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
    if (port_a <= 0 || port_a > 65535 || port_b <= 0 || port_b > 65535) return 2;

    Client sender{Connect(argv[1], static_cast<std::uint16_t>(port_a))};
    Client local_receiver{Connect(argv[1], static_cast<std::uint16_t>(port_a))};
    Client remote_receiver{Connect(argv[3], static_cast<std::uint16_t>(port_b))};
    if (!sender.fd.Valid() || !local_receiver.fd.Valid() || !remote_receiver.fd.Valid()) {
        std::cerr << "failed to connect all three E2E clients\n";
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
    if (!JoinGroup(&local_receiver, codec, *group_id, "m17b2-join-local-") ||
        !JoinGroup(&remote_receiver, codec, *group_id, "m17b2-join-remote-")) {
        return 1;
    }

    // Scenario 1: normal local + remote delivery and receiver ACK.
    const std::string normal_content = Unique("m17-b2-normal-");
    const auto normal_message_id = SendGroupMessage(&sender, codec, *group_id, normal_content);
    if (!normal_message_id.has_value()) return 1;
    std::uint64_t local_from = 0;
    std::uint64_t remote_from = 0;
    if (!ReceiveAndAck(&local_receiver, codec, *group_id, *normal_message_id,
                       normal_content, &local_from)) {
        std::cerr << "normal local group delivery/ack failed\n";
        return 1;
    }
    if (!ReceiveAndAck(&remote_receiver, codec, *group_id, *normal_message_id,
                       normal_content, &remote_from)) {
        std::cerr << "normal remote group delivery/ack failed\n";
        return 1;
    }

    // Scenario 2: a sender forges an ACK for another recipient. It must not
    // confirm the durable recipient row. The real remote recipient withholds
    // the first ACK, waits for a retry with a fresh delivery sequence, then
    // acknowledges using the old sequence. This proves receiver identity is
    // session-derived, retries use fresh attempt identity, a late ACK remains
    // valid, and a duplicate ACK is harmless.
    const std::string adversarial_content = Unique("m17-b2-ack-semantics-");
    const auto adversarial_message_id = SendGroupMessage(
        &sender, codec, *group_id, adversarial_content);
    if (!adversarial_message_id.has_value()) return 1;
    if (!ReceiveAndAck(&local_receiver, codec, *group_id, *adversarial_message_id,
                       adversarial_content, nullptr)) {
        std::cerr << "adversarial scenario local delivery failed\n";
        return 1;
    }
    ReceivedGroupDelivery first_remote_attempt;
    if (!ReceiveGroupDelivery(&remote_receiver, codec, *group_id,
                              *adversarial_message_id, adversarial_content,
                              &first_remote_attempt)) {
        std::cerr << "adversarial scenario first remote delivery failed\n";
        return 1;
    }
    if (!SendGroupDeliveryAck(&sender, codec, *adversarial_message_id,
                              first_remote_attempt.packet.seq)) {
        std::cerr << "unable to submit spoofed sender ACK\n";
        return 1;
    }
    ReceivedGroupDelivery retry_remote_attempt;
    if (!ReceiveGroupDelivery(&remote_receiver, codec, *group_id,
                              *adversarial_message_id, adversarial_content,
                              &retry_remote_attempt)) {
        std::cerr << "spoofed ACK incorrectly suppressed durable retry\n";
        return 1;
    }
    if (retry_remote_attempt.packet.seq == first_remote_attempt.packet.seq) {
        std::cerr << "group retry reused delivery attempt sequence\n";
        return 1;
    }
    if (!SendGroupDeliveryAck(&remote_receiver, codec, *adversarial_message_id,
                              first_remote_attempt.packet.seq) ||
        !SendGroupDeliveryAck(&remote_receiver, codec, *adversarial_message_id,
                              first_remote_attempt.packet.seq)) {
        std::cerr << "late/duplicate remote ACK submission failed\n";
        return 1;
    }

    // Scenario 3: route moves after an unacknowledged cross-Gateway attempt.
    // The next durable retry must resolve the recipient again instead of using
    // a sticky stale route.
    const std::string route_content = Unique("m17-b2-route-refresh-");
    const auto route_message_id = SendGroupMessage(&sender, codec, *group_id, route_content);
    if (!route_message_id.has_value()) return 1;
    if (!ReceiveAndAck(&local_receiver, codec, *group_id, *route_message_id,
                       route_content, nullptr)) {
        std::cerr << "route-refresh scenario local delivery failed\n";
        return 1;
    }
    ReceivedGroupDelivery remote_before_move;
    if (!ReceiveGroupDelivery(&remote_receiver, codec, *group_id,
                              *route_message_id, route_content,
                              &remote_before_move)) {
        std::cerr << "route-refresh initial remote attempt missing\n";
        return 1;
    }
    remote_receiver.fd = ScopedFd{};
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    Client moved_remote{Connect(argv[1], static_cast<std::uint16_t>(port_a))};
    if (!moved_remote.fd.Valid() ||
        !Login(&moved_remote, codec, argv[7], argv[8], nullptr)) {
        std::cerr << "remote recipient failed to reconnect through Gateway-A\n";
        return 1;
    }
    ReceivedGroupDelivery remote_after_move;
    if (!ReceiveGroupDelivery(&moved_remote, codec, *group_id,
                              *route_message_id, route_content,
                              &remote_after_move)) {
        std::cerr << "durable retry did not follow refreshed recipient route\n";
        return 1;
    }
    if (remote_after_move.packet.seq == remote_before_move.packet.seq) {
        std::cerr << "route-refresh retry reused delivery attempt sequence\n";
        return 1;
    }
    if (!SendGroupDeliveryAck(&moved_remote, codec, *route_message_id,
                              remote_after_move.packet.seq)) {
        return 1;
    }

    // Scenario 4: an active group member goes offline before a new send. The
    // durable recipient row remains part of the send-time snapshot but B2 must
    // stop at DEFERRED_OFFLINE; replay/unread belongs to B3.
    local_receiver.fd = ScopedFd{};
    std::this_thread::sleep_for(std::chrono::milliseconds(750));
    const std::string offline_content = Unique("m17-b2-offline-");
    const auto offline_message_id = SendGroupMessage(&sender, codec, *group_id, offline_content);
    if (!offline_message_id.has_value()) return 1;
    if (!ReceiveAndAck(&moved_remote, codec, *group_id, *offline_message_id,
                       offline_content, nullptr)) {
        std::cerr << "offline scenario online recipient delivery failed\n";
        return 1;
    }

    if (SenderReceivedUnexpectedDelivery(&sender, codec)) {
        std::cerr << "sender unexpectedly received its own group message\n";
        return 1;
    }

    std::cout << "[PASS] M17-B2 real local+cross-gateway group delivery and ACK\n";
    std::cout << "[PASS] M17-B2 receiver spoof + retry + late/duplicate ACK semantics\n";
    std::cout << "[PASS] M17-B2 route refresh follows recipient Gateway movement\n";
    std::cout << "M17_B2_RESULT group_id=" << *group_id
              << " sender_user_id=" << sender_user_id
              << " local_user_id=" << local_user_id
              << " remote_user_id=" << remote_user_id
              << " normal_message_id=" << *normal_message_id
              << " adversarial_message_id=" << *adversarial_message_id
              << " route_message_id=" << *route_message_id
              << " offline_message_id=" << *offline_message_id
              << " local_from_user_id=" << local_from
              << " remote_from_user_id=" << remote_from << '\n';
    return 0;
}
