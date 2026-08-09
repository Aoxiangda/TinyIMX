#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/cache/OnlineStatusCache.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

void PrintOnlineStatus(
    const tinyimx::OnlineStatusRecord& record
) {
    std::cout
        << "online_status:"
        << " user_id="
        << record.user_id
        << " gateway_id="
        << record.gateway_id
        << " connection_name="
        << record.connection_name
        << " login_time="
        << record.login_time
        << '\n';
}

bool RunEvalIntegerDemo(
    tinyimx::RedisConnectionPool* pool
) {
    if (pool == nullptr) {
        return false;
    }

    auto connection =
        pool->Acquire();

    if (!connection) {
        std::cerr
            << "eval integer demo failed: "
            << "acquire redis connection failed\n";

        return false;
    }

    const std::string key =
        "tinyimx:demo:eval_integer";

    const std::string value =
        "eval-demo-value";

    const std::string script = R"(
local current = redis.call('GET', KEYS[1])

if not current then
    redis.call('SET', KEYS[1], ARGV[1])
    return 1
end

if current == ARGV[1] then
    redis.call('DEL', KEYS[1])
    return 2
end

return 0
)";

    if (!connection->Del(key)) {
        std::cerr
            << "eval integer cleanup failed: "
            << connection->LastError()
            << '\n';

        return false;
    }

    const auto first_result =
        connection->EvalInteger(
            script,
            std::vector<std::string>{
                key
            },
            std::vector<std::string>{
                value
            }
        );

    if (!first_result.has_value()) {
        std::cerr
            << "first eval integer failed: "
            << connection->LastError()
            << '\n';

        return false;
    }

    if (first_result.value() != 1) {
        std::cerr
            << "first eval integer returned "
            << "unexpected value: "
            << first_result.value()
            << '\n';

        return false;
    }

    const auto second_result =
        connection->EvalInteger(
            script,
            std::vector<std::string>{
                key
            },
            std::vector<std::string>{
                value
            }
        );

    if (!second_result.has_value()) {
        std::cerr
            << "second eval integer failed: "
            << connection->LastError()
            << '\n';

        return false;
    }

    if (second_result.value() != 2) {
        std::cerr
            << "second eval integer returned "
            << "unexpected value: "
            << second_result.value()
            << '\n';

        return false;
    }

    std::cout
        << "eval_integer_first = "
        << first_result.value()
        << '\n';

    std::cout
        << "eval_integer_second = "
        << second_result.value()
        << '\n';

    return true;
}

}  // namespace

