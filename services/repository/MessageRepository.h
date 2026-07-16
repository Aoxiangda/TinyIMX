#pragma once

#include "common/db/MySqlConnectionPool.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx {

enum class PrivateMessageType : std::uint32_t {
    kText = 1,
    kImage = 2,
    kFile = 3
};

enum class DeliveryStatus : std::uint32_t {
    kPending = 0,
    kDelivered = 1,
    kRead = 2,
    kFailed = 3
};

struct PrivateMessageRecord {
    std::uint64_t message_id{0};
    std::string client_message_id;

    std::uint64_t from_user_id{0};
    std::uint64_t to_user_id{0};

    std::uint32_t message_type{1};
    std::string content;

    std::uint32_t delivery_status{0};

    std::string created_at;
    std::string delivered_at;
    std::string read_at;
};


struct ConversationRecord {
    std::uint64_t peer_user_id{0};

    std::uint64_t last_message_id{0};
    std::string last_client_message_id;

    std::uint64_t last_from_user_id{0};
    std::uint64_t last_to_user_id{0};

    std::uint32_t last_message_type{1};
    std::string last_content;

    std::uint32_t last_delivery_status{0};

    std::string last_created_at;
    std::string last_delivered_at;
    std::string last_read_at;
};

class MessageRepository {
public:
    explicit MessageRepository(MySqlConnectionPool* pool);

    MessageRepository(const MessageRepository&) = delete;
    MessageRepository& operator=(const MessageRepository&) = delete;

    std::uint64_t SavePrivateMessage(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& content,
        DeliveryStatus delivery_status,
        PrivateMessageType message_type = PrivateMessageType::kText,
        const std::string& client_message_id = ""
    );

    std::vector<PrivateMessageRecord> ListPendingMessages(
        std::uint64_t to_user_id,
        std::size_t limit
    );

    std::vector<PrivateMessageRecord> ListDialogMessages(
        std::uint64_t user_id,
        std::uint64_t peer_user_id,
        std::uint64_t before_message_id,
        std::size_t limit
    );

    std::vector<ConversationRecord> ListConversations(
        std::uint64_t user_id,
        std::size_t limit
    );

    bool MarkDelivered(std::uint64_t message_id);

    bool MarkDeliveredBatch(
        const std::vector<std::uint64_t>& message_ids
    );

    const std::string& LastError() const;

    std::size_t CountPendingMessages(std::uint64_t to_user_id);

    std::uint64_t MarkReadByDialog(
        std::uint64_t reader_user_id,
        std::uint64_t peer_user_id
    );

private:
    std::vector<PrivateMessageRecord> BuildMessagesFromResult(
        const MySqlQueryResult& result
    );

    std::vector<ConversationRecord> BuildConversationsFromResult(
        const MySqlQueryResult& result
    );

    void SetError(const std::string& error_message);

private:
    MySqlConnectionPool* pool_{nullptr};
    std::string last_error_;
};

}  // namespace tinyimx