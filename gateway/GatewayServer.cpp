#include "gateway/GatewayServer.h"

#include "common/logging/LogMacros.h"
#include "common/net/EventLoop.h"
#include "services/repository/MessageRepository.h"
#include "services/repository/UserRepository.h"
#include "services/repository/FriendRequestRepository.h"
#include "services/cache/OnlineStatusCache.h"
#include "services/cache/UnreadCountCache.h"

#include <utility>
#include <nlohmann/json.hpp>
#include <optional>

namespace {

using Json = nlohmann::json;

bool ParseJsonBody(const tinyimx::Packet& packet,
                   Json* body,
                   std::string* error_message) {
    if (body == nullptr) {
        return false;
    }

    try {
        *body = Json::parse(packet.body);
        return true;
    } catch (const std::exception& e) {
        if (error_message != nullptr) {
            *error_message = e.what();
        }
        return false;
    }
}

bool GetUserIdField(const Json& body,
                    const char* field_name,
                    tinyimx::UserId* user_id,
                    std::string* error_message) {
    if (user_id == nullptr || field_name == nullptr) {
        return false;
    }

    if (!body.contains(field_name)) {
        if (error_message != nullptr) {
            *error_message =
                std::string("missing field: ") + field_name;
        }
        return false;
    }

    if (!body.at(field_name).is_number_unsigned() &&
        !body.at(field_name).is_number_integer()) {
        if (error_message != nullptr) {
            *error_message =
                std::string("invalid user id field: ") + field_name;
        }
        return false;
    }

    const auto value = body.at(field_name).get<std::int64_t>();
    if (value <= 0) {
        if (error_message != nullptr) {
            *error_message =
                std::string("user id must be positive: ") + field_name;
        }
        return false;
    }

    *user_id = static_cast<tinyimx::UserId>(value);
    return true;
}

}  // namespace

