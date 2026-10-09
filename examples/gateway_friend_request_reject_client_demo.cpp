#include "common/net/Buffer.h"
#include "common/protocol/ProtocolCodec.h"

#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

namespace {
using Json = nlohmann::json;

constexpr std::uint64_t kRequesterUserId = 10003;
constexpr std::uint64_t kReceiverUserId = 10004;

constexpr const char* kRequesterUsername =
    "user10003";

constexpr const char* kReceiverUsername =
    "user10004";

constexpr const char* kDemoPassword =
    "123456";

constexpr const char* kRequestMessage =
    "reject integration request from user10003";


class GatewayTestClient {
public:
            GatewayTestClient() = default;

            ~GatewayTestClient() {
                Close();
            }

            GatewayTestClient(const GatewayTestClient&) = delete;
            GatewayTestClient& operator = (const GatewayTestClient&) = delete;

            bool Connect(
                const std::string& host,
                std::uint16_t port
            );

            void Close();

            bool SendPacket(const tinyimx::Packet& packet);

            bool ReceivePacket(tinyimx::Packet* packet);

            bool Connected() const { return fd_ >= 0; }

        private:
            bool ConfigureSocket();

            bool SendAll(const char* data, std::size_t size);

            bool PopPendingPacket(tinyimx::Packet* packet);

        private:
            int fd_{-1};

            tinyimx::ProtocolCodec codec_;
            tinyimx::Buffer input_buffer_;

            std::deque<tinyimx::Packet> pending_packets_;

    };

    bool GatewayTestClient::ConfigureSocket() {
        if (fd_ < 0) {
            return false;
        }

        timeval timeout {};
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        if (::setsockopt(
                fd_,
                SOL_SOCKET,
                SO_RCVTIMEO,
                &timeout,
                static_cast<socklen_t>(
                    sizeof(timeout)
                )) != 0) {
            std::cerr
                << "set receive timeout failed: "
                << std::strerror(errno)
                << '\n';

            return false;
        }

        if (::setsockopt(
                fd_,
                SOL_SOCKET,
                SO_SNDTIMEO,
                &timeout,
                static_cast<socklen_t>(
                    sizeof(timeout)
                )) != 0) {
            std::cerr
                << "set send timeout failed: "
                << std::strerror(errno)
                << '\n';

            return false;
        }

        int no_delay = 1;

        if (::setsockopt(
                fd_,
                IPPROTO_TCP,
                TCP_NODELAY,
                &no_delay,
                static_cast<socklen_t>(
                    sizeof(no_delay)
                )) != 0) {
            std::cerr
                << "set TCP_NODELAY failed: "
                << std::strerror(errno)
                << '\n';

            return false;
        }

        return true;
    }

    bool GatewayTestClient::Connect(
        const std::string& host,
        std::uint16_t port
    ) {
        Close();

        fd_ = ::socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

        if (fd_ < 0) {
            std::cerr
                << "create socket failed: "
                << std::strerror(errno)
                << '\n';

            return false;
        }

        if (!ConfigureSocket()) {
            Close();
            return false;
        }

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);

        if (::inet_pton(
                AF_INET,
                host.c_str(),
                &address.sin_addr) != 1) {
            std::cerr
                << "invalid IPv4 address: "
                << host
                << '\n';

            Close();
            return false;
        }

        if (::connect(
                fd_,
                reinterpret_cast<sockaddr*>(
                    &address
                ),
                static_cast<socklen_t>(
                    sizeof(address)
                )) != 0) {
            std::cerr
                << "connect failed: "
                << std::strerror(errno)
                << '\n';

            Close();
            return false;
        }

