#pragma once

#include "common/net/InetAddress.h"
#include "common/net/TcpConnection.h"
#include "common/net/TcpServer.h"
#include "common/protocol/ProtocolCodec.h"
#include "gateway/SessionManager.h"
#include "gateway/OfflineMessageStore.h"
#include "services/repository/FriendRepository.h"

#include <cstddef>
#include <functional>
#include <string>

namespace tinyimx {

class MessageRepository;
class UserRepository;
class FriendRequestRepository;

class OnlineStatusCache;
class UnreadCountCache;

class EventLoop;


/*
    struct GatewayServerOptions {
        std::string name{"tinyimx-gateway"};
        std::size_t max_body_size{kDefaultMaxBodySize};
        bool close_on_decode_error{true};
    };
*/


struct GatewayServerOptions {
    std::string name{"tinyimx-gateway"};

    std::size_t max_body_size{kDefaultMaxBodySize};

    bool close_on_decode_error{true};

    std::size_t io_thread_count{0};

    std::size_t
        max_offline_messages_per_user{
            100
        };

    std::string gateway_id{
        "tinyimx-gateway-1"
    };

    int online_status_ttl_seconds{120};
};


class GatewayServer {
public:

    using PacketHandler =
        std::function<void(const TcpConnectionPtr&, const Packet&)>;

    GatewayServer(EventLoop* loop,
                  const InetAddress& listen_address,
                  GatewayServerOptions options);

    ~GatewayServer();

    GatewayServer(const GatewayServer&) = delete;
    GatewayServer& operator=(const GatewayServer&) = delete;

    bool Start();
    void Stop();

    void SetPacketHandler(PacketHandler handler);

    bool SendPacket(const TcpConnectionPtr& connection,
                    const Packet& packet);

    const std::string& Name() const;
    const InetAddress& ListenAddress() const;
    std::size_t ConnectionCount() const;

    void SetMessageRepository(MessageRepository* message_repository);
    void SetUserRepository(UserRepository* user_repository);
    void SetFriendRepository(FriendRepository* repository);
    void SetFriendRequestRepository(
        FriendRequestRepository* repository
    );

    void SetOnlineStatusCache(OnlineStatusCache* online_status_cache);
    void SetUnreadCountCache(UnreadCountCache* unread_count_cache);
/*
    private:
    void HandleConnection(const TcpConnectionPtr& connection);
    void HandleMessage(const TcpConnectionPtr& connection,
                       Buffer* buffer);

    void HandlePacket(const TcpConnectionPtr& connection,
                      const Packet& packet);

    Packet BuildDefaultResponse(const Packet& request) const;
    Packet MakeErrorPacket(std::uint32_t seq,
                           const std::string& message) const;
*/


private:
    void HandleConnection(const TcpConnectionPtr& connection);
    void HandleMessage(const TcpConnectionPtr& connection,
                       Buffer* buffer);

    void HandlePacket(const TcpConnectionPtr& connection,
                      const Packet& packet);

    void HandleLoginRequest(const TcpConnectionPtr& connection,
                            const Packet& packet);

    void HandleChatMessage(const TcpConnectionPtr& connection,
                           const Packet& packet);

    void HandleReadRequest(const TcpConnectionPtr& connection,
                           const Packet& packet);

    void HandleHistoryRequest(const TcpConnectionPtr& connection,
                              const Packet& packet);

    void HandleFriendListRequest(const TcpConnectionPtr& connection,
                                 const Packet& packet);

    void HandleConversationListRequest(const TcpConnectionPtr& connection,
                                       const Packet& packet);

    void HandleFriendRequestCreateRequest(
        const TcpConnectionPtr& connection,
        const Packet& packet
    );

    void HandleFriendRequestListRequest(
        const TcpConnectionPtr& connection,
        const Packet& packet
    );

    void HandleFriendRequestAcceptRequest(
        const TcpConnectionPtr& connection,
        const Packet& packet
    );

    void HandleFriendRequestRejectRequest(
        const TcpConnectionPtr& connection,
        const Packet& packet
    );

    bool ClearUnread(UserId reader_user_id,
                     UserId peer_user_id,
                     std::int64_t* total_unread);

    void HandleHeartbeat(const TcpConnectionPtr& connection,
                         const Packet& packet);

    Packet MakeErrorPacket(std::uint32_t seq,
                           const std::string& message) const;

    void PushOfflineMessages(UserId user_id,
                         const TcpConnectionPtr& connection);

    void PushPersistentOfflineMessages(UserId user_id,
                                   const TcpConnectionPtr& connection);

    bool HasMessageRepository() const;
    bool HasUserRepository() const;
    bool HasFriendRepository() const;
    bool HasFriendRequestRepository() const;

    bool HasOnlineStatusCache() const;
    bool HasUnreadCountCache() const;

    void NotifyLoginReplaced(UserId user_id,
                             const TcpConnectionPtr& old_connection);

    void SetUserOnline(UserId user_id,
                    const TcpConnectionPtr& connection);

    void SetUserOfflineIfMatch(UserId user_id, const TcpConnectionPtr& connection);

    std::int64_t GetTotalUnread(UserId user_id);

    std::int64_t GetPrivateUnread(UserId receiver_user_id,
                                  UserId sender_user_id);

    std::int64_t IncrementUnread(UserId receiver_user_id,
                                 UserId sender_user_id,
                                 std::int64_t* total_unread);

    void RefreshUserOnlineIfMatch(
        UserId user_id,
        const TcpConnectionPtr& connection
    );

private:
    EventLoop* loop_{nullptr};

    GatewayServerOptions options_;

    ProtocolCodec codec_;
    TcpServer server_;

    SessionManager session_manager_;
    OfflineMessageStore offline_message_store_;

    MessageRepository* message_repository_{nullptr};
    UserRepository* user_repository_{nullptr};
    FriendRepository* friend_repository_{nullptr};
    FriendRequestRepository* friend_request_repository_{nullptr};

    OnlineStatusCache* online_status_cache_{nullptr};
    UnreadCountCache* unread_count_cache_{nullptr};

    PacketHandler packet_handler_;
};

}  // namespace tinyimx