namespace tinyimx {

/*
    GatewayServer::GatewayServer(EventLoop* loop,
                                const InetAddress& listen_address,
                                GatewayServerOptions options)
        : loop_(loop),
        options_(std::move(options)),
        codec_(options_.max_body_size),
        server_(loop_, listen_address, options_.name) {
*/
GatewayServer::GatewayServer(EventLoop* loop,
                            const InetAddress& listen_address,
                            GatewayServerOptions options)
    : loop_(loop),
        options_(std::move(options)),
        codec_(options_.max_body_size),
        server_(loop_, listen_address, options_.name),
        offline_message_store_(
            options_.max_offline_messages_per_user) {
    server_.SetConnectionCallback([this](
        const TcpConnectionPtr& connection
    ) {
        HandleConnection(connection);
    });

    server_.SetMessageCallback([this](
        const TcpConnectionPtr& connection,
        Buffer* buffer
    ) {
        HandleMessage(connection, buffer);
    });
}


GatewayServer::~GatewayServer() {
    Stop();
}

bool GatewayServer::Start() {
    LOG_INFO("gateway server starting"
             << ", name=" << options_.name
             << ", listen=" << server_.ListenAddress().ToString()
             << ", max_body_size=" << options_.max_body_size);

    const bool ok = server_.Start();

    if (!ok) {
        LOG_ERROR("gateway server start failed"
                  << ", name=" << options_.name);
        return false;
    }

    LOG_INFO("gateway server started"
             << ", name=" << options_.name
             << ", listen=" << server_.ListenAddress().ToString());

    return true;
}

void GatewayServer::Stop() {
    server_.Stop();

    LOG_INFO("gateway server stopped"
             << ", name=" << options_.name);
}

void GatewayServer::SetPacketHandler(PacketHandler handler) {
    packet_handler_ = std::move(handler);
}

void GatewayServer::SetOnlineStatusCache(
    OnlineStatusCache* online_status_cache
) {
    online_status_cache_ = online_status_cache;

    LOG_INFO("gateway online status cache attached"
             << ", enabled=" << (online_status_cache_ != nullptr));
}


bool GatewayServer::HasOnlineStatusCache() const {
    return online_status_cache_ != nullptr;
}

void GatewayServer::SetUnreadCountCache(
    UnreadCountCache* unread_count_cache
) {
    unread_count_cache_ = unread_count_cache;

    LOG_INFO("gateway unread count cache attached"
             << ", enabled=" << (unread_count_cache_ != nullptr));
}

bool GatewayServer::HasUnreadCountCache() const {
    return unread_count_cache_ != nullptr;
}

std::int64_t GatewayServer::GetTotalUnread(UserId user_id) {
    if (!HasUnreadCountCache()) {
        return 0;
    }

    const auto total =
        unread_count_cache_->GetTotalUnread(user_id);

    if (!total.has_value()) {
        LOG_WARN("gateway get total unread failed"
                 << ", user_id=" << user_id
                 << ", error=" << unread_count_cache_->LastError());
        return 0;
    }

    return total.value();
}

std::int64_t GatewayServer::GetPrivateUnread(
    UserId receiver_user_id,
    UserId sender_user_id
) {
    if (!HasUnreadCountCache()) {
        return 0;
    }

    const auto private_unread =
        unread_count_cache_->GetPrivateUnread(
            receiver_user_id,
            sender_user_id
        );

    if (!private_unread.has_value()) {
        LOG_WARN("gateway get private unread failed"
                 << ", receiver=" << receiver_user_id
                 << ", sender=" << sender_user_id
                 << ", error=" << unread_count_cache_->LastError());
        return 0;
    }

    return private_unread.value();
}

std::int64_t GatewayServer::IncrementUnread(
    UserId receiver_user_id,
    UserId sender_user_id,
    std::int64_t* total_unread
) {
    if (total_unread != nullptr) {
        *total_unread = 0;
    }

    if (!HasUnreadCountCache()) {
        return 0;
    }

    const auto private_unread =
        unread_count_cache_->IncrementPrivateUnread(
            receiver_user_id,
            sender_user_id
        );

    if (!private_unread.has_value()) {
        LOG_WARN("gateway increment unread failed"
                 << ", receiver=" << receiver_user_id
                 << ", sender=" << sender_user_id
                 << ", error=" << unread_count_cache_->LastError());
        return 0;
    }

    const auto total =
        unread_count_cache_->GetTotalUnread(receiver_user_id);

    if (total.has_value() && total_unread != nullptr) {
        *total_unread = total.value();
    }

    return private_unread.value();
}

void GatewayServer::SetUserRepository(
    UserRepository* user_repository
) {
    user_repository_ = user_repository;

    LOG_INFO("gateway user repository attached"
             << ", enabled=" << (user_repository_ != nullptr));
}

bool GatewayServer::HasUserRepository() const {
    return user_repository_ != nullptr;
}

void GatewayServer::SetMessageRepository(
    MessageRepository* message_repository
) {
    message_repository_ = message_repository;

    LOG_INFO("gateway message repository attached"
             << ", enabled=" << (message_repository_ != nullptr));
}


bool GatewayServer::HasMessageRepository() const {
    return message_repository_ != nullptr;
}


void GatewayServer::SetFriendRepository(
    FriendRepository* repository
) {
    friend_repository_ = repository;

    LOG_INFO("gateway friend repository attached"
             << ", enabled=" << (friend_repository_ != nullptr));
}

bool GatewayServer::HasFriendRepository() const {
    return friend_repository_ != nullptr;
}

void GatewayServer::SetFriendRequestRepository(
    FriendRequestRepository* repository
) {
    friend_request_repository_ = repository;

    LOG_INFO("gateway friend request repository attached"
             << ", enabled="
             << (friend_request_repository_ != nullptr));
}

bool GatewayServer::HasFriendRequestRepository() const {
    return friend_request_repository_ != nullptr;
}

bool GatewayServer::SendPacket(const TcpConnectionPtr& connection,
                               const Packet& packet) {
    if (!connection || !connection->IsConnected()) {
        LOG_WARN("gateway send packet ignored: invalid connection");
        return false;
    }

    Buffer output;
    std::string error_message;

    if (!codec_.Encode(packet, &output, &error_message)) {
        LOG_ERROR("gateway encode packet failed"
                  << ", type=" << MessageTypeToString(packet.type)
                  << ", seq=" << packet.seq
                  << ", error=" << error_message);
        return false;
    }

    const std::string bytes = output.RetrieveAllAsString();
    connection->Send(bytes);

    return true;
}

const std::string& GatewayServer::Name() const {
    return options_.name;
}

const InetAddress& GatewayServer::ListenAddress() const {
    return server_.ListenAddress();
}

std::size_t GatewayServer::ConnectionCount() const {
    return server_.ConnectionCount();
}
/*
    void GatewayServer::HandleConnection(
        const TcpConnectionPtr& connection
    ) {
        if (!connection) {
            return;
        }

        if (connection->IsConnected()) {
            LOG_INFO("gateway client connected"
                    << ", name=" << options_.name
                    << ", peer=" << connection->PeerAddress().ToString()
                    << ", connection_count=" << server_.ConnectionCount());
        }
    }
*/


void GatewayServer::HandleConnection(
    const TcpConnectionPtr& connection
) {
    if (!connection) {
        return;
    }

    if (connection->IsConnected()) {
        LOG_INFO("gateway client connected"
                 << ", name=" << options_.name
                 << ", peer=" << connection->PeerAddress().ToString()
                 << ", connection_count=" << server_.ConnectionCount());
        return;
    }

    const auto user_id =
    session_manager_.FindUserByConnection(connection);

    if (user_id.has_value()) {
        SetUserOffline(user_id.value());
    }

    session_manager_.UnbindByConnection(connection);

    //session_manager_.UnbindByConnection(connection);

    LOG_INFO("gateway client disconnected"
             << ", name=" << options_.name
             << ", peer=" << connection->PeerAddress().ToString()
             << ", online_count=" << session_manager_.OnlineCount());
}

void GatewayServer::HandleMessage(const TcpConnectionPtr& connection,
                                  Buffer* buffer) {
    if (!connection || buffer == nullptr) {
        return;
    }

    const DecodeResult decode_result = codec_.Decode(buffer);

    if (decode_result.status == DecodeStatus::kNeedMoreData) {
        return;
    }

    if (decode_result.status != DecodeStatus::kOk) {
        LOG_WARN("gateway decode packet failed"
                 << ", peer=" << connection->PeerAddress().ToString()
                 << ", status="
                 << DecodeStatusToString(decode_result.status)
                 << ", error=" << decode_result.error_message);

        const Packet error_packet =
            MakeErrorPacket(0, decode_result.error_message);

        SendPacket(connection, error_packet);

        if (options_.close_on_decode_error) {
            connection->Shutdown();
        }

        return;
    }

    for (const auto& packet : decode_result.packets) {
        LOG_INFO("gateway received packet"
                 << ", peer=" << connection->PeerAddress().ToString()
                 << ", type=" << MessageTypeToString(packet.type)
                 << ", seq=" << packet.seq
                 << ", body_size=" << packet.body.size());

        HandlePacket(connection, packet);
    }
}
/*
    void GatewayServer::HandlePacket(const TcpConnectionPtr& connection,
                                 const Packet& packet) {
        if (packet_handler_) {
            packet_handler_(connection, packet);
            return;
        }

        const Packet response = BuildDefaultResponse(packet);
        SendPacket(connection, response);
    }


*/

void GatewayServer::HandlePacket(const TcpConnectionPtr& connection,
                                 const Packet& packet) {
    if (packet_handler_) {
        packet_handler_(connection, packet);
        return;
    }

    switch (packet.type) {
        case MessageType::kLoginRequest:
            HandleLoginRequest(connection, packet);
            return;

        case MessageType::kChatMessage:
            HandleChatMessage(connection, packet);
            return;

        case MessageType::kReadRequest:
            HandleReadRequest(connection, packet);
            break;

        case MessageType::kHistoryRequest:
            HandleHistoryRequest(connection, packet);
            break;

        case MessageType::kConversationListRequest:
            HandleConversationListRequest(connection, packet);
            break;

        case MessageType::kFriendListRequest:
            HandleFriendListRequest(connection, packet);
            break;


        case MessageType::kFriendRequestCreateRequest:
            HandleFriendRequestCreateRequest(
                connection,
                packet
            );
            break;

        case MessageType::kFriendRequestListRequest:
            HandleFriendRequestListRequest(
                connection,
                packet
            );
            break;

        case MessageType::kFriendRequestAcceptRequest:
            HandleFriendRequestAcceptRequest(
                connection,
                packet
            );
            break;

        case MessageType::kFriendRequestRejectRequest:
            HandleFriendRequestRejectRequest(
                connection,
                packet
            );
            break;

        case MessageType::kHeartbeat:
            HandleHeartbeat(connection, packet);
            return;

        default:
            SendPacket(
                connection,
                MakeErrorPacket(packet.seq, "unsupported message type")
            );
            return;
    }
}

bool GatewayServer::ClearUnread(
    UserId reader_user_id,
    UserId peer_user_id,
    std::int64_t* total_unread
) {
    if (total_unread != nullptr) {
        *total_unread = 0;
    }

    if (!HasUnreadCountCache()) {
        return true;
    }

    if (!unread_count_cache_->ClearPrivateUnread(
            reader_user_id,
            peer_user_id)) {
        LOG_WARN("gateway clear unread failed"
                 << ", reader=" << reader_user_id
                 << ", peer=" << peer_user_id
                 << ", error=" << unread_count_cache_->LastError());
        return false;
    }

    const auto total =
        unread_count_cache_->GetTotalUnread(reader_user_id);

    if (total.has_value() && total_unread != nullptr) {
        *total_unread = total.value();
    }

    return true;
}

/*
    Packet GatewayServer::BuildDefaultResponse(
        const Packet& request
    ) const {
        Packet response;
        response.seq = request.seq;

        switch (request.type) {
            case MessageType::kLoginRequest:
                response.type = MessageType::kLoginResponse;
                response.body =
                    R"({"success":true,"message":"gateway login accepted"})";
                return response;

            case MessageType::kChatMessage:
                response.type = MessageType::kChatAck;
                response.body = request.body;
                return response;

            case MessageType::kHeartbeat:
                response.type = MessageType::kHeartbeat;
                response.body = R"({"pong":true})";
                return response;

            default:
                return MakeErrorPacket(
                    request.seq,
                    "unsupported message type"
                );
        }
    }

*/

// void GatewayServer::HandleLoginRequest(
//     const TcpConnectionPtr& connection,
//     const Packet& packet
// ) {
//     Json body;
//     std::string error_message;
//     if (!ParseJsonBody(packet, &body, &error_message)) {
//         SendPacket(
//             connection,
//             MakeErrorPacket(packet.seq, "invalid login json")
//         );
//         return;
//     }
//     UserId user_id = 0;
//     if (!GetUserIdField(body, "user_id", &user_id, &error_message)) {
//         SendPacket(connection, MakeErrorPacket(packet.seq, error_message));
//         return;
//     }
//     /*
//         session_manager_.Bind(user_id, connection);
//         Json response_body;
//         response_body["success"] = true;
//         response_body["message"] = "login accepted";
//         response_body["user_id"] = user_id;
//         response_body["online_count"] = session_manager_.OnlineCount();
//         Packet response;
//         response.type = MessageType::kLoginResponse;
//         response.seq = packet.seq;
//         response.body = response_body.dump();
//         SendPacket(connection, response);
//     */
//     if (HasUserRepository()) {
//         const auto user = user_repository_->FindById(user_id);
//         if (!user.has_value()) {
//             Json response_body;
//             response_body["success"] = false;
//             response_body["message"] = "user not found";
//             response_body["user_id"] = user_id;
//             Packet response;
//             response.type = MessageType::kLoginResponse;
//             response.seq = packet.seq;
//             response.body = response_body.dump();
//             SendPacket(connection, response);
//             LOG_WARN("gateway login rejected: user not found"
//                     << ", user_id=" << user_id
//                     << ", peer=" << connection->PeerAddress().ToString());
//             return;
//         }
//         if (user->status != 1) {
//             Json response_body;
//             response_body["success"] = false;
//             response_body["message"] = "user disabled";
//             response_body["user_id"] = user_id;
//             Packet response;
//             response.type = MessageType::kLoginResponse;
//             response.seq = packet.seq;
//             response.body = response_body.dump();
//             SendPacket(connection, response);
//             LOG_WARN("gateway login rejected: user disabled"
//                     << ", user_id=" << user_id
//                     << ", status=" << user->status);
//             return;
//         }
//         user_repository_->UpdateLastLogin(user_id);
//     }
//     session_manager_.Bind(user_id, connection);
//     SetUserOnline(user_id, connection);
//     /*
//         const std::size_t offline_count =
//         offline_message_store_.PendingCount(user_id);
//     */
//     std::size_t offline_count = 0;
//     if (HasMessageRepository()) {
//         const auto pending_messages =
//             message_repository_->ListPendingMessages(user_id, 100);
//         offline_count = pending_messages.size();
//     } else {
//         offline_count = offline_message_store_.PendingCount(user_id);
//     }
//     Json response_body;
//     response_body["success"] = true;
//     response_body["message"] = "login accepted";
//     response_body["user_id"] = user_id;
//     response_body["online_count"] = session_manager_.OnlineCount();
//     response_body["offline_count"] = offline_count;
//     response_body["total_unread"] = GetTotalUnread(user_id);
//     Packet response;
//     response.type = MessageType::kLoginResponse;
//     response.seq = packet.seq;
//     response.body = response_body.dump();
//     SendPacket(connection, response);
//     if (HasMessageRepository()) {
//         PushPersistentOfflineMessages(user_id, connection);
//     } else {
//         PushOfflineMessages(user_id, connection);
//     }
//     LOG_INFO("gateway user logged in"
//             << ", user_id=" << user_id
//             << ", peer=" << connection->PeerAddress().ToString()
//             << ", online_count=" << session_manager_.OnlineCount());
// }

void GatewayServer::HandleLoginRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    Json body;
    std::string error_message;

