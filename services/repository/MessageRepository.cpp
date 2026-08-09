#include "services/repository/MessageRepository.h"

#include "common/logging/LogMacros.h"

#include <sstream>
#include <stdexcept>
#include <utility>

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

const char* MessageQueryStatusToString(
    MessageQueryStatus status
) {
    switch (status) {
        case MessageQueryStatus::
            kSucceeded:
            return "succeeded";

        case MessageQueryStatus::
            kInvalidArgument:
            return "invalid_argument";

        case MessageQueryStatus::
            kInvalidRecord:
            return "invalid_record";

        case MessageQueryStatus::
            kStorageError:
            return "storage_error";

        default:
            return "unknown";
    }
}

const char* MessageMutationStatusToString(
    MessageMutationStatus status
) {
    switch (status) {
        case MessageMutationStatus::
            kSucceeded:
            return "succeeded";

        case MessageMutationStatus::
            kInvalidArgument:
            return "invalid_argument";

        case MessageMutationStatus::
            kStorageError:
            return "storage_error";

        default:
            return "unknown";
    }
}

SavePrivateMessageResult MessageRepository::SavePrivateMessage(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& content,
    DeliveryStatus delivery_status,
    PrivateMessageType message_type,
    const std::string& client_message_id
) {
    SavePrivateMessageResult result;

    if (from_user_id == 0 ||
        to_user_id == 0) {
        result.status =
            MessageMutationStatus::
                kInvalidArgument;

        result.message =
            "message repository save failed: "
            "invalid user id";

        return result;
    }

    if (from_user_id == to_user_id) {
        result.status =
            MessageMutationStatus::
                kInvalidArgument;

        result.message =
            "message repository save failed: "
            "sender equals receiver";

        return result;
    }

    const std::uint32_t
        message_type_value =
            static_cast<std::uint32_t>(
                message_type
            );

    const std::uint32_t
        min_message_type =
            static_cast<std::uint32_t>(
                PrivateMessageType::kText
            );

    const std::uint32_t
        max_message_type =
            static_cast<std::uint32_t>(
                PrivateMessageType::kFile
            );

    if (message_type_value <
            min_message_type ||
        message_type_value >
            max_message_type) {
        result.status =
            MessageMutationStatus::
                kInvalidArgument;

        result.message =
            "message repository save failed: "
            "invalid message type";

        return result;
    }

    const std::uint32_t
        delivery_status_value =
            static_cast<std::uint32_t>(
                delivery_status
            );

    const std::uint32_t
        max_delivery_status =
            static_cast<std::uint32_t>(
                DeliveryStatus::kFailed
            );

    if (delivery_status_value >
        max_delivery_status) {
        result.status =
            MessageMutationStatus::
                kInvalidArgument;

        result.message =
            "message repository save failed: "
            "invalid delivery status";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            MessageMutationStatus::
                kStorageError;

        result.message =
            "message repository save failed: "
            "pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            MessageMutationStatus::
                kStorageError;

        result.message =
            "message repository save failed: "
            "acquire connection failed";

        return result;
    }

    const std::string escaped_content =
        connection->EscapeString(
            content
        );

    std::string client_message_sql =
        "NULL";

    if (!client_message_id.empty()) {
        client_message_sql =
            "'" +
            connection->EscapeString(
                client_message_id
            ) +
            "'";
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
        + std::to_string(
            from_user_id
        ) + ", "
        + std::to_string(
            to_user_id
        ) + ", "
        + std::to_string(
            message_type_value
        ) + ", "
        + "'" +
        escaped_content +
        "', "
        + std::to_string(
            delivery_status_value
        ) +
        ")";

    if (!connection->Execute(sql)) {
        result.status =
            MessageMutationStatus::
                kStorageError;

        result.message =
            connection->LastError();

        if (result.message.empty()) {
            result.message =
                "message repository save "
                "failed: execute failed";
        }

        return result;
    }

    const std::uint64_t message_id =
        connection->LastInsertId();

    if (message_id == 0) {
        result.status =
            MessageMutationStatus::
                kStorageError;

        result.message =
            "message repository save failed: "
            "invalid last insert id";

        return result;
    }

    result.status =
        MessageMutationStatus::kSucceeded;

    result.message_id = message_id;

    result.message =
        "private message saved";

    LOG_INFO(
        "private message saved"
        << ", message_id="
        << result.message_id
        << ", from="
        << from_user_id
        << ", to="
        << to_user_id
        << ", delivery_status="
        << delivery_status_value
    );

    return result;
}

ListPrivateMessagesResult MessageRepository::ListPendingMessages(
    std::uint64_t to_user_id,
    std::size_t limit
) {
    ListPrivateMessagesResult result;

    if (to_user_id == 0) {
        result.status =
            MessageQueryStatus::
                kInvalidArgument;

        result.message =
            "message repository list pending "
            "failed: invalid user id";

        return result;
    }

    if (limit == 0) {
        result.status =
            MessageQueryStatus::kSucceeded;

        return result;
    }

    if (limit > 100) {
        limit = 100;
    }

    if (pool_ == nullptr) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            "message repository list pending "
            "failed: pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            "message repository list pending "
            "failed: acquire connection failed";

        return result;
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
        "WHERE to_user_id = " +
        std::to_string(to_user_id) +
        " "
        "AND delivery_status = 0 "
        "ORDER BY message_id ASC "
        "LIMIT " +
        std::to_string(limit);

    MySqlQueryResult query_result;

    if (!connection->Query(
            sql,
            &query_result
        )) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            connection->LastError();

        if (result.message.empty()) {
            result.message =
                "message repository list pending "
                "failed: query failed";
        }

        return result;
    }

    return BuildMessagesFromResult(
        query_result
    );
}

