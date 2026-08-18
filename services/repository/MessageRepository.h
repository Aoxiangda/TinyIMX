#pragma once

#include "common/db/MySqlConnectionPool.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

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


struct FindPrivateMessageResult {
    MessageQueryStatus status{
        MessageQueryStatus::kStorageError
    };

    /*
     * status == kSucceeded以后，
     * found才有业务意义。
     *
     * found == false：
     * SQL查询成功，但是没有对应message_id。
     */
    bool found{false};

    PrivateMessageRecord record;

    std::string message;


    bool Succeeded() const noexcept {
        return status ==
               MessageQueryStatus::kSucceeded;
    }


    bool Found() const noexcept {
        return Succeeded() && found;
    }
};

enum class IdempotentSavePrivateMessageStatus {
    kCreated = 0,
    kReused,
    kIdempotencyConflict,
    kInvalidArgument,
    kInvalidRecord,
    kStorageError
};


const char*
IdempotentSavePrivateMessageStatusToString(
    IdempotentSavePrivateMessageStatus status
);


struct IdempotentSavePrivateMessageResult {
    IdempotentSavePrivateMessageStatus status{
        IdempotentSavePrivateMessageStatus::
            kStorageError
    };

    std::uint64_t message_id{0};

    PrivateMessageRecord record;

    std::string message;


    bool Succeeded() const noexcept {
        return
            status ==
                IdempotentSavePrivateMessageStatus::
                    kCreated ||
            status ==
                IdempotentSavePrivateMessageStatus::
                    kReused;
    }


    bool Created() const noexcept {
        return
            status ==
            IdempotentSavePrivateMessageStatus::
                kCreated;
    }


    bool Reused() const noexcept {
        return
            status ==
            IdempotentSavePrivateMessageStatus::
                kReused;
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

    IdempotentSavePrivateMessageResult
    SavePrivateMessageIdempotent(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& content,
        const std::string& client_message_id,
        PrivateMessageType message_type =
            PrivateMessageType::kText
    );

    FindPrivateMessageResult
        FindPrivateMessageById(
            std::uint64_t message_id
    );

    using IdempotentPreInsertHookForTest =
        std::function<void()>;


    /*
    * 仅用于确定性并发测试。
    *
    * Hook执行位置：
    *
    * FindByClientMessageId
    *       ↓
    * Not Found
    *       ↓
    * [这里调用Hook]
    *       ↓
    * INSERT
    *
    * 正常生产环境不设置该Hook，
    * 因此没有任何行为变化。
    *
    * 注意：
    * 必须在并发调用开始前设置，
    * 不能一边执行Save一边修改Hook。
    */
    void SetIdempotentPreInsertHookForTest(
        IdempotentPreInsertHookForTest hook
    );


        /*
     * 按客户端发送幂等键查找私聊消息。
     *
     * 业务唯一键：
     *
     * (
     *     from_user_id,
     *     client_message_id
     * )
     *
     * 用于Client发送ACK丢失后的重试：
     *
     * Client Retry
     *      ↓
     * same client_message_id
     *      ↓
     * 找回第一次生成的server message_id
     */
    FindPrivateMessageResult FindPrivateMessageByClientMessageId(
        std::uint64_t from_user_id,
        const std::string& client_message_id
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

    IdempotentPreInsertHookForTest idempotent_pre_insert_hook_for_test_;


private:
    MySqlConnectionPool* pool_{nullptr};
};

}  // namespace tinyimx