    auto send_login_response = [this, &connection, &packet](
        bool success,
        const std::string& message,
        const std::string& reason,
        UserId user_id,
        const std::string& username,
        std::size_t online_count,
        std::size_t offline_count,
        std::int64_t total_unread
    ) {
        Json response_body;
        response_body["success"] = success;
        response_body["message"] = message;

        if (!reason.empty()) {
            response_body["reason"] = reason;
        }

        if (success) {
            response_body["user_id"] = user_id;
            response_body["username"] = username;
        }

        response_body["online_count"] = online_count;
        response_body["offline_count"] = offline_count;
        response_body["total_unread"] = total_unread;

        Packet response;
        response.type = MessageType::kLoginResponse;
        response.seq = packet.seq;
        response.body = response_body.dump();

        SendPacket(connection, response);
    };

    if (!ParseJsonBody(packet, &body, &error_message)) {
        send_login_response(
            false,
            "invalid login json",
            "invalid_json",
            0,
            "",
            session_manager_.OnlineCount(),
            0,
            0
        );
        return;
    }

    if (!HasUserRepository()) {
        send_login_response(
            false,
            "auth service unavailable",
            "auth_unavailable",
            0,
            "",
            session_manager_.OnlineCount(),
            0,
            0
        );
        return;
    }

    if (!body.contains("username") ||
        !body.at("username").is_string()) {
        send_login_response(
            false,
            "missing or invalid username",
            "invalid_username",
            0,
            "",
            session_manager_.OnlineCount(),
            0,
            0
        );

        LOG_WARN("gateway login rejected: missing or invalid username"
                 << ", peer=" << connection->PeerAddress().ToString());

        return;
    }

    if (!body.contains("password") ||
        !body.at("password").is_string()) {
        send_login_response(
            false,
            "missing or invalid password",
            "invalid_password",
            0,
            "",
            session_manager_.OnlineCount(),
            0,
            0
        );

        LOG_WARN("gateway login rejected: missing or invalid password"
                 << ", username=" << body.value("username", "")
                 << ", peer=" << connection->PeerAddress().ToString());

        return;
    }

    const std::string username =
        body.at("username").get<std::string>();

    const std::string password =
        body.at("password").get<std::string>();

    const LoginVerifyResult login_result =
        user_repository_->VerifyLogin(username, password);

    if (!login_result.Success()) {
        send_login_response(
            false,
            login_result.message,
            LoginVerifyStatusToString(login_result.status),
            0,
            "",
            session_manager_.OnlineCount(),
            0,
            0
        );

        LOG_WARN("gateway login rejected"
                 << ", username=" << username
                 << ", reason="
                 << LoginVerifyStatusToString(login_result.status)
                 << ", peer="
                 << connection->PeerAddress().ToString());

        return;
    }

    const UserId user_id = login_result.user->user_id;
    const std::string verified_username = login_result.user->username;

    const SessionBindResult bind_result =
        session_manager_.BindOrReplace(user_id, connection);

    if (!bind_result.success) {
        send_login_response(
            false,
            "session bind failed",
            "session_bind_failed",
            0,
            "",
            session_manager_.OnlineCount(),
            0,
            0
        );

        LOG_WARN("gateway login rejected: session bind failed"
                 << ", user_id=" << user_id
                 << ", username=" << verified_username
                 << ", peer="
                 << connection->PeerAddress().ToString());

        return;
    }

    if (bind_result.replaced &&
        bind_result.old_connection &&
        bind_result.old_connection != connection) {
        NotifyLoginReplaced(user_id, bind_result.old_connection);
    }

    SetUserOnline(user_id, connection);

    std::size_t offline_count = 0;

    if (HasMessageRepository()) {
        const auto pending_messages =
            message_repository_->ListPendingMessages(user_id, 100);

        offline_count = pending_messages.size();
    } else {
        offline_count = offline_message_store_.PendingCount(user_id);
    }

    send_login_response(
        true,
        "login accepted",
        "",
        user_id,
        verified_username,
        session_manager_.OnlineCount(),
        offline_count,
        GetTotalUnread(user_id)
    );

    if (HasMessageRepository()) {
        PushPersistentOfflineMessages(user_id, connection);
    } else {
        PushOfflineMessages(user_id, connection);
    }

    LOG_INFO("gateway user logged in"
             << ", user_id=" << user_id
             << ", username=" << verified_username
             << ", peer=" << connection->PeerAddress().ToString()
             << ", online_count=" << session_manager_.OnlineCount());
}


