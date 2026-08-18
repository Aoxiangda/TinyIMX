#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/registry/GatewayRegistry.h"
#include "services/registry/GatewayDiscovery.h"
#include "services/cache/OnlineStatusCache.h"
#include "gateway/GatewayRouteResolver.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace {

constexpr const char*
    kDemoKeyPrefix =
        "tinyimx:demo:gateway:registry:";

constexpr const char*
    kGatewayId =
        "gateway-registry-demo";

constexpr const char*
    kLeaseTokenA =
        "lease-token-a";

constexpr const char*
    kLeaseTokenB =
        "lease-token-b";

constexpr const char*
    kListenHost =
        "127.0.0.1";

constexpr std::uint16_t
    kListenPort = 19001;


std::int64_t UnixNowSeconds() {
    const auto now =
        std::chrono::system_clock::now();

    return
        std::chrono::duration_cast<
            std::chrono::seconds
        >(
            now.time_since_epoch()
        ).count();
}


std::string BuildDemoKey() {
    return
        std::string(kDemoKeyPrefix) +
        kGatewayId;
}


bool CleanupDemoKey(
    tinyimx::RedisConnectionPool* pool
) {
    if (pool == nullptr) {
        return false;
    }

    auto connection =
        pool->Acquire();

    if (!connection) {
        std::cerr
            << "cleanup demo key failed: "
            << "acquire redis connection failed\n";

        return false;
    }

    if (!connection->Del(
            BuildDemoKey()
        )) {
        std::cerr
            << "cleanup demo key failed"
            << ", error="
            << connection->LastError()
            << '\n';

        return false;
    }

    return true;
}


tinyimx::GatewayInstanceRecord
MakeRecord(
    const std::string& lease_token
) {
    tinyimx::GatewayInstanceRecord
        record;

    record.gateway_id =
        kGatewayId;

    record.lease_token =
        lease_token;

    record.listen_host =
        kListenHost;

    record.listen_port =
        kListenPort;

    record.started_at =
        UnixNowSeconds();

    return record;
}


void PrintRecord(
    const tinyimx::GatewayInstanceRecord&
        record
) {
    std::cout
        << "gateway_record:"
        << " gateway_id="
        << record.gateway_id
        << " lease_token="
        << record.lease_token
        << " listen="
        << record.listen_host
        << ':'
        << record.listen_port
        << " started_at="
        << record.started_at
        << '\n';
}


bool ExpectRegisterStatus(
    const std::string& name,
    const tinyimx::
        RegisterGatewayResult& result,
    tinyimx::RegisterGatewayStatus
        expected
) {
    std::cout
        << name
        << " = "
        << tinyimx::
            RegisterGatewayStatusToString(
                result.status
            )
        << '\n';

    if (result.status != expected) {
        std::cerr
            << name
            << " expected="
            << tinyimx::
                RegisterGatewayStatusToString(
                    expected
                )
            << " actual="
            << tinyimx::
                RegisterGatewayStatusToString(
                    result.status
                )
            << " error="
            << result.error_message
            << '\n';

        return false;
    }

    return true;
}


bool ExpectRefreshStatus(
    const std::string& name,
    const tinyimx::
        RefreshGatewayLeaseResult& result,
    tinyimx::RefreshGatewayLeaseStatus
        expected
) {
    std::cout
        << name
        << " = "
        << tinyimx::
            RefreshGatewayLeaseStatusToString(
                result.status
            )
        << '\n';

    if (result.status != expected) {
        std::cerr
            << name
            << " expected="
            << tinyimx::
                RefreshGatewayLeaseStatusToString(
                    expected
                )
            << " actual="
            << tinyimx::
                RefreshGatewayLeaseStatusToString(
                    result.status
                )
            << " error="
            << result.error_message
            << '\n';

        return false;
    }

    return true;
}


