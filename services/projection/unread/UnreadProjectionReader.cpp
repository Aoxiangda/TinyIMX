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
    DialogUnreadSnapshot snapshot;
    snapshot.receiver_user_id = receiver_user_id;
    snapshot.peer_user_id = peer_user_id;

    // Both counts come from the same ordinary InnoDB SELECT read view. The
    // statement cannot mix a private count before a concurrent write with a
    // total count after it. COALESCE preserves zero for an empty receiver.
    const std::string sql =
        "SELECT COALESCE(SUM(from_user_id = " + std::to_string(peer_user_id) +
        "), 0), COUNT(*) FROM im_private_messages WHERE to_user_id = " +
        std::to_string(receiver_user_id) +
        " AND delivery_status IN (0, 1)";
    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        result.message = connection->LastError();
        return result;
    }
    if (query.rows.size() != 1 || query.rows.front().size() != 2 ||
        !ParseCount(query.rows.front()[0], &snapshot.private_unread) ||
        !ParseCount(query.rows.front()[1], &snapshot.total_unread) ||
        snapshot.private_unread > snapshot.total_unread) {
        result.message = "unread projection query returned invalid count snapshot";
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
    // This grouped SELECT already yields every peer and the total derived
    // below from one read view, including historical peers with zero unread.
    const std::string sql =
        "SELECT from_user_id, "
        "SUM(CASE WHEN delivery_status IN (0, 1) THEN 1 ELSE 0 END) "
        "FROM im_private_messages WHERE to_user_id = " +
        std::to_string(receiver_user_id) +
        " GROUP BY from_user_id ORDER BY from_user_id";

    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        result.message = connection->LastError();
        return result;
    }

    UserUnreadSnapshot snapshot;
    snapshot.receiver_user_id = receiver_user_id;
    snapshot.peer_counts.reserve(query.rows.size());
    for (const auto& row : query.rows) {
        if (row.size() != 2) {
            result.message = "unread projection user read returned invalid shape";
            return result;
        }
        std::uint64_t peer = 0;
        std::int64_t count = 0;
        if (!ParseUserId(row[0], &peer) || !ParseCount(row[1], &count)) {
            result.message = "unread projection user read returned invalid row";
            return result;
        }
        snapshot.peer_counts.emplace_back(peer, count);
        if (count > std::numeric_limits<std::int64_t>::max() - snapshot.total_unread) {
            result.message = "unread projection total count overflow";
            return result;
        }
        snapshot.total_unread += count;
    }

    result.success = true;
    result.value = std::move(snapshot);
    result.message = "unread projection user snapshot loaded";
    return result;
}

}  // namespace tinyimx::projection::unread