void GatewayServer::HandleChatMessage(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    Json body;
    std::string error_message;

    if (!ParseJsonBody(packet, &body, &error_message)) {
        SendPacket(
            connection,
            MakeErrorPacket(packet.seq, "invalid chat json")
        );
        return;
    }

    const std::optional<UserId> logged_user_id =
        session_manager_.FindUserByConnection(connection);

    if (!logged_user_id.has_value()) {
        Json ack_body;
        ack_body["success"] = false;
        ack_body["delivered"] = false;
        ack_body["reason"] = "sender_not_logged_in";

        Packet ack;
        ack.type = MessageType::kChatAck;
        ack.seq = packet.seq;
        ack.body = ack_body.dump();

        SendPacket(connection, ack);

        LOG_WARN("gateway rejected chat message: sender not logged in"
                 << ", peer=" << connection->PeerAddress().ToString());
        return;
    }

    const UserId from_user_id = logged_user_id.value();

    if (body.contains("from")) {
        UserId client_from_user_id = 0;

        if (!GetUserIdField(
                body,
                "from",
                &client_from_user_id,
                &error_message)) {
            SendPacket(connection, MakeErrorPacket(packet.seq, error_message));
            return;
        }

        if (client_from_user_id != from_user_id) {
            UserId to_user_id = 0;
            if (body.contains("to")) {
                GetUserIdField(body, "to", &to_user_id, nullptr);
            }

            Json ack_body;
            ack_body["success"] = false;
            ack_body["delivered"] = false;
            ack_body["reason"] = "from_user_mismatch";
            ack_body["login_user_id"] = from_user_id;
            ack_body["client_from"] = client_from_user_id;

            if (to_user_id != 0) {
                ack_body["to"] = to_user_id;
            }

            Packet ack;
            ack.type = MessageType::kChatAck;
            ack.seq = packet.seq;
            ack.body = ack_body.dump();

            SendPacket(connection, ack);

            LOG_WARN("gateway rejected chat message: from user mismatch"
                     << ", login_user_id=" << from_user_id
                     << ", client_from=" << client_from_user_id
                     << ", peer=" << connection->PeerAddress().ToString());
            return;
        }
    }

    UserId to_user_id = 0;

    if (!GetUserIdField(body, "to", &to_user_id, &error_message)) {
        SendPacket(connection, MakeErrorPacket(packet.seq, error_message));
        return;
    }

    if (to_user_id == from_user_id) {
        Json ack_body;
        ack_body["success"] = false;
        ack_body["delivered"] = false;
        ack_body["reason"] = "invalid_to_user";
        ack_body["from"] = from_user_id;
        ack_body["to"] = to_user_id;

        Packet ack;
        ack.type = MessageType::kChatAck;
        ack.seq = packet.seq;
        ack.body = ack_body.dump();

        SendPacket(connection, ack);
        return;
    }

    if (!HasFriendRepository()) {
    Json ack_body;
    ack_body["success"] = false;
    ack_body["delivered"] = false;
    ack_body["reason"] = "relation_service_unavailable";
    ack_body["from"] = from_user_id;
    ack_body["to"] = to_user_id;

    Packet ack;
    ack.type = MessageType::kChatAck;
    ack.seq = packet.seq;
    ack.body = ack_body.dump();

    SendPacket(connection, ack);

    LOG_WARN("gateway rejected chat message: relation service unavailable"
             << ", from=" << from_user_id
             << ", to=" << to_user_id);

        return;
    }

    const ChatPermissionResult permission_result =
        friend_repository_->CheckPrivateChatPermission(
            from_user_id,
            to_user_id
        );

    if (!permission_result.Allowed()) {
        Json ack_body;
        ack_body["success"] = false;
        ack_body["delivered"] = false;
        ack_body["reason"] =
            ChatPermissionStatusToString(permission_result.status);
        ack_body["message"] = permission_result.message;
        ack_body["from"] = from_user_id;
        ack_body["to"] = to_user_id;
        ack_body["stored_offline"] = false;
        ack_body["stored_persistent"] = false;
        ack_body["receiver_private_unread"] = 0;
        ack_body["receiver_total_unread"] = 0;

        Packet ack;
        ack.type = MessageType::kChatAck;
        ack.seq = packet.seq;
        ack.body = ack_body.dump();

        SendPacket(connection, ack);

        LOG_WARN("gateway rejected chat message: permission denied"
                << ", from=" << from_user_id
                << ", to=" << to_user_id
                << ", reason="
                << ChatPermissionStatusToString(permission_result.status)
                << ", message=" << permission_result.message);

        return;
    }

    Json server_body = body;
    server_body["from"] = from_user_id;
    server_body["to"] = to_user_id;

    const std::string server_body_text = server_body.dump();

    TcpConnectionPtr target_connection =
        session_manager_.FindConnection(to_user_id);

    const bool delivered =
        target_connection && target_connection->IsConnected();

    bool stored_offline = false;

    Packet forward_packet;
    forward_packet.type = MessageType::kChatMessage;
    forward_packet.seq = packet.seq;
    forward_packet.body = server_body_text;

    std::uint64_t server_message_id = 0;
    bool stored_persistent = false;

    if (HasMessageRepository()) {
        server_message_id =
            message_repository_->SavePrivateMessage(
                from_user_id,
                to_user_id,
                server_body_text,
                delivered
                    ? DeliveryStatus::kDelivered
                    : DeliveryStatus::kPending
            );

        stored_persistent = server_message_id != 0;
    }

    if (delivered) {
        SendPacket(target_connection, forward_packet);
    } else {
        if (HasMessageRepository()) {
            stored_offline = stored_persistent;
        } else {
            stored_offline =
                offline_message_store_.Store(to_user_id, forward_packet);
        }
    }

    std::int64_t receiver_private_unread = 0;
    std::int64_t receiver_total_unread = 0;

    const bool message_accepted =
        delivered || stored_offline || stored_persistent;

    if (message_accepted) {
        receiver_private_unread =
            IncrementUnread(
                to_user_id,
                from_user_id,
                &receiver_total_unread
            );
    }

    Json ack_body;
    ack_body["success"] = message_accepted;
    ack_body["from"] = from_user_id;
    ack_body["to"] = to_user_id;
    ack_body["delivered"] = delivered;
    ack_body["stored_offline"] = stored_offline;
    ack_body["stored_persistent"] = stored_persistent;
    ack_body["message_id"] = server_message_id;
    ack_body["receiver_private_unread"] = receiver_private_unread;
    ack_body["receiver_total_unread"] = receiver_total_unread;

    if (!message_accepted) {
        ack_body["reason"] = "message_not_accepted";
    } else if (!delivered) {
        ack_body["reason"] = stored_offline
            ? "target_user_offline"
            : "store_offline_failed";
    }

    Packet ack;
    ack.type = MessageType::kChatAck;
    ack.seq = packet.seq;
    ack.body = ack_body.dump();

    SendPacket(connection, ack);

    LOG_INFO("gateway chat message handled"
             << ", from=" << from_user_id
             << ", to=" << to_user_id
             << ", delivered=" << delivered
             << ", stored_offline=" << stored_offline
             << ", stored_persistent=" << stored_persistent
             << ", message_id=" << server_message_id
             << ", receiver_private_unread=" << receiver_private_unread
             << ", receiver_total_unread=" << receiver_total_unread
             << ", offline_total="
             << offline_message_store_.TotalCount());
}


void GatewayServer::HandleReadRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    Json response_body;
    response_body["success"] = false;

    const auto login_user_id =
        session_manager_.FindUserByConnection(connection);

    if (!login_user_id.has_value()) {
        response_body["message"] = "not logged in";

        Packet response;
        response.type = MessageType::kReadResponse;
        response.seq = packet.seq;
        response.body = response_body.dump();

        SendPacket(connection, response);
        return;
    }

    Json request_body;

    try {
        request_body = Json::parse(packet.body);
    } catch (const std::exception& e) {
        response_body["message"] = "invalid json body";

        Packet response;
        response.type = MessageType::kReadResponse;
        response.seq = packet.seq;
        response.body = response_body.dump();

        SendPacket(connection, response);

        LOG_WARN("gateway read request parse failed"
                 << ", error=" << e.what());
        return;
    }

    //const UserId reader_user_id = login_user_id.value();
    //const UserId peer_user_id =
    //    request_body.value("peer_user_id", 0ULL);

    const UserId reader_user_id = login_user_id.value();

    if (request_body.contains("user_id")) {
        UserId client_reader_user_id = 0;

        if (!GetUserIdField(
                request_body,
                "user_id",
                &client_reader_user_id,
                nullptr)) {
            response_body["message"] = "invalid user_id";

            Packet response;
            response.type = MessageType::kReadResponse;
            response.seq = packet.seq;
            response.body = response_body.dump();

            SendPacket(connection, response);
            return;
        }

        if (client_reader_user_id != reader_user_id) {
            response_body["message"] = "reader user mismatch";
            response_body["reason"] = "reader_user_mismatch";
            response_body["login_user_id"] = reader_user_id;
            response_body["client_user_id"] = client_reader_user_id;

            Packet response;
            response.type = MessageType::kReadResponse;
            response.seq = packet.seq;
            response.body = response_body.dump();

            SendPacket(connection, response);

            LOG_WARN("gateway rejected read request: reader user mismatch"
                    << ", login_user_id=" << reader_user_id
                    << ", client_user_id=" << client_reader_user_id);
            return;
        }
    }

    const UserId peer_user_id =
        request_body.value("peer_user_id", 0ULL);

    if (peer_user_id == 0 || peer_user_id == reader_user_id) {
        response_body["message"] = "invalid peer_user_id";
        response_body["user_id"] = reader_user_id;
        response_body["peer_user_id"] = peer_user_id;

        Packet response;
        response.type = MessageType::kReadResponse;
        response.seq = packet.seq;
        response.body = response_body.dump();

        SendPacket(connection, response);
        return;
    }

    std::uint64_t marked_read_count = 0;

    if (HasMessageRepository()) {
        marked_read_count =
            message_repository_->MarkReadByDialog(
                reader_user_id,
                peer_user_id
            );
    }

    std::int64_t total_unread = 0;
    const bool cleared =
        ClearUnread(
            reader_user_id,
            peer_user_id,
            &total_unread
        );

    response_body["success"] = cleared;
    response_body["message"] = cleared
        ? "read accepted"
        : "clear unread failed";
    response_body["user_id"] = reader_user_id;
    response_body["peer_user_id"] = peer_user_id;
    response_body["private_unread"] = 0;
    response_body["total_unread"] = total_unread;
    response_body["marked_read_count"] = marked_read_count;

    Packet response;
    response.type = MessageType::kReadResponse;
    response.seq = packet.seq;
    response.body = response_body.dump();

    SendPacket(connection, response);

    LOG_INFO("gateway read request handled"
             << ", reader=" << reader_user_id
             << ", peer=" << peer_user_id
             << ", cleared=" << cleared
             << ", total_unread=" << total_unread
             << ", marked_read_count=" << marked_read_count);
}