ListPrivateMessagesResult MessageRepository::ListDialogMessages(
    std::uint64_t user_id,
    std::uint64_t peer_user_id,
    std::uint64_t before_message_id,
    std::size_t limit
) {
    ListPrivateMessagesResult result;

    if (user_id == 0 ||
        peer_user_id == 0) {
        result.status =
            MessageQueryStatus::
                kInvalidArgument;

        result.message =
            "message repository list dialog "
            "failed: invalid user id";

        return result;
    }

    if (user_id == peer_user_id) {
        result.status =
            MessageQueryStatus::
                kInvalidArgument;

        result.message =
            "message repository list dialog "
            "failed: user equals peer";

        return result;
    }

    if (limit == 0) {
        result.status =
            MessageQueryStatus::kSucceeded;

        return result;
    }

    if (limit > 100) {
        limit = 100;
    }

    if (pool_ == nullptr) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            "message repository list dialog "
            "failed: pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            "message repository list dialog "
            "failed: acquire connection failed";

        return result;
    }

    std::string before_condition;

    if (before_message_id > 0) {
        before_condition =
            "AND message_id < " +
            std::to_string(
                before_message_id
            ) +
            " ";
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
            "((from_user_id = " +
            std::to_string(user_id) +
            " AND to_user_id = " +
            std::to_string(peer_user_id) +
            ") "
            "OR "
            "(from_user_id = " +
            std::to_string(peer_user_id) +
            " AND to_user_id = " +
            std::to_string(user_id) +
            ")) " +
            before_condition +
            "ORDER BY message_id DESC "
            "LIMIT " +
            std::to_string(limit) +
        ") AS recent_messages "
        "ORDER BY message_id ASC";

    MySqlQueryResult query_result;

    if (!connection->Query(
            sql,
            &query_result
        )) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            connection->LastError();

        if (result.message.empty()) {
            result.message =
                "message repository list dialog "
                "failed: query failed";
        }

        return result;
    }

    return BuildMessagesFromResult(
        query_result
    );
}

ListConversationsResult MessageRepository::ListConversations(
    std::uint64_t user_id,
    std::size_t limit
) {
    ListConversationsResult result;

    if (user_id == 0) {
        result.status =
            MessageQueryStatus::
                kInvalidArgument;

        result.message =
            "message repository list "
            "conversations failed: "
            "invalid user id";

        return result;
    }

    if (limit == 0) {
        result.status =
            MessageQueryStatus::kSucceeded;

        return result;
    }

    if (limit > 100) {
        limit = 100;
    }

    if (pool_ == nullptr) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            "message repository list "
            "conversations failed: pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            "message repository list "
            "conversations failed: acquire "
            "connection failed";

        return result;
    }

    const std::string user_id_text =
        std::to_string(user_id);

    const std::string peer_expr =
        "CASE "
        "WHEN from_user_id = " +
        user_id_text +
        " "
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
            "SELECT " +
            peer_expr +
            " AS peer_user_id, "
            "MAX(message_id) AS "
            "last_message_id "
            "FROM im_private_messages "
            "WHERE from_user_id = " +
            user_id_text +
            " "
            "OR to_user_id = " +
            user_id_text +
            " "
            "GROUP BY " +
            peer_expr +
            " "
        ") AS last_messages "
        "ON m.message_id = "
        "last_messages.last_message_id "
        "ORDER BY m.message_id DESC "
        "LIMIT " +
        std::to_string(limit);

    MySqlQueryResult query_result;

    if (!connection->Query(
            sql,
            &query_result
        )) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            connection->LastError();

        if (result.message.empty()) {
            result.message =
                "message repository list "
                "conversations failed: "
                "query failed";
        }

        return result;
    }

    return BuildConversationsFromResult(
        query_result
    );
}

