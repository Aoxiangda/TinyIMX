#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/cache/UnreadCountCache.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

namespace {

/*
 * M10-v2.8.3-c-1阶段中：
 *
 * IncrementPrivateUnread()尚未改造，
 * 仍然返回std::optional<std::int64_t>。
 *
 * 所以这个辅助函数只用于检查递增操作。
 */
bool ExpectIncrementResult(
    const std::string& name,
    const tinyimx::IncrementUnreadResult& result,
    tinyimx::IncrementUnreadStatus expected_status,
    std::int64_t expected_private_count
) {
    std::cout
        << name
        << "_status = "
        << tinyimx::
            IncrementUnreadStatusToString(
                result.status
            )
        << ", private_count="
        << result.private_count
        << '\n';

    if (result.status != expected_status) {
        std::cerr
            << name
            << " expected status="
            << tinyimx::
                IncrementUnreadStatusToString(
                    expected_status
                )
            << ", actual status="
            << tinyimx::
                IncrementUnreadStatusToString(
                    result.status
                )
            << ", error="
            << result.error_message
            << '\n';

        return false;
    }

    if (result.private_count !=
        expected_private_count) {
        std::cerr
            << name
            << " expected private_count="
            << expected_private_count
            << ", actual private_count="
            << result.private_count
            << '\n';

        return false;
    }

    return true;
}
/*
 * GetPrivateUnread()和GetTotalUnread()
 * 已经改为返回GetUnreadCountResult。
 *
 * 查询结果不能只检查count，还必须检查：
 *
 * found
 * not_found
 * invalid_value
 * invalid_argument
 * redis_error
 */
bool ExpectClearResult(
    const std::string& name,
    const tinyimx::ClearUnreadResult& result,
    tinyimx::ClearUnreadStatus expected_status,
    std::int64_t expected_total_count
) {
    std::cout
        << name
        << "_status = "
        << tinyimx::
            ClearUnreadStatusToString(
                result.status
            )
        << ", total_count="
        << result.total_count
        << '\n';

    if (result.status != expected_status) {
        std::cerr
            << name
            << " expected status="
            << tinyimx::
                ClearUnreadStatusToString(
                    expected_status
                )
            << ", actual status="
            << tinyimx::
                ClearUnreadStatusToString(
                    result.status
                )
            << ", error="
            << result.error_message
            << '\n';

        return false;
    }

    if (result.total_count !=
        expected_total_count) {
        std::cerr
            << name
            << " expected total_count="
            << expected_total_count
            << ", actual total_count="
            << result.total_count
            << '\n';

        return false;
    }

    return true;
}

bool ExpectQueryResult(
    const std::string& name,
    const tinyimx::GetUnreadCountResult& result,
    tinyimx::GetUnreadCountStatus expected_status,
    std::int64_t expected_count
) {
    std::cout
        << name
        << "_status = "
        << tinyimx::
            GetUnreadCountStatusToString(
                result.status
            )
        << ", count="
        << result.count
        << '\n';

    if (result.status != expected_status) {
        std::cerr
            << name
            << " expected status="
            << tinyimx::
                GetUnreadCountStatusToString(
                    expected_status
                )
            << ", actual status="
            << tinyimx::
                GetUnreadCountStatusToString(
                    result.status
                )
            << ", error="
            << result.error_message
            << '\n';

        return false;
    }

    if (result.count != expected_count) {
        std::cerr
            << name
            << " expected count="
            << expected_count
            << ", actual count="
            << result.count
            << '\n';

        return false;
    }

    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path =
        "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;

    if (!config.LoadFromFile(config_path)) {
        std::cerr
            << "load config failed: "
            << config.LastError()
            << '\n';

        return 1;
    }

    if (!tinyimx::Logger::
            Instance().
            Init(config.Logger())) {
        std::cerr
            << "logger init failed\n";

        return 1;
    }

    if (!config.Redis().enable) {
        std::cerr
            << "redis is disabled in config\n";

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    tinyimx::RedisConnectionPool redis_pool;

    if (!redis_pool.Initialize(
            config.Redis()
        )) {
        std::cerr
            << "redis pool init failed\n";

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const std::uint64_t receiver =
        10002;

    const std::uint64_t sender_a =
        10001;

    const std::uint64_t sender_b =
        10003;

    const std::string key_prefix =
        "tinyimx:demo:unread:";

    const std::string private_a_key =
        key_prefix +
        "private:" +
        std::to_string(receiver) +
        ":" +
        std::to_string(sender_a);

    const std::string private_b_key =
        key_prefix +
        "private:" +
        std::to_string(receiver) +
        ":" +
        std::to_string(sender_b);

    const std::string total_key =
        key_prefix +
        "total:" +
        std::to_string(receiver);

    /*
     * 清理上一次Demo可能遗留的测试数据。
     */
    {
        auto connection =
            redis_pool.Acquire();

        if (!connection) {
            std::cerr
                << "acquire redis "
                << "connection failed\n";

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }

        connection->Del(private_a_key);
        connection->Del(private_b_key);
        connection->Del(total_key);
    }

    tinyimx::UnreadCountCache
        unread_cache(
            &redis_pool,
            key_prefix
        );

    std::cout
        << "========== "
        << "Unread Count Cache Demo "
        << "==========\n";

    if (!ExpectIncrementResult(
            "count_after_a_1",
            unread_cache.
                IncrementPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                IncrementUnreadStatus::
                    kIncremented,
            1
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectIncrementResult(
            "count_after_a_2",
            unread_cache.
                IncrementPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                IncrementUnreadStatus::
                    kIncremented,
            2
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectIncrementResult(
            "count_after_b_1",
            unread_cache.
                IncrementPrivateUnread(
                    receiver,
                    sender_b
                ),
            tinyimx::
                IncrementUnreadStatus::
                    kIncremented,
            1
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
    /*
     * 已经写入Redis，所以三个查询均应返回found。
     */
    if (!ExpectQueryResult(
            "private_unread_from_a",
            unread_cache.
                GetPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kFound,
            2
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectQueryResult(
            "private_unread_from_b",
            unread_cache.
                GetPrivateUnread(
                    receiver,
                    sender_b
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kFound,
            1
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectQueryResult(
            "total_unread",
            unread_cache.
                GetTotalUnread(
                    receiver
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kFound,
            3
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    /*
     * 清除sender_a对应的私聊未读数。
     *
     * 当前ClearPrivateUnread()会删除private键，
     * 因此之后查询private_a应返回not_found，而不是found=0。
     */
    if (!ExpectClearResult(
            "clear_private_a",
            unread_cache.
                ClearPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                ClearUnreadStatus::
                    kCleared,
            1
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectQueryResult(
            "private_unread_from_a_after_clear",
            unread_cache.
                GetPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kNotFound,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    /*
     * 原总数为3，清除了sender_a的2条，
     * 所以总数还剩1。
     */
    if (!ExpectQueryResult(
            "total_unread_after_clear_a",
            unread_cache.
                GetTotalUnread(
                    receiver
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kFound,
            1
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    /*
     * 清除sender_b剩余的1条未读。
     */
    if (!ExpectClearResult(
            "clear_private_b",
            unread_cache.
                ClearPrivateUnread(
                    receiver,
                    sender_b
                ),
            tinyimx::
                ClearUnreadStatus::
                    kCleared,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    /*
     * 当前ClearPrivateUnread()把total键递减到0，
     * 但不会删除total键。
     *
     * 所以这里应是：
     *
     * found,count=0
     *
     * 而不是not_found。
     */
    if (!ExpectQueryResult(
            "total_unread_after_clear_all",
            unread_cache.
                GetTotalUnread(
                    receiver
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kFound,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    /*
     * 显式删除全部键，
     * 验证Redis键不存在时返回not_found。
     */
    {
        auto connection =
            redis_pool.Acquire();

        if (!connection) {
            std::cerr
                << "acquire redis connection "
                << "for missing test failed\n";

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }

        connection->Del(private_a_key);
        connection->Del(private_b_key);
        connection->Del(total_key);
    }

    if (!ExpectQueryResult(
            "get_private_missing",
            unread_cache.
                GetPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kNotFound,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectQueryResult(
            "get_total_missing",
            unread_cache.
                GetTotalUnread(
                    receiver
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kNotFound,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectClearResult(
            "clear_private_missing",
            unread_cache.
                ClearPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                ClearUnreadStatus::
                    kNotFound,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    /*
     * 非法参数。
     */
    if (!ExpectQueryResult(
            "get_unread_invalid_argument",
            unread_cache.
                GetPrivateUnread(
                    0,
                    sender_a
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kInvalidArgument,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectIncrementResult(
            "increment_unread_invalid_argument",
            unread_cache.
                IncrementPrivateUnread(
                    0,
                    sender_a
                ),
            tinyimx::
                IncrementUnreadStatus::
                    kInvalidArgument,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
    /*
     * 写入不能被完整解析的非法计数。
     */
    {
        auto connection =
            redis_pool.Acquire();

        if (!connection) {
            std::cerr
                << "acquire redis connection "
                << "for invalid unread "
                << "value failed\n";

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }

        if (!connection->Set(
                private_a_key,
                "12abc"
            )) {
            std::cerr
                << "write invalid unread "
                << "value failed: "
                << connection->LastError()
                << '\n';

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }
    }

    if (!ExpectQueryResult(
            "get_unread_invalid_value",
            unread_cache.
                GetPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kInvalidValue,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    {
    auto connection =
            redis_pool.Acquire();

        if (!connection) {
            std::cerr
                << "acquire redis connection "
                << "for atomic increment test "
                << "failed\n";

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }

        if (!connection->Set(
                total_key,
                "7"
            )) {
            std::cerr
                << "prepare total count "
                << "failed: "
                << connection->LastError()
                << '\n';

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }
    }

    if (!ExpectClearResult(
            "clear_with_invalid_private",
            unread_cache.
                ClearPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                ClearUnreadStatus::
                    kInvalidValue,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectIncrementResult(
            "increment_with_invalid_private",
            unread_cache.
                IncrementPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                IncrementUnreadStatus::
                    kInvalidValue,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectQueryResult(
            "total_after_invalid_private_operations",
            unread_cache.
                GetTotalUnread(
                    receiver
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kFound,
            7
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectClearResult(
            "clear_unread_invalid_argument",
            unread_cache.
                ClearPrivateUnread(
                    0,
                    sender_a
                ),
            tinyimx::
                ClearUnreadStatus::
                    kInvalidArgument,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    {
    auto connection =
            redis_pool.Acquire();

        if (!connection) {
            std::cerr
                << "acquire redis connection "
                << "for invalid total test "
                << "failed\n";

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }

        if (!connection->Set(
                private_a_key,
                "5"
            ) ||
            !connection->Set(
                total_key,
                "bad-total"
            )) {
            std::cerr
                << "prepare invalid total "
                << "test failed: "
                << connection->LastError()
                << '\n';

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }
    }

    if (!ExpectClearResult(
            "clear_with_invalid_total",
            unread_cache.
                ClearPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                ClearUnreadStatus::
                    kInvalidValue,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectIncrementResult(
            "increment_with_invalid_total",
            unread_cache.
                IncrementPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                IncrementUnreadStatus::
                    kInvalidValue,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectQueryResult(
            "private_after_invalid_total_increment",
            unread_cache.
                GetPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kFound,
            5
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
    {
    auto connection =
            redis_pool.Acquire();

        if (!connection) {
            std::cerr
                << "acquire redis connection "
                << "for clear clamp test "
                << "failed\n";

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }

        if (!connection->Set(
                private_a_key,
                "5"
            ) ||
            !connection->Set(
                total_key,
                "3"
            )) {
            std::cerr
                << "prepare clear clamp test "
                << "failed: "
                << connection->LastError()
                << '\n';

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }
    }
    if (!ExpectClearResult(
            "clear_with_total_underflow",
            unread_cache.
                ClearPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                ClearUnreadStatus::
                    kCleared,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
    if (!ExpectQueryResult(
            "private_after_underflow_clear",
            unread_cache.
                GetPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kNotFound,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
    if (!ExpectQueryResult(
            "total_after_underflow_clear",
            unread_cache.
                GetTotalUnread(
                    receiver
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kFound,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
        /*
     * 删除非法测试数据。
     */
    {
        auto connection =
            redis_pool.Acquire();

        if (!connection ||
            !connection->Del(
                private_a_key
            ) ||
            !connection->Del(
                total_key
            )) {
            std::cerr
                << "cleanup invalid unread "
                << "test data failed\n";

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }
    }

    /*
     * Redis依赖不可用。
     */
    tinyimx::UnreadCountCache unavailable_unread_cache(nullptr, key_prefix);
    if (!ExpectClearResult(
            "clear_unread_redis_error",
            unavailable_unread_cache.
                ClearPrivateUnread(
                    receiver,
                    sender_a
                ),
    tinyimx::ClearUnreadStatus::kRedisError,0)) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
        if (!ExpectIncrementResult(
            "increment_unread_redis_error",
            unavailable_unread_cache.
                IncrementPrivateUnread(
                    receiver,
                    sender_a
                ),
            tinyimx::
                IncrementUnreadStatus::
                    kRedisError,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    if (!ExpectQueryResult(
            "get_unread_redis_error",
            unavailable_unread_cache.
                GetTotalUnread(
                    receiver
                ),
            tinyimx::
                GetUnreadCountStatus::
                    kRedisError,
            0
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    redis_pool.Shutdown();

    std::cout
        << "Unread count cache demo "
        << "finished\n";

    std::cout
        << "=========================="
        << "==================\n";

    tinyimx::Logger::
        Instance().
        Shutdown();

    return 0;
}