void GatewayServer::HandleHistoryRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    Json response_body;
    response_body["success"] = false;
    response_body["messages"] = Json::array();
    response_body["has_more"] = false;

    auto send_response = [this, &connection, &packet](
        const Json& body
    ) {
        Packet response;
        response.type = MessageType::kHistoryResponse;
        response.seq = packet.seq;
        response.body = body.dump();

        SendPacket(connection, response);
    };

    Json request_body;
    std::string error_message;

    if (!ParseJsonBody(packet, &request_body, &error_message)) {
        response_body["message"] = "invalid history json";
        response_body["reason"] = "invalid_json";
        send_response(response_body);
        return;
    }

    const auto login_user_id =
        session_manager_.FindUserByConnection(connection);

    if (!login_user_id.has_value()) {
        response_body["message"] = "history request rejected: not logged in";
        response_body["reason"] = "not_logged_in";
        send_response(response_body);

        LOG_WARN("gateway rejected history request: not logged in"
                 << ", peer=" << connection->PeerAddress().ToString());

        return;
    }

    const UserId self_user_id = login_user_id.value();

    UserId peer_user_id = 0;

    if (!GetUserIdField(
            request_body,
            "peer_user_id",
            &peer_user_id,
            &error_message)) {
        response_body["message"] = error_message;
        response_body["reason"] = "invalid_peer_user_id";
        response_body["user_id"] = self_user_id;
        send_response(response_body);
        return;
    }

    if (peer_user_id == self_user_id) {
        response_body["message"] = "peer user equals self";
        response_body["reason"] = "invalid_peer_user_id";
        response_body["user_id"] = self_user_id;
        response_body["peer_user_id"] = peer_user_id;
        send_response(response_body);
        return;
    }

    std::uint64_t before_message_id = 0;

    if (request_body.contains("before_message_id")) {
        if (!request_body.at("before_message_id").is_number_unsigned()) {
            response_body["message"] = "invalid before_message_id";
            response_body["reason"] = "invalid_before_message_id";
            response_body["user_id"] = self_user_id;
            response_body["peer_user_id"] = peer_user_id;
            send_response(response_body);
            return;
        }

        before_message_id =
            request_body.at("before_message_id").get<std::uint64_t>();
    }

    std::size_t limit = 20;

    if (request_body.contains("limit")) {
        if (!request_body.at("limit").is_number_unsigned()) {
            response_body["message"] = "invalid limit";
            response_body["reason"] = "invalid_limit";
            response_body["user_id"] = self_user_id;
            response_body["peer_user_id"] = peer_user_id;
            send_response(response_body);
            return;
        }

        limit = request_body.at("limit").get<std::size_t>();
    }

    if (limit == 0) {
        limit = 20;
    }

    if (limit > 50) {
        limit = 50;
    }

    if (!HasMessageRepository()) {
        response_body["message"] = "message repository unavailable";
        response_body["reason"] = "message_service_unavailable";
        response_body["user_id"] = self_user_id;
        response_body["peer_user_id"] = peer_user_id;
        send_response(response_body);
        return;
    }

    if (!HasFriendRepository()) {
        response_body["message"] = "relation repository unavailable";
        response_body["reason"] = "relation_service_unavailable";
        response_body["user_id"] = self_user_id;
        response_body["peer_user_id"] = peer_user_id;
        send_response(response_body);
        return;
    }

    const ChatPermissionResult permission_result =
        friend_repository_->CheckPrivateChatPermission(
            self_user_id,
            peer_user_id
        );

    if (!permission_result.Allowed()) {
        response_body["message"] = permission_result.message;
        response_body["reason"] =
            ChatPermissionStatusToString(permission_result.status);
        response_body["user_id"] = self_user_id;
        response_body["peer_user_id"] = peer_user_id;
        send_response(response_body);

        LOG_WARN("gateway rejected history request: permission denied"
                 << ", user_id=" << self_user_id
                 << ", peer_user_id=" << peer_user_id
                 << ", reason="
                 << ChatPermissionStatusToString(permission_result.status));

        return;
    }

    const std::size_t query_limit = limit + 1;

    auto messages =
        message_repository_->ListDialogMessages(
            self_user_id,
            peer_user_id,
            before_message_id,
            query_limit
        );

    bool has_more = false;

    /*
        if (messages.size() > limit) {
            has_more = true;
            messages.resize(limit);
        }
    */

    if (messages.size() > limit) {
        has_more = true;
        messages.erase(messages.begin());
    }

    Json message_array = Json::array();

    for (const auto& message : messages) {
        Json item;
        item["message_id"] = message.message_id;
        item["from"] = message.from_user_id;
        item["to"] = message.to_user_id;
        item["message_type"] = message.message_type;
        item["content"] = message.content;
        item["delivery_status"] = message.delivery_status;
        item["created_at"] = message.created_at;
        item["delivered_at"] = message.delivered_at;
        item["read_at"] = message.read_at;

        message_array.push_back(item);
    }

    response_body["success"] = true;
    response_body["message"] = "history accepted";
    response_body["user_id"] = self_user_id;
    response_body["peer_user_id"] = peer_user_id;
    response_body["before_message_id"] = before_message_id;
    response_body["limit"] = limit;
    response_body["has_more"] = has_more;
    response_body["messages"] = message_array;

    send_response(response_body);

    LOG_INFO("gateway history request handled"
             << ", user_id=" << self_user_id
             << ", peer_user_id=" << peer_user_id
             << ", before_message_id=" << before_message_id
             << ", limit=" << limit
             << ", returned=" << messages.size()
             << ", has_more=" << has_more);
}

void GatewayServer::HandleConversationListRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    Json response_body;
    response_body["success"] = false;
    response_body["conversations"] = Json::array();
    response_body["has_more"] = false;

    auto send_response = [this, &connection, &packet](
        const Json& body
    ) {
        Packet response;
        response.type = MessageType::kConversationListResponse;
        response.seq = packet.seq;
        response.body = body.dump();

        SendPacket(connection, response);
    };

    Json request_body;
    std::string error_message;

    if (!ParseJsonBody(packet, &request_body, &error_message)) {
        response_body["message"] = "invalid conversation list json";
        response_body["reason"] = "invalid_json";
        send_response(response_body);
        return;
    }

    const auto login_user_id =
        session_manager_.FindUserByConnection(connection);

    if (!login_user_id.has_value()) {
        response_body["message"] =
            "conversation list request rejected: not logged in";
        response_body["reason"] = "not_logged_in";
        send_response(response_body);

        LOG_WARN("gateway rejected conversation list request: not logged in"
                 << ", peer=" << connection->PeerAddress().ToString());

        return;
    }

    const UserId self_user_id = login_user_id.value();

    std::size_t limit = 20;

    if (request_body.contains("limit")) {
        if (!request_body.at("limit").is_number_unsigned()) {
            response_body["message"] = "invalid limit";
            response_body["reason"] = "invalid_limit";
            response_body["user_id"] = self_user_id;
            send_response(response_body);
            return;
        }

        limit = request_body.at("limit").get<std::size_t>();
    }

    if (limit == 0) {
        limit = 20;
    }

    if (limit > 50) {
        limit = 50;
    }

    if (!HasMessageRepository()) {
        response_body["message"] = "message repository unavailable";
        response_body["reason"] = "message_service_unavailable";
        response_body["user_id"] = self_user_id;
        send_response(response_body);
        return;
    }

    const std::size_t query_limit = limit + 1;

    auto conversations =
        message_repository_->ListConversations(
            self_user_id,
            query_limit
        );

    bool has_more = false;

    if (conversations.size() > limit) {
        has_more = true;

        // ListConversations 返回的是按 last_message_id DESC 排序。
        // 多查出来的一条在最后面，是更旧的会话。
        // 所以这里 resize 可以安全丢掉最后一条。
        conversations.resize(limit);
    }

    Json conversation_array = Json::array();

    for (const auto& conversation : conversations) {
        Json item;

        item["peer_user_id"] = conversation.peer_user_id;

        item["last_message_id"] = conversation.last_message_id;
        item["last_client_message_id"] =
            conversation.last_client_message_id;

        item["last_from"] = conversation.last_from_user_id;
        item["last_to"] = conversation.last_to_user_id;

        item["last_message_type"] = conversation.last_message_type;
        item["last_content"] = conversation.last_content;
        item["last_delivery_status"] =
            conversation.last_delivery_status;

        item["last_created_at"] = conversation.last_created_at;
        item["last_delivered_at"] = conversation.last_delivered_at;
        item["last_read_at"] = conversation.last_read_at;

        item["unread_count"] =
            GetPrivateUnread(
                self_user_id,
                conversation.peer_user_id
            );

        conversation_array.push_back(item);
    }

    response_body["success"] = true;
    response_body["message"] = "conversation list accepted";
    response_body["user_id"] = self_user_id;
    response_body["limit"] = limit;
    response_body["has_more"] = has_more;
    response_body["conversations"] = conversation_array;

    send_response(response_body);

    LOG_INFO("gateway conversation list request handled"
             << ", user_id=" << self_user_id
             << ", limit=" << limit
             << ", returned=" << conversations.size()
             << ", has_more=" << has_more);
}

