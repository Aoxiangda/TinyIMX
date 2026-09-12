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


void MessageRepository::SetIdempotentPreInsertHookForTest(
    IdempotentPreInsertHookForTest hook
) {
    idempotent_pre_insert_hook_for_test_ =
        std::move(hook);
}

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


const char*
IdempotentSavePrivateMessageStatusToString(
    IdempotentSavePrivateMessageStatus status
) {
    switch (status) {
        case IdempotentSavePrivateMessageStatus::
            kCreated:
            return "created";

        case IdempotentSavePrivateMessageStatus::
            kReused:
            return "reused";

        case IdempotentSavePrivateMessageStatus::
            kIdempotencyConflict:
            return "idempotency_conflict";

        case IdempotentSavePrivateMessageStatus::
            kInvalidArgument:
            return "invalid_argument";

        case IdempotentSavePrivateMessageStatus::
            kInvalidRecord:
            return "invalid_record";

        case IdempotentSavePrivateMessageStatus::
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

FindPrivateMessageResult
MessageRepository::FindPrivateMessageById(
    std::uint64_t message_id
) {
    FindPrivateMessageResult result;


    /*
     * message_id == 0在TinyIMX中
     * 代表无效业务消息身份。
     */
    if (message_id == 0) {
        result.status =
            MessageQueryStatus::
                kInvalidArgument;

        result.message =
            "message repository mark receiver "
            "confirmed failed: invalid message id";

        return result;
    }


    /*
     * Repository依赖MySQL连接池。
     *
     * pool为空不是NotFound，
     * 而是当前根本无法访问存储。
     */
    if (pool_ == nullptr) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            "message repository find failed: "
            "pool is null";

        return result;
    }


    auto connection =
        pool_->Acquire();


    if (!connection) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            "message repository find failed: "
            "acquire connection failed";

        return result;
    }


    /*
     * 列顺序必须与
     * BuildMessagesFromResult()
     * 的row[0]~row[9]完全一致。
     */
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
        "WHERE message_id = " +
        std::to_string(
            message_id
        ) +
        " "
        "LIMIT 1";


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
                "message repository find failed: "
                "query failed";
        }


        return result;
    }


    /*
     * SQL成功 + 0 row
     *
     * 注意：
     * 这不是StorageError。
     *
     * 数据库工作完全正常，
     * 只是这个message_id不存在。
     */
    if (query_result.rows.empty()) {
        result.status =
            MessageQueryStatus::
                kSucceeded;

        result.found =
            false;

        result.message =
            "private message not found";

        return result;
    }


    /*
     * message_id理论上应是唯一标识，
     * 并且SQL还有LIMIT 1。
     *
     * 这里仍然保留结构约束，
     * 防止未来修改SQL后错误地把
     * 多条记录当作单条记录处理。
     */
    if (query_result.rows.size() != 1) {
        result.status =
            MessageQueryStatus::
                kInvalidRecord;

        result.message =
            "message repository find failed: "
            "unexpected row count";

        return result;
    }


    /*
     * 不重复手写row[0]~row[9]解析。
     *
     * 复用已经验证过的统一Builder，
     * 从而保证：
     *
     * ListPending
     * ListDialog
     * FindById
     *
     * 对PrivateMessageRecord的解释一致。
     */
    const ListPrivateMessagesResult
        build_result =
            BuildMessagesFromResult(
                query_result
            );


    if (!build_result.Succeeded()) {
        result.status =
            build_result.status;

        result.message =
            build_result.message;

        return result;
    }


    /*
     * 正常情况下上面的query_result
     * 已经确认只有1行，
     * Builder也应该生成1条Record。
     */
    if (build_result.records.size() != 1) {
        result.status =
            MessageQueryStatus::
                kInvalidRecord;

        result.message =
            "message repository find failed: "
            "unexpected record count";

        return result;
    }


    result.status =
        MessageQueryStatus::
            kSucceeded;

    result.found =
        true;

    result.record =
        build_result.records.front();

    result.message =
        "private message found";


    LOG_INFO(
        "private message found by id"
        << ", message_id="
        << result.record.message_id
        << ", from="
        << result.record.from_user_id
        << ", to="
        << result.record.to_user_id
        << ", delivery_status="
        << result.record.delivery_status
    );


    return result;
}