bool ExpectUnregisterStatus(
    const std::string& name,
    const tinyimx::
        UnregisterGatewayResult& result,
    tinyimx::UnregisterGatewayStatus
        expected
) {
    std::cout
        << name
        << " = "
        << tinyimx::
            UnregisterGatewayStatusToString(
                result.status
            )
        << '\n';

    if (result.status != expected) {
        std::cerr
            << name
            << " expected="
            << tinyimx::
                UnregisterGatewayStatusToString(
                    expected
                )
            << " actual="
            << tinyimx::
                UnregisterGatewayStatusToString(
                    result.status
                )
            << " error="
            << result.error_message
            << '\n';

        return false;
    }

    return true;
}


bool ExpectGetStatus(
    const std::string& name,
    const tinyimx::GetGatewayResult& result,
    tinyimx::GetGatewayStatus expected
) {
    std::cout
        << name
        << " = "
        << tinyimx::
            GetGatewayStatusToString(
                result.status
            )
        << '\n';

    if (result.status != expected) {
        std::cerr
            << name
            << " expected="
            << tinyimx::
                GetGatewayStatusToString(
                    expected
                )
            << " actual="
            << tinyimx::
                GetGatewayStatusToString(
                    result.status
                )
            << " error="
            << result.error_message
            << '\n';

        return false;
    }

    return true;
}


