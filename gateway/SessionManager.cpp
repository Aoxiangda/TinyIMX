#include "gateway/SessionManager.h"

#include "common/logging/LogMacros.h"

namespace tinyimx {

bool SessionManager::Bind(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    return BindOrReplace(
        user_id,
        connection
    ).success;
}

SessionBindResult
SessionManager::BindOrReplace(
    UserId user_id,
    const TcpConnectionPtr& connection
) {
    SessionBindResult result;

    if (user_id == 0 ||
        !connection) {
        return result;
    }

    std::lock_guard<std::mutex>
        lock(mutex_);

    const std::string new_connection_key =
        ConnectionKey(connection);

    if (new_connection_key.empty()) {
        return result;
    }

    auto old_connection_iter =
        user_connections_.find(
            user_id
        );

    if (old_connection_iter !=
        user_connections_.end()) {
        TcpConnectionPtr old_connection =
            old_connection_iter->
                second.lock();

        if (old_connection) {
            const std::string
                old_connection_key =
                    ConnectionKey(
                        old_connection
                    );

            if (old_connection_key !=
                new_connection_key) {
                result.replaced = true;
                result.old_connection =
                    old_connection;
            }

            connection_users_.erase(
                old_connection_key
            );
        }

        user_connections_.erase(
            old_connection_iter
        );
    }

    auto old_user_iter =
        connection_users_.find(
            new_connection_key
        );

    if (old_user_iter !=
        connection_users_.end()) {
        user_connections_.erase(
            old_user_iter->second
        );

        connection_users_.erase(
            old_user_iter
        );
    }

    user_connections_[user_id] =
        connection;

    connection_users_[
        new_connection_key
    ] = user_id;

    result.success = true;

    LOG_INFO(
        "session bound"
        << ", user_id=" << user_id
        << ", connection="
        << new_connection_key
        << ", replaced="
        << result.replaced
        << ", online_count="
        << user_connections_.size()
    );

    return result;
}

SessionUnbindResult
SessionManager::UnbindIfCurrent(
    const TcpConnectionPtr& connection
) {
    SessionUnbindResult result;

    if (!connection) {
        return result;
    }

    std::lock_guard<std::mutex>
        lock(mutex_);

    const std::string connection_key =
        ConnectionKey(connection);

    if (connection_key.empty()) {
        return result;
    }

    auto connection_iter =
        connection_users_.find(
            connection_key
        );

    if (connection_iter ==
        connection_users_.end()) {
        return result;
    }

    const UserId user_id =
        connection_iter->second;

    auto user_iter =
        user_connections_.find(
            user_id
        );

    if (user_iter ==
        user_connections_.end()) {
        connection_users_.erase(
            connection_iter
        );

        LOG_WARN(
            "stale connection-to-user "
            "session mapping removed"
            << ", user_id=" << user_id
            << ", connection="
            << connection_key
        );

        return result;
    }

    TcpConnectionPtr current_connection =
        user_iter->second.lock();

    if (!current_connection ||
        current_connection != connection) {
        connection_users_.erase(
            connection_iter
        );

        if (!current_connection) {
            user_connections_.erase(
                user_iter
            );
        }

        LOG_INFO(
            "session unbind ignored: "
            "connection is not current"
            << ", user_id=" << user_id
            << ", connection="
            << connection_key
            << ", current_connection="
            << (
                current_connection
                    ? current_connection->
                        Name()
                    : "expired"
            )
        );

        return result;
    }

    connection_users_.erase(
        connection_iter
    );

    user_connections_.erase(
        user_iter
    );

    result.unbound = true;
    result.user_id = user_id;

    LOG_INFO(
        "session unbound"
        << ", user_id=" << user_id
        << ", connection="
        << connection_key
        << ", online_count="
        << user_connections_.size()
    );

    return result;
}

void SessionManager::UnbindByConnection(
    const TcpConnectionPtr& connection
) {
    (void)UnbindIfCurrent(
        connection
    );
}

TcpConnectionPtr
SessionManager::FindConnection(
    UserId user_id
) const {
    std::lock_guard<std::mutex>
        lock(mutex_);

    auto iter =
        user_connections_.find(
            user_id
        );

    if (iter ==
        user_connections_.end()) {
        return nullptr;
    }

    return iter->second.lock();
}

std::optional<UserId>
SessionManager::FindUserByConnection(
    const TcpConnectionPtr& connection
) const {
    if (!connection) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex>
        lock(mutex_);

    const std::string connection_key =
        ConnectionKey(connection);

    auto iter =
        connection_users_.find(
            connection_key
        );

    if (iter ==
        connection_users_.end()) {
        return std::nullopt;
    }

    return iter->second;
}

bool SessionManager::IsOnline(
    UserId user_id
) const {
    return FindConnection(user_id) !=
           nullptr;
}

std::size_t
SessionManager::OnlineCount() const {
    std::lock_guard<std::mutex>
        lock(mutex_);

    return user_connections_.size();
}

std::string
SessionManager::ConnectionKey(
    const TcpConnectionPtr& connection
) {
    if (!connection) {
        return {};
    }

    return connection->Name();
}

}  // namespace tinyimx