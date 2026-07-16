#include "services/repository/MessageRepository.h"

#include "common/logging/LogMacros.h"

#include <sstream>
#include <stdexcept>

namespace tinyimx {
namespace {

std::uint64_t ToUInt64(const std::string& value) {
    return static_cast<std::uint64_t>(std::stoull(value));
}

std::uint32_t ToUInt32(const std::string& value) {
    return static_cast<std::uint32_t>(std::stoul(value));
}

std::string JoinMessageIds(
    const std::vector<std::uint64_t>& message_ids
) {
    std::ostringstream oss;

    for (std::size_t i = 0; i < message_ids.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }

        oss << message_ids[i];
    }

    return oss.str();
}

}  // namespace

MessageRepository::MessageRepository(MySqlConnectionPool* pool)
    : pool_(pool) {}

std::uint64_t MessageRepository::SavePrivateMessage(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& content,
    DeliveryStatus delivery_status,
    PrivateMessageType message_type,
    const std::string& client_message_id
) {
    if (pool_ == nullptr) {
        SetError("message repository save failed: pool is null");
        return 0;
    }

    if (from_user_id == 0 || to_user_id == 0) {
        SetError("message repository save failed: invalid user id");
        return 0;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("message repository save failed: acquire connection failed");
        return 0;
    }

    const std::string escaped_content =
        connection->EscapeString(content);

    std::string client_message_sql = "NULL";
    if (!client_message_id.empty()) {
        client_message_sql =
            "'" + connection->EscapeString(client_message_id) + "'";
    }

    const std::string sql =
        "INSERT INTO im_private_messages "
        "("
        "client_message_id, "
        "from_user_id, "
        "to_user_id, "
        "message_type, "
        "content, "
        "delivery_status"
        ") VALUES ("
        + client_message_sql + ", "
        + std::to_string(from_user_id) + ", "
        + std::to_string(to_user_id) + ", "
        + std::to_string(
            static_cast<std::uint32_t>(message_type)
          ) + ", "
        + "'" + escaped_content + "', "
        + std::to_string(
            static_cast<std::uint32_t>(delivery_status)
          ) +
        ")";

    if (!connection->Execute(sql)) {
        SetError(connection->LastError());
        return 0;
    }

    const std::uint64_t message_id = connection->LastInsertId();

    LOG_INFO("private message saved"
             << ", message_id=" << message_id
             << ", from=" << from_user_id
             << ", to=" << to_user_id
             << ", delivery_status="
             << static_cast<std::uint32_t>(delivery_status));

    last_error_.clear();
    return message_id;
}

std::vector<PrivateMessageRecord>
MessageRepository::ListPendingMessages(
    std::uint64_t to_user_id,
    std::size_t limit
) {
    std::vector<PrivateMessageRecord> empty;

    if (pool_ == nullptr) {
        SetError("message repository list pending failed: pool is null");
        return empty;
    }

    if (to_user_id == 0) {
        SetError("message repository list pending failed: invalid user id");
        return empty;
    }

    if (limit == 0) {
        return empty;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("message repository list pending failed: acquire connection failed");
        return empty;
    }

    const std::string sql =
        "SELECT "
        "message_id, "
        "IFNULL(client_message_id, ''), "
        "from_user_id, "
        "to_user_id, "
        "message_type, "
        "content, "
        "delivery_status, "
        "created_at, "
        "IFNULL(delivered_at, ''), "
        "IFNULL(read_at, '') "
        "FROM im_private_messages "
        "WHERE to_user_id = " + std::to_string(to_user_id) + " "
        "AND delivery_status = 0 "
        "ORDER BY message_id ASC "
        "LIMIT " + std::to_string(limit);

    MySqlQueryResult result;
    if (!connection->Query(sql, &result)) {
        SetError(connection->LastError());
        return empty;
    }

    last_error_.clear();
    return BuildMessagesFromResult(result);
}

