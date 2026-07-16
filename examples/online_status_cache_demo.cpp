#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/cache/OnlineStatusCache.h"

#include <iostream>
#include <string>

namespace {

void PrintOnlineStatus(const tinyimx::OnlineStatusRecord& record) {
    std::cout << "online_status:"
              << " user_id=" << record.user_id
              << " gateway_id=" << record.gateway_id
              << " connection_name=" << record.connection_name
              << " login_time=" << record.login_time
              << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;

    if (!config.LoadFromFile(config_path)) {
        std::cerr << "load config failed: "
                  << config.LastError() << '\n';
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "logger init failed\n";
        return 1;
    }

    if (!config.Redis().enable) {
        std::cerr << "redis is disabled in config\n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::RedisConnectionPool redis_pool;

    if (!redis_pool.Initialize(config.Redis())) {
        std::cerr << "redis pool init failed\n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::OnlineStatusCache online_cache(&redis_pool);

    std::cout << "========== Online Status Cache Demo ==========\n";

    const std::uint64_t user_id = 10001;
    const std::string gateway_id = "gateway-demo-1";
    const std::string connection_name =
        "tinyimx-gateway-conn-demo-10001";

    if (!online_cache.SetOnline(
            user_id,
            gateway_id,
            connection_name,
            60)) {
        std::cerr << "set online failed: "
                  << online_cache.LastError() << '\n';
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const auto status = online_cache.GetOnlineStatus(user_id);
    if (!status.has_value()) {
        std::cerr << "get online status failed: "
                  << online_cache.LastError() << '\n';
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    PrintOnlineStatus(status.value());

    std::cout << "is_online = "
              << online_cache.IsOnline(user_id) << '\n';

    if (!online_cache.RefreshOnline(user_id, 120)) {
        std::cerr << "refresh online failed: "
                  << online_cache.LastError() << '\n';
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!online_cache.SetOffline(user_id)) {
        std::cerr << "set offline failed: "
                  << online_cache.LastError() << '\n';
        redis_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    std::cout << "is_online_after_offline = "
              << online_cache.IsOnline(user_id) << '\n';

    redis_pool.Shutdown();

    std::cout << "Online status cache demo finished\n";
    std::cout << "============================================\n";

    tinyimx::Logger::Instance().Shutdown();

    return 0;
}