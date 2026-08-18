#include "gateway/GatewayServer.h"

#include "common/logging/LogMacros.h"
#include "common/net/EventLoop.h"
#include "services/repository/MessageRepository.h"
#include "services/repository/UserRepository.h"
#include "services/repository/FriendRequestRepository.h"
#include "services/cache/OnlineStatusCache.h"
#include "services/cache/UnreadCountCache.h"
#include "gateway/GatewayRouteResolver.h"
#include "common/protocol/GatewayPeerProtocol.h"
#include "gateway/GatewayPeerTransportManager.h"
#include "common/protocol/ClientChatProtocol.h"

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
GatewayServer::GatewayServer(
    EventLoop* loop,
    const InetAddress& listen_address,
    GatewayServerOptions options
)
    : loop_(loop),
      options_(std::move(options)),
      codec_(options_.max_body_size),
      server_(
          loop_,
          listen_address,
          options_.name,
          options_.io_thread_count
      ),
      offline_message_store_(
          options_.
              max_offline_messages_per_user
      ) {
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
    if (options_.gateway_id.empty()) {
        LOG_ERROR(
            "gateway server start failed"
            << ", reason=empty_gateway_id"
            << ", name=" << options_.name
        );

        return false;
    }

    LOG_INFO(
        "gateway server starting"
        << ", name=" << options_.name
        << ", gateway_id="
        << options_.gateway_id
        << ", listen="
        << server_.ListenAddress().ToString()
        << ", max_body_size="
        << options_.max_body_size
        << ", io_thread_count="
        << options_.io_thread_count
    );

    const bool ok = server_.Start();

    if (!ok) {
        LOG_ERROR(
            "gateway server start failed"
            << ", name=" << options_.name
            << ", gateway_id="
            << options_.gateway_id
        );

        return false;
    }

    LOG_INFO(
        "gateway server started"
        << ", name=" << options_.name
        << ", gateway_id="
        << options_.gateway_id
        << ", listen="
        << server_.ListenAddress().ToString()
        << ", io_thread_count="
        << options_.io_thread_count
    );

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

void GatewayServer::SetGatewayRouteResolver(
    GatewayRouteResolver*
        gateway_route_resolver
) {
    gateway_route_resolver_ =
        gateway_route_resolver;

    LOG_INFO(
        "gateway route resolver attached"
        << ", enabled="
        << (
            gateway_route_resolver_ !=
            nullptr
        )
    );
}



void GatewayServer::
SetGatewayPeerTransportManager(
    GatewayPeerTransportManager*
        manager
) {
    gateway_peer_transport_manager_ =
        manager;

    LOG_INFO(
        "gateway peer transport manager "
        "attached"
        << ", enabled="
        << (
            gateway_peer_transport_manager_
            != nullptr
        )
    );
}


bool GatewayServer::HasGatewayPeerTransportManager()
    const {
    return
        gateway_peer_transport_manager_
        != nullptr;
}


void GatewayServer::SetGatewayPeerVerifyCallback(
    GatewayPeerVerifyCallback callback) {
    gateway_peer_verify_callback_ =std::move(callback);

    LOG_INFO(
        "gateway peer verifier attached"
        << ", enabled="
        << static_cast<bool>(
            gateway_peer_verify_callback_
        )
    );
}



void GatewayServer::SetGatewayPeerResponseDropCallbackForTest(
    GatewayPeerResponseDropCallback callback
) {
    gateway_peer_response_drop_callback_for_test_ =
        std::move(callback);


    LOG_WARN(
        "gateway peer response fault injection "
        "callback attached"
        << ", enabled="
        << static_cast<bool>(
            gateway_peer_response_drop_callback_for_test_
        )
    );
}



bool GatewayServer::HasOnlineStatusCache() const {
    return online_status_cache_ != nullptr;
}

bool GatewayServer::HasGatewayRouteResolver() const {
    return
        gateway_route_resolver_ !=
        nullptr;
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

std::int64_t GatewayServer::GetTotalUnread(
    UserId user_id
) {
    if (!HasUnreadCountCache()) {
        return 0;
    }

    const GetUnreadCountResult result =
        unread_count_cache_->
            GetTotalUnread(
                user_id
            );

    if (!result.Completed()) {
        LOG_WARN(
            "gateway get total unread failed"
            << ", user_id="
            << user_id
            << ", status="
            << GetUnreadCountStatusToString(
                result.status
            )
            << ", error="
            << result.error_message
        );

        return 0;
    }

    return result.count;
}

std::int64_t GatewayServer::GetPrivateUnread(
    UserId receiver_user_id,
    UserId sender_user_id
) {
    if (!HasUnreadCountCache()) {
        return 0;
    }

    const GetUnreadCountResult result =
        unread_count_cache_->
            GetPrivateUnread(
                receiver_user_id,
                sender_user_id
            );

    if (!result.Completed()) {
        LOG_WARN(
            "gateway get private unread "
            "failed"
            << ", receiver="
            << receiver_user_id
            << ", sender="
            << sender_user_id
            << ", status="
            << GetUnreadCountStatusToString(
                result.status
            )
            << ", error="
            << result.error_message
        );

        return 0;
    }

    return result.count;
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

    const IncrementUnreadResult
        increment_result =
            unread_count_cache_->
                IncrementPrivateUnread(
                    receiver_user_id,
                    sender_user_id
                );

    if (!increment_result.Succeeded()) {
        LOG_WARN(
            "gateway increment unread failed"
            << ", receiver="
            << receiver_user_id
            << ", sender="
            << sender_user_id
            << ", status="
            << IncrementUnreadStatusToString(
                increment_result.status
            )
            << ", error="
            << increment_result.error_message
        );

        return 0;
    }

    const GetUnreadCountResult
        total_result =
            unread_count_cache_->
                GetTotalUnread(
                    receiver_user_id
                );

    if (total_result.Completed()) {
        if (total_unread != nullptr) {
            *total_unread =
                total_result.count;
        }
    } else {
        LOG_WARN(
            "gateway get total unread "
            "after increment failed"
            << ", receiver="
            << receiver_user_id
            << ", status="
            << GetUnreadCountStatusToString(
                total_result.status
            )
            << ", error="
            << total_result.error_message
        );
    }

    return increment_result.private_count;
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

void GatewayServer::SetUserOfflineIfMatch(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    if (!HasOnlineStatusCache()) {
        return;
    }

    if (user_id == 0 ||
        !connection) {
        return;
    }

    const SetOfflineIfMatchResult result =
        online_status_cache_->
            SetOfflineIfMatch(
                user_id,
                options_.gateway_id,
                connection->Name()
            );

    if (result.Completed()) {
        return;
    }

    LOG_WARN(
        "gateway conditional user "
        "offline failed"
        << ", user_id=" << user_id
        << ", gateway_id="
        << options_.gateway_id
        << ", connection="
        << connection->Name()
        << ", status="
        << SetOfflineIfMatchStatusToString(
            result.status
        )
        << ", error="
        << result.error_message
    );
}

void GatewayServer::HandleConnection(
    const TcpConnectionPtr& connection
) {
    if (!connection) {
        return;
    }

    if (connection->IsConnected()) {
        LOG_INFO(
            "gateway client connected"
            << ", name=" << options_.name
            << ", peer="
            << connection->
                PeerAddress().
                ToString()
            << ", connection_count="
            << server_.ConnectionCount()
        );

        return;
    }

    const SessionUnbindResult
        unbind_result =
            session_manager_.
                UnbindIfCurrent(
                    connection
                );

    if (unbind_result.unbound) {
        SetUserOfflineIfMatch(
            unbind_result.user_id,
            connection
        );
    }

    LOG_INFO(
        "gateway client disconnected"
        << ", name=" << options_.name
        << ", peer="
        << connection->
            PeerAddress().
            ToString()
        << ", connection="
        << connection->Name()
        << ", session_unbound="
        << unbind_result.unbound
        << ", user_id="
        << unbind_result.user_id
        << ", online_count="
        << session_manager_.
            OnlineCount()
    );
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

        case MessageType::
            kGatewayForwardChatRequest:
            HandleGatewayForwardChatRequest(
                connection,
                packet
            );
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

    const ClearUnreadResult
        clear_result =
            unread_count_cache_->
                ClearPrivateUnread(
                    reader_user_id,
                    peer_user_id
                );

    if (!clear_result.Completed()) {
        LOG_WARN(
            "gateway clear unread failed"
            << ", reader="
            << reader_user_id
            << ", peer="
            << peer_user_id
            << ", status="
            << ClearUnreadStatusToString(
                clear_result.status
            )
            << ", error="
            << clear_result.error_message
        );

        return false;
    }

    /*
     * private键存在并完成清除时，
     * Lua已经返回清除后的总未读数。
     */
    if (clear_result.Cleared()) {
        if (total_unread != nullptr) {
            *total_unread =
                clear_result.total_count;
        }

        return true;
    }

    /*
     * private键不存在时，Lua返回not_found。
     *
     * 但用户仍可能有其他发送者对应的未读消息，
     * 所以这里查询真实总未读数。
     */
    const GetUnreadCountResult
        total_result =
            unread_count_cache_->
                GetTotalUnread(
                    reader_user_id
                );

    if (total_result.Completed()) {
        if (total_unread != nullptr) {
            *total_unread =
                total_result.count;
        }
    } else {
        LOG_WARN(
            "gateway get total unread "
            "after clear not_found failed"
            << ", reader="
            << reader_user_id
            << ", status="
            << GetUnreadCountStatusToString(
                total_result.status
            )
            << ", error="
            << total_result.error_message
        );
    }

    return true;
}

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
        const auto pending_result =
            message_repository_->
                ListPendingMessages(
                    user_id,
                    100
                );

        if (pending_result.Succeeded()) {
            offline_count =
                pending_result.records.size();
        } else {
            LOG_WARN(
                "gateway count persistent "
                "offline messages failed"
                << ", user_id="
                << user_id
                << ", status="
                << MessageQueryStatusToString(
                    pending_result.status
                )
                << ", message="
                << pending_result.message
            );
        }
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
    auto send_client_chat_ack =
    [
        this,
        &connection,
        &packet
    ](
        const ClientChatAck& ack_value
    ) -> bool {
        std::string ack_body;
        std::string serialize_error;


        if (
            !SerializeClientChatAck(
                ack_value,
                &ack_body,
                &serialize_error
            )
        ) {
            LOG_ERROR(
                "gateway serialize client chat ack failed"
                << ", seq="
                << packet.seq
                << ", error="
                << serialize_error
            );


            SendPacket(
                connection,
                MakeErrorPacket(
                    packet.seq,
                    "chat ack serialization failed"
                )
            );


            return false;
        }


        Packet ack;

        ack.type =
            MessageType::kChatAck;

        ack.seq =
            packet.seq;

        ack.body =
            std::move(ack_body);


        return SendPacket(
            connection,
            ack
        );
    };


    ClientChatRequest request;

    std::string error_message;


    if (
        !DeserializeClientChatRequest(
            packet.body,
            &request,
            &error_message
        )
    ) {
        ClientChatAck ack;

        ack.success = false;
        ack.delivered = false;

        ack.reason =
            "invalid_chat_request";


        send_client_chat_ack(
            ack
        );


        LOG_WARN(
            "gateway rejected chat message: "
            "invalid client chat request"
            << ", peer="
            << connection->
                PeerAddress().
                ToString()
            << ", seq="
            << packet.seq
            << ", error="
            << error_message
        );


        return;
    }
    const std::optional<UserId> logged_user_id =
        session_manager_.
            FindUserByConnection(
                connection
            );

    if (!logged_user_id.has_value()) {
        ClientChatAck ack;

        ack.success = false;
        ack.delivered = false;

        ack.client_message_id =
            request.client_message_id;

        ack.to_user_id =
            request.to_user_id;

        ack.reason =
            "sender_not_logged_in";


        send_client_chat_ack(
            ack
        );


        LOG_WARN(
            "gateway rejected chat message: "
            "sender not logged in"
            << ", client_message_id="
            << request.client_message_id
            << ", peer="
            << connection->
                PeerAddress().
                ToString()
        );


        return;
    }

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


    if (
        request.claimed_from_user_id.
            has_value() &&
        request.claimed_from_user_id.
            value() !=
            from_user_id
    ) {
        ClientChatAck ack;

        ack.success = false;
        ack.delivered = false;

        ack.client_message_id =
            request.client_message_id;

        ack.from_user_id =
            from_user_id;

        ack.to_user_id =
            request.to_user_id;

        ack.reason =
            "from_user_mismatch";


        send_client_chat_ack(
            ack
        );


        LOG_WARN(
            "gateway rejected chat message: "
            "from user mismatch"
            << ", login_user_id="
            << from_user_id
            << ", claimed_from="
            << request.
                claimed_from_user_id.
                value()
            << ", client_message_id="
            << request.client_message_id
            << ", peer="
            << connection->
                PeerAddress().
                ToString()
        );


        return;
    }

    const UserId to_user_id = request.to_user_id;

    if (to_user_id == from_user_id) {
        ClientChatAck ack;

        ack.success = false;
        ack.delivered = false;

        ack.client_message_id =
            request.client_message_id;

        ack.from_user_id =
            from_user_id;

        ack.to_user_id =
            to_user_id;

        ack.reason =
            "invalid_to_user";


        send_client_chat_ack(
            ack
        );


        return;
    }

    if (!HasFriendRepository()) {
    Json ack_body;
    ack_body["success"] = false;
    ack_body["delivered"] = false;
    ack_body["client_message_id"] = request.client_message_id;
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
        ack_body["client_message_id"] = request.client_message_id;
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
    Json server_body{
        {
            "from",
            from_user_id
        },
        {
            "to",
            to_user_id
        },
        {
            "text",
            request.text
        }
    };

    const std::string
        server_body_text =
            server_body.dump();

    if (HasGatewayRouteResolver()) {
        const GatewayRouteResult route =
            gateway_route_resolver_->Resolve(
                to_user_id
            );

        /*
        * 目标在线于其他 Gateway。
        *
        * 这一阶段还没有 Gateway-to-Gateway
        * Transport，所以不能真正发送。
        *
        * 但最重要的是：
        * 不能再把它错误当成离线用户。
        */
        if (route.IsRemote()) {
            if (
                !route.remote_gateway.
                    has_value()
            ) {
                Packet ack;

                ack.type =
                    MessageType::kChatAck;

                ack.seq =
                    packet.seq;

                ack.body =
                    Json{
                        {"success", false},
                        {"delivered", false},
                        {"stored_offline", false},
                        {"stored_persistent", false},
                        {"message_id", 0},
                        {"from", from_user_id},
                        {"to", to_user_id},
                        {"reason",
                        "remote_gateway_missing"}
                    }.dump();

                SendPacket(
                    connection,
                    ack
                );

                return;
            }


            const GatewayInstanceRecord
                remote_gateway =
                    route.remote_gateway.value();


            /*
            * 1. 先做持久化。
            *
            * 跨节点投递开始时先保存Pending，
            * 防止网络投递过程中消息直接丢失。
            */
            std::uint64_t server_message_id = 0;

            bool stored_persistent = false;

            /*
            * false:
            * 当前Client发送首次创建了服务端消息。
            *
            * true:
            * 当前Client请求是对已经存在业务消息的Retry。
            */
            bool client_request_reused = false;

            if (HasMessageRepository()) {
                const IdempotentSavePrivateMessageResult
                    persist_result =
                        message_repository_->
                            SavePrivateMessageIdempotent(
                                from_user_id,
                                to_user_id,
                                server_body_text,
                                request.client_message_id,
                                PrivateMessageType::kText
                            );


                if (!persist_result.Succeeded()) {
                    ClientChatAck ack;

                    ack.success = false;
                    ack.delivered = false;

                    ack.client_message_id =
                        request.client_message_id;

                    /*
                    * Conflict情况下Repository会把
                    * 原client_message_id对应的message_id
                    * 返回出来。
                    *
                    * 其他Storage Error通常保持0。
                    */
                    ack.message_id =
                        persist_result.message_id;

                    ack.from_user_id =
                        from_user_id;

                    ack.to_user_id =
                        to_user_id;

                    ack.remote_gateway_id =
                        remote_gateway.gateway_id;

                    ack.remote_host =
                        remote_gateway.listen_host;

                    ack.remote_port =
                        remote_gateway.listen_port;


                    if (
                        persist_result.status ==
                        IdempotentSavePrivateMessageStatus::
                            kIdempotencyConflict
                    ) {
                        ack.reason =
                            "client_message_id_conflict";
                    } else {
                        ack.reason =
                            "remote_persistence_failed";
                    }


                    send_client_chat_ack(
                        ack
                    );


                    LOG_WARN(
                        "gateway remote idempotent "
                        "persistence failed"
                        << ", from="
                        << from_user_id
                        << ", to="
                        << to_user_id
                        << ", client_message_id="
                        << request.client_message_id
                        << ", status="
                        << IdempotentSavePrivateMessageStatusToString(
                            persist_result.status
                        )
                        << ", existing_message_id="
                        << persist_result.message_id
                        << ", message="
                        << persist_result.message
                    );


                    return;
                }


                server_message_id =
                    persist_result.message_id;

                stored_persistent = true;

                client_request_reused =
                    persist_result.Reused();


                LOG_INFO(
                    "gateway remote message "
                    "idempotent persistence accepted"
                    << ", client_message_id="
                    << request.client_message_id
                    << ", message_id="
                    << server_message_id
                    << ", created="
                    << persist_result.Created()
                    << ", reused="
                    << persist_result.Reused()
                    << ", from="
                    << from_user_id
                    << ", to="
                    << to_user_id
                );
            } else {
                /*
                * M12可靠发送依赖持久化业务幂等Key。
                *
                * 没有Repository就无法保证：
                *
                * Client Retry
                *     ↓
                * same client_message_id
                *     ↓
                * same server_message_id
                *
                * 所以远程Reliable Chat不再偷偷降级
                * 成一个没有幂等语义的发送。
                */
                ClientChatAck ack;

                ack.success = false;
                ack.delivered = false;

                ack.client_message_id =
                    request.client_message_id;

                ack.from_user_id =
                    from_user_id;

                ack.to_user_id =
                    to_user_id;

                ack.remote_gateway_id =
                    remote_gateway.gateway_id;

                ack.reason =
                    "message_persistence_unavailable";


                send_client_chat_ack(
                    ack
                );


                return;
            }


            std::int64_t
                receiver_private_unread = 0;

            std::int64_t
                receiver_total_unread = 0;

            /*
            * 已经持久化为Pending，
            * 即使后面RPC失败，该消息也已经被系统接受。
            */
            if (stored_persistent) {
                if (!client_request_reused) {
                    /*
                    * 只有真正创建新业务消息时，
                    * unread才允许发生一次增量副作用。
                    */
                    receiver_private_unread =
                        IncrementUnread(
                            to_user_id,
                            from_user_id,
                            &receiver_total_unread
                        );
                } else {
                    /*
                    * Client Retry不能再次增加Unread。
                    *
                    * 这里读取当前稳定状态，
                    * 只是为了保持ChatAck中的计数字段有意义。
                    */
                    receiver_private_unread =
                        GetPrivateUnread(
                            to_user_id,
                            from_user_id
                        );

                    receiver_total_unread =
                        GetTotalUnread(
                            to_user_id
                        );


                    LOG_INFO(
                        "gateway skipped unread increment "
                        "for reused client message"
                        << ", client_message_id="
                        << request.client_message_id
                        << ", message_id="
                        << server_message_id
                        << ", private_unread="
                        << receiver_private_unread
                        << ", total_unread="
                        << receiver_total_unread
                    );
                }
            }


            if (
                !HasGatewayPeerTransportManager()
            ) {
                Packet ack;

                ack.type =
                    MessageType::kChatAck;

                ack.seq =
                    packet.seq;

                const bool accepted =
                    stored_persistent;

                ack.body =
                    Json{
                        {"success", accepted},
                        {"delivered", false},
                        {"stored_offline",
                        stored_persistent},
                        {"stored_persistent",
                        stored_persistent},
                        {"client_message_id",
                        request.client_message_id},
                        {"reused",
                        client_request_reused},
                        {"message_id",
                        server_message_id},
                        {"from", from_user_id},
                        {"to", to_user_id},
                        {"remote_gateway_id",
                        remote_gateway.gateway_id},
                        {"reason",
                        stored_persistent
                            ? "remote_transport_unavailable_stored_pending"
                            : "remote_transport_unavailable"},
                        {"receiver_private_unread",
                        receiver_private_unread},
                        {"receiver_total_unread",
                        receiver_total_unread}
                    }.dump();

                SendPacket(
                    connection,
                    ack
                );

                return;
            }


            EventLoop* sender_loop =
                connection
                    ? connection->GetLoop()
                    : nullptr;


            const bool submitted =
                gateway_peer_transport_manager_->
                    ForwardChat(
                        remote_gateway,
                        server_message_id,
                        from_user_id,
                        to_user_id,
                        server_body_text,

                        [
                            this,
                            connection,
                            sender_loop,
                            from_user_id,
                            to_user_id,
                            remote_gateway,
                            server_message_id,
                            stored_persistent,
                            receiver_private_unread,
                            receiver_total_unread,

                            client_message_id =
                                request.client_message_id,

                            client_request_reused,

                            original_seq =
                                packet.seq
                        ](
                            GatewayPeerTransportResult
                                result
                        ) mutable {

                            auto complete =
                                [
                                    this,
                                    connection,
                                    from_user_id,
                                    to_user_id,
                                    remote_gateway,
                                    server_message_id,
                                    stored_persistent,
                                    receiver_private_unread,
                                    receiver_total_unread,

                                    client_message_id,
                                    client_request_reused,

                                    original_seq,
                                    result =
                                        std::move(result)
                                ]() mutable {

                                    bool delivered =
                                        false;

                                    bool stored_offline =
                                        false;

                                    bool delivery_marked =
                                        true;

                                    std::string reason;


                                    if (
                                        result.Succeeded() &&
                                        result.response.
                                            Delivered()
                                    ) {
                                        delivered = true;

                                        reason =
                                            "remote_delivered";

                                        if (
                                            stored_persistent &&
                                            server_message_id
                                                != 0 &&
                                            HasMessageRepository()
                                        ) {
                                            const auto
                                                mark_result =
                                                    message_repository_->
                                                        MarkDelivered(
                                                            server_message_id
                                                        );

                                            if (
                                                !mark_result.
                                                    Succeeded()
                                            ) {
                                                delivery_marked =
                                                    false;

                                                reason =
                                                    "remote_delivered_mark_failed";

                                                LOG_WARN(
                                                    "gateway remote "
                                                    "message delivered "
                                                    "but mark delivered "
                                                    "failed"
                                                    << ", message_id="
                                                    << server_message_id
                                                    << ", from="
                                                    << from_user_id
                                                    << ", to="
                                                    << to_user_id
                                                );
                                            }
                                        }
                                    } else {
                                        stored_offline =
                                            stored_persistent;

                                        if (
                                            result.Succeeded()
                                        ) {
                                            reason =
                                                "remote_" +
                                                GatewayForwardChatStatusToString(
                                                    result.response.
                                                        status
                                                );
                                        } else {
                                            reason =
                                                "remote_transport_" +
                                                GatewayPeerTransportStatusToString(
                                                    result.status
                                                );
                                        }
                                    }


                                    /*
                                    * 没有MySQL时，只有真实投递成功
                                    * 才认为消息被接受并计Unread。
                                    */
                                    std::int64_t
                                        private_unread =
                                            receiver_private_unread;

                                    std::int64_t
                                        total_unread =
                                            receiver_total_unread;

                                    if (
                                        delivered &&
                                        !stored_persistent
                                    ) {
                                        private_unread =
                                            IncrementUnread(
                                                to_user_id,
                                                from_user_id,
                                                &total_unread
                                            );
                                    }


                                    if (
                                        delivered &&
                                        stored_persistent &&
                                        !delivery_marked
                                    ) {
                                        /*
                                        * 实际已经送达，
                                        * 但数据库仍处于Pending。
                                        *
                                        * 先在ACK中暴露这一状态，
                                        * 后续可靠性阶段再解决
                                        * 幂等/重复投递问题。
                                        */
                                        stored_offline = true;
                                    }


                                    const bool accepted =
                                        delivered ||
                                        stored_persistent;


                                    Json ack_body;

                                    ack_body["success"] =
                                        accepted;

                                    ack_body["delivered"] =
                                        delivered;

                                    ack_body["stored_offline"] =
                                        stored_offline;

                                    ack_body[
                                        "stored_persistent"
                                    ] =
                                        stored_persistent;

                                    ack_body["message_id"] =
                                        server_message_id;

                                    ack_body["from"] =
                                        from_user_id;

                                    ack_body["to"] =
                                        to_user_id;

                                    ack_body[
                                        "remote_gateway_id"
                                    ] =
                                        remote_gateway.
                                            gateway_id;

                                    ack_body[
                                        "remote_host"
                                    ] =
                                        remote_gateway.
                                            listen_host;

                                    ack_body[
                                        "remote_port"
                                    ] =
                                        remote_gateway.
                                            listen_port;

                                    ack_body["reason"] =
                                        reason;

                                    ack_body[
                                        "receiver_private_unread"
                                    ] =
                                        private_unread;

                                    ack_body[
                                        "receiver_total_unread"
                                    ] =
                                        total_unread;


                                    Packet ack;

                                    ack.type =
                                        MessageType::
                                            kChatAck;

                                    ack.seq =
                                        original_seq;

                                    ack.body =
                                        ack_body.dump();

                                    SendPacket(
                                        connection,
                                        ack
                                    );


                                    LOG_INFO(
                                        "gateway remote chat "
                                        "completed"
                                        << ", from="
                                        << from_user_id
                                        << ", to="
                                        << to_user_id
                                        << ", remote_gateway="
                                        << remote_gateway.
                                            gateway_id
                                        << ", delivered="
                                        << delivered
                                        << ", reason="
                                        << reason
                                    );
                                };


                            /*
                            * RPC回调发生在Peer Reactor。
                            *
                            * MySQL MarkDelivered以及
                            * 客户端ACK重新投回原来的
                            * Client Sub-Reactor。
                            */
                            if (sender_loop != nullptr) {
                                sender_loop->RunInLoop(
                                    std::move(
                                        complete
                                    )
                                );
                            } else {
                                complete();
                            }
                        }
                    );


            if (!submitted) {
                Packet ack;

                ack.type =
                    MessageType::kChatAck;

                ack.seq =
                    packet.seq;

                ack.body =
                    Json{
                        {"success",
                        stored_persistent},
                        {"delivered", false},
                        {"stored_offline",
                        stored_persistent},
                        {"stored_persistent",
                        stored_persistent},
                        {"message_id",
                        server_message_id},
                        {"from", from_user_id},
                        {"to", to_user_id},
                        {"remote_gateway_id",
                        remote_gateway.gateway_id},
                        {"reason",
                        stored_persistent
                            ? "remote_submit_failed_stored_pending"
                            : "remote_submit_failed"},
                        {"receiver_private_unread",
                        receiver_private_unread},
                        {"receiver_total_unread",
                        receiver_total_unread}
                    }.dump();

                SendPacket(
                    connection,
                    ack
                );
            }

            return;
        }
        /*
        * OnlineStatus 还存在，
        * 但对应 Gateway 已从 Discovery 消失。
        *
        * 当前把它继续交给原来的
        * pending/offline persistence 逻辑。
        */
        if (
            route.status ==
            GatewayRouteStatus::
                kGatewayUnavailable
        ) {
            LOG_WARN(
                "gateway target route unavailable"
                << ", from="
                << from_user_id
                << ", to="
                << to_user_id
                << ", error="
                << route.error_message
            );
        }

        /*
        * Redis / Resolver 暂时错误时，
        * 不直接拒绝当前单机能力，
        * 继续使用原 SessionManager 路径。
        */
        if (
            !route.Resolved() &&
            route.status !=
                GatewayRouteStatus::
                    kGatewayUnavailable
        ) {
            LOG_WARN(
                "gateway route resolve failed, "
                "fallback to local session path"
                << ", from="
                << from_user_id
                << ", to="
                << to_user_id
                << ", status="
                << GatewayRouteStatusToString(
                    route.status
                )
                << ", error="
                << route.error_message
            );
        }
    }

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
        const SavePrivateMessageResult
            save_result =
                message_repository_->
                    SavePrivateMessage(
                        from_user_id,
                        to_user_id,
                        server_body_text,
                        delivered
                            ? DeliveryStatus::
                                kDelivered
                            : DeliveryStatus::
                                kPending
                    );

        if (save_result.Succeeded()) {
            server_message_id =
                save_result.message_id;

            stored_persistent = true;
        } else {
            LOG_WARN(
                "gateway save private "
                "message failed"
                << ", from="
                << from_user_id
                << ", to="
                << to_user_id
                << ", status="
                << MessageMutationStatusToString(
                    save_result.status
                )
                << ", message="
                << save_result.message
            );
        }
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



void GatewayServer::
HandleGatewayForwardChatRequest(
    const TcpConnectionPtr& connection,
    const Packet& packet
) {
    GatewayForwardChatResponse response;

    response.status =
        GatewayForwardChatStatus::
            kInternalError;

    response.target_gateway_id =
        options_.gateway_id;


    /*
     * 所有路径统一使用同一个
     * RPC Response。
     *
     * 最重要：
     * Response.seq 必须等于 Request.seq。
     */
    auto send_response =
        [
            this,
            &connection,
            &packet
        ](
            const GatewayForwardChatResponse&
                response_value
        ) {
            std::string body;
            std::string error_message;

            if (
                !SerializeGatewayForwardChatResponse(
                    response_value,
                    &body,
                    &error_message
                )
            ) {
                LOG_ERROR(
                    "gateway failed to serialize "
                    "forward chat response"
                    << ", seq="
                    << packet.seq
                    << ", error="
                    << error_message
                );

                return;
            }

            Packet response_packet;

            response_packet.type =
                MessageType::
                    kGatewayForwardChatResponse;

            response_packet.seq =
                packet.seq;

            response_packet.body =
                std::move(body);


            /*
            * Fault Injection Test Seam
            *
            * 这里只模拟：
            *
            * B业务已经执行完成，
            * 但Peer RPC Response丢失。
            *
            * 默认callback为空，所以正常运行完全不受影响。
            */
            if (
                gateway_peer_response_drop_callback_for_test_ &&
                gateway_peer_response_drop_callback_for_test_(
                    response_value
                )
            ) {
                LOG_WARN(
                    "gateway peer response dropped by "
                    "fault injection"
                    << ", message_id="
                    << response_value.message_id
                    << ", status="
                    << static_cast<std::uint32_t>(
                        response_value.status
                    )
                    << ", duplicate="
                    << response_value.duplicate
                    << ", seq="
                    << packet.seq
                );


                return;
            }


            SendPacket(
                connection,
                response_packet
            );
        };


    /*
     * 1. 解析内部RPC请求。
     */
    GatewayForwardChatRequest request;

    std::string error_message;

    if (
        !DeserializeGatewayForwardChatRequest(
            packet.body,
            &request,
            &error_message
        )
    ) {
        response.status =
            GatewayForwardChatStatus::
                kInvalidRequest;

        response.error_message =
            error_message;

        send_response(response);

        LOG_WARN(
            "gateway rejected invalid "
            "forward chat request"
            << ", seq="
            << packet.seq
            << ", peer="
            << (
                connection
                    ? connection->
                        PeerAddress().
                        ToString()
                    : std::string{}
            )
            << ", error="
            << error_message
        );

        return;
    }


    response.message_id =
        request.message_id;

    response.to_user_id =
        request.to_user_id;

    response.duplicate =
        false;


    /*
     * 2. 内部协议不允许走普通用户Session。
     *
     * 一个已经登录为用户的连接，
     * 不应该同时充当Gateway Peer。
     */
    if (
        session_manager_.
            FindUserByConnection(
                connection
            ).
            has_value()
    ) {
        response.status =
            GatewayForwardChatStatus::
                kUnauthorized;

        response.error_message =
            "user session cannot send "
            "gateway internal request";

        send_response(response);

        LOG_WARN(
            "gateway rejected internal "
            "request from user session"
            << ", source_gateway="
            << request.source_gateway_id
            << ", to="
            << request.to_user_id
        );

        return;
    }


    /*
     * 3. 必须配置Gateway Peer鉴权器。
     *
     * 默认拒绝，而不是默认信任。
     */
    if (!gateway_peer_verify_callback_) {
        response.status =
            GatewayForwardChatStatus::
                kInternalError;

        response.error_message =
            "gateway peer verifier unavailable";

        send_response(response);

        LOG_ERROR(
            "gateway peer verifier unavailable"
            << ", source_gateway="
            << request.source_gateway_id
            << ", to="
            << request.to_user_id
        );

        return;
    }



    /*
     * 4. 验证：
     *
     * source_gateway_id
     * +
     * source_lease_token
     *
     * 当前生产实现走
     * GatewayDiscovery本地snapshot。
     */
    std::string verify_error;

    if (
        !gateway_peer_verify_callback_(
            request.source_gateway_id,
            request.source_lease_token,
            &verify_error
        )
    ) {
        response.status =
            GatewayForwardChatStatus::
                kUnauthorized;

        response.error_message =
            verify_error.empty()
                ? "gateway peer unauthorized"
                : verify_error;

        send_response(response);

        LOG_WARN(
            "gateway rejected unauthorized "
            "peer request"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", to="
            << request.to_user_id
            << ", error="
            << response.error_message
        );

        return;
    }
    /*
     * Gateway Peer身份已经真正验证通过。
     *
     * 只有合法Peer现在才允许进入
     * message_id幂等状态机。
     */
  const GatewayPeerDedupBeginStatus
        dedup_status =
            gateway_peer_delivery_deduplicator_.
                Begin(
                    request.message_id
                );

                /*
        * 同一个业务message_id以前
        * 已经完成过本地投递。
        *
        * 本次绝对不能再次Push给用户。
        */
        if (
            dedup_status ==
            GatewayPeerDedupBeginStatus::
                kAlreadyDelivered
        ) {
            response.status =
                GatewayForwardChatStatus::
                    kDelivered;

            response.duplicate =
                true;

            response.error_message.clear();


            send_response(response);


            LOG_INFO(
                "gateway suppressed already "
                "delivered peer message"
                << ", message_id="
                << request.message_id
                << ", source_gateway="
                << request.source_gateway_id
                << ", from="
                << request.from_user_id
                << ", to="
                << request.to_user_id
            );


            return;
        }

                /*
        * 同一个message_id已经有
        * 另外一个Sub-Reactor线程
        * 正在处理。
        *
        * 当前请求不能再执行一次。
        */
        if (
            dedup_status ==
            GatewayPeerDedupBeginStatus::
                kAlreadyProcessing
        ) {
            response.status =
                GatewayForwardChatStatus::
                    kDuplicateInProgress;

            response.duplicate =
                true;

            response.error_message =
                "same message is already "
                "being delivered";


            send_response(response);


            LOG_INFO(
                "gateway suppressed concurrent "
                "duplicate peer message"
                << ", message_id="
                << request.message_id
                << ", source_gateway="
                << request.source_gateway_id
                << ", to="
                << request.to_user_id
            );


            return;
        }

        if (
            dedup_status !=
            GatewayPeerDedupBeginStatus::
                kAcquired
        ) {
            response.status =
                GatewayForwardChatStatus::
                    kInvalidRequest;

            response.duplicate =
                false;

            response.error_message =
                "invalid message id";


            send_response(response);


            return;
        }

    /*
     * 5. 内存Dedup miss以后，
     * 使用MySQL持久状态进行第二层幂等检查。
     *
     * 当前已经获得了message_id执行权，
     * 所以后续任何“不产生用户副作用”的失败路径
     * 都必须Abort()释放Processing状态。
     */
    if (message_repository_ == nullptr) {
        gateway_peer_delivery_deduplicator_.
            Abort(
                request.message_id
            );

        response.status =
            GatewayForwardChatStatus::
                kInternalError;

        response.duplicate =
            false;

        response.error_message =
            "message repository unavailable";

        send_response(response);

        LOG_ERROR(
            "gateway peer durable dedup "
            "repository unavailable"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", from="
            << request.from_user_id
            << ", to="
            << request.to_user_id
        );

        return;
    }

    const FindPrivateMessageResult
        persisted_result =
            message_repository_->
                FindPrivateMessageById(
                    request.message_id
                );


    if (!persisted_result.Succeeded()) {
        gateway_peer_delivery_deduplicator_.
            Abort(
                request.message_id
            );

        response.status =
            GatewayForwardChatStatus::
                kInternalError;

        response.duplicate =
            false;

        response.error_message =
            persisted_result.message.empty()
                ? "durable message lookup failed"
                : persisted_result.message;

        send_response(response);

        LOG_ERROR(
            "gateway peer durable dedup "
            "lookup failed"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", status="
            << MessageQueryStatusToString(
                   persisted_result.status
               )
            << ", error="
            << response.error_message
        );

        return;
    }

        if (!persisted_result.Found()) {
        gateway_peer_delivery_deduplicator_.
            Abort(
                request.message_id
            );

        response.status =
            GatewayForwardChatStatus::
                kInvalidRequest;

        response.duplicate =
            false;

        response.error_message =
            "persisted message not found";

        send_response(response);

        LOG_WARN(
            "gateway rejected peer message "
            "without persisted record"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", from="
            << request.from_user_id
            << ", to="
            << request.to_user_id
        );

        return;
    }

    const PrivateMessageRecord&
        persisted_message =
            persisted_result.record;


                const std::uint32_t
        expected_message_type =
            static_cast<std::uint32_t>(
                PrivateMessageType::kText
            );


    if (
        persisted_message.message_id !=
            request.message_id ||
        persisted_message.from_user_id !=
            request.from_user_id ||
        persisted_message.to_user_id !=
            request.to_user_id ||
        persisted_message.message_type !=
            expected_message_type ||
        persisted_message.content !=
            request.message_body
    ) {
        gateway_peer_delivery_deduplicator_.
            Abort(
                request.message_id
            );

        response.status =
            GatewayForwardChatStatus::
                kInvalidRequest;

        response.duplicate =
            false;

        response.error_message =
            "persisted message identity mismatch";

        send_response(response);

        LOG_WARN(
            "gateway rejected peer message "
            "identity mismatch"
            << ", message_id="
            << request.message_id
            << ", request_from="
            << request.from_user_id
            << ", persisted_from="
            << persisted_message.from_user_id
            << ", request_to="
            << request.to_user_id
            << ", persisted_to="
            << persisted_message.to_user_id
        );

        return;
    }

    const std::uint32_t
        persisted_status =
            persisted_message.
                delivery_status;


                    const std::uint32_t
        delivered_status =
            static_cast<std::uint32_t>(
                DeliveryStatus::
                    kDelivered
            );

    const std::uint32_t
        read_status =
            static_cast<std::uint32_t>(
                DeliveryStatus::
                    kRead
            );


    if (
        persisted_status ==
            delivered_status ||
        persisted_status ==
            read_status
    ) {
        /*
         * MySQL已经证明这条消息过去
         * 完成过投递。
         *
         * 即使Gateway B刚重启、
         * 内存Dedup已经丢失，
         * 这里也绝不能再次Push。
         *
         * 同时把Memory Dedup恢复为Delivered，
         * 后续重复RPC就不用继续访问MySQL。
         */
        const bool restored =
            gateway_peer_delivery_deduplicator_.
                MarkDelivered(
                    request.message_id
                );


        if (!restored) {
            LOG_ERROR(
                "gateway durable duplicate "
                "memory restore failed"
                << ", message_id="
                << request.message_id
            );
        }


        response.status =
            GatewayForwardChatStatus::
                kDelivered;

        response.duplicate =
            true;

        response.error_message.clear();

        send_response(response);


        LOG_INFO(
            "gateway suppressed durable "
            "duplicate peer message"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", persisted_status="
            << persisted_status
            << ", from="
            << request.from_user_id
            << ", to="
            << request.to_user_id
        );

        return;
    }



        const std::uint32_t
        failed_status =
            static_cast<std::uint32_t>(
                DeliveryStatus::
                    kFailed
            );


    if (
        persisted_status ==
        failed_status
    ) {
        gateway_peer_delivery_deduplicator_.
            Abort(
                request.message_id
            );

        response.status =
            GatewayForwardChatStatus::
                kInternalError;

        response.duplicate =
            false;

        response.error_message =
            "persisted message is failed";

        send_response(response);

        LOG_WARN(
            "gateway rejected peer message "
            "in failed persisted state"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
        );

        return;
    }

    const std::uint32_t
        pending_status =
            static_cast<std::uint32_t>(
                DeliveryStatus::
                    kPending
            );

                if (
        persisted_status !=
        pending_status
    ) {
        gateway_peer_delivery_deduplicator_.
            Abort(
                request.message_id
            );

        response.status =
            GatewayForwardChatStatus::
                kInternalError;

        response.duplicate =
            false;

        response.error_message =
            "unexpected persisted delivery status";

        send_response(response);

        LOG_ERROR(
            "gateway peer message has "
            "unexpected persisted status"
            << ", message_id="
            << request.message_id
            << ", delivery_status="
            << persisted_status
        );

        return;
    }
    /*
     * 5. 找本机用户Session。
     *
     * 注意：
     *
     * 这里绝对不是Redis OnlineStatus。
     *
     * 请求已经被路由到当前Gateway，
     * 服务端最终投递必须以
     * 本机SessionManager为准。
     */
    TcpConnectionPtr target_connection =
        session_manager_.
            FindConnection(
                request.to_user_id
            );

    if (
        !target_connection ||
        !target_connection->
            IsConnected()
    ) {
                /*
        * 当前没有产生消息投递副作用，
        * 所以释放message_id执行权，
        * 允许未来重新Retry。
        */
        gateway_peer_delivery_deduplicator_.
            Abort(
                request.message_id
            );
        response.status =
            GatewayForwardChatStatus::
                kTargetNotConnected;

        response.error_message =
            "target user has no active "
            "local session";

        send_response(response);

        LOG_INFO(
            "gateway forward chat target "
            "not connected"
            << ", source_gateway="
            << request.source_gateway_id
            << ", message_id="
            << request.message_id
            << ", to="
            << request.to_user_id
        );

        return;
    }


    /*
     * 6. 构造普通客户端能够理解的
     * kChatMessage。
     *
     * 内部协议3001到这里终止，
     * 不能直接把3001发给用户。
     */
    Packet forward_packet;

    forward_packet.type =
        MessageType::kChatMessage;

    forward_packet.seq =
        packet.seq;

    forward_packet.body =
        request.message_body;


    if (
        !SendPacket(
            target_connection,
            forward_packet
        )
    ) {

        gateway_peer_delivery_deduplicator_.
            Abort(
                request.message_id
            );
        response.status =
            GatewayForwardChatStatus::
                kTargetNotConnected;

        response.error_message =
            "target connection became "
            "unavailable";

        send_response(response);

        LOG_WARN(
            "gateway forward chat local "
            "send failed"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", to="
            << request.to_user_id
        );

        return;
    }



        /*
     * 7. 本地客户端发送路径已经接受消息。
     *
     * 此时用户侧副作用已经发生，
     * 因此立即把共享MySQL中的消息状态
     * 从Pending推进到Delivered。
     *
     * 这样即使后续Gateway Peer Response
     * 丢失，Gateway B重启以后仍然可以
     * 从MySQL恢复“已经投递”的事实。
     */
    const UpdatePrivateMessagesResult
        durable_mark_result =
            message_repository_->
                MarkDelivered(
                    request.message_id
                );


    if (!durable_mark_result.Succeeded()) {
        /*
         * 非常重要：
         *
         * 此时绝对不能Abort内存Dedup。
         *
         * 因为SendPacket已经成功，
         * 用户侧副作用已经发生。
         *
         * 如果这里Abort，
         * 随后的Retry可能再次Push，
         * 直接制造重复消息。
         */
        LOG_ERROR(
            "gateway peer local delivery "
            "succeeded but durable mark failed"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", from="
            << request.from_user_id
            << ", to="
            << request.to_user_id
            << ", status="
            << MessageMutationStatusToString(
                   durable_mark_result.status
               )
            << ", error="
            << durable_mark_result.message
        );
        } else if (
            durable_mark_result.
                affected_rows == 0
        ) {
            LOG_WARN(
                "gateway peer durable delivery "
                "mark changed zero rows"
                << ", message_id="
                << request.message_id
                << ", source_gateway="
                << request.source_gateway_id
                << ", from="
                << request.from_user_id
                << ", to="
                << request.to_user_id
            );
        } else {
            LOG_INFO(
                "gateway peer durable delivery "
                "state advanced"
                << ", message_id="
                << request.message_id
                << ", source_gateway="
                << request.source_gateway_id
                << ", from="
                << request.from_user_id
                << ", to="
                << request.to_user_id
                << ", affected_rows="
                << durable_mark_result.
                    affected_rows
            );
        }

    /*
     * 7. 服务端RPC成功。
     */
    const bool dedup_marked =
        gateway_peer_delivery_deduplicator_.
            MarkDelivered(
                request.message_id
            );


    if (!dedup_marked) {
        /*
        * 理论上：
        *
        * Begin()已经kAcquired，
        * 所以这里必须能成功Mark。
        *
        * 如果失败属于内部状态机异常。
        *
        * 但用户消息已经发送出去，
        * 绝不能因为这个内部异常
        * 返回“未投递”诱导上游直接重发。
        */
        LOG_ERROR(
            "gateway peer delivery "
            "dedup mark failed"
            << ", message_id="
            << request.message_id
            << ", source_gateway="
            << request.source_gateway_id
            << ", to="
            << request.to_user_id
        );
    }
    response.status =
        GatewayForwardChatStatus::
            kDelivered;

    response.error_message.clear();

    send_response(response);

    LOG_INFO(
        "gateway forwarded remote chat "
        "to local session"
        << ", message_id="
        << request.message_id
        << ", source_gateway="
        << request.source_gateway_id
        << ", from="
        << request.from_user_id
        << ", to="
        << request.to_user_id
        << ", seq="
        << packet.seq
    );
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
        const UpdatePrivateMessagesResult
            mark_read_result =
                message_repository_->
                    MarkReadByDialog(
                        reader_user_id,
                        peer_user_id
                    );

        if (!mark_read_result.Succeeded()) {
            response_body["success"] = false;

            response_body["message"] =
                mark_read_result.message;

            response_body["reason"] =
                MessageMutationStatusToString(
                    mark_read_result.status
                );

            response_body["user_id"] =
                reader_user_id;

            response_body["peer_user_id"] =
                peer_user_id;

            response_body["private_unread"] = 0;
            response_body["total_unread"] = 0;
            response_body["marked_read_count"] = 0;

            Packet response;
            response.type =
                MessageType::kReadResponse;

            response.seq = packet.seq;

            response.body =
                response_body.dump();

            SendPacket(
                connection,
                response
            );

            LOG_WARN(
                "gateway mark dialog read failed"
                << ", reader="
                << reader_user_id
                << ", peer="
                << peer_user_id
                << ", status="
                << MessageMutationStatusToString(
                    mark_read_result.status
                )
                << ", message="
                << mark_read_result.message
            );

            return;
        }

        marked_read_count =
            mark_read_result.affected_rows;
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

    auto history_result =
        message_repository_->
            ListDialogMessages(
                self_user_id,
                peer_user_id,
                before_message_id,
                query_limit
            );

    if (!history_result.Succeeded()) {
        response_body["message"] =
            history_result.message;

        response_body["reason"] =
            MessageQueryStatusToString(
                history_result.status
            );

        response_body["user_id"] =
            self_user_id;

        response_body["peer_user_id"] =
            peer_user_id;

        send_response(response_body);

        LOG_WARN(
            "gateway history query failed"
            << ", user_id="
            << self_user_id
            << ", peer_user_id="
            << peer_user_id
            << ", status="
            << MessageQueryStatusToString(
                history_result.status
            )
            << ", message="
            << history_result.message
        );

        return;
    }

    auto messages =
        std::move(
            history_result.records
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

    auto conversation_result =
        message_repository_->
            ListConversations(
                self_user_id,
                query_limit
            );

    if (!conversation_result.Succeeded()) {
        response_body["message"] =
            conversation_result.message;

        response_body["reason"] =
            MessageQueryStatusToString(
                conversation_result.status
            );

        response_body["user_id"] =
            self_user_id;

        send_response(response_body);

        LOG_WARN(
            "gateway conversation list "
            "query failed"
            << ", user_id="
            << self_user_id
            << ", status="
            << MessageQueryStatusToString(
                conversation_result.status
            )
            << ", message="
            << conversation_result.message
        );

        return;
    }

    auto conversations =
        std::move(
            conversation_result.records
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

    auto list_result =
        friend_repository_->ListFriends(
            self_user_id,
            query_limit
        );

    if (!list_result.Succeeded()) {
        response_body["message"] =
            list_result.message;

        response_body["reason"] =
            ListFriendsStatusToString(
                list_result.status
            );

        response_body["user_id"] =
            self_user_id;

        send_response(response_body);

        LOG_WARN(
            "gateway friend list request failed"
            << ", user_id="
            << self_user_id
            << ", status="
            << ListFriendsStatusToString(
                list_result.status
            )
            << ", message="
            << list_result.message
        );

        return;
    }

    auto friends = std::move(list_result.records);
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

    auto list_result =
        friend_request_repository_->
            ListPendingIncomingRequests(
                self_user_id,
                before_created_at,
                before_request_id,
                query_limit
            );

    if (!list_result.Succeeded()) {
        response_body["message"] =
            list_result.message;

        response_body["reason"] =
            ListPendingIncomingRequestsStatusToString(
                list_result.status
            );

        send_response(response_body);
        return;
    }

    auto requests = std::move(list_result.records);

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
        session_manager_.
            FindUserByConnection(
                connection
            );

    if (user_id.has_value()) {
        RefreshUserOnlineIfMatch(
            user_id.value(),
            connection
        );
    }

    Json response_body;
    response_body["pong"] = true;

    Packet response;
    response.type =
        MessageType::kHeartbeat;
    response.seq = packet.seq;
    response.body =
        response_body.dump();

    SendPacket(
        connection,
        response
    );
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

const SetOnlineResult result = online_status_cache_->
            SetOnline(
                user_id,
                options_.gateway_id,
                connection->Name(),
                options_.
                    online_status_ttl_seconds
            );

    if (result.Succeeded()) {
        return;
    }

    LOG_WARN(
        "gateway set user online "
        "status failed"
        << ", user_id=" << user_id
        << ", gateway_id="
        << options_.gateway_id
        << ", connection="
        << connection->Name()
        << ", status="
        << SetOnlineStatusToString(
            result.status
        )
        << ", error="
        << result.error_message
    );
}

void GatewayServer::RefreshUserOnlineIfMatch(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    if (!HasOnlineStatusCache()) {
        return;
    }

    if (user_id == 0 ||
        !connection) {
        return;
    }

    const RefreshOnlineIfMatchResult result =
        online_status_cache_->
            RefreshOnlineIfMatch(
                user_id,
                options_.gateway_id,
                connection->Name(),
                options_.
                    online_status_ttl_seconds
            );

    if (result.Refreshed()) {
        return;
    }

    if (result.status ==
        RefreshOnlineIfMatchStatus::
            kMismatch) {
        LOG_INFO(
            "gateway ignored stale online "
            "status refresh"
            << ", user_id="
            << user_id
            << ", gateway_id="
            << options_.gateway_id
            << ", connection="
            << connection->Name()
        );

        return;
    }

    LOG_WARN(
        "gateway refresh user online "
        "status failed"
        << ", user_id="
        << user_id
        << ", gateway_id="
        << options_.gateway_id
        << ", connection="
        << connection->Name()
        << ", status="
        << RefreshOnlineIfMatchStatusToString(
            result.status
        )
        << ", error="
        << result.error_message
    );
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

    auto pending_result =
        message_repository_->
            ListPendingMessages(
                user_id,
                100
            );

    if (!pending_result.Succeeded()) {
        LOG_WARN(
            "gateway list persistent "
            "offline messages failed"
            << ", user_id="
            << user_id
            << ", status="
            << MessageQueryStatusToString(
                pending_result.status
            )
            << ", message="
            << pending_result.message
        );

        return;
    }

    auto pending_messages =
        std::move(
            pending_result.records
        );

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
        const UpdatePrivateMessagesResult
            delivered_result =
                message_repository_->
                    MarkDeliveredBatch(
                        delivered_message_ids
                    );

        if (!delivered_result.Succeeded()) {
            LOG_WARN(
                "gateway mark persistent "
                "offline messages delivered "
                "failed"
                << ", user_id="
                << user_id
                << ", requested_count="
                << delivered_message_ids.size()
                << ", status="
                << MessageMutationStatusToString(
                    delivered_result.status
                )
                << ", message="
                << delivered_result.message
            );
        }
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