FindPrivateMessageResult
MessageRepository::
FindPrivateMessageByClientMessageId(
    std::uint64_t from_user_id,
    const std::string& client_message_id
) {
    FindPrivateMessageResult result;


    /*
     * client_message_id是新可靠发送协议
     * 的业务幂等键。
     *
     * 空字符串代表“没有幂等键”，
     * 不能用于查询新协议消息。
     */
    if (
        from_user_id == 0 ||
        client_message_id.empty() ||
        client_message_id.size() > 64
    ) {
        result.status =
            MessageQueryStatus::
                kInvalidArgument;

        result.message =
            "message repository find by "
            "client message id failed: "
            "invalid argument";

        return result;
    }


    if (pool_ == nullptr) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            "message repository find by "
            "client message id failed: "
            "pool is null";

        return result;
    }


    auto connection =
        pool_->Acquire();


    if (!connection) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            "message repository find by "
            "client message id failed: "
            "acquire connection failed";

        return result;
    }


    const std::string
        escaped_client_message_id =
            connection->EscapeString(
                client_message_id
            );


    /*
     * 字段顺序故意与
     * BuildMessagesFromResult()
     * 使用的10列完全一致。
     */
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
        "WHERE from_user_id = " +
        std::to_string(
            from_user_id
        ) +
        " "
        "AND client_message_id = '" +
        escaped_client_message_id +
        "' "
        "LIMIT 2";


    MySqlQueryResult query_result;


    if (
        !connection->Query(
            sql,
            &query_result
        )
    ) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            connection->LastError();


        if (result.message.empty()) {
            result.message =
                "message repository find by "
                "client message id failed: "
                "query failed";
        }


        return result;
    }


    /*
     * 没找到不是数据库错误。
     *
     * 对Client第一次发送来说：
     *
     * not found
     * =
     * 可以进入INSERT路径。
     */
    if (query_result.rows.empty()) {
        result.status =
            MessageQueryStatus::
                kSucceeded;

        result.found = false;

        result.message =
            "private message not found";

        return result;
    }


    /*
     * Live DB已经存在：
     *
     * UNIQUE(
     *     from_user_id,
     *     client_message_id
     * )
     *
     * 理论上不可能出现两行。
     *
     * LIMIT 2是防御式设计：
     * 若以后schema被误改，
     * Repository不能默默选第一条。
     */
    if (query_result.rows.size() != 1) {
        result.status =
            MessageQueryStatus::
                kInvalidRecord;

        result.found = false;

        result.message =
            "message repository find by "
            "client message id failed: "
            "multiple records";

        return result;
    }


    /*
     * 复用现有统一Record解析逻辑，
     * 避免FindById / List / ClientId Query
     * 各维护一套字段转换。
     */
    const ListPrivateMessagesResult
        build_result =
            BuildMessagesFromResult(
                query_result
            );


    if (!build_result.Succeeded()) {
        result.status =
            build_result.status;

        result.found = false;

        result.message =
            build_result.message;

        return result;
    }


    if (build_result.records.size() != 1) {
        result.status =
            MessageQueryStatus::
                kInvalidRecord;

        result.found = false;

        result.message =
            "message repository find by "
            "client message id failed: "
            "invalid record count";

        return result;
    }


    result.status =
        MessageQueryStatus::
            kSucceeded;

    result.found = true;

    result.record =
        build_result.records.front();

    result.message =
        "private message found";


    LOG_INFO(
        "private message found by "
        "client message id"
        << ", from_user_id="
        << from_user_id
        << ", client_message_id="
        << client_message_id
        << ", message_id="
        << result.record.message_id
        << ", to="
        << result.record.to_user_id
        << ", delivery_status="
        << result.record.delivery_status
    );


    return result;
}



