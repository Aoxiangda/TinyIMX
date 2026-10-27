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

std::vector<UnreadProjectionReadResult<DialogUnreadSnapshot>>
UnreadProjectionReader::LoadDialogSnapshots(
    const std::vector<std::pair<std::uint64_t, std::uint64_t>>& dialogs
) {
    if (dialogs.empty() || dialogs.size() > 256) return {};
    std::vector<UnreadProjectionReadResult<DialogUnreadSnapshot>> results(dialogs.size());
    const auto fail = [&](const std::string& message) {
        for (auto& result : results) {
            result.success = false;
            result.value = {};
            result.message = message;
        }
        return results;
    };
    if (pool_ == nullptr)
        return fail("unread projection batch read requires MySQL pool");
    for (const auto& [receiver, peer] : dialogs) {
        if (receiver == 0 || peer == 0 || receiver == peer)
            return fail("unread projection batch read rejected invalid dialog");
    }
    auto connection = pool_->Acquire();
    if (!connection)
        return fail("unread projection batch read failed: acquire MySQL connection");

    // One SELECT gives all dialogs the same InnoDB read view. The bounded
    // derived request table keeps result cardinality equal to the input even
    // for empty receivers, duplicate dialogs and several peers of one user.
    // COUNT(message_id), not COUNT(*), makes a LEFT JOIN miss count as zero.
    std::string sql =
        "SELECT requested.request_index, "
        "COALESCE(SUM(messages.from_user_id = requested.peer_id), 0), "
        "COUNT(messages.message_id) FROM (";
    for (std::size_t i = 0; i < dialogs.size(); ++i) {
        if (i != 0) sql += " UNION ALL ";
        sql += "SELECT " + std::to_string(i) + " AS request_index, " +
            std::to_string(dialogs[i].first) + " AS receiver_id, " +
            std::to_string(dialogs[i].second) + " AS peer_id";
    }
    sql += ") AS requested LEFT JOIN im_private_messages AS messages "
        "ON messages.to_user_id = requested.receiver_id "
        "AND messages.delivery_status IN (0, 1) "
        "GROUP BY requested.request_index";
    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) return fail(connection->LastError());
    if (query.rows.size() != dialogs.size())
        return fail("unread projection batch returned invalid result count");
    std::vector<bool> seen(dialogs.size(), false);
    for (const auto& row : query.rows) {
        if (row.size() != 3)
            return fail("unread projection batch returned invalid row shape");
        std::size_t consumed = 0;
        std::uint64_t ordinal = 0;
        try {
            ordinal = std::stoull(row[0], &consumed, 10);
        } catch (...) {
            return fail("unread projection batch returned invalid request index");
        }
        if (consumed != row[0].size() || ordinal >= dialogs.size() || seen[ordinal])
            return fail("unread projection batch returned duplicate/foreign request index");
        auto& snapshot = results[ordinal].value;
        snapshot.receiver_user_id = dialogs[ordinal].first;
        snapshot.peer_user_id = dialogs[ordinal].second;
        if (!ParseCount(row[1], &snapshot.private_unread) ||
            !ParseCount(row[2], &snapshot.total_unread) ||
            snapshot.private_unread > snapshot.total_unread)
            return fail("unread projection batch returned invalid count snapshot");
        seen[ordinal] = true;
        results[ordinal].success = true;
        results[ordinal].message = "unread projection batch snapshot loaded";
    }
    return results;
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