UpdatePrivateMessagesResult
MessageRepository::MarkReadByDialog(
    std::uint64_t reader_user_id,
    std::uint64_t peer_user_id
) {
    UpdatePrivateMessagesResult result;

    if (reader_user_id == 0 ||
        peer_user_id == 0) {
        result.status =
            MessageMutationStatus::
                kInvalidArgument;

        result.message =
            "message repository mark read "
            "failed: invalid user id";

        return result;
    }

    if (reader_user_id ==
        peer_user_id) {
        result.status =
            MessageMutationStatus::
                kInvalidArgument;

        result.message =
            "message repository mark read "
            "failed: reader equals peer";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            MessageMutationStatus::
                kStorageError;

        result.message =
            "message repository mark read "
            "failed: pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            MessageMutationStatus::
                kStorageError;

        result.message =
            "message repository mark read "
            "failed: acquire connection "
            "failed";

        return result;
    }

    const std::string sql =
        "UPDATE im_private_messages "
        "SET delivery_status = 2, "
        "read_at = NOW() "
        "WHERE to_user_id = " +
        std::to_string(
            reader_user_id
        ) +
        " "
        "AND from_user_id = " +
        std::to_string(
            peer_user_id
        ) +
        " "
        "AND delivery_status = 1";

    if (!connection->Execute(sql)) {
        result.status =
            MessageMutationStatus::
                kStorageError;

        result.message =
            connection->LastError();

        if (result.message.empty()) {
            result.message =
                "message repository mark read "
                "failed: execute failed";
        }

        return result;
    }

    result.status =
        MessageMutationStatus::kSucceeded;

    result.affected_rows =
        connection->AffectedRows();

    result.message =
        "private messages marked read";

    LOG_INFO(
        "private messages marked read"
        << ", reader="
        << reader_user_id
        << ", peer="
        << peer_user_id
        << ", affected_rows="
        << result.affected_rows
    );

    return result;
}

UpdatePrivateMessagesResult MessageRepository::MarkDelivered(
    std::uint64_t message_id
) {
    if (message_id == 0) {
        UpdatePrivateMessagesResult result;

        result.status =
            MessageMutationStatus::
                kInvalidArgument;

        result.message =
            "message repository mark "
            "delivered failed: invalid "
            "message id";

        return result;
    }

    return MarkDeliveredBatch(
        std::vector<std::uint64_t>{
            message_id
        }
    );
}

UpdatePrivateMessagesResult MessageRepository::MarkDeliveredBatch(
    const std::vector<std::uint64_t>&
        message_ids
) {
    UpdatePrivateMessagesResult result;

    if (message_ids.empty()) {
        result.status =
            MessageMutationStatus::
                kSucceeded;

        result.message =
            "no messages to mark delivered";

        return result;
    }

    for (const std::uint64_t message_id :
         message_ids) {
        if (message_id == 0) {
            result.status =
                MessageMutationStatus::
                    kInvalidArgument;

            result.message =
                "message repository mark "
                "delivered failed: invalid "
                "message id";

            return result;
        }
    }

    if (pool_ == nullptr) {
        result.status =
            MessageMutationStatus::
                kStorageError;

        result.message =
            "message repository mark "
            "delivered failed: pool is null";

        return result;
    }

    auto connection =
        pool_->Acquire();

    if (!connection) {
        result.status =
            MessageMutationStatus::
                kStorageError;

        result.message =
            "message repository mark "
            "delivered failed: acquire "
            "connection failed";

        return result;
    }

    const std::string sql =
        "UPDATE im_private_messages "
        "SET delivery_status = 1, "
        "delivered_at = NOW() "
        "WHERE message_id IN (" +
        JoinMessageIds(
            message_ids
        ) +
        ")";

    if (!connection->Execute(sql)) {
        result.status =
            MessageMutationStatus::
                kStorageError;

        result.message =
            connection->LastError();

        if (result.message.empty()) {
            result.message =
                "message repository mark "
                "delivered failed: "
                "execute failed";
        }

        return result;
    }

    result.status =
        MessageMutationStatus::
            kSucceeded;

    result.affected_rows =
        connection->AffectedRows();

    result.message =
        "private messages marked delivered";

    LOG_INFO(
        "private messages marked delivered"
        << ", requested_count="
        << message_ids.size()
        << ", affected_rows="
        << result.affected_rows
    );

    return result;
}