IdempotentSavePrivateMessageResult
MessageRepository::SavePrivateMessageIdempotent(
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& content,
    const std::string& client_message_id,
    PrivateMessageType message_type
) {
    IdempotentSavePrivateMessageResult result;


    /*
     * =====================================================
     * 1. 参数合法性
     * =====================================================
     */
    if (from_user_id == 0 ||
        to_user_id == 0) {
        result.status =
            IdempotentSavePrivateMessageStatus::
                kInvalidArgument;

        result.message =
            "idempotent private message save failed: "
            "invalid user id";

        return result;
    }


    if (from_user_id == to_user_id) {
        result.status =
            IdempotentSavePrivateMessageStatus::
                kInvalidArgument;

        result.message =
            "idempotent private message save failed: "
            "sender equals receiver";

        return result;
    }


    if (client_message_id.empty()) {
        result.status =
            IdempotentSavePrivateMessageStatus::
                kInvalidArgument;

        result.message =
            "idempotent private message save failed: "
            "client_message_id is empty";

        return result;
    }


    if (client_message_id.size() > 64) {
        result.status =
            IdempotentSavePrivateMessageStatus::
                kInvalidArgument;

        result.message =
            "idempotent private message save failed: "
            "client_message_id is too long";

        return result;
    }


    const std::uint32_t message_type_value =
        static_cast<std::uint32_t>(
            message_type
        );


    const std::uint32_t min_message_type =
        static_cast<std::uint32_t>(
            PrivateMessageType::kText
        );


    const std::uint32_t max_message_type =
        static_cast<std::uint32_t>(
            PrivateMessageType::kFile
        );


    if (message_type_value <
            min_message_type ||
        message_type_value >
            max_message_type) {
        result.status =
            IdempotentSavePrivateMessageStatus::
                kInvalidArgument;

        result.message =
            "idempotent private message save failed: "
            "invalid message type";

        return result;
    }


    /*
     * 相同client_message_id必须映射到完全相同的
     * 业务消息Identity。
     *
     * 注意：
     *
     * 不比较delivery_status。
     *
     * 因为第一次发送后消息可能已经从：
     *
     * Pending -> Delivered -> Read
     *
     * 客户端晚到的Retry仍然是同一条业务消息。
     */
    const auto same_identity =
        [&](
            const PrivateMessageRecord& record
        ) -> bool {
            return
                record.message_id != 0 &&
                record.client_message_id ==
                    client_message_id &&
                record.from_user_id ==
                    from_user_id &&
                record.to_user_id ==
                    to_user_id &&
                record.message_type ==
                    message_type_value &&
                record.content ==
                    content;
        };


    /*
     * 将Repository查询失败映射到
     * Idempotent Save自己的结果语义。
     */
    const auto apply_query_failure =
        [&](
            const FindPrivateMessageResult&
                query_result,
            const std::string& phase
        ) {
            switch (query_result.status) {
                case MessageQueryStatus::
                    kInvalidArgument:
                    result.status =
                        IdempotentSavePrivateMessageStatus::
                            kInvalidArgument;
                    break;

                case MessageQueryStatus::
                    kInvalidRecord:
                    result.status =
                        IdempotentSavePrivateMessageStatus::
                            kInvalidRecord;
                    break;

                case MessageQueryStatus::
                    kStorageError:
                    result.status =
                        IdempotentSavePrivateMessageStatus::
                            kStorageError;
                    break;

                case MessageQueryStatus::
                    kSucceeded:
                default:
                    result.status =
                        IdempotentSavePrivateMessageStatus::
                            kInvalidRecord;
                    break;
            }


            result.message =
                "idempotent private message save "
                + phase +
                " failed";

            if (!query_result.message.empty()) {
                result.message +=
                    ": " +
                    query_result.message;
            }
        };


    /*
     * =====================================================
     * 2. Fast Path：
     *
     * 顺序Retry时，大概率记录已经存在。
     *
     * 注意：
     * 这里仅用于性能优化，
     * 不能负责并发正确性。
     * =====================================================
     */
    const FindPrivateMessageResult existing =
        FindPrivateMessageByClientMessageId(
            from_user_id,
            client_message_id
        );


    if (!existing.Succeeded()) {
        apply_query_failure(
            existing,
            "precheck"
        );

        return result;
    }


    if (existing.Found()) {
        result.message_id =
            existing.record.message_id;

        result.record =
            existing.record;


        if (!same_identity(existing.record)) {
            result.status =
                IdempotentSavePrivateMessageStatus::
                    kIdempotencyConflict;

            result.message =
                "client_message_id already belongs "
                "to a different private message";

            LOG_WARN(
                "private message idempotency conflict"
                << ", from_user_id="
                << from_user_id
                << ", client_message_id="
                << client_message_id
                << ", existing_message_id="
                << existing.record.message_id
            );

            return result;
        }


        result.status =
            IdempotentSavePrivateMessageStatus::
                kReused;

        result.message =
            "existing private message reused";


        LOG_INFO(
            "idempotent private message reused"
            << ", message_id="
            << result.message_id
            << ", from_user_id="
            << from_user_id
            << ", client_message_id="
            << client_message_id
        );


        return result;
    }


        /*
    * =====================================================
    * Deterministic Concurrency Test Seam
    *
    * 正常生产环境callback为空，
    * 因此没有任何运行成本和语义变化。
    *
    * 并发测试会让所有线程在：
    *
    * Precheck Miss
    *      ↓
    * 这里
    *
    * 集合，然后同时进入INSERT，
    * 从而确定性制造Unique Key竞争。
    * =====================================================
    */
    if (idempotent_pre_insert_hook_for_test_) {
        idempotent_pre_insert_hook_for_test_();
    }


    /*
     * =====================================================
     * 3. 没找到：
     *
     * 尝试真正INSERT。
     *
     * 这里仍然使用现有普通Save API，
     * 因为真正的原子裁决由数据库UNIQUE约束完成。
     * =====================================================
     */
    const SavePrivateMessageResult save_result =
        SavePrivateMessage(
            from_user_id,
            to_user_id,
            content,
            DeliveryStatus::kPending,
            message_type,
            client_message_id
        );


    /*
     * =====================================================
     * 4. INSERT成功：
     *
     * 当前调用就是创建者。
     * =====================================================
     */
    if (save_result.Succeeded()) {
        const FindPrivateMessageResult
            created_query =
                FindPrivateMessageById(
                    save_result.message_id
                );


        if (!created_query.Succeeded()) {
            apply_query_failure(
                created_query,
                "post-insert verification"
            );

            return result;
        }


        if (!created_query.Found()) {
            result.status =
                IdempotentSavePrivateMessageStatus::
                    kInvalidRecord;

            result.message =
                "idempotent private message save "
                "failed: inserted message not found";

            return result;
        }


        if (!same_identity(
                created_query.record)) {
            result.status =
                IdempotentSavePrivateMessageStatus::
                    kInvalidRecord;

            result.message =
                "idempotent private message save "
                "failed: inserted message identity "
                "mismatch";

            return result;
        }


        if (created_query.record.message_id !=
            save_result.message_id) {
            result.status =
                IdempotentSavePrivateMessageStatus::
                    kInvalidRecord;

            result.message =
                "idempotent private message save "
                "failed: inserted message id mismatch";

            return result;
        }


        result.status =
            IdempotentSavePrivateMessageStatus::
                kCreated;

        result.message_id =
            created_query.record.message_id;

        result.record =
            created_query.record;

        result.message =
            "private message created";


        LOG_INFO(
            "idempotent private message created"
            << ", message_id="
            << result.message_id
            << ", from_user_id="
            << from_user_id
            << ", to_user_id="
            << to_user_id
            << ", client_message_id="
            << client_message_id
        );


        return result;
    }


    /*
     * =====================================================
     * 5. INSERT失败：
     *
     * 千万不能立即：
     *
     * return StorageError;
     *
     * 因为失败可能意味着：
     *
     * 线程A刚刚使用相同client_message_id
     * 赢得UNIQUE约束。
     *
     * 所以进行Recovery Read。
     * =====================================================
     */
    const FindPrivateMessageResult recovered =
        FindPrivateMessageByClientMessageId(
            from_user_id,
            client_message_id
        );


    if (!recovered.Succeeded()) {
        apply_query_failure(
            recovered,
            "recovery query"
        );


        if (!save_result.message.empty()) {
            result.message +=
                ", original_insert_error=" +
                save_result.message;
        }


        return result;
    }


    /*
     * INSERT失败，而且稳定数据库中也没有记录。
     *
     * 那么它就不是幂等冲突恢复，
     * 而是真正的StorageError。
     */
    if (!recovered.Found()) {
        result.status =
            IdempotentSavePrivateMessageStatus::
                kStorageError;

        result.message =
            save_result.message.empty()
                ? "idempotent private message save "
                  "failed: insert failed and no "
                  "existing message was found"
                : save_result.message;

        return result;
    }


    result.message_id =
        recovered.record.message_id;

    result.record =
        recovered.record;


    /*
     * client_message_id存在，
     * 但业务Identity不同。
     *
     * 这不是Retry。
     *
     * 是调用者复用了幂等Key。
     */
    if (!same_identity(recovered.record)) {
        result.status =
            IdempotentSavePrivateMessageStatus::
                kIdempotencyConflict;

        result.message =
            "client_message_id already belongs "
            "to a different private message";


        LOG_WARN(
            "private message idempotency conflict "
            "after insert failure"
            << ", from_user_id="
            << from_user_id
            << ", client_message_id="
            << client_message_id
            << ", existing_message_id="
            << recovered.record.message_id
        );


        return result;
    }


    /*
     * INSERT失败，
     * 但数据库中已经存在完全相同的业务消息。
     *
     * 典型情况：
     *
     * Concurrent Retry
     *
     * Thread A INSERT success
     * Thread B INSERT duplicate
     * Thread B SELECT A's row
     *
     * 因此：
     * 这是成功的Reused，而不是StorageError。
     */
    result.status =
        IdempotentSavePrivateMessageStatus::
            kReused;

    result.message =
        "existing private message recovered "
        "after insert conflict";


    LOG_INFO(
        "idempotent private message recovered"
        << ", message_id="
        << result.message_id
        << ", from_user_id="
        << from_user_id
        << ", client_message_id="
        << client_message_id
    );


    return result;
}