std::vector<PrivateMessageRecord>
MessageRepository::ListDialogMessages(
    std::uint64_t user_id,
    std::uint64_t peer_user_id,
    std::uint64_t before_message_id,
    std::size_t limit
) {
    std::vector<PrivateMessageRecord> empty;

    if (pool_ == nullptr) {
        SetError("message repository list dialog failed: pool is null");
        return empty;
    }

    if (user_id == 0 || peer_user_id == 0) {
        SetError("message repository list dialog failed: invalid user id");
        return empty;
    }

    if (user_id == peer_user_id) {
        SetError("message repository list dialog failed: user equals peer");
        return empty;
    }

    if (limit == 0) {
        return empty;
    }

    if (limit > 100) {
        limit = 100;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError(
            "message repository list dialog failed: acquire connection failed"
        );
        return empty;
    }

    std::string before_condition;

    if (before_message_id > 0) {
        before_condition =
            "AND message_id < " + std::to_string(before_message_id) + " ";
    }

    const std::string sql =
        "SELECT "
        "message_id, "
        "IFNULL(client_message_id, ''), "
        "from_user_id, "
        "to_user_id, "
        "message_type, "
        "content, "
        "delivery_status, "
        "created_at, "
        "IFNULL(delivered_at, ''), "
        "IFNULL(read_at, '') "
        "FROM ("
            "SELECT "
            "message_id, "
            "client_message_id, "
            "from_user_id, "
            "to_user_id, "
            "message_type, "
            "content, "
            "delivery_status, "
            "created_at, "
            "delivered_at, "
            "read_at "
            "FROM im_private_messages "
            "WHERE "
            "((from_user_id = " + std::to_string(user_id) +
            " AND to_user_id = " + std::to_string(peer_user_id) + ") "
            "OR "
            "(from_user_id = " + std::to_string(peer_user_id) +
            " AND to_user_id = " + std::to_string(user_id) + ")) "
            + before_condition +
            "ORDER BY message_id DESC "
            "LIMIT " + std::to_string(limit) +
        ") AS recent_messages "
        "ORDER BY message_id ASC";

    MySqlQueryResult result;

    if (!connection->Query(sql, &result)) {
        SetError(connection->LastError());
        return empty;
    }

    last_error_.clear();

    return BuildMessagesFromResult(result);
}

std::vector<ConversationRecord>
MessageRepository::ListConversations(
    std::uint64_t user_id,
    std::size_t limit
) {
    std::vector<ConversationRecord> empty;

    if (pool_ == nullptr) {
        SetError("message repository list conversations failed: pool is null");
        return empty;
    }

    if (user_id == 0) {
        SetError("message repository list conversations failed: invalid user id");
        return empty;
    }

    if (limit == 0) {
        return empty;
    }

    if (limit > 100) {
        limit = 100;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError(
            "message repository list conversations failed: acquire connection failed"
        );
        return empty;
    }

    const std::string user_id_text = std::to_string(user_id);

    const std::string peer_expr =
        "CASE "
        "WHEN from_user_id = " + user_id_text + " "
        "THEN to_user_id "
        "ELSE from_user_id "
        "END";

    const std::string sql =
        "SELECT "
        "last_messages.peer_user_id, "
        "m.message_id, "
        "IFNULL(m.client_message_id, ''), "
        "m.from_user_id, "
        "m.to_user_id, "
        "m.message_type, "
        "m.content, "
        "m.delivery_status, "
        "m.created_at, "
        "IFNULL(m.delivered_at, ''), "
        "IFNULL(m.read_at, '') "
        "FROM im_private_messages AS m "
        "INNER JOIN ("
            "SELECT "
            + peer_expr + " AS peer_user_id, "
            "MAX(message_id) AS last_message_id "
            "FROM im_private_messages "
            "WHERE from_user_id = " + user_id_text + " "
            "OR to_user_id = " + user_id_text + " "
            "GROUP BY " + peer_expr + " "
        ") AS last_messages "
        "ON m.message_id = last_messages.last_message_id "
        "ORDER BY m.message_id DESC "
        "LIMIT " + std::to_string(limit);

    MySqlQueryResult result;

    if (!connection->Query(sql, &result)) {
        SetError(connection->LastError());
        return empty;
    }

    last_error_.clear();

    return BuildConversationsFromResult(result);
}

