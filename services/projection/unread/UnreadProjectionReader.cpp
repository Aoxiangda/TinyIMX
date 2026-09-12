#include "services/projection/unread/UnreadProjectionReader.h"

#include "common/db/MySqlConnection.h"
#include "common/db/MySqlConnectionPool.h"

#include <limits>
#include <string>

namespace tinyimx::projection::unread {
namespace {

bool ParseCount(const std::string& text, std::int64_t* value) {
    if (value == nullptr || text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(text, &consumed, 10);
        if (consumed != text.size() ||
            parsed > static_cast<unsigned long long>(
                std::numeric_limits<std::int64_t>::max()
            )) {
            return false;
        }
        *value = static_cast<std::int64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseUserId(const std::string& text, std::uint64_t* value) {
    if (value == nullptr || text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(text, &consumed, 10);
        if (consumed != text.size() || parsed == 0) {
            return false;
        }
        *value = static_cast<std::uint64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool QuerySingleCount(
    MySqlConnection* connection,
    const std::string& sql,
    std::int64_t* value,
    std::string* error
) {
    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        if (error != nullptr) {
            *error = connection->LastError();
        }
        return false;
    }
    if (query.rows.size() != 1 || query.rows.front().size() != 1 ||
        !ParseCount(query.rows.front().front(), value)) {
        if (error != nullptr) {
            *error = "unread projection query returned invalid count";
        }
        return false;
    }
    return true;
}

}  // namespace

UnreadProjectionReader::UnreadProjectionReader(MySqlConnectionPool* pool)
    : pool_(pool) {
}

UnreadProjectionReadResult<DialogUnreadSnapshot>
UnreadProjectionReader::LoadDialogSnapshot(
    std::uint64_t receiver_user_id,
    std::uint64_t peer_user_id
) {
    UnreadProjectionReadResult<DialogUnreadSnapshot> result;
    if (pool_ == nullptr || receiver_user_id == 0 || peer_user_id == 0 ||
        receiver_user_id == peer_user_id) {
        result.message = "unread projection dialog read rejected invalid input";
        return result;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        result.message = "unread projection dialog read failed: acquire MySQL connection";
        return result;
    }
    if (!connection->BeginTransaction()) {
        result.message = connection->LastError();
        return result;
    }

    DialogUnreadSnapshot snapshot;
    snapshot.receiver_user_id = receiver_user_id;
    snapshot.peer_user_id = peer_user_id;

    const std::string private_sql =
        "SELECT COUNT(*) FROM im_private_messages WHERE to_user_id = " +
        std::to_string(receiver_user_id) +
        " AND from_user_id = " + std::to_string(peer_user_id) +
        " AND delivery_status IN (0, 1)";
    if (!QuerySingleCount(
            connection.operator->(),
            private_sql,
            &snapshot.private_unread,
            &result.message)) {
        (void)connection->Rollback();
        return result;
    }

    const std::string total_sql =
        "SELECT COUNT(*) FROM im_private_messages WHERE to_user_id = " +
        std::to_string(receiver_user_id) +
        " AND delivery_status IN (0, 1)";
    if (!QuerySingleCount(
            connection.operator->(),
            total_sql,
            &snapshot.total_unread,
            &result.message)) {
        (void)connection->Rollback();
        return result;
    }

    if (!connection->Commit()) {
        result.message = connection->LastError();
        return result;
    }

    result.success = true;
    result.value = snapshot;
    result.message = "unread projection dialog snapshot loaded";
    return result;
}

UnreadProjectionReadResult<UserUnreadSnapshot>
UnreadProjectionReader::LoadUserSnapshot(
    std::uint64_t receiver_user_id
) {
    UnreadProjectionReadResult<UserUnreadSnapshot> result;
    if (pool_ == nullptr || receiver_user_id == 0) {
        result.message = "unread projection user read rejected invalid input";
        return result;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        result.message = "unread projection user read failed: acquire MySQL connection";
        return result;
    }
    if (!connection->BeginTransaction()) {
        result.message = connection->LastError();
        return result;
    }

    const std::string sql =
        "SELECT from_user_id, "
        "SUM(CASE WHEN delivery_status IN (0, 1) THEN 1 ELSE 0 END) "
        "FROM im_private_messages WHERE to_user_id = " +
        std::to_string(receiver_user_id) +
        " GROUP BY from_user_id ORDER BY from_user_id";

    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        result.message = connection->LastError();
        (void)connection->Rollback();
        return result;
    }

    UserUnreadSnapshot snapshot;
    snapshot.receiver_user_id = receiver_user_id;
    snapshot.peer_counts.reserve(query.rows.size());
    for (const auto& row : query.rows) {
        if (row.size() != 2) {
            result.message = "unread projection user read returned invalid shape";
            (void)connection->Rollback();
            return result;
        }
        std::uint64_t peer = 0;
        std::int64_t count = 0;
        if (!ParseUserId(row[0], &peer) || !ParseCount(row[1], &count)) {
            result.message = "unread projection user read returned invalid row";
            (void)connection->Rollback();
            return result;
        }
        snapshot.peer_counts.emplace_back(peer, count);
        if (count > std::numeric_limits<std::int64_t>::max() - snapshot.total_unread) {
            result.message = "unread projection total count overflow";
            (void)connection->Rollback();
            return result;
        }
        snapshot.total_unread += count;
    }

    if (!connection->Commit()) {
        result.message = connection->LastError();
        return result;
    }

    result.success = true;
    result.value = std::move(snapshot);
    result.message = "unread projection user snapshot loaded";
    return result;
}

}  // namespace tinyimx::projection::unread
