#pragma once

#include "common/net/TcpConnection.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace tinyimx {

using UserId = std::uint64_t;

struct SessionBindResult {
    bool success{false};
    bool replaced{false};
    TcpConnectionPtr old_connection;
};


class SessionManager {
public:
    SessionManager() = default;
    ~SessionManager() = default;

    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    bool Bind(UserId user_id, const TcpConnectionPtr& connection);

    SessionBindResult BindOrReplace(UserId user_id,
                                    const TcpConnectionPtr& connection);

    void UnbindByConnection(const TcpConnectionPtr& connection);

    TcpConnectionPtr FindConnection(UserId user_id) const;

    std::optional<UserId> FindUserByConnection(
        const TcpConnectionPtr& connection
    ) const;

    bool IsOnline(UserId user_id) const;

    std::size_t OnlineCount() const;

private:
    static std::string ConnectionKey(
        const TcpConnectionPtr& connection
    );

private:
    mutable std::mutex mutex_;

    std::unordered_map<UserId, std::weak_ptr<TcpConnection>>
        user_connections_;

    std::unordered_map<std::string, UserId>
        connection_users_;
};

}  // namespace tinyimx