std::uint64_t MessageRepository::MarkReadByDialog(
    std::uint64_t reader_user_id,
    std::uint64_t peer_user_id
) {
    if (pool_ == nullptr) {
        SetError("message repository mark read failed: pool is null");
        return 0;
    }

    if (reader_user_id == 0 || peer_user_id == 0) {
        SetError("message repository mark read failed: invalid user id");
        return 0;
    }

    if (reader_user_id == peer_user_id) {
        SetError("message repository mark read failed: reader equals peer");
        return 0;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("message repository mark read failed: acquire connection failed");
        return 0;
    }

    const std::string sql =
        "UPDATE im_private_messages "
        "SET delivery_status = 2, read_at = NOW() "
        "WHERE to_user_id = " + std::to_string(reader_user_id) + " "
        "AND from_user_id = " + std::to_string(peer_user_id) + " "
        "AND delivery_status = 1";

    if (!connection->Execute(sql)) {
        SetError(connection->LastError());
        return 0;
    }

    const std::uint64_t affected_rows =
        connection->AffectedRows();

    LOG_INFO("private messages marked read"
             << ", reader=" << reader_user_id
             << ", peer=" << peer_user_id
             << ", affected_rows=" << affected_rows);

    last_error_.clear();
    return affected_rows;
}

bool MessageRepository::MarkDelivered(std::uint64_t message_id) {
    if (message_id == 0) {
        SetError("message repository mark delivered failed: invalid message id");
        return false;
    }

    return MarkDeliveredBatch({message_id});
}

bool MessageRepository::MarkDeliveredBatch(
    const std::vector<std::uint64_t>& message_ids
) {
    if (pool_ == nullptr) {
        SetError("message repository mark delivered failed: pool is null");
        return false;
    }

    if (message_ids.empty()) {
        return true;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        SetError("message repository mark delivered failed: acquire connection failed");
        return false;
    }

    const std::string sql =
        "UPDATE im_private_messages "
        "SET delivery_status = 1, delivered_at = NOW() "
        "WHERE message_id IN (" + JoinMessageIds(message_ids) + ")";

    if (!connection->Execute(sql)) {
        SetError(connection->LastError());
        return false;
    }

    LOG_INFO("private messages marked delivered"
             << ", count=" << message_ids.size());

    last_error_.clear();
    return true;
}

const std::string& MessageRepository::LastError() const {
    return last_error_;
}

std::vector<PrivateMessageRecord>
MessageRepository::BuildMessagesFromResult(
    const MySqlQueryResult& result
) {
    std::vector<PrivateMessageRecord> messages;
    messages.reserve(result.rows.size());

    try {
        for (const auto& row : result.rows) {
            if (row.size() < 10) {
                SetError("message repository build failed: invalid row size");
                return {};
            }

            PrivateMessageRecord message;
            message.message_id = ToUInt64(row[0]);
            message.client_message_id = row[1];
            message.from_user_id = ToUInt64(row[2]);
            message.to_user_id = ToUInt64(row[3]);
            message.message_type = ToUInt32(row[4]);
            message.content = row[5];
            message.delivery_status = ToUInt32(row[6]);
            message.created_at = row[7];
            message.delivered_at = row[8];
            message.read_at = row[9];

            messages.push_back(std::move(message));
        }
    } catch (const std::exception& e) {
        SetError(
            std::string("message repository build failed: ") +
            e.what()
        );
        return {};
    }

    last_error_.clear();
    return messages;
}

std::vector<ConversationRecord>
    MessageRepository::BuildConversationsFromResult(
    const MySqlQueryResult& result
) {
    std::vector<ConversationRecord> conversations;
    conversations.reserve(result.rows.size());

    try {
        for (const auto& row : result.rows) {
            if (row.size() < 11) {
                SetError("message repository build conversations failed: invalid row size");
                return {};
            }

            ConversationRecord conversation;
            conversation.peer_user_id = ToUInt64(row[0]);

            conversation.last_message_id = ToUInt64(row[1]);
            conversation.last_client_message_id = row[2];

            conversation.last_from_user_id = ToUInt64(row[3]);
            conversation.last_to_user_id = ToUInt64(row[4]);

            conversation.last_message_type = ToUInt32(row[5]);
            conversation.last_content = row[6];

            conversation.last_delivery_status = ToUInt32(row[7]);

            conversation.last_created_at = row[8];
            conversation.last_delivered_at = row[9];
            conversation.last_read_at = row[10];

            conversations.push_back(std::move(conversation));
        }
    } catch (const std::exception& e) {
        SetError(
            std::string("message repository build conversations failed: ") +
            e.what()
        );
        return {};
    }

    last_error_.clear();
    return conversations;
}

void MessageRepository::SetError(
    const std::string& error_message
) {
    last_error_ = error_message;
    LOG_ERROR(error_message);
}

}  // namespace tinyimx