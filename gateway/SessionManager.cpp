#include "gateway/SessionManager.h"

#include "common/logging/LogMacros.h"

namespace tinyimx {

SessionEpoch
SessionManager::AllocateEpochLocked() {
    /*
     * epoch=0始终保留给invalid。
     */
    if (next_epoch_ == 0) {
        next_epoch_ = 1;
    }

    const SessionEpoch epoch =
        next_epoch_;

    ++next_epoch_;

    /*
     * uint64_t wrap在实际系统几乎不可达到，
     * 这里仍保证下一次不会返回0。
     */
    if (next_epoch_ == 0) {
        next_epoch_ = 1;
    }

    return epoch;
}


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


    if (
        user_id == 0 ||
        !connection
    ) {
        return result;
    }


    std::lock_guard<std::mutex>
        lock(mutex_);


    const std::string
        new_connection_key =
            ConnectionKey(
                connection
            );


    if (new_connection_key.empty()) {
        return result;
    }


    /*
     * ============================================================
     * 1. 清理当前user原来的Session ownership
     * ============================================================
     */
    auto old_session_iter =
        user_sessions_.find(
            user_id
        );

    if (
        old_session_iter !=
        user_sessions_.end()) {
        const SessionRecord
            old_record =
                old_session_iter->second;
        TcpConnectionPtr old_connection =
            old_record.connection.lock();


        if (
            old_connection &&
            old_record.connection_key !=
                new_connection_key
        ) {
            result.replaced = true;

            result.old_connection =
                old_connection;
        }


        /*
         * 删除reverse mapping时必须同时匹配：
         *
         * user_id
         * epoch
         *
         * 防止stale record误删新Session。
         */
        auto old_reverse_iter =
            connection_sessions_.find(
                old_record.connection_key
            );


        if (
            old_reverse_iter !=
                connection_sessions_.end() &&
            old_reverse_iter->
                second.user_id ==
                user_id &&
            old_reverse_iter->
                second.epoch ==
                old_record.epoch
        ) {
            connection_sessions_.erase(
                old_reverse_iter
            );
        }


        user_sessions_.erase(
            old_session_iter
        );
    }


    /*
     * ============================================================
     * 2. 当前connection如果曾属于其他user，
     *    清理它旧的ownership
     * ============================================================
     */
    auto old_owner_iter =
        connection_sessions_.find(
            new_connection_key
        );


    if (
        old_owner_iter !=
        connection_sessions_.end()
    ) {
        const ConnectionSessionRecord
            old_owner =
                old_owner_iter->second;


        auto old_owner_session_iter =
            user_sessions_.find(
                old_owner.user_id
            );


        if (
            old_owner_session_iter !=
                user_sessions_.end() &&
            old_owner_session_iter->
                second.epoch ==
                old_owner.epoch &&
            old_owner_session_iter->
                second.connection_key ==
                new_connection_key
        ) {
            user_sessions_.erase(
                old_owner_session_iter
            );
        }


        connection_sessions_.erase(
            old_owner_iter
        );
    }


    /*
     * ============================================================
     * 3. 创建全新的Session generation
     * ============================================================
     */
    const SessionEpoch new_epoch =
        AllocateEpochLocked();


    SessionRecord new_record;

    new_record.connection =
        connection;

    new_record.epoch =
        new_epoch;

    new_record.connection_key =
        new_connection_key;


    user_sessions_[
        user_id
    ] = std::move(
        new_record
    );


    connection_sessions_[
        new_connection_key
    ] = ConnectionSessionRecord{
        user_id,
        new_epoch
    };


    result.success = true;

    result.epoch =
        new_epoch;