ListConversationsResult MessageRepository::
    BuildConversationsFromResult(
        const MySqlQueryResult& result
    ) {
    ListConversationsResult build_result;

    build_result.records.reserve(
        result.rows.size()
    );

    try {
        for (const auto& row :
             result.rows) {
            if (row.size() < 11) {
                build_result.status =
                    MessageQueryStatus::
                        kInvalidRecord;

                build_result.message =
                    "message repository build "
                    "conversations failed: "
                    "invalid row size";

                build_result.records.clear();

                return build_result;
            }

            ConversationRecord conversation;

            conversation.peer_user_id =
                ToUInt64(row[0]);

            conversation.last_message_id =
                ToUInt64(row[1]);

            conversation.
                last_client_message_id =
                    row[2];

            conversation.last_from_user_id =
                ToUInt64(row[3]);

            conversation.last_to_user_id =
                ToUInt64(row[4]);

            conversation.last_message_type =
                ToUInt32(row[5]);

            conversation.last_content =
                row[6];

            conversation.
                last_delivery_status =
                    ToUInt32(row[7]);

            conversation.last_created_at =
                row[8];

            conversation.last_delivered_at =
                row[9];

            conversation.last_read_at =
                row[10];

            const std::uint32_t min_type =
                static_cast<std::uint32_t>(
                    PrivateMessageType::kText
                );

            const std::uint32_t max_type =
                static_cast<std::uint32_t>(
                    PrivateMessageType::kFile
                );

            const std::uint32_t max_status =
                static_cast<std::uint32_t>(
                    DeliveryStatus::kFailed
                );

            if (conversation.peer_user_id == 0 ||
                conversation.
                    last_message_id == 0 ||
                conversation.
                    last_from_user_id == 0 ||
                conversation.
                    last_to_user_id == 0 ||
                conversation.
                    last_message_type <
                    min_type ||
                conversation.
                    last_message_type >
                    max_type ||
                conversation.
                    last_delivery_status >
                    max_status ||
                conversation.
                    last_created_at.empty()) {
                build_result.status =
                    MessageQueryStatus::
                        kInvalidRecord;

                build_result.message =
                    "message repository build "
                    "conversations failed: "
                    "invalid record data";

                build_result.records.clear();

                return build_result;
            }

            build_result.records.push_back(
                std::move(conversation)
            );
        }
    } catch (const std::exception& e) {
        build_result.status =
            MessageQueryStatus::
                kInvalidRecord;

        build_result.message =
            std::string(
                "message repository build "
                "conversations failed: "
            ) +
            e.what();

        build_result.records.clear();

        return build_result;
    }

    build_result.status =
        MessageQueryStatus::kSucceeded;

    return build_result;
}

ListPrivateMessagesResult
MessageRepository::BuildMessagesFromResult(
    const MySqlQueryResult& result
) {
    ListPrivateMessagesResult build_result;

    build_result.records.reserve(
        result.rows.size()
    );

    try {
        for (const auto& row :
             result.rows) {
            if (row.size() < 10) {
                build_result.status =
                    MessageQueryStatus::
                        kInvalidRecord;

                build_result.message =
                    "message repository build "
                    "failed: invalid row size";

                build_result.records.clear();

                return build_result;
            }

            PrivateMessageRecord message;

            message.message_id =
                ToUInt64(row[0]);

            message.client_message_id =
                row[1];

            message.from_user_id =
                ToUInt64(row[2]);

            message.to_user_id =
                ToUInt64(row[3]);

            message.message_type =
                ToUInt32(row[4]);

            message.content = row[5];

            message.delivery_status =
                ToUInt32(row[6]);

            message.created_at = row[7];
            message.delivered_at = row[8];
            message.read_at = row[9];

            const std::uint32_t min_type =
                static_cast<std::uint32_t>(
                    PrivateMessageType::kText
                );

            const std::uint32_t max_type =
                static_cast<std::uint32_t>(
                    PrivateMessageType::kFile
                );

            const std::uint32_t max_status =
                static_cast<std::uint32_t>(
                    DeliveryStatus::kFailed
                );

            if (message.message_id == 0 ||
                message.from_user_id == 0 ||
                message.to_user_id == 0 ||
                message.message_type <
                    min_type ||
                message.message_type >
                    max_type ||
                message.delivery_status >
                    max_status ||
                message.created_at.empty()) {
                build_result.status =
                    MessageQueryStatus::
                        kInvalidRecord;

                build_result.message =
                    "message repository build "
                    "failed: invalid record data";

                build_result.records.clear();

                return build_result;
            }

            build_result.records.push_back(
                std::move(message)
            );
        }
    } catch (const std::exception& e) {
        build_result.status =
            MessageQueryStatus::
                kInvalidRecord;

        build_result.message =
            std::string(
                "message repository build "
                "failed: "
            ) +
            e.what();

        build_result.records.clear();

        return build_result;
    }

    build_result.status =
        MessageQueryStatus::kSucceeded;

    return build_result;
}

}  // namespace tinyimx