ListPrivateMessagesResult MessageRepository::ListPendingMessages(
    std::uint64_t to_user_id,
    std::size_t limit
) {
    return ListPendingMessagesAfter(
        to_user_id,
        0,
        limit
    );
}


ListPrivateMessagesResult
MessageRepository::ListPendingMessagesAfter(
    std::uint64_t to_user_id,
    std::uint64_t after_message_id,
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
        "AND message_id > " +
        std::to_string(after_message_id) +
        " "
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


CountPendingMessagesResult
MessageRepository::CountPendingMessages(
    std::uint64_t to_user_id
) {
    CountPendingMessagesResult result;

    if (to_user_id == 0) {
        result.status =
            MessageQueryStatus::
                kInvalidArgument;

        result.message =
            "message repository count pending "
            "failed: invalid user id";

        return result;
    }

    if (pool_ == nullptr) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            "message repository count pending "
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
            "message repository count pending "
            "failed: acquire connection failed";

        return result;
    }

    const std::string sql =
        "SELECT COUNT(*) "
        "FROM im_private_messages "
        "WHERE to_user_id = " +
        std::to_string(to_user_id) +
        " "
        "AND delivery_status = 0";

    MySqlQueryResult query_result;

    if (
        !connection->Query(
            sql,
            &query_result
        )
    ) {
        result.status =
            MessageQueryStatus::
                kStorageError;

        result.message =
            connection->LastError();

        if (result.message.empty()) {
            result.message =
                "message repository count pending "
                "failed: query failed";
        }

        return result;
    }

    if (
        query_result.RowCount() != 1 ||
        query_result.rows.front().size() != 1
    ) {
        result.status =
            MessageQueryStatus::
                kInvalidRecord;

        result.message =
            "message repository count pending "
            "failed: invalid count result";

        return result;
    }

    try {
        result.count =
            ToUInt64(
                query_result.rows.front().front()
            );
    } catch (const std::exception&) {
        result.status =
            MessageQueryStatus::
                kInvalidRecord;

        result.message =
            "message repository count pending "
            "failed: invalid count value";

        return result;
    }

    result.status =
        MessageQueryStatus::kSucceeded;

    result.message =
        "pending message count queried";

    return result;
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

SavePrivateMessageResult MessageRepository::SavePrivateMessageOnConnection(
    MySqlConnection* connection,
    std::uint64_t from_user_id,
    std::uint64_t to_user_id,
    const std::string& content,
    DeliveryStatus delivery_status,
    PrivateMessageType message_type,
    const std::string& client_message_id
) {
    SavePrivateMessageResult result;

    if (from_user_id == 0 || to_user_id == 0 || from_user_id == to_user_id) {
        result.status = MessageMutationStatus::kInvalidArgument;
        result.message = "message repository transactional save failed: invalid user id";
        return result;
    }

    if (client_message_id.size() > 64) {
        result.status = MessageMutationStatus::kInvalidArgument;
        result.message = "message repository transactional save failed: client_message_id too long";
        return result;
    }

    const auto message_type_value = static_cast<std::uint32_t>(message_type);
    if (message_type_value < static_cast<std::uint32_t>(PrivateMessageType::kText) ||
        message_type_value > static_cast<std::uint32_t>(PrivateMessageType::kFile)) {
        result.status = MessageMutationStatus::kInvalidArgument;
        result.message = "message repository transactional save failed: invalid message type";
        return result;
    }

    const auto delivery_status_value = static_cast<std::uint32_t>(delivery_status);
    if (delivery_status_value > static_cast<std::uint32_t>(DeliveryStatus::kFailed)) {
        result.status = MessageMutationStatus::kInvalidArgument;
        result.message = "message repository transactional save failed: invalid delivery status";
        return result;
    }

    if (connection == nullptr || !connection->IsConnected()) {
        result.status = MessageMutationStatus::kStorageError;
        result.message = "message repository transactional save failed: connection unavailable";
        return result;
    }

    const std::string escaped_content = connection->EscapeString(content);
    std::string client_message_sql = "NULL";
    if (!client_message_id.empty()) {
        client_message_sql = "'" + connection->EscapeString(client_message_id) + "'";
    }

    const std::string sql =
        "INSERT INTO im_private_messages ("
        "client_message_id, from_user_id, to_user_id, message_type, content, delivery_status"
        ") VALUES (" +
        client_message_sql + ", " +
        std::to_string(from_user_id) + ", " +
        std::to_string(to_user_id) + ", " +
        std::to_string(message_type_value) + ", '" +
        escaped_content + "', " +
        std::to_string(delivery_status_value) + ")";

    if (!connection->Execute(sql)) {
        result.status = MessageMutationStatus::kStorageError;
        result.message = connection->LastError();
        if (result.message.empty()) {
            result.message = "message repository transactional save failed: execute failed";
        }
        return result;
    }

    result.message_id = connection->LastInsertId();
    if (result.message_id == 0) {
        result.status = MessageMutationStatus::kStorageError;
        result.message = "message repository transactional save failed: invalid last insert id";
        return result;
    }

    result.status = MessageMutationStatus::kSucceeded;
    result.message = "private message saved on caller transaction";
    return result;
}

FindPrivateMessageResult MessageRepository::FindPrivateMessageByIdOnConnection(
    MySqlConnection* connection,
    std::uint64_t message_id
) {
    FindPrivateMessageResult result;

    if (message_id == 0) {
        result.status = MessageQueryStatus::kInvalidArgument;
        result.message = "message repository transactional find failed: invalid message id";
        return result;
    }

    if (connection == nullptr || !connection->IsConnected()) {
        result.status = MessageQueryStatus::kStorageError;
        result.message = "message repository transactional find failed: connection unavailable";
        return result;
    }

    const std::string sql =
        "SELECT message_id, IFNULL(client_message_id, ''), from_user_id, to_user_id, "
        "message_type, content, delivery_status, created_at, "
        "IFNULL(delivered_at, ''), IFNULL(read_at, '') "
        "FROM im_private_messages WHERE message_id = " +
        std::to_string(message_id) + " LIMIT 1";

    MySqlQueryResult query_result;
    if (!connection->Query(sql, &query_result)) {
        result.status = MessageQueryStatus::kStorageError;
        result.message = connection->LastError();
        if (result.message.empty()) {
            result.message = "message repository transactional find failed: query failed";
        }
        return result;
    }

    if (query_result.rows.empty()) {
        result.status = MessageQueryStatus::kSucceeded;
        result.found = false;
        result.message = "private message not found";
        return result;
    }

    if (query_result.rows.size() != 1) {
        result.status = MessageQueryStatus::kInvalidRecord;
        result.message = "message repository transactional find failed: unexpected row count";
        return result;
    }

    const auto built = BuildMessagesFromResult(query_result);
    if (!built.Succeeded() || built.records.size() != 1) {
        result.status = built.Succeeded() ? MessageQueryStatus::kInvalidRecord : built.status;
        result.message = built.message.empty()
            ? "message repository transactional find failed: invalid record"
            : built.message;
        return result;
    }

    result.status = MessageQueryStatus::kSucceeded;
    result.found = true;
    result.record = built.records.front();
    result.message = "private message found";
    return result;
}

FindPrivateMessageResult
MessageRepository::FindPrivateMessageByClientMessageIdOnConnection(
    MySqlConnection* connection,
    std::uint64_t from_user_id,
    const std::string& client_message_id
) {
    FindPrivateMessageResult result;

    if (from_user_id == 0 || client_message_id.empty() || client_message_id.size() > 64) {
        result.status = MessageQueryStatus::kInvalidArgument;
        result.message = "message repository transactional idempotency find failed: invalid argument";
        return result;
    }

    if (connection == nullptr || !connection->IsConnected()) {
        result.status = MessageQueryStatus::kStorageError;
        result.message = "message repository transactional idempotency find failed: connection unavailable";
        return result;
    }

    const std::string escaped = connection->EscapeString(client_message_id);
    const std::string sql =
        "SELECT message_id, IFNULL(client_message_id, ''), from_user_id, to_user_id, "
        "message_type, content, delivery_status, created_at, "
        "IFNULL(delivered_at, ''), IFNULL(read_at, '') "
        "FROM im_private_messages WHERE from_user_id = " +
        std::to_string(from_user_id) +
        " AND client_message_id = '" + escaped + "' LIMIT 2";

    MySqlQueryResult query_result;
    if (!connection->Query(sql, &query_result)) {
        result.status = MessageQueryStatus::kStorageError;
        result.message = connection->LastError();
        if (result.message.empty()) {
            result.message = "message repository transactional idempotency find failed: query failed";
        }
        return result;
    }

    if (query_result.rows.empty()) {
        result.status = MessageQueryStatus::kSucceeded;
        result.found = false;
        result.message = "private message not found";
        return result;
    }

    if (query_result.rows.size() != 1) {
        result.status = MessageQueryStatus::kInvalidRecord;
        result.message = "message repository transactional idempotency find failed: duplicate records";
        return result;
    }

    const auto built = BuildMessagesFromResult(query_result);
    if (!built.Succeeded() || built.records.size() != 1) {
        result.status = built.Succeeded() ? MessageQueryStatus::kInvalidRecord : built.status;
        result.message = built.message.empty()
            ? "message repository transactional idempotency find failed: invalid record"
            : built.message;
        return result;
    }

    result.status = MessageQueryStatus::kSucceeded;
    result.found = true;
    result.record = built.records.front();
    result.message = "private message found";
    return result;
}

UpdatePrivateMessagesResult MessageRepository::MarkReadByDialogOnConnection(
    MySqlConnection* connection,
    std::uint64_t reader_user_id,
    std::uint64_t peer_user_id
) {
    UpdatePrivateMessagesResult result;

    if (reader_user_id == 0 || peer_user_id == 0 || reader_user_id == peer_user_id) {
        result.status = MessageMutationStatus::kInvalidArgument;
        result.message = "message repository transactional mark read failed: invalid user id";
        return result;
    }

    if (connection == nullptr || !connection->IsConnected()) {
        result.status = MessageMutationStatus::kStorageError;
        result.message = "message repository transactional mark read failed: connection unavailable";
        return result;
    }

    const std::string sql =
        "UPDATE im_private_messages SET delivery_status = 2, read_at = NOW() "
        "WHERE to_user_id = " + std::to_string(reader_user_id) +
        " AND from_user_id = " + std::to_string(peer_user_id) +
        " AND delivery_status = 1";

    if (!connection->Execute(sql)) {
        result.status = MessageMutationStatus::kStorageError;
        result.message = connection->LastError();
        if (result.message.empty()) {
            result.message = "message repository transactional mark read failed: execute failed";
        }
        return result;
    }

    result.status = MessageMutationStatus::kSucceeded;
    result.affected_rows = connection->AffectedRows();
    result.message = "private messages marked read on caller transaction";
    return result;
}

UpdatePrivateMessagesResult MessageRepository::MarkReceiverConfirmed(
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

    return MarkReceiverConfirmedBatch(
        std::vector<std::uint64_t>{
            message_id
        }
    );
}

UpdatePrivateMessagesResult MessageRepository::MarkReceiverConfirmedBatch(
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
        ") "
        "AND delivery_status = 0";

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
        "private messages marked receiver confirmed";

    LOG_INFO(
        "private messages marked receiver confirmed"
        << ", requested_count="
        << message_ids.size()
        << ", affected_rows="
        << result.affected_rows
    );

    return result;
}


UpdatePrivateMessagesResult
MessageRepository::MarkDelivered(
    std::uint64_t message_id
) {
    return MarkReceiverConfirmed(
        message_id
    );
}


UpdatePrivateMessagesResult
MessageRepository::MarkDeliveredBatch(
    const std::vector<std::uint64_t>&
        message_ids
) {
    return MarkReceiverConfirmedBatch(
        message_ids
    );
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