void GatewayServer::HandleFriendListRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    Json response_body;
    response_body["success"] = false;
    response_body["friends"] = Json::array();
    response_body["has_more"] = false;

    auto send_response = [this, &connection, &packet](
        const Json& body
    ) {
        Packet response;
        response.type = MessageType::kFriendListResponse;
        response.seq = packet.seq;
        response.body = body.dump();

        SendPacket(connection, response);
    };

    Json request_body;
    std::string error_message;

    if (!ParseJsonBody(packet, &request_body, &error_message)) {
        response_body["message"] = "invalid friend list json";
        response_body["reason"] = "invalid_json";
        send_response(response_body);
        return;
    }

    const auto login_user_id =
        session_manager_.FindUserByConnection(connection);

    if (!login_user_id.has_value()) {
        response_body["message"] =
            "friend list request rejected: not logged in";
        response_body["reason"] = "not_logged_in";
        send_response(response_body);

        LOG_WARN("gateway rejected friend list request: not logged in"
                 << ", peer=" << connection->PeerAddress().ToString());

        return;
    }

    const UserId self_user_id = login_user_id.value();

    std::size_t limit = 20;

    if (request_body.contains("limit")) {
        if (!request_body.at("limit").is_number_unsigned()) {
            response_body["message"] = "invalid limit";
            response_body["reason"] = "invalid_limit";
            response_body["user_id"] = self_user_id;
            send_response(response_body);
            return;
        }

        limit = request_body.at("limit").get<std::size_t>();
    }

    if (limit == 0) {
        limit = 20;
    }

    if (limit > 100) {
        limit = 100;
    }

    if (!HasFriendRepository()) {
        response_body["message"] = "friend repository unavailable";
        response_body["reason"] = "friend_service_unavailable";
        response_body["user_id"] = self_user_id;
        send_response(response_body);
        return;
    }

    const std::size_t query_limit = limit + 1;

    auto friends =
        friend_repository_->ListFriends(
            self_user_id,
            query_limit
        );

    bool has_more = false;

    if (friends.size() > limit) {
        has_more = true;

        // ListFriends 当前按 relation_updated_at DESC 排序。
        // 多查出来的一条在最后面，代表更旧的好友关系。
        friends.resize(limit);
    }

    Json friend_array = Json::array();

    for (const auto& friend_record : friends) {
        Json item;

        item["friend_user_id"] = friend_record.friend_user_id;
        item["username"] = friend_record.username;
        item["nickname"] = friend_record.nickname;
        item["avatar_url"] = friend_record.avatar_url;

        item["user_status"] = friend_record.user_status;
        item["relation_status"] = friend_record.relation_status;

        item["relation_created_at"] =
            friend_record.relation_created_at;
        item["relation_updated_at"] =
            friend_record.relation_updated_at;

        friend_array.push_back(item);
    }

    response_body["success"] = true;
    response_body["message"] = "friend list accepted";
    response_body["user_id"] = self_user_id;
    response_body["limit"] = limit;
    response_body["has_more"] = has_more;
    response_body["friends"] = friend_array;

    send_response(response_body);

    LOG_INFO("gateway friend list request handled"
             << ", user_id=" << self_user_id
             << ", limit=" << limit
             << ", returned=" << friends.size()
             << ", has_more=" << has_more);
}


void GatewayServer::HandleFriendRequestCreateRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    Json response_body;
    response_body["success"] = false;
    response_body["changed"] = false;
    response_body["request_id"] = 0;

    auto send_response = [this, &connection, &packet](
        const Json& body
    ) {
        Packet response;
        response.type =
            MessageType::kFriendRequestCreateResponse;
        response.seq = packet.seq;
        response.body = body.dump();

        SendPacket(connection, response);
    };

    Json request_body;
    std::string error_message;

    if (!ParseJsonBody(packet, &request_body, &error_message) ||
        !request_body.is_object()) {
        response_body["message"] =
            "invalid friend request create json";
        response_body["reason"] = "invalid_json";
        send_response(response_body);
        return;
    }

    const auto login_user_id =
        session_manager_.FindUserByConnection(connection);

    if (!login_user_id.has_value()) {
        response_body["message"] =
            "friend request create rejected: not logged in";
        response_body["reason"] = "not_logged_in";
        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request create: not logged in"
            << ", peer="
            << connection->PeerAddress().ToString()
        );

        return;
    }

    const UserId self_user_id = login_user_id.value();
    response_body["user_id"] = self_user_id;

    if (request_body.contains("from_user_id")) {
        response_body["message"] =
            "from_user_id must not be provided by client";
        response_body["reason"] =
            "forbidden_identity_field";
        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request create: "
            "client supplied from_user_id"
            << ", login_user_id=" << self_user_id
        );

        return;
    }

    UserId to_user_id = 0;

    if (!GetUserIdField(
            request_body,
            "to_user_id",
            &to_user_id,
            &error_message)) {
        response_body["message"] = error_message;
        response_body["reason"] = "invalid_to_user_id";
        send_response(response_body);
        return;
    }

    response_body["to_user_id"] = to_user_id;

    if (to_user_id == self_user_id) {
        response_body["message"] =
            "cannot send friend request to self";
        response_body["reason"] = "invalid_argument";
        send_response(response_body);
        return;
    }

    std::string request_message;

    if (request_body.contains("request_message")) {
        if (!request_body.at("request_message").is_string()) {
            response_body["message"] =
                "request_message must be a string";
            response_body["reason"] =
                "invalid_request_message";
            send_response(response_body);
            return;
        }

        request_message =
            request_body.at("request_message").get<std::string>();
    }

    if (request_message.size() > 255) {
        response_body["message"] =
            "friend request message is too long";
        response_body["reason"] =
            "invalid_request_message";
        send_response(response_body);
        return;
    }

    if (!HasFriendRequestRepository()) {
        response_body["message"] =
            "friend request repository unavailable";
        response_body["reason"] =
            "friend_request_service_unavailable";
        send_response(response_body);
        return;
    }

    const CreateFriendRequestResult result =
        friend_request_repository_->CreateFriendRequest(
            self_user_id,
            to_user_id,
            request_message
        );

    const bool changed =
        result.status == CreateFriendRequestStatus::kCreated ||
        result.status == CreateFriendRequestStatus::kReopened;

    const bool success =
        changed ||
        result.status ==
            CreateFriendRequestStatus::kAlreadyPending;

    response_body["success"] = success;
    response_body["changed"] = changed;
    response_body["message"] = result.message;
    response_body["reason"] =
        CreateFriendRequestStatusToString(result.status);
    response_body["request_id"] = result.request_id;
    response_body["from_user_id"] = self_user_id;
    response_body["to_user_id"] = to_user_id;

    send_response(response_body);

    LOG_INFO(
        "gateway friend request create handled"
        << ", from_user_id=" << self_user_id
        << ", to_user_id=" << to_user_id
        << ", request_id=" << result.request_id
        << ", status="
        << CreateFriendRequestStatusToString(result.status)
        << ", success=" << success
        << ", changed=" << changed
    );
}

