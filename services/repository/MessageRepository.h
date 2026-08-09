#pragma once

#include "common/db/MySqlConnectionPool.h"

#include <cstddef>
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

enum class MessageQueryStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kInvalidRecord,
    kStorageError
};

const char*
MessageQueryStatusToString(
    MessageQueryStatus status
);

struct ListPrivateMessagesResult {
    MessageQueryStatus status{
        MessageQueryStatus::kStorageError
    };

    std::vector<PrivateMessageRecord> records;
    std::string message;

    bool Succeeded() const noexcept {
        return status ==
               MessageQueryStatus::kSucceeded;
    }
};

struct ListConversationsResult {
    MessageQueryStatus status{
        MessageQueryStatus::kStorageError
    };

    std::vector<ConversationRecord> records;
    std::string message;

    bool Succeeded() const noexcept {
        return status ==
               MessageQueryStatus::kSucceeded;
    }
};

enum class MessageMutationStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kStorageError
};

const char* MessageMutationStatusToString(
    MessageMutationStatus status
);

struct SavePrivateMessageResult {
    MessageMutationStatus status{
        MessageMutationStatus::kStorageError
    };

    std::uint64_t message_id{0};
    std::string message;

    bool Succeeded() const noexcept {
        return status ==
               MessageMutationStatus::kSucceeded;
    }
};

struct UpdatePrivateMessagesResult {
    MessageMutationStatus status{
        MessageMutationStatus::kStorageError
    };

    std::uint64_t affected_rows{0};
    std::string message;

    bool Succeeded() const noexcept {
        return status ==
               MessageMutationStatus::kSucceeded;
    }
};

class MessageRepository {
public:
    explicit MessageRepository(
        MySqlConnectionPool* pool
    );

    MessageRepository(
        const MessageRepository&
    ) = delete;

    MessageRepository& operator=(
        const MessageRepository&
    ) = delete;

    SavePrivateMessageResult
    SavePrivateMessage(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& content,
        DeliveryStatus delivery_status,
        PrivateMessageType message_type =
            PrivateMessageType::kText,
        const std::string& client_message_id =
            ""
    );

    ListPrivateMessagesResult
    ListPendingMessages(
        std::uint64_t to_user_id,
        std::size_t limit
    );

    ListPrivateMessagesResult
    ListDialogMessages(
        std::uint64_t user_id,
        std::uint64_t peer_user_id,
        std::uint64_t before_message_id,
        std::size_t limit
    );

    ListConversationsResult
    ListConversations(
        std::uint64_t user_id,
        std::size_t limit
    );

    UpdatePrivateMessagesResult MarkDelivered(
        std::uint64_t message_id
    );

    UpdatePrivateMessagesResult MarkDeliveredBatch(
        const std::vector<std::uint64_t>&
            message_ids
    );

    UpdatePrivateMessagesResult MarkReadByDialog(
        std::uint64_t reader_user_id,
        std::uint64_t peer_user_id
    );

private:
    static ListPrivateMessagesResult BuildMessagesFromResult(
        const MySqlQueryResult& result
    );

    static ListConversationsResult BuildConversationsFromResult(
        const MySqlQueryResult& result
    );

private:
    MySqlConnectionPool* pool_{nullptr};
};

}  // namespace tinyimx