int main(
    int argc,
    char* argv[]
) {
    std::string config_path =
        "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;

    if (!config.LoadFromFile(
            config_path
        )) {
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
            << "redis is disabled "
            << "in config\n";

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    tinyimx::RedisConnectionPool
        redis_pool;

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

    std::cout
        << "========== "
        << "Online Status Cache Demo "
        << "==========\n";

    if (!RunEvalIntegerDemo(
            &redis_pool
        )) {
        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    tinyimx::OnlineStatusCache
        online_cache(
            &redis_pool
        );

    const std::uint64_t user_id =
        10001;

    const std::string gateway_id =
        "gateway-demo-1";

    const std::string connection_name = "tinyimx-gateway-conn-demo-10001";

    const std::string new_connection_name = "tinyimx-gateway-conn-demo-10001-new";

    const auto set_online_result = online_cache.SetOnline(
            user_id,
            gateway_id,
            connection_name,
            60
        );

    std::cout
        << "set_online_status = "
        << tinyimx::
            SetOnlineStatusToString(
                set_online_result.status
            )
        << '\n';

    if (!set_online_result.Succeeded()) {
        std::cerr
            << "set online failed"
            << ", status="
            << tinyimx::
                SetOnlineStatusToString(
                    set_online_result.status
                )
            << ", error="
            << set_online_result.
                error_message
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto status_result =
        online_cache.GetOnlineStatus(
            user_id
        );

    std::cout
        << "get_online_found_status = "
        << tinyimx::
            GetOnlineStatusStatusToString(
                status_result.status
            )
        << '\n';

    if (!status_result.Found()) {
        std::cerr
            << "get online status failed"
            << ", status="
            << tinyimx::
                GetOnlineStatusStatusToString(
                    status_result.status
                )
            << ", error="
            << status_result.error_message
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    PrintOnlineStatus(
        status_result.record.value()
    );

    std::cout
        << "is_online = "
        << status_result.Found()
        << '\n';

    const auto refresh_current_result =
        online_cache.RefreshOnlineIfMatch(
            user_id,
            gateway_id,
            connection_name,
            120
        );

    std::cout
        << "refresh_online_current_status = "
        << tinyimx::
            RefreshOnlineIfMatchStatusToString(
                refresh_current_result.status
            )
        << '\n';

    if (!refresh_current_result.Refreshed()) {
        std::cerr
            << "refresh current online "
            << "status failed"
            << ", status="
            << tinyimx::
                RefreshOnlineIfMatchStatusToString(
                    refresh_current_result.status
                )
            << ", error="
            << refresh_current_result.
                error_message
            << '\n';

        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto wrong_refresh_result =
        online_cache.RefreshOnlineIfMatch(
            user_id,
            gateway_id,
            connection_name + "-wrong",
            120
        );

    std::cout
        << "refresh_online_wrong_connection_status = "
        << tinyimx::
            RefreshOnlineIfMatchStatusToString(
                wrong_refresh_result.status
            )
        << '\n';

    if (wrong_refresh_result.status !=
        tinyimx::
            RefreshOnlineIfMatchStatus::
                kMismatch) {
        std::cerr
            << "expected wrong connection "
            << "refresh mismatch\n";

        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto wrong_connection_result =
        online_cache.SetOfflineIfMatch(
            user_id,
            gateway_id,
            new_connection_name
        );

    std::cout
        << "wrong_connection_offline_status = "
        << tinyimx::
            SetOfflineIfMatchStatusToString(
                wrong_connection_result.status
            )
        << '\n';

    if (wrong_connection_result.status !=
        tinyimx::SetOfflineIfMatchStatus::
            kMismatch) {
        std::cerr
            << "wrong connection should "
            << "not remove online status"
            << ", status="
            << tinyimx::
                SetOfflineIfMatchStatusToString(
                    wrong_connection_result.status
                )
            << ", error="
            << wrong_connection_result.
                error_message
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto status_after_wrong_connection =
        online_cache.GetOnlineStatus(
            user_id
        );

    if (!status_after_wrong_connection.Found() ||
        status_after_wrong_connection.
            record->
            connection_name !=
            connection_name) {
        std::cerr
            << "wrong connection removed "
            << "current online status"
            << ", lookup_status="
            << tinyimx::
                GetOnlineStatusStatusToString(
                    status_after_wrong_connection.
                        status
                )
            << ", error="
            << status_after_wrong_connection.
                error_message
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
    const auto replace_online_result = online_cache.SetOnline(
            user_id,
            gateway_id,
            new_connection_name,
            120
        );

    std::cout
        << "replace_online_status = "
        << tinyimx::
            SetOnlineStatusToString(
                replace_online_result.status
            )
        << '\n';

    if (!replace_online_result.Succeeded()) {
        std::cerr
            << "replace online status failed"
            << ", status="
            << tinyimx::
                SetOnlineStatusToString(
                    replace_online_result.status
                )
            << ", error="
            << replace_online_result.
                error_message
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto stale_refresh_result =
        online_cache.RefreshOnlineIfMatch(
            user_id,
            gateway_id,
            connection_name,
            120
        );

    std::cout
        << "refresh_online_stale_connection_status = "
        << tinyimx::
            RefreshOnlineIfMatchStatusToString(
                stale_refresh_result.status
            )
        << '\n';

    if (stale_refresh_result.status !=
        tinyimx::
            RefreshOnlineIfMatchStatus::
                kMismatch) {
        std::cerr
            << "expected stale connection "
            << "refresh mismatch\n";

        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto new_refresh_result =
        online_cache.RefreshOnlineIfMatch(
            user_id,
            gateway_id,
            new_connection_name,
            120
        );

    std::cout
        << "refresh_online_new_connection_status = "
        << tinyimx::
            RefreshOnlineIfMatchStatusToString(
                new_refresh_result.status
            )
        << '\n';

    if (!new_refresh_result.Refreshed()) {
        std::cerr
            << "refresh new connection failed"
            << ", status="
            << tinyimx::
                RefreshOnlineIfMatchStatusToString(
                    new_refresh_result.status
                )
            << ", error="
            << new_refresh_result.
                error_message
            << '\n';

        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto invalid_refresh_argument =
        online_cache.RefreshOnlineIfMatch(
            0,
            gateway_id,
            connection_name,
            120
        );

    std::cout
        << "refresh_online_invalid_argument_status = "
        << tinyimx::
            RefreshOnlineIfMatchStatusToString(
                invalid_refresh_argument.status
            )
        << '\n';

    if (invalid_refresh_argument.status !=
        tinyimx::
            RefreshOnlineIfMatchStatus::
                kInvalidArgument) {
        std::cerr
            << "expected refresh "
            << "invalid_argument\n";

        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto stale_connection_result =
        online_cache.SetOfflineIfMatch(
            user_id,
            gateway_id,
            connection_name
        );

    std::cout
        << "stale_connection_offline_status = "
        << tinyimx::
            SetOfflineIfMatchStatusToString(
                stale_connection_result.status
            )
        << '\n';

    if (stale_connection_result.status !=
        tinyimx::SetOfflineIfMatchStatus::
            kMismatch) {
        std::cerr
            << "stale connection should "
            << "not remove new online status"
            << ", status="
            << tinyimx::
                SetOfflineIfMatchStatusToString(
                    stale_connection_result.status
                )
            << ", error="
            << stale_connection_result.
                error_message
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto current_status =
        online_cache.GetOnlineStatus(
            user_id
        );

    if (!current_status.Found() ||
        current_status.record->
            connection_name !=
            new_connection_name) {
        std::cerr
            << "stale connection deleted "
            << "new online status"
            << ", lookup_status="
            << tinyimx::
                GetOnlineStatusStatusToString(
                    current_status.status
                )
            << ", error="
            << current_status.error_message
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto current_connection_result =
        online_cache.SetOfflineIfMatch(
            user_id,
            gateway_id,
            new_connection_name
        );

    std::cout
        << "current_connection_offline_status = "
        << tinyimx::
            SetOfflineIfMatchStatusToString(
                current_connection_result.status
            )
        << '\n';

    if (!current_connection_result.Deleted()) {
        std::cerr
            << "current connection offline "
            << "failed"
            << ", status="
            << tinyimx::
                SetOfflineIfMatchStatusToString(
                    current_connection_result.status
                )
            << ", error="
            << current_connection_result.
                error_message
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto missing_status_result =
        online_cache.SetOfflineIfMatch(
            user_id,
            gateway_id,
            new_connection_name
        );

    std::cout
        << "missing_status_offline_status = "
        << tinyimx::
            SetOfflineIfMatchStatusToString(
                missing_status_result.status
            )
        << '\n';

    if (missing_status_result.status !=
        tinyimx::SetOfflineIfMatchStatus::
            kNotFound) {
        std::cerr
            << "missing online status "
            << "returned unexpected result"
            << ", status="
            << tinyimx::
                SetOfflineIfMatchStatusToString(
                    missing_status_result.status
                )
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto missing_refresh_result =
        online_cache.RefreshOnlineIfMatch(
            user_id,
            gateway_id,
            new_connection_name,
            120
        );

    std::cout
        << "refresh_online_missing_status = "
        << tinyimx::
            RefreshOnlineIfMatchStatusToString(
                missing_refresh_result.status
            )
        << '\n';

    if (missing_refresh_result.status !=
        tinyimx::
            RefreshOnlineIfMatchStatus::
                kNotFound) {
        std::cerr
            << "expected missing refresh "
            << "not_found\n";

        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }
   const auto not_found_result =
        online_cache.GetOnlineStatus(
            user_id
        );

    std::cout
        << "get_online_not_found_status = "
        << tinyimx::
            GetOnlineStatusStatusToString(
                not_found_result.status
            )
        << '\n';

    if (!not_found_result.NotFound()) {
        std::cerr
            << "expected online status "
            << "not_found"
            << ", actual="
            << tinyimx::
                GetOnlineStatusStatusToString(
                    not_found_result.status
                )
            << ", error="
            << not_found_result.error_message
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    std::cout
        << "is_online_after_offline = "
        << not_found_result.Found()
        << '\n';

    {
    auto connection =
        redis_pool.Acquire();

    if (!connection) {
        std::cerr
            << "acquire redis connection "
            << "for invalid record failed\n";

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const std::string key =
        "tinyimx:online:" +
        std::to_string(user_id);

    if (!connection->Set(
            key,
            "{invalid-json"
        )) {
        std::cerr
            << "write invalid online "
            << "record failed: "
            << connection->LastError()
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }
}

    const auto invalid_refresh_result =
        online_cache.RefreshOnlineIfMatch(
            user_id,
            gateway_id,
            new_connection_name,
            120
        );

    std::cout
        << "refresh_online_invalid_record_status = "
        << tinyimx::
            RefreshOnlineIfMatchStatusToString(
                invalid_refresh_result.status
            )
        << '\n';

    if (invalid_refresh_result.status !=
        tinyimx::
            RefreshOnlineIfMatchStatus::
                kInvalidRecord) {
        std::cerr
            << "expected invalid record "
            << "refresh status\n";

        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }
    const auto invalid_record_result =
        online_cache.GetOnlineStatus(
            user_id
        );

    std::cout
        << "get_online_invalid_record_status = "
        << tinyimx::
            GetOnlineStatusStatusToString(
                invalid_record_result.status
            )
        << '\n';

    if (invalid_record_result.status !=
        tinyimx::GetOnlineStatusStatus::
            kInvalidRecord) {
        std::cerr
            << "expected invalid_record"
            << ", actual="
            << tinyimx::
                GetOnlineStatusStatusToString(
                    invalid_record_result.status
                )
            << ", error="
            << invalid_record_result.
                error_message
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    {
        auto connection =
            redis_pool.Acquire();

        if (!connection ||
            !connection->Del(
                "tinyimx:online:" +
                std::to_string(user_id)
            )) {
            std::cerr
                << "cleanup invalid online "
                << "record failed\n";

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }
    }

    const auto invalid_argument_result =
        online_cache.GetOnlineStatus(0);

    std::cout
        << "get_online_invalid_argument_status = "
        << tinyimx::
            GetOnlineStatusStatusToString(
                invalid_argument_result.status
            )
        << '\n';

    if (invalid_argument_result.status !=
        tinyimx::GetOnlineStatusStatus::
            kInvalidArgument) {
        std::cerr
            << "expected invalid_argument"
            << ", actual="
            << tinyimx::
                GetOnlineStatusStatusToString(
                    invalid_argument_result.status
                )
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    tinyimx::OnlineStatusCache
        unavailable_online_cache(
            nullptr
        );

    const auto refresh_redis_error_result =
        unavailable_online_cache.
            RefreshOnlineIfMatch(
                user_id,
                gateway_id,
                connection_name,
                120
            );

    std::cout
        << "refresh_online_redis_error_status = "
        << tinyimx::
            RefreshOnlineIfMatchStatusToString(
                refresh_redis_error_result.status
            )
        << '\n';

    if (refresh_redis_error_result.status !=
        tinyimx::
            RefreshOnlineIfMatchStatus::
                kRedisError) {
        std::cerr
            << "expected refresh redis_error\n";

        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto set_redis_error_result =
        unavailable_online_cache.SetOnline(
            user_id,
            gateway_id,
            connection_name,
            60
        );

    std::cout
        << "set_online_redis_error_status = "
        << tinyimx::
            SetOnlineStatusToString(
                set_redis_error_result.status
            )
        << '\n';

    if (set_redis_error_result.status !=
        tinyimx::SetOnlineStatus::
            kRedisError) {
        std::cerr
            << "expected set online "
            << "redis_error"
            << ", actual="
            << tinyimx::
                SetOnlineStatusToString(
                    set_redis_error_result.status
                )
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto redis_error_result =
        unavailable_online_cache.
            GetOnlineStatus(
                user_id
            );

    std::cout
        << "get_online_redis_error_status = "
        << tinyimx::
            GetOnlineStatusStatusToString(
                redis_error_result.status
            )
        << '\n';

    if (redis_error_result.status !=
        tinyimx::GetOnlineStatusStatus::
            kRedisError) {
        std::cerr
            << "expected redis_error"
            << ", actual="
            << tinyimx::
                GetOnlineStatusStatusToString(
                    redis_error_result.status
                )
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }

    const auto invalid_set_result =
        online_cache.SetOnline(
            0,
            gateway_id,
            connection_name,
            60
        );

    std::cout
        << "set_online_invalid_argument_status = "
        << tinyimx::
            SetOnlineStatusToString(
                invalid_set_result.status
            )
        << '\n';

    if (invalid_set_result.status !=
        tinyimx::SetOnlineStatus::
            kInvalidArgument) {
        std::cerr
            << "expected set online "
            << "invalid_argument"
            << ", actual="
            << tinyimx::
                SetOnlineStatusToString(
                    invalid_set_result.status
                )
            << '\n';

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 1;
    }



    redis_pool.Shutdown();

    std::cout
        << "Online status cache "
        << "demo finished\n";

    std::cout
        << "=========================="
        << "==================\n";

    tinyimx::Logger::
        Instance().
        Shutdown();

    return 0;
}