#pragma once

#include "common/net/TcpConnection.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace tinyimx {

using UserId = std::uint64_t;

using SessionEpoch = std::uint64_t;

struct SessionSnapshot {
    UserId user_id{0};

    SessionEpoch epoch{0};

    TcpConnectionPtr connection;

    bool Valid() const noexcept {
        return
            user_id != 0 &&
            epoch != 0 &&
            static_cast<bool>(
                connection
            );
    }
};


struct SessionBindResult {
    bool success{false};
    bool replaced{false};

    TcpConnectionPtr old_connection;

    SessionEpoch epoch{0};
};

struct SessionUnbindResult {
    bool unbound{false};
    UserId user_id{0};
    SessionEpoch epoch{0};
};

class SessionManager {
public:
    SessionManager() = default;
    ~SessionManager() = default;

    SessionManager(
        const SessionManager&
    ) = delete;

    SessionManager& operator=(
        const SessionManager&
    ) = delete;

    bool Bind(
        UserId user_id,
        const TcpConnectionPtr& connection
    );

    SessionBindResult BindOrReplace(
        UserId user_id,
        const TcpConnectionPtr& connection
    );

    SessionUnbindResult UnbindIfCurrent(
        const TcpConnectionPtr& connection
    );

    void UnbindByConnection(
        const TcpConnectionPtr& connection
    );

    TcpConnectionPtr FindConnection(
        UserId user_id
    ) const;

    std::optional<UserId>
    FindUserByConnection(
        const TcpConnectionPtr& connection
    ) const;
    std::optional<SessionSnapshot>
    FindSession(
        UserId user_id
    ) const;


    std::optional<SessionSnapshot>
    FindSessionByConnection(
        const TcpConnectionPtr& connection
    ) const;

    bool IsCurrent(
        UserId user_id,
        SessionEpoch epoch,
        const TcpConnectionPtr& connection
    ) const;


    bool IsOnline(
        UserId user_id
    ) const;

    std::size_t OnlineCount() const;

private:
    static std::string ConnectionKey(
        const TcpConnectionPtr& connection
    );

private:
    mutable std::mutex mutex_;

    struct SessionRecord {
    std::weak_ptr<TcpConnection>
        connection;

    SessionEpoch epoch{0};

    std::string connection_key;
};


struct ConnectionSessionRecord {
    UserId user_id{0};

    SessionEpoch epoch{0};
};


SessionEpoch AllocateEpochLocked();


std::unordered_map<
    UserId,
    SessionRecord
> user_sessions_;


std::unordered_map<
    std::string,
    ConnectionSessionRecord
> connection_sessions_;


/*
 * 所有读写都在mutex_内，
 * 因此不需要atomic。
 *
 * epoch=0保留为invalid。
 */
SessionEpoch next_epoch_{1};
};

}  // namespace tinyimx