bool ExpectLeaseToken(
    const std::string& name,
    const tinyimx::GetGatewayResult& result,
    const std::string& expected_token
) {
    if (!result.Found() ||
        !result.record.has_value()) {
        std::cerr
            << name
            << " expected a gateway record\n";

        return false;
    }

    PrintRecord(
        result.record.value()
    );

    if (result.record->
            lease_token !=
        expected_token) {
        std::cerr
            << name
            << " lease token mismatch"
            << ", expected="
            << expected_token
            << ", actual="
            << result.record->
                lease_token
            << '\n';

        return false;
    }

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
        config_path =
            argv[1];
    }

    const bool list_mode =
        argc >= 3 &&
        std::string(argv[2]) ==
            "--list";

    const bool resolve_mode =
        argc >= 4 &&
        std::string(argv[2]) ==
            "--resolve-user";

    std::uint64_t resolve_user_id = 0;

    if (resolve_mode) {
        try {
            resolve_user_id =
                std::stoull(argv[3]);
        } catch (...) {
            std::cerr
                << "invalid resolve user id\n";

            return 1;
        }
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

    if (list_mode) {
        tinyimx::GatewayRegistry
            registry(&redis_pool);

        const auto result =
            registry.ListActiveGateways();

        if (!result.Succeeded()) {
            std::cerr
                << "list active gateways failed"
                << ", error="
                << result.error_message
                << '\n';

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }

        std::cout
            << "active_gateway_count="
            << result.instances.size()
            << '\n';

        for (const auto& instance :
            result.instances) {
            std::cout
                << "gateway_id="
                << instance.gateway_id
                << ", address="
                << instance.listen_host
                << ':'
                << instance.listen_port
                << ", started_at="
                << instance.started_at
                << '\n';
        }

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return 0;
    }

    if (resolve_mode) {
        tinyimx::GatewayRegistry
            registry(&redis_pool);

        tinyimx::GatewayDiscovery
            discovery(
                &registry,
                config.GatewayRegistry().
                    discovery_refresh_interval_seconds
            );

        /*
        * Resolve demo 不需要启动后台线程，
        * 当前只主动刷新一次 snapshot。
        */
        if (!discovery.RefreshNow()) {
            std::cerr
                << "gateway discovery refresh failed\n";

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return 1;
        }

        tinyimx::OnlineStatusCache
            online_status_cache(
                &redis_pool
            );

        tinyimx::GatewayRouteResolver
            resolver(
                config.App().instance_id,
                &online_status_cache,
                &discovery
            );

        const auto route =
            resolver.Resolve(
                resolve_user_id
            );

        std::cout
            << "route_user_id="
            << resolve_user_id
            << '\n';

        std::cout
            << "route_status="
            << tinyimx::
                GatewayRouteStatusToString(
                    route.status
                )
            << '\n';

        if (route.online_status.has_value()) {
            std::cout
                << "online_gateway_id="
                << route.online_status->
                    gateway_id
                << '\n';

            std::cout
                << "connection_name="
                << route.online_status->
                    connection_name
                << '\n';
        }

        if (route.remote_gateway.has_value()) {
            std::cout
                << "remote_gateway_id="
                << route.remote_gateway->
                    gateway_id
                << '\n';

            std::cout
                << "remote_address="
                << route.remote_gateway->
                    listen_host
                << ':'
                << route.remote_gateway->
                    listen_port
                << '\n';
        }

        if (!route.error_message.empty()) {
            std::cout
                << "route_error="
                << route.error_message
                << '\n';
        }

        redis_pool.Shutdown();

        tinyimx::Logger::
            Instance().
            Shutdown();

        return route.Resolved()
            ? 0
            : 1;
    }

    const auto finish =
        [&redis_pool](int exit_code) {
            CleanupDemoKey(
                &redis_pool
            );

            redis_pool.Shutdown();

            tinyimx::Logger::
                Instance().
                Shutdown();

            return exit_code;
        };

    if (!CleanupDemoKey(
            &redis_pool
        )) {
        return finish(1);
    }

    tinyimx::GatewayRegistry
        registry(
            &redis_pool,
            kDemoKeyPrefix
        );

    std::cout
        << "========== "
        << "Gateway Registry Demo "
        << "==========\n";


    /*
     * Phase 1:
     * 基础所有权与 CAS 行为。
     */
    std::cout
        << "\n"
        << "----- Phase 1: "
        << "ownership and CAS -----\n";

    const auto record_a =
        MakeRecord(
            kLeaseTokenA
        );

    const auto record_b =
        MakeRecord(
            kLeaseTokenB
        );


    const auto register_a =
        registry.Register(
            record_a,
            10
        );

    if (!ExpectRegisterStatus(
            "register_a",
            register_a,
            tinyimx::
                RegisterGatewayStatus::
                    kRegistered
        )) {
        return finish(1);
    }


    const auto register_a_again =
        registry.Register(
            record_a,
            10
        );

    if (!ExpectRegisterStatus(
            "register_a_again",
            register_a_again,
            tinyimx::
                RegisterGatewayStatus::
                    kRenewed
        )) {
        return finish(1);
    }


    const auto register_b_conflict =
        registry.Register(
            record_b,
            10
        );

    if (!ExpectRegisterStatus(
            "register_b_conflict",
            register_b_conflict,
            tinyimx::
                RegisterGatewayStatus::
                    kConflict
        )) {
        return finish(1);
    }


    const auto get_after_register =
        registry.Get(
            kGatewayId
        );

    if (!ExpectGetStatus(
            "get_after_register",
            get_after_register,
            tinyimx::
                GetGatewayStatus::
                    kFound
        )) {
        return finish(1);
    }

    if (!ExpectLeaseToken(
            "get_after_register",
            get_after_register,
            kLeaseTokenA
        )) {
        return finish(1);
    }


    const auto refresh_b =
        registry.RefreshIfMatch(
            kGatewayId,
            kLeaseTokenB,
            10
        );

    if (!ExpectRefreshStatus(
            "refresh_b_wrong_owner",
            refresh_b,
            tinyimx::
                RefreshGatewayLeaseStatus::
                    kMismatch
        )) {
        return finish(1);
    }


    const auto refresh_a =
        registry.RefreshIfMatch(
            kGatewayId,
            kLeaseTokenA,
            10
        );

    if (!ExpectRefreshStatus(
            "refresh_a_current_owner",
            refresh_a,
            tinyimx::
                RefreshGatewayLeaseStatus::
                    kRefreshed
        )) {
        return finish(1);
    }


    const auto unregister_b =
        registry.UnregisterIfMatch(
            kGatewayId,
            kLeaseTokenB
        );

    if (!ExpectUnregisterStatus(
            "unregister_b_wrong_owner",
            unregister_b,
            tinyimx::
                UnregisterGatewayStatus::
                    kMismatch
        )) {
        return finish(1);
    }


    const auto get_after_wrong_delete =
        registry.Get(
            kGatewayId
        );

    if (!ExpectGetStatus(
            "get_after_wrong_delete",
            get_after_wrong_delete,
            tinyimx::
                GetGatewayStatus::
                    kFound
        )) {
        return finish(1);
    }

    if (!ExpectLeaseToken(
            "get_after_wrong_delete",
            get_after_wrong_delete,
            kLeaseTokenA
        )) {
        return finish(1);
    }


    const auto unregister_a =
        registry.UnregisterIfMatch(
            kGatewayId,
            kLeaseTokenA
        );

    if (!ExpectUnregisterStatus(
            "unregister_a_current_owner",
            unregister_a,
            tinyimx::
                UnregisterGatewayStatus::
                    kDeleted
        )) {
        return finish(1);
    }


    const auto get_after_delete =
        registry.Get(
            kGatewayId
        );

    if (!ExpectGetStatus(
            "get_after_delete",
            get_after_delete,
            tinyimx::
                GetGatewayStatus::
                    kNotFound
        )) {
        return finish(1);
    }


    /*
     * Phase 2:
     * 模拟旧 Gateway Lease 到期，
     * 新代际接管，
     * 旧进程恢复后不能续租、
     * 不能删除新进程。
     */
    std::cout
        << "\n"
        << "----- Phase 2: "
        << "lease expiry and takeover "
        << "-----\n";


    const auto old_owner_register =
        registry.Register(
            record_a,
            2
        );

    if (!ExpectRegisterStatus(
            "old_owner_register",
            old_owner_register,
            tinyimx::
                RegisterGatewayStatus::
                    kRegistered
        )) {
        return finish(1);
    }


    std::cout
        << "waiting for old lease "
        << "to expire...\n";

    std::this_thread::sleep_for(
        std::chrono::seconds(3)
    );


    const auto get_after_expiry =
        registry.Get(
            kGatewayId
        );

    if (!ExpectGetStatus(
            "get_after_expiry",
            get_after_expiry,
            tinyimx::
                GetGatewayStatus::
                    kNotFound
        )) {
        return finish(1);
    }


    const auto new_owner_register =
        registry.Register(
            record_b,
            10
        );

    if (!ExpectRegisterStatus(
            "new_owner_register",
            new_owner_register,
            tinyimx::
                RegisterGatewayStatus::
                    kRegistered
        )) {
        return finish(1);
    }


    const auto stale_owner_refresh =
        registry.RefreshIfMatch(
            kGatewayId,
            kLeaseTokenA,
            10
        );

    if (!ExpectRefreshStatus(
            "stale_owner_refresh",
            stale_owner_refresh,
            tinyimx::
                RefreshGatewayLeaseStatus::
                    kMismatch
        )) {
        return finish(1);
    }


    const auto stale_owner_unregister =
        registry.UnregisterIfMatch(
            kGatewayId,
            kLeaseTokenA
        );

    if (!ExpectUnregisterStatus(
            "stale_owner_unregister",
            stale_owner_unregister,
            tinyimx::
                UnregisterGatewayStatus::
                    kMismatch
        )) {
        return finish(1);
    }


    const auto get_new_owner =
        registry.Get(
            kGatewayId
        );

    if (!ExpectGetStatus(
            "get_new_owner",
            get_new_owner,
            tinyimx::
                GetGatewayStatus::
                    kFound
        )) {
        return finish(1);
    }

    if (!ExpectLeaseToken(
            "get_new_owner",
            get_new_owner,
            kLeaseTokenB
        )) {
        return finish(1);
    }


    const auto new_owner_unregister =
        registry.UnregisterIfMatch(
            kGatewayId,
            kLeaseTokenB
        );

    if (!ExpectUnregisterStatus(
            "new_owner_unregister",
            new_owner_unregister,
            tinyimx::
                UnregisterGatewayStatus::
                    kDeleted
        )) {
        return finish(1);
    }


    const auto final_get =
        registry.Get(
            kGatewayId
        );

    if (!ExpectGetStatus(
            "final_get",
            final_get,
            tinyimx::
                GetGatewayStatus::
                    kNotFound
        )) {
        return finish(1);
    }


    std::cout
        << "\n"
        << "gateway registry "
        << "integration validation passed\n";

    std::cout
        << "================================"
        << "==============\n";

    return finish(0);
}