void GatewayServer::HandleFriendRequestListRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    Json response_body;
    response_body["success"] = false;
    response_body["requests"] = Json::array();
    response_body["has_more"] = false;
    response_body["next_before_created_at"] = "";
    response_body["next_before_request_id"] = 0;

    auto send_response = [this, &connection, &packet](
        const Json& body
    ) {
        Packet response;
        response.type =
            MessageType::kFriendRequestListResponse;
        response.seq = packet.seq;
        response.body = body.dump();

        SendPacket(connection, response);
    };

    Json request_body;
    std::string error_message;

    if (!ParseJsonBody(packet, &request_body, &error_message) ||
        !request_body.is_object()) {
        response_body["message"] =
            "invalid friend request list json";
        response_body["reason"] = "invalid_json";
        send_response(response_body);
        return;
    }

    const auto login_user_id =
        session_manager_.FindUserByConnection(connection);

    if (!login_user_id.has_value()) {
        response_body["message"] =
            "friend request list rejected: not logged in";
        response_body["reason"] = "not_logged_in";
        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request list: not logged in"
            << ", peer="
            << connection->PeerAddress().ToString()
        );

        return;
    }

    const UserId self_user_id = login_user_id.value();
    response_body["user_id"] = self_user_id;

    if (request_body.contains("receiver_user_id")) {
        response_body["message"] =
            "receiver_user_id must not be provided by client";
        response_body["reason"] =
            "forbidden_identity_field";
        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request list: "
            "client supplied receiver_user_id"
            << ", login_user_id=" << self_user_id
        );

        return;
    }

    std::size_t limit = 20;

    if (request_body.contains("limit")) {
        if (!request_body.at("limit").is_number_unsigned()) {
            response_body["message"] = "invalid limit";
            response_body["reason"] = "invalid_limit";
            send_response(response_body);
            return;
        }

        limit = request_body.at("limit").get<std::size_t>();
    }

    if (limit == 0) {
        limit = 20;
    }

    if (limit > 50) {
        limit = 50;
    }

    std::string before_created_at;

    if (request_body.contains("before_created_at")) {
        if (!request_body.at("before_created_at").is_string()) {
            response_body["message"] =
                "invalid before_created_at";
            response_body["reason"] =
                "invalid_pagination_cursor";
            send_response(response_body);
            return;
        }

        before_created_at =
            request_body.at("before_created_at")
                .get<std::string>();
    }

    std::uint64_t before_request_id = 0;

    if (request_body.contains("before_request_id")) {
        if (!request_body.at("before_request_id")
                 .is_number_unsigned()) {
            response_body["message"] =
                "invalid before_request_id";
            response_body["reason"] =
                "invalid_pagination_cursor";
            send_response(response_body);
            return;
        }

        before_request_id =
            request_body.at("before_request_id")
                .get<std::uint64_t>();
    }

    const bool first_page =
        before_created_at.empty() &&
        before_request_id == 0;

    const bool next_page =
        !before_created_at.empty() &&
        before_request_id != 0;

    if (!first_page && !next_page) {
        response_body["message"] =
            "before_created_at and before_request_id "
            "must be provided together";
        response_body["reason"] =
            "invalid_pagination_cursor";
        send_response(response_body);
        return;
    }

    response_body["limit"] = limit;
    response_body["before_created_at"] =
        before_created_at;
    response_body["before_request_id"] =
        before_request_id;

    if (!HasFriendRequestRepository()) {
        response_body["message"] =
            "friend request repository unavailable";
        response_body["reason"] =
            "friend_request_service_unavailable";
        send_response(response_body);
        return;
    }

    const std::size_t query_limit = limit + 1;

    auto requests =
        friend_request_repository_->
            ListPendingIncomingRequests(
                self_user_id,
                before_created_at,
                before_request_id,
                query_limit
            );

    if (!friend_request_repository_->LastError().empty()) {
        response_body["message"] =
            friend_request_repository_->LastError();
        response_body["reason"] = "storage_error";
        send_response(response_body);
        return;
    }

    bool has_more = false;

    if (requests.size() > limit) {
        has_more = true;
        requests.resize(limit);
    }

    Json request_array = Json::array();

    for (const auto& record : requests) {
        Json item;

        item["request_id"] = record.request_id;
        item["from_user_id"] = record.from_user_id;
        item["to_user_id"] = record.to_user_id;
        item["request_message"] =
            record.request_message;

        item["request_status"] =
            static_cast<std::uint32_t>(
                record.request_status
            );
        item["request_status_name"] = "pending";

        item["created_at"] = record.created_at;
        item["handled_at"] = record.handled_at;
        item["updated_at"] = record.updated_at;

        item["from_username"] =
            record.from_username;
        item["from_nickname"] =
            record.from_nickname;
        item["from_avatar_url"] =
            record.from_avatar_url;
        item["from_user_status"] =
            record.from_user_status;

        request_array.push_back(item);
    }

    if (has_more && !requests.empty()) {
        response_body["next_before_created_at"] =
            requests.back().created_at;
        response_body["next_before_request_id"] =
            requests.back().request_id;
    }

    response_body["success"] = true;
    response_body["message"] =
        "friend request list accepted";
    response_body["reason"] = "ok";
    response_body["has_more"] = has_more;
    response_body["requests"] = request_array;

    send_response(response_body);

    LOG_INFO(
        "gateway friend request list handled"
        << ", user_id=" << self_user_id
        << ", before_created_at=" << before_created_at
        << ", before_request_id=" << before_request_id
        << ", limit=" << limit
        << ", returned=" << requests.size()
        << ", has_more=" << has_more
    );
}

void GatewayServer::HandleFriendRequestAcceptRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    Json response_body;

    response_body["success"] = false;
    response_body["changed"] = false;
    response_body["request_id"] = 0;

    auto send_response =
        [this, &connection, &packet](
            const Json& body
        ) {
            Packet response;

            response.type =
                MessageType::
                    kFriendRequestAcceptResponse;

            response.seq = packet.seq;
            response.body = body.dump();

            SendPacket(
                connection,
                response
            );
        };

    Json request_body;
    std::string error_message;

    if (!ParseJsonBody(
            packet,
            &request_body,
            &error_message
        ) ||
        !request_body.is_object()) {
        response_body["message"] =
            "invalid friend request accept json";

        response_body["reason"] =
            "invalid_json";

        send_response(response_body);
        return;
    }

    const auto login_user_id =
        session_manager_.
            FindUserByConnection(
                connection
            );

    if (!login_user_id.has_value()) {
        response_body["message"] =
            "friend request accept rejected: "
            "not logged in";

        response_body["reason"] =
            "not_logged_in";

        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request "
            "accept: not logged in"
            << ", peer="
            << connection->
                   PeerAddress().
                   ToString()
        );

        return;
    }

    const UserId self_user_id =
        login_user_id.value();

    response_body["user_id"] =
        self_user_id;

    if (request_body.contains(
            "handler_user_id")) {
        response_body["message"] =
            "handler_user_id must not be "
            "provided by client";

        response_body["reason"] =
            "forbidden_identity_field";

        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request "
            "accept: client supplied "
            "handler_user_id"
            << ", login_user_id="
            << self_user_id
        );

        return;
    }

    if (!request_body.contains(
            "request_id") ||
        !request_body.at(
            "request_id"
        ).is_number_unsigned()) {
        response_body["message"] =
            "invalid request_id";

        response_body["reason"] =
            "invalid_request_id";

        send_response(response_body);
        return;
    }

    const std::uint64_t request_id =
        request_body.at(
            "request_id"
        ).get<std::uint64_t>();

    response_body["request_id"] =
        request_id;

    if (request_id == 0) {
        response_body["message"] =
            "request_id must not be zero";

        response_body["reason"] =
            "invalid_request_id";

        send_response(response_body);
        return;
    }

    if (!HasFriendRequestRepository()) {
        response_body["message"] =
            "friend request repository "
            "unavailable";

        response_body["reason"] =
            "friend_request_service_unavailable";

        send_response(response_body);
        return;
    }

    const AcceptFriendRequestResult result =
        friend_request_repository_->
            AcceptFriendRequest(
                request_id,
                self_user_id
            );

    const bool changed =
        result.status ==
            AcceptFriendRequestStatus::
                kAccepted;

    const bool success =
        changed ||
        result.status ==
            AcceptFriendRequestStatus::
                kAlreadyAccepted;

    response_body["success"] =
        success;

    response_body["changed"] =
        changed;

    response_body["message"] =
        result.message;

    response_body["reason"] =
        AcceptFriendRequestStatusToString(
            result.status
        );

    send_response(response_body);

    LOG_INFO(
        "gateway friend request accept handled"
        << ", handler_user_id="
        << self_user_id
        << ", request_id="
        << request_id
        << ", status="
        << AcceptFriendRequestStatusToString(
               result.status
           )
        << ", success="
        << success
        << ", changed="
        << changed
    );
}

void GatewayServer::HandleFriendRequestRejectRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    Json response_body;

    response_body["success"] = false;
    response_body["changed"] = false;
    response_body["request_id"] = 0;

    auto send_response = [this, &connection, &packet] (
        const Json& body
    ) {
        Packet response;

        response.type = MessageType::kFriendRequestRejectResponse;

        response.seq = packet.seq;
        response.body = body.dump();

        SendPacket(connection, response);
    };

    Json request_body;
    std::string error_message;

    if (!ParseJsonBody(packet, &request_body, &error_message) ||
        !request_body.is_object()) {
            response_body["message"] = "invalid friend request reject json";
            response_body["reason"] = "invalid_json";

            send_response(response_body);
            return;
        }

        const auto login_user_id =
        session_manager_.
            FindUserByConnection(
                connection
            );

    if (!login_user_id.has_value()) {
        response_body["message"] =
            "friend request reject operation "
            "rejected: not logged in";

        response_body["reason"] =
            "not_logged_in";

        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request "
            "reject: not logged in"
            << ", peer="
            << connection->
                   PeerAddress().
                   ToString()
        );

        return;
    }

    const UserId self_user_id =
        login_user_id.value();

    response_body["user_id"] =
        self_user_id;

    if (request_body.contains(
            "handler_user_id")) {
        response_body["message"] =
            "handler_user_id must not be "
            "provided by client";

        response_body["reason"] =
            "forbidden_identity_field";

        send_response(response_body);

        LOG_WARN(
            "gateway rejected friend request "
            "reject: client supplied "
            "handler_user_id"
            << ", login_user_id="
            << self_user_id
        );

        return;
    }

    if (!request_body.contains(
            "request_id") ||
        !request_body.at(
            "request_id"
        ).is_number_unsigned()) {
        response_body["message"] =
            "invalid request_id";

        response_body["reason"] =
            "invalid_request_id";

        send_response(response_body);
        return;
    }

    const std::uint64_t request_id =
        request_body.at(
            "request_id"
        ).get<std::uint64_t>();

    response_body["request_id"] =
        request_id;

    if (request_id == 0) {
        response_body["message"] =
            "request_id must not be zero";

        response_body["reason"] =
            "invalid_request_id";

        send_response(response_body);
        return;
    }

    if (!HasFriendRequestRepository()) {
        response_body["message"] =
            "friend request repository "
            "unavailable";

        response_body["reason"] =
            "friend_request_service_unavailable";

        send_response(response_body);
        return;
    }

    const RejectFriendRequestResult result =
        friend_request_repository_->
            RejectFriendRequest(
                request_id,
                self_user_id
            );

    const bool success =
        result.RejectedOrAlreadyRejected();

    const bool changed =
        result.status ==
            RejectFriendRequestStatus::
                kRejected;

    response_body["success"] =
        success;

    response_body["changed"] =
        changed;

    response_body["message"] =
        result.message;

    response_body["reason"] =
        RejectFriendRequestStatusToString(
            result.status
        );

    send_response(response_body);

    LOG_INFO(
        "gateway friend request reject handled"
        << ", handler_user_id="
        << self_user_id
        << ", request_id="
        << request_id
        << ", status="
        << RejectFriendRequestStatusToString(
               result.status
           )
        << ", success="
        << success
        << ", changed="
        << changed
    );
}

void GatewayServer::HandleHeartbeat(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {

    const auto user_id =
    session_manager_.FindUserByConnection(connection);

    if (user_id.has_value()) {
        RefreshUserOnline(user_id.value());
    }

    Json response_body;
    response_body["pong"] = true;

    Packet response;
    response.type = MessageType::kHeartbeat;
    response.seq = packet.seq;
    response.body = response_body.dump();

    SendPacket(connection, response);
}

void GatewayServer::PushOfflineMessages(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    if (!connection || !connection->IsConnected()) {
        return;
    }

    std::vector<Packet> offline_packets =
        offline_message_store_.PopAll(user_id);

    if (offline_packets.empty()) {
        return;
    }

    LOG_INFO("gateway pushing offline messages"
             << ", user_id=" << user_id
             << ", count=" << offline_packets.size());

    for (const auto& offline_packet : offline_packets) {
        SendPacket(connection, offline_packet);
    }
}

void GatewayServer::NotifyLoginReplaced(
    UserId user_id,
    const TcpConnectionPtr& old_connection
) {
    if (!old_connection) {
        return;
    }

    if (!old_connection->IsConnected()) {
        return;
    }

    Json body;
    body["success"] = false;
    body["reason"] = "login_replaced";
    body["message"] = "account logged in from another connection";
    body["user_id"] = user_id;

    Packet packet;
    packet.type = MessageType::kError;
    packet.seq = 0;
    packet.body = body.dump();

    SendPacket(old_connection, packet);

    LOG_WARN("gateway replaced old login connection"
             << ", user_id=" << user_id
             << ", old_connection=" << old_connection->Name()
             << ", peer=" << old_connection->PeerAddress().ToString());

    old_connection->Shutdown();
}

void GatewayServer::SetUserOnline(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    if (!HasOnlineStatusCache()) {
        return;
    }

    if (!connection) {
        return;
    }

    const bool ok = online_status_cache_->SetOnline(
        user_id,
        options_.gateway_id,
        connection->Name(),
        options_.online_status_ttl_seconds
    );

    if (!ok) {
        LOG_WARN("gateway set user online status failed"
                 << ", user_id=" << user_id
                 << ", error=" << online_status_cache_->LastError());
    }
}

void GatewayServer::SetUserOffline(UserId user_id) {
    if (!HasOnlineStatusCache()) {
        return;
    }

    if (user_id == 0) {
        return;
    }

    const bool ok = online_status_cache_->SetOffline(user_id);

    if (!ok) {
        LOG_WARN("gateway set user offline status failed"
                 << ", user_id=" << user_id
                 << ", error=" << online_status_cache_->LastError());
    }
}

void GatewayServer::RefreshUserOnline(UserId user_id) {
    if (!HasOnlineStatusCache()) {
        return;
    }

    if (user_id == 0) {
        return;
    }

    const bool ok = online_status_cache_->RefreshOnline(
        user_id,
        options_.online_status_ttl_seconds
    );

    if (!ok) {
        LOG_WARN("gateway refresh user online status failed"
                 << ", user_id=" << user_id
                 << ", error=" << online_status_cache_->LastError());
    }
}

void GatewayServer::PushPersistentOfflineMessages(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    if (!connection || !connection->IsConnected()) {
        return;
    }

    if (!HasMessageRepository()) {
        return;
    }

    const auto pending_messages =
        message_repository_->ListPendingMessages(user_id, 100);

    if (pending_messages.empty()) {
        return;
    }

    LOG_INFO("gateway pushing persistent offline messages"
             << ", user_id=" << user_id
             << ", count=" << pending_messages.size());

    std::vector<std::uint64_t> delivered_message_ids;
    delivered_message_ids.reserve(pending_messages.size());

    for (const auto& message : pending_messages) {
        Packet packet;
        packet.type = MessageType::kChatMessage;
        packet.seq = static_cast<std::uint32_t>(message.message_id);
        packet.body = message.content;

        if (SendPacket(connection, packet)) {
            delivered_message_ids.push_back(message.message_id);
        }
    }

    if (!delivered_message_ids.empty()) {
        message_repository_->MarkDeliveredBatch(delivered_message_ids);
    }
}

/*
    Packet GatewayServer::MakeErrorPacket(std::uint32_t seq,
                    const std::string& message) const {
        Packet packet;
        packet.type = MessageType::kError;
        packet.seq = seq;

        packet.body =
            std::string(R"({"success":false,"message":")") +
            message +
            R"("})";

        return packet;
    }
*/
Packet GatewayServer::MakeErrorPacket(std::uint32_t seq,
                                      const std::string& message) const {
    Packet packet;
    packet.type = MessageType::kError;
    packet.seq = seq;

    packet.body =
        std::string(R"({"success":false,"message":")") +
        message +
        R"("})";

    return packet;
}

}  // namespace tinyimx