    LOG_INFO(
        "session bound"
        << ", user_id="
        << user_id
        << ", epoch="
        << new_epoch
        << ", connection="
        << new_connection_key
        << ", replaced="
        << result.replaced
        << ", online_count="
        << user_sessions_.size()
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
        ConnectionKey(
            connection
        );


    if (connection_key.empty()) {
        return result;
    }


    auto connection_iter =
        connection_sessions_.find(
            connection_key
        );


    if (
        connection_iter ==
        connection_sessions_.end()
    ) {
        return result;
    }


    const ConnectionSessionRecord
        reverse_record =
            connection_iter->second;


    auto session_iter =
        user_sessions_.find(
            reverse_record.user_id
        );


    if (
        session_iter ==
        user_sessions_.end()
    ) {
        connection_sessions_.erase(
            connection_iter
        );


        LOG_WARN(
            "stale connection session "
            "mapping removed"
            << ", user_id="
            << reverse_record.user_id
            << ", epoch="
            << reverse_record.epoch
            << ", connection="
            << connection_key
        );


        return result;
    }


    const SessionRecord&
        current_record =
            session_iter->second;


    TcpConnectionPtr current_connection =
        current_record.connection.lock();


    const bool same_generation =
        current_record.epoch ==
            reverse_record.epoch &&
        current_record.connection_key ==
            connection_key;


    if (
        !same_generation ||
        !current_connection ||
        current_connection != connection
    ) {
        /*
         * reverse mapping已经不是current ownership，
         * 删除stale reverse即可。
         */
        connection_sessions_.erase(
            connection_iter
        );


        /*
         * 只有当user record本身也是这一代，
         * 但connection对象已经expired，
         * 才能删除user record。
         */
        if (
            same_generation &&
            !current_connection
        ) {
            user_sessions_.erase(
                session_iter
            );
        }


        LOG_INFO(
            "session unbind ignored: "
            "connection generation "
            "is not current"
            << ", user_id="
            << reverse_record.user_id
            << ", stale_epoch="
            << reverse_record.epoch
            << ", current_epoch="
            << current_record.epoch
            << ", connection="
            << connection_key
        );


        return result;
    }


    connection_sessions_.erase(
        connection_iter
    );

    user_sessions_.erase(
        session_iter
    );


    result.unbound = true;

    result.user_id =
        reverse_record.user_id;

    result.epoch =
        reverse_record.epoch;


    LOG_INFO(
        "session unbound"
        << ", user_id="
        << result.user_id
        << ", epoch="
        << result.epoch
        << ", connection="
        << connection_key
        << ", online_count="
        << user_sessions_.size()
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

std::optional<SessionSnapshot>
SessionManager::FindSession(
    UserId user_id
) const {
    if (user_id == 0) {
        return std::nullopt;
    }


    std::lock_guard<std::mutex>
        lock(mutex_);


    auto iter =
        user_sessions_.find(
            user_id
        );


    if (
        iter ==
        user_sessions_.end()
    ) {
        return std::nullopt;
    }


    TcpConnectionPtr connection =
        iter->second.connection.lock();


    if (!connection) {
        return std::nullopt;
    }


    SessionSnapshot snapshot;

    snapshot.user_id =
        user_id;

    snapshot.epoch =
        iter->second.epoch;

    snapshot.connection =
        std::move(connection);


    return snapshot;
}


std::optional<SessionSnapshot>
SessionManager::FindSessionByConnection(
    const TcpConnectionPtr& connection
) const {
    if (!connection) {
        return std::nullopt;
    }


    const std::string connection_key =
        ConnectionKey(
            connection
        );


    if (connection_key.empty()) {
        return std::nullopt;
    }


    std::lock_guard<std::mutex>
        lock(mutex_);


    auto reverse_iter =
        connection_sessions_.find(
            connection_key
        );


    if (
        reverse_iter ==
        connection_sessions_.end()
    ) {
        return std::nullopt;
    }


    const ConnectionSessionRecord&
        reverse =
            reverse_iter->second;


    auto session_iter =
        user_sessions_.find(
            reverse.user_id
        );


    if (
        session_iter ==
        user_sessions_.end()
    ) {
        return std::nullopt;
    }


    const SessionRecord&
        record =
            session_iter->second;


    if (
        record.epoch !=
            reverse.epoch ||
        record.connection_key !=
            connection_key
    ) {
        return std::nullopt;
    }


    TcpConnectionPtr current =
        record.connection.lock();


    if (
        !current ||
        current != connection
    ) {
        return std::nullopt;
    }


    SessionSnapshot snapshot;

    snapshot.user_id =
        reverse.user_id;

    snapshot.epoch =
        reverse.epoch;

    snapshot.connection =
        std::move(current);


    return snapshot;
}


bool SessionManager::IsCurrent(
    UserId user_id,
    SessionEpoch epoch,
    const TcpConnectionPtr& connection
) const {
    if (
        user_id == 0 ||
        epoch == 0 ||
        !connection
    ) {
        return false;
    }


    std::lock_guard<std::mutex>
        lock(mutex_);


    auto iter =
        user_sessions_.find(
            user_id
        );


    if (
        iter ==
        user_sessions_.end()
    ) {
        return false;
    }


    const SessionRecord&
        record =
            iter->second;


    if (
        record.epoch != epoch ||
        record.connection_key !=
            ConnectionKey(connection)
    ) {
        return false;
    }


    TcpConnectionPtr current =
        record.connection.lock();


    return
        current &&
        current == connection;
}

TcpConnectionPtr
SessionManager::FindConnection(
    UserId user_id
) const {
    auto snapshot =
        FindSession(
            user_id
        );

    if (!snapshot) {
        return nullptr;
    }

    return snapshot->connection;
}

std::optional<UserId>
SessionManager::FindUserByConnection(
    const TcpConnectionPtr& connection
) const {
    auto snapshot =
        FindSessionByConnection(
            connection
        );

    if (!snapshot) {
        return std::nullopt;
    }

    return snapshot->user_id;
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

    return user_sessions_.size();
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