        return true;
    }

    void GatewayTestClient::Close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }

        input_buffer_.RetrieveAll();
        pending_packets_.clear();
    }

    bool GatewayTestClient::SendAll(
        const char* data,
        std::size_t size
    ) {
        if (fd_ < 0 ||
            (data == nullptr && size != 0)) {
            return false;
        }

        std::size_t sent_total = 0;

        while (sent_total < size) {
            const ssize_t sent = ::send(
                fd_,
                data + sent_total,
                size - sent_total,
                MSG_NOSIGNAL
            );

            if (sent > 0) {
                sent_total +=
                    static_cast<std::size_t>(
                        sent
                    );

                continue;
            }

            if (sent < 0 && errno == EINTR) {
                continue;
            }

            std::cerr
                << "send failed: "
                << std::strerror(errno)
                << '\n';

            return false;
        }

        return true;
    }

    bool GatewayTestClient::SendPacket(
        const tinyimx::Packet& packet
    ) {
        if (!Connected()) {
            std::cerr
                << "send packet failed: "
                << "client is not connected"
                << '\n';

            return false;
        }

        tinyimx::Buffer output_buffer;
        std::string error_message;

        if (!codec_.Encode(
                packet,
                &output_buffer,
                &error_message)) {
            std::cerr
                << "encode packet failed: "
                << error_message
                << '\n';

            return false;
        }

        const std::string encoded_data =
            output_buffer.RetrieveAllAsString();

        return SendAll(
            encoded_data.data(),
            encoded_data.size()
        );
    }


    bool GatewayTestClient::PopPendingPacket(
        tinyimx::Packet* packet
    ) {
        if (packet == nullptr ||
            pending_packets_.empty()) {
            return false;
        }

        *packet =
            std::move(
                pending_packets_.front()
            );

        pending_packets_.pop_front();

        return true;
    }

    bool GatewayTestClient::ReceivePacket(
        tinyimx::Packet* packet
    ) {
        if (!Connected() || packet == nullptr) {
            return false;
        }

        if (PopPendingPacket(packet)) {
            return true;
        }

        while (true) {
            const tinyimx::DecodeResult
                decode_result =
                    codec_.Decode(
                        &input_buffer_
                    );

            if (decode_result.status ==
                tinyimx::DecodeStatus::kOk) {
                for (auto decoded_packet :
                    decode_result.packets) {
                    pending_packets_.push_back(
                        std::move(decoded_packet)
                    );
                }

                if (PopPendingPacket(packet)) {
                    return true;
                }
            } else if (
                decode_result.status !=
                tinyimx::DecodeStatus::kNeedMoreData
            ) {
                std::cerr
                    << "decode failed: status="
                    << tinyimx::DecodeStatusToString(
                        decode_result.status
                    )
                    << ", error="
                    << decode_result.error_message
                    << '\n';

                return false;
            }

            char temporary_buffer[4096];

            const ssize_t received = ::recv(
                fd_,
                temporary_buffer,
                sizeof(temporary_buffer),
                0
            );

            if (received > 0) {
                input_buffer_.Append(
                    temporary_buffer,
                    static_cast<std::size_t>(
                        received
                    )
                );

                continue;
            }

            if (received == 0) {
                std::cerr
                    << "server closed connection"
                    << '\n';

                return false;
            }

            if (errno == EINTR) {
                continue;
            }

            std::cerr
                << "recv failed: "
                << std::strerror(errno)
                << '\n';

            return false;
        }
    }

    tinyimx::Packet MakeJsonPacket(
        tinyimx::MessageType type,
        std::uint32_t seq,
        const Json& body) {
            tinyimx::Packet packet;

            packet.type = type;
            packet.seq  = seq;
            packet.body = body.dump();

            return packet;
    }

    tinyimx::Packet MakeLoginRequest(
        const std::string& username,
        const std::string& password,
        std::uint32_t seq
    ) {
        Json body;
        body["username"] = username;
        body["password"] = password;

        return MakeJsonPacket(
            tinyimx::MessageType::kLoginRequest,
            seq,
            body
        );
    }

    tinyimx::Packet MakeFriendRequestCreateRequest(
        std::uint64_t to_user_id,
        const std::string& request_message,
        std::uint32_t seq,
        std::optional<std::uint64_t>
            claimed_from_user_id = std::nullopt
    ) {
        Json body;

        body["to_user_id"] = to_user_id;
        body["request_message"] = request_message;

        if (claimed_from_user_id.has_value()) {
            body["from_user_id"] =
                claimed_from_user_id.value();
        }

        return MakeJsonPacket(
            tinyimx::MessageType::
                kFriendRequestCreateRequest,
            seq,
            body
        );
    }


    tinyimx::Packet MakeFriendRequestListRequest(
        std::uint32_t limit,
        const std::string& before_created_at,
        std::uint64_t before_request_id,
        std::uint32_t seq,
        std::optional<std::uint64_t>
        claimed_receiver_user_id = std::nullopt
    ) {
        Json body;

        body["limit"] = limit;
        body["before_created_at"] =
            before_created_at;
        body["before_request_id"] =
            before_request_id;

        if (claimed_receiver_user_id.has_value()) {
            body["receiver_user_id"] =
                claimed_receiver_user_id.value();
        }

        return MakeJsonPacket(
            tinyimx::MessageType::
                kFriendRequestListRequest,
            seq,
            body
        );
    }

    tinyimx::Packet MakeFriendListRequest(
        std::uint32_t limit,
        std::uint32_t seq
    ) {
        Json body;

        body["limit"] = limit;

        return MakeJsonPacket(
            tinyimx::MessageType::
                kFriendListRequest,
            seq,
            body
        );
    }

    tinyimx::Packet MakeFriendRequestAcceptRequest(
        std::uint64_t request_id,
        std::uint32_t seq,
        std::optional<std::uint64_t>
            claimed_handler_user_id = std::nullopt
    ) {
        Json body;

        body["request_id"] = request_id;

        if (claimed_handler_user_id.has_value()) {
            body["handler_user_id"] =
                claimed_handler_user_id.value();
        }

        return MakeJsonPacket(
            tinyimx::MessageType::
                kFriendRequestAcceptRequest,
            seq,
            body
        );
    }

    tinyimx::Packet MakeFriendRequestRejectRequest (
        std::uint64_t request_id,
        std::uint32_t seq,
        std::optional<std::uint64_t>
            claimed_handler_user_id = std::nullopt
        ) {
            Json body;

            body["request_id"] = request_id;

            if (claimed_handler_user_id.has_value()) {
                body["handler_user_id"] =
                    claimed_handler_user_id.value();
            }
            return MakeJsonPacket(
                tinyimx::MessageType::kFriendRequestRejectRequest,
                seq,
                body
            );
        }

    bool ParseJsonObject(
        const tinyimx::Packet& packet,
        Json* body
    ) {
        if (body == nullptr) {
            return false;
        }

        try {
            *body = Json::parse(packet.body);
        } catch (const std::exception& e) {
            std::cerr
                << "parse response json failed: "
                << e.what()
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        if (!body->is_object()) {
            std::cerr
                << "response body is not object"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        return true;
    }

    bool ValidateRejectResponse(
        const tinyimx::Packet& packet,
        std::uint32_t expected_seq,
        std::uint64_t expected_request_id,
        std::uint64_t expected_user_id,
        const std::string& expected_reason,
        bool expected_success,
        bool expected_changed
    ) {
        if (packet.type !=
            tinyimx::MessageType::
                kFriendRequestRejectResponse) {
            std::cerr
                << "expected friend request "
                "reject response"
                << ", actual="
                << tinyimx::MessageTypeToString(
                    packet.type
                )
                << '\n';

            return false;
        }

        if (packet.seq != expected_seq) {
            std::cerr
                << "reject response seq mismatch"
                << ", expected="
                << expected_seq
                << ", actual="
                << packet.seq
                << '\n';

            return false;
        }

        Json body;

        if (!ParseJsonObject(packet, &body)) {
            return false;
        }

        const std::uint64_t request_id =
            body.value(
                "request_id",
                std::uint64_t{0}
            );

        if (request_id != expected_request_id) {
            std::cerr
                << "reject request_id mismatch"
                << ", expected="
                << expected_request_id
                << ", actual="
                << request_id
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        const std::uint64_t user_id =
            body.value(
                "user_id",
                std::uint64_t{0}
            );

        if (user_id != expected_user_id) {
            std::cerr
                << "reject user_id mismatch"
                << ", expected="
                << expected_user_id
                << ", actual="
                << user_id
                << '\n';

            return false;
        }

        const std::string reason =
            body.value(
                "reason",
                std::string{}
            );

        if (reason != expected_reason) {
            std::cerr
                << "reject reason mismatch"
                << ", expected="
                << expected_reason
                << ", actual="
                << reason
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        const bool success =
            body.value("success", false);

        const bool changed =
            body.value("changed", false);

        if (success != expected_success ||
            changed != expected_changed) {
            std::cerr
                << "reject state mismatch"
                << ", expected_success="
                << expected_success
                << ", actual_success="
                << success
                << ", expected_changed="
                << expected_changed
                << ", actual_changed="
                << changed
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        return true;
    }

    bool ValidateFriendListDoesNotContainUser(
        const tinyimx::Packet& packet,
        std::uint32_t expected_seq,
        std::uint64_t expected_user_id,
        std::uint64_t unexpected_friend_user_id
    ) {
        if (packet.type !=
            tinyimx::MessageType::
                kFriendListResponse) {
            std::cerr
                << "expected friend list response"
                << ", actual="
                << tinyimx::MessageTypeToString(
                    packet.type
                )
                << '\n';

            return false;
        }

        if (packet.seq != expected_seq) {
            std::cerr
                << "friend list seq mismatch"
                << ", expected="
                << expected_seq
                << ", actual="
                << packet.seq
                << '\n';

            return false;
        }

        Json body;

        if (!ParseJsonObject(packet, &body)) {
            return false;
        }

        if (!body.value("success", false)) {
            std::cerr
                << "friend list request failed"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        const std::uint64_t user_id =
            body.value(
                "user_id",
                std::uint64_t{0}
            );

        if (user_id != expected_user_id) {
            std::cerr
                << "friend list user_id mismatch"
                << ", expected="
                << expected_user_id
                << ", actual="
                << user_id
                << '\n';

            return false;
        }

        if (!body.contains("friends") ||
            !body["friends"].is_array()) {
            std::cerr
                << "friends must be an array"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        for (const auto& item : body["friends"]) {
            if (!item.is_object()) {
                continue;
            }

            const std::uint64_t friend_user_id =
                item.value(
                    "friend_user_id",
                    std::uint64_t{0}
                );

            if (friend_user_id ==
                unexpected_friend_user_id) {
                std::cerr
                    << "unexpected friend found"
                    << ", user_id="
                    << expected_user_id
                    << ", friend_user_id="
                    << unexpected_friend_user_id
                    << ", item="
                    << item.dump()
                    << '\n';

                return false;
            }
        }

        return true;
    }

    void PrintPacket(
        const std::string& tag,
        const tinyimx::Packet& packet
    ) {
        std::cout << tag
                  << ": type = "
                  << tinyimx::MessageTypeToString(packet.type)
                    << ", seq = " << packet.seq
                    << ", body = " << packet.body
                    << '\n';
    }



    bool ValidateLoginResponse(
        const tinyimx::Packet& packet,
        std::uint32_t expected_seq,
        std::uint64_t expected_user_id,
        const std::string& expected_username
    ) {
        if (packet.type !=
            tinyimx::MessageType::kLoginResponse) {
            std::cerr
                << "expected login_response, got "
                << tinyimx::MessageTypeToString(
                    packet.type
                )
                << '\n';

            return false;
        }

        if (packet.seq != expected_seq) {
            std::cerr
                << "login response seq mismatch"
                << ", expected=" << expected_seq
                << ", actual=" << packet.seq
                << '\n';

            return false;
        }

        Json body;

        if (!ParseJsonObject(packet, &body)) {
            return false;
        }

        if (!body.value("success", false)) {
            std::cerr
                << "login rejected"
                << ", reason="
                << body.value("reason", "")
                << ", message="
                << body.value("message", "")
                << '\n';

            return false;
        }

        const std::uint64_t user_id =
            body.value(
                "user_id",
                std::uint64_t{0}
            );

        const std::string username =
            body.value(
                "username",
                std::string{}
            );

        if (user_id != expected_user_id) {
            std::cerr
                << "login user_id mismatch"
                << ", expected="
                << expected_user_id
                << ", actual="
                << user_id
                << '\n';

            return false;
        }

        if (username != expected_username) {
            std::cerr
                << "login username mismatch"
                << ", expected="
                << expected_username
                << ", actual="
                << username
                << '\n';

            return false;
        }

        return true;
    }


    bool ValidateAcceptResponse(
        const tinyimx::Packet& packet,
        std::uint32_t expected_seq,
        std::uint64_t expected_request_id,
        std::uint64_t expected_user_id,
        const std::string& expected_reason,
        bool expected_success,
        bool expected_changed
    ) {
        if (packet.type !=
            tinyimx::MessageType::
                kFriendRequestAcceptResponse) {
            std::cerr
                << "expected friend request "
                "accept response"
                << ", actual="
                << tinyimx::MessageTypeToString(
                    packet.type
                )
                << '\n';

            return false;
        }

        if (packet.seq != expected_seq) {
            std::cerr
                << "accept response seq mismatch"
                << ", expected="
                << expected_seq
                << ", actual="
                << packet.seq
                << '\n';

            return false;
        }

        Json body;

        if (!ParseJsonObject(
                packet,
                &body
            )) {
            return false;
        }

        const std::uint64_t request_id =
            body.value(
                "request_id",
                std::uint64_t{0}
            );

        if (request_id != expected_request_id) {
            std::cerr
                << "accept request_id mismatch"
                << ", expected="
                << expected_request_id
                << ", actual="
                << request_id
                << '\n';

            return false;
        }

        const std::uint64_t user_id =
            body.value(
                "user_id",
                std::uint64_t{0}
            );

        if (user_id != expected_user_id) {
            std::cerr
                << "accept user_id mismatch"
                << ", expected="
                << expected_user_id
                << ", actual="
                << user_id
                << '\n';

            return false;
        }

        const std::string reason =
            body.value(
                "reason",
                std::string{}
            );

        if (reason != expected_reason) {
            std::cerr
                << "accept reason mismatch"
                << ", expected="
                << expected_reason
                << ", actual="
                << reason
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        const bool success =
            body.value(
                "success",
                false
            );

        if (success != expected_success) {
            std::cerr
                << "accept success mismatch"
                << ", expected="
                << expected_success
                << ", actual="
                << success
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        const bool changed =
            body.value(
                "changed",
                false
            );

        if (changed != expected_changed) {
            std::cerr
                << "accept changed mismatch"
                << ", expected="
                << expected_changed
                << ", actual="
                << changed
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        return true;
    }

    bool ValidateRejectedResponse(
        const tinyimx::Packet& packet,
        tinyimx::MessageType expected_type,
        std::uint32_t expected_seq,
        const std::string& expected_reason
    ) {
        if (packet.type != expected_type) {
            std::cerr
                << "response type mismatch"
                << ", expected="
                << tinyimx::MessageTypeToString(
                    expected_type
                )
                << ", actual="
                << tinyimx::MessageTypeToString(
                    packet.type
                )
                << '\n';

            return false;
        }

        if (packet.seq != expected_seq) {
            std::cerr
                << "response seq mismatch"
                << ", expected="
                << expected_seq
                << ", actual="
                << packet.seq
                << '\n';

            return false;
        }

        Json body;

        if (!ParseJsonObject(packet, &body)) {
            return false;
        }

        if (body.value("success", true)) {
            std::cerr
                << "request should be rejected"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        const std::string actual_reason =
            body.value(
                "reason",
                std::string{}
            );

        if (actual_reason != expected_reason) {
            std::cerr
                << "rejection reason mismatch"
                << ", expected="
                << expected_reason
                << ", actual="
                << actual_reason
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        return true;
    }

    bool ValidateCreateSuccess(
        const tinyimx::Packet& packet,
        std::uint32_t expected_seq,
        std::uint64_t expected_from_user_id,
        std::uint64_t expected_to_user_id,
        std::uint64_t* request_id
    ) {
        if (request_id == nullptr) {
            return false;
        }

        if (packet.type !=
            tinyimx::MessageType::
                kFriendRequestCreateResponse) {
            std::cerr
                << "expected friend request create response"
                << ", actual="
                << tinyimx::MessageTypeToString(
                    packet.type
                )
                << '\n';

            return false;
        }

        if (packet.seq != expected_seq) {
            std::cerr
                << "create response seq mismatch"
                << ", expected="
                << expected_seq
                << ", actual="
                << packet.seq
                << '\n';

            return false;
        }

        Json body;

        if (!ParseJsonObject(packet, &body)) {
            return false;
        }

        if (!body.value("success", false)) {
            std::cerr
                << "friend request create failed"
                << ", reason="
                << body.value(
                    "reason",
                    std::string{}
                )
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        const std::string reason =
            body.value(
                "reason",
                std::string{}
            );

        if (reason != "created" &&
            reason != "reopened" &&
            reason != "already_pending") {
            std::cerr
                << "unexpected create reason"
                << ", reason="
                << reason
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        const std::uint64_t from_user_id =
            body.value(
                "from_user_id",
                std::uint64_t{0}
            );

        const std::uint64_t to_user_id =
            body.value(
                "to_user_id",
                std::uint64_t{0}
            );

        if (from_user_id !=
            expected_from_user_id) {
            std::cerr
                << "from_user_id mismatch"
                << ", expected="
                << expected_from_user_id
                << ", actual="
                << from_user_id
                << '\n';

            return false;
        }

        if (to_user_id !=
            expected_to_user_id) {
            std::cerr
                << "to_user_id mismatch"
                << ", expected="
                << expected_to_user_id
                << ", actual="
                << to_user_id
                << '\n';

            return false;
        }

        *request_id =
            body.value(
                "request_id",
                std::uint64_t{0}
            );

        if (*request_id == 0) {
            std::cerr
                << "request_id must not be zero"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        const bool changed =
            body.value(
                "changed",
                false
            );

        if ((reason == "created" ||
            reason == "reopened") &&
            !changed) {
            std::cerr
                << "created/reopened must set "
                "changed=true"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        if (reason == "already_pending" &&
            changed) {
            std::cerr
                << "already_pending must set "
                "changed=false"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        return true;
    }

    bool ValidateListContainsRequest(
        const tinyimx::Packet& packet,
        std::uint32_t expected_seq,
        std::uint64_t expected_user_id,
        std::uint64_t expected_request_id,
        std::uint64_t expected_from_user_id,
        std::uint64_t expected_to_user_id
    ) {
        if (packet.type !=
            tinyimx::MessageType::
                kFriendRequestListResponse) {
            std::cerr
                << "expected friend request list response"
                << ", actual="
                << tinyimx::MessageTypeToString(
                    packet.type
                )
                << '\n';

            return false;
        }

        if (packet.seq != expected_seq) {
            std::cerr
                << "list response seq mismatch"
                << ", expected="
                << expected_seq
                << ", actual="
                << packet.seq
                << '\n';

            return false;
        }

        Json body;

        if (!ParseJsonObject(packet, &body)) {
            return false;
        }

        if (!body.value("success", false)) {
            std::cerr
                << "friend request list failed"
                << ", reason="
                << body.value(
                    "reason",
                    std::string{}
                )
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        const std::uint64_t user_id =
            body.value(
                "user_id",
                std::uint64_t{0}
            );

        if (user_id != expected_user_id) {
            std::cerr
                << "list user_id mismatch"
                << ", expected="
                << expected_user_id
                << ", actual="
                << user_id
                << '\n';

            return false;
        }

        if (!body.contains("requests") ||
            !body["requests"].is_array()) {
            std::cerr
                << "requests must be an array"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        for (const auto& item :
            body["requests"]) {
            if (!item.is_object()) {
                continue;
            }

            const std::uint64_t request_id =
                item.value(
                    "request_id",
                    std::uint64_t{0}
                );

            if (request_id !=
                expected_request_id) {
                continue;
            }

            const std::uint64_t from_user_id =
                item.value(
                    "from_user_id",
                    std::uint64_t{0}
                );

            const std::uint64_t to_user_id =
                item.value(
                    "to_user_id",
                    std::uint64_t{0}
                );

            if (from_user_id !=
                    expected_from_user_id ||
                to_user_id !=
                    expected_to_user_id) {
                std::cerr
                    << "friend request direction mismatch"
                    << ", item="
                    << item.dump()
                    << '\n';

                return false;
            }

            const std::uint32_t request_status =
                item.value(
                    "request_status",
                    std::uint32_t{99}
                );

            const std::string
                request_status_name =
                    item.value(
                        "request_status_name",
                        std::string{}
                    );

            if (request_status != 0 ||
                request_status_name != "pending") {
                std::cerr
                    << "friend request status mismatch"
                    << ", item="
                    << item.dump()
                    << '\n';

                return false;
            }

            return true;
        }

        std::cerr
            << "expected friend request not found"
            << ", request_id="
            << expected_request_id
            << ", body="
            << packet.body
            << '\n';

        return false;
    }

    bool ValidateListDoesNotContainRequest(
        const tinyimx::Packet& packet,
        std::uint32_t expected_seq,
        std::uint64_t expected_user_id,
        std::uint64_t expected_request_id
    ) {
        if (packet.type !=
            tinyimx::MessageType::
                kFriendRequestListResponse) {
            std::cerr
                << "expected friend request list response"
                << '\n';

            return false;
        }

        if (packet.seq != expected_seq) {
            std::cerr
                << "list response seq mismatch"
                << ", expected="
                << expected_seq
                << ", actual="
                << packet.seq
                << '\n';

            return false;
        }

        Json body;

        if (!ParseJsonObject(
                packet,
                &body
            )) {
            return false;
        }

        if (!body.value(
                "success",
                false
            )) {
            std::cerr
                << "friend request list failed"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        if (body.value(
                "user_id",
                std::uint64_t{0}
            ) != expected_user_id) {
            std::cerr
                << "friend request list user mismatch"
                << '\n';

            return false;
        }

        if (!body.contains("requests") ||
            !body["requests"].is_array()) {
            std::cerr
                << "requests must be array"
                << '\n';

            return false;
        }

        for (const auto& item :
            body["requests"]) {
            if (!item.is_object()) {
                continue;
            }

            if (item.value(
                    "request_id",
                    std::uint64_t{0}
                ) == expected_request_id) {
                std::cerr
                    << "accepted request still appears "
                    "in pending list"
                    << ", request_id="
                    << expected_request_id
                    << '\n';

                return false;
            }
        }

        return true;
    }

    bool ValidateFriendListContainsUser(
        const tinyimx::Packet& packet,
        std::uint32_t expected_seq,
        std::uint64_t expected_user_id,
        std::uint64_t expected_friend_user_id
    ) {
        if (packet.type !=
            tinyimx::MessageType::
                kFriendListResponse) {
            std::cerr
                << "expected friend list response"
                << ", actual="
                << tinyimx::MessageTypeToString(
                    packet.type
                )
                << '\n';

            return false;
        }

        if (packet.seq != expected_seq) {
            std::cerr
                << "friend list seq mismatch"
                << ", expected="
                << expected_seq
                << ", actual="
                << packet.seq
                << '\n';

            return false;
        }

        Json body;

        if (!ParseJsonObject(
                packet,
                &body
            )) {
            return false;
        }

        if (!body.value(
                "success",
                false
            )) {
            std::cerr
                << "friend list request failed"
                << ", body="
                << packet.body
                << '\n';

            return false;
        }

        const std::uint64_t user_id =
            body.value(
                "user_id",
                std::uint64_t{0}
            );

        if (user_id != expected_user_id) {
            std::cerr
                << "friend list user_id mismatch"
                << ", expected="
                << expected_user_id
                << ", actual="
                << user_id
                << '\n';

            return false;
        }

        if (!body.contains("friends") ||
            !body["friends"].is_array()) {
            std::cerr
                << "friends must be array"
                << '\n';

            return false;
        }

        for (const auto& item :
            body["friends"]) {
            if (!item.is_object()) {
                continue;
            }

            const std::uint64_t friend_user_id =
                item.value(
                    "friend_user_id",
                    std::uint64_t{0}
                );

            if (friend_user_id !=
                expected_friend_user_id) {
                continue;
            }

            const std::uint32_t relation_status =
                item.value(
                    "relation_status",
                    std::uint32_t{0}
                );

            if (relation_status != 1) {
                std::cerr
                    << "friend relation status mismatch"
                    << ", friend_user_id="
                    << friend_user_id
                    << ", relation_status="
                    << relation_status
                    << '\n';

                return false;
            }

            return true;
        }

        std::cerr
            << "expected friend not found"
            << ", user_id="
            << expected_user_id
            << ", friend_user_id="
            << expected_friend_user_id
            << '\n';

        return false;
    }

    bool SendAndReceive(
        GatewayTestClient& client,
        const tinyimx::Packet& request,
        const std::string& tag,
        tinyimx::Packet* response
    ) {
        if (response == nullptr) {
            std::cerr
                << tag
                << " response output is null"
                << '\n';

            return false;
        }

        if (!client.SendPacket(request)) {
            std::cerr
                << tag
                << " send failed"
                << '\n';

            return false;
        }

        if (!client.ReceivePacket(response)) {
            std::cerr
                << tag
                << " receive failed"
                << '\n';

            return false;
        }

        PrintPacket(tag, *response);

        return true;
    }

} // namespace

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;

    if (argc >= 2) {
        host = argv[1];
    }

    if (argc >= 3 ) {
        try {
            const unsigned long parsed_port = std::stoul(argv[2]);

            if (parsed_port == 0 || parsed_port > 65535) {
                std::cerr << "invalid port: " << argv[2] << "\n";
                return 1;
            }

            port = static_cast<std::uint16_t>(parsed_port);
        } catch (const std::exception& e) {
            std::cerr << "parse port failed: " << e.what() << "\n";
            return 1;
        }
    }

        GatewayTestClient requester_client;

    if (!requester_client.Connect(
            host,
            port
        )) {
        std::cerr
            << "requester client connect failed"
            << '\n';

        return 1;
    }

    std::cout
        << "requester client connected"
        << ", host=" << host
        << ", port=" << port
        << '\n';

    constexpr std::uint32_t
        kRequesterLoginSeq = 1;

    tinyimx::Packet response;

    if (!SendAndReceive(
            requester_client,
            MakeLoginRequest(
                kRequesterUsername,
                kDemoPassword,
                kRequesterLoginSeq
            ),
            "[requester_login]",
            &response
        )) {
        return 1;
    }

    if (!ValidateLoginResponse(
            response,
            kRequesterLoginSeq,
            kRequesterUserId,
            kRequesterUsername
        )) {
        std::cerr
            << "requester login validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "requester login validation passed"
        << '\n';

        constexpr std::uint32_t
        kCreateSeq = 2;

    if (!SendAndReceive(
            requester_client,
            MakeFriendRequestCreateRequest(
                kReceiverUserId,
                kRequestMessage,
                kCreateSeq
            ),
            "[create]",
            &response
        )) {
        return 1;
    }

    std::uint64_t request_id = 0;

    if (!ValidateCreateSuccess(
            response,
            kCreateSeq,
            kRequesterUserId,
            kReceiverUserId,
            &request_id
        )) {
        std::cerr
            << "friend request create "
               "validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "friend request create "
           "validation passed"
        << ", request_id="
        << request_id
        << '\n';

        GatewayTestClient no_login_client;

    if (!no_login_client.Connect(
            host,
            port
        )) {
        std::cerr
            << "no-login client connect failed"
            << '\n';

        return 1;
    }

    constexpr std::uint32_t
        kNoLoginRejectSeq = 3;

    if (!SendAndReceive(
            no_login_client,
            MakeFriendRequestRejectRequest(
                request_id,
                kNoLoginRejectSeq
            ),
            "[no_login_reject]",
            &response
        )) {
        return 1;
    }

    if (!ValidateRejectedResponse(
            response,
            tinyimx::MessageType::
                kFriendRequestRejectResponse,
            kNoLoginRejectSeq,
            "not_logged_in"
        )) {
        std::cerr
            << "no-login reject validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "no-login reject validation passed"
        << '\n';

    no_login_client.Close();

        GatewayTestClient receiver_client;

    if (!receiver_client.Connect(
            host,
            port
        )) {
        std::cerr
            << "receiver client connect failed"
            << '\n';

        return 1;
    }

    std::cout
        << "receiver client connected"
        << ", host=" << host
        << ", port=" << port
        << '\n';

    constexpr std::uint32_t
        kReceiverLoginSeq = 4;

    if (!SendAndReceive(
            receiver_client,
            MakeLoginRequest(
                kReceiverUsername,
                kDemoPassword,
                kReceiverLoginSeq
            ),
            "[receiver_login]",
            &response
        )) {
        return 1;
    }

    if (!ValidateLoginResponse(
            response,
            kReceiverLoginSeq,
            kReceiverUserId,
            kReceiverUsername
        )) {
        std::cerr
            << "receiver login validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "receiver login validation passed"
        << '\n';


    constexpr std::uint32_t
        kListBeforeRejectSeq = 5;

    if (!SendAndReceive(
            receiver_client,
            MakeFriendRequestListRequest(
                20,
                "",
                0,
                kListBeforeRejectSeq
            ),
            "[list_before_reject]",
            &response
        )) {
        return 1;
    }

    if (!ValidateListContainsRequest(
            response,
            kListBeforeRejectSeq,
            kReceiverUserId,
            request_id,
            kRequesterUserId,
            kReceiverUserId
        )) {
        std::cerr
            << "list before reject "
               "validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "list before reject "
           "validation passed"
        << ", request_id="
        << request_id
        << '\n';


    constexpr std::uint32_t
        kWrongReceiverRejectSeq = 6;

    if (!SendAndReceive(
            requester_client,
            MakeFriendRequestRejectRequest(
                request_id,
                kWrongReceiverRejectSeq
            ),
            "[wrong_receiver_reject]",
            &response
        )) {
        return 1;
    }

    if (!ValidateRejectResponse(
            response,
            kWrongReceiverRejectSeq,
            request_id,
            kRequesterUserId,
            "not_request_receiver",
            false,
            false
        )) {
        std::cerr
            << "wrong receiver reject "
               "validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "wrong receiver reject "
           "validation passed"
        << '\n';

    constexpr std::uint32_t
        kSpoofRejectSeq = 7;

    if (!SendAndReceive(
            receiver_client,
            MakeFriendRequestRejectRequest(
                request_id,
                kSpoofRejectSeq,
                kRequesterUserId
            ),
            "[spoof_reject]",
            &response
        )) {
        return 1;
    }

    if (!ValidateRejectedResponse(
            response,
            tinyimx::MessageType::
                kFriendRequestRejectResponse,
            kSpoofRejectSeq,
            "forbidden_identity_field"
        )) {
        std::cerr
            << "spoof reject validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "spoof reject validation passed"
        << '\n';

    constexpr std::uint32_t
        kRejectSeq = 8;

    if (!SendAndReceive(
            receiver_client,
            MakeFriendRequestRejectRequest(
                request_id,
                kRejectSeq
            ),
            "[reject]",
            &response
        )) {
        return 1;
    }

    if (!ValidateRejectResponse(
            response,
            kRejectSeq,
            request_id,
            kReceiverUserId,
            "rejected",
            true,
            true
        )) {
        std::cerr
            << "friend request reject "
               "validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "friend request reject "
           "validation passed"
        << '\n';


    constexpr std::uint32_t
        kAlreadyRejectedSeq = 9;

    if (!SendAndReceive(
            receiver_client,
            MakeFriendRequestRejectRequest(
                request_id,
                kAlreadyRejectedSeq
            ),
            "[already_rejected]",
            &response
        )) {
        return 1;
    }

    if (!ValidateRejectResponse(
            response,
            kAlreadyRejectedSeq,
            request_id,
            kReceiverUserId,
            "already_rejected",
            true,
            false
        )) {
        std::cerr
            << "repeated reject validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "repeated reject validation passed"
        << '\n';

    std::cout
        << "gateway friend request reject "
           "main-chain validation passed"
        << '\n';

    constexpr std::uint32_t
        kListAfterRejectSeq = 10;

    if (!SendAndReceive(
            receiver_client,
            MakeFriendRequestListRequest(
                20,
                "",
                0,
                kListAfterRejectSeq
            ),
            "[list_after_reject]",
            &response
        )) {
        return 1;
    }

    if (!ValidateListDoesNotContainRequest(
            response,
            kListAfterRejectSeq,
            kReceiverUserId,
            request_id
        )) {
        std::cerr
            << "rejected request still appears "
            "in pending list"
            << '\n';

        return 1;
    }

    std::cout
        << "rejected request removed from "
        "pending list validation passed"
        << '\n';



    constexpr std::uint32_t
        kAcceptAfterRejectSeq = 11;

    if (!SendAndReceive(
            receiver_client,
            MakeFriendRequestAcceptRequest(
                request_id,
                kAcceptAfterRejectSeq
            ),
            "[accept_after_reject]",
            &response
        )) {
        return 1;
    }

    if (!ValidateAcceptResponse(
            response,
            kAcceptAfterRejectSeq,
            request_id,
            kReceiverUserId,
            "request_not_pending",
            false,
            false
        )) {
        std::cerr
            << "accept after reject validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "accept after reject validation passed"
        << '\n';


    constexpr std::uint32_t
        kRequesterFriendListSeq = 12;

    if (!SendAndReceive(
            requester_client,
            MakeFriendListRequest(
                20,
                kRequesterFriendListSeq
            ),
            "[requester_friend_list]",
            &response
        )) {
        return 1;
    }

    if (!ValidateFriendListDoesNotContainUser(
            response,
            kRequesterFriendListSeq,
            kRequesterUserId,
            kReceiverUserId
        )) {
        std::cerr
            << "requester friend-list absence "
            "validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "requester friend list does not "
        "contain receiver validation passed"
        << '\n';

    constexpr std::uint32_t
        kReceiverFriendListSeq = 13;

    if (!SendAndReceive(
            receiver_client,
            MakeFriendListRequest(
                20,
                kReceiverFriendListSeq
            ),
            "[receiver_friend_list]",
            &response
        )) {
        return 1;
    }

    if (!ValidateFriendListDoesNotContainUser(
            response,
            kReceiverFriendListSeq,
            kReceiverUserId,
            kRequesterUserId
        )) {
        std::cerr
            << "receiver friend-list absence "
            "validation failed"
            << '\n';

        return 1;
    }

    std::cout
        << "receiver friend list does not "
        "contain requester validation passed"
        << '\n';

    std::cout
        << "gateway friend request reject "
        "final-state validation passed"
        << '\n';

    return 0;

}
