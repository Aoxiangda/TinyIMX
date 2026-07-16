#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"

#include <iostream>
#include <string>

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

    tinyimx::RedisConnectionPool pool;

    if (!pool.Initialize(config.Redis())) {
        std::cerr << "redis pool init failed\n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    std::cout << "========== Redis Pool Demo ==========\n";
    std::cout << "pool_size = " << pool.Size() << '\n';
    std::cout << "available = " << pool.AvailableCount() << '\n';

    {
        auto connection = pool.Acquire();

        if (!connection) {
            std::cerr << "acquire redis connection failed\n";
            pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        if (!connection->Ping()) {
            std::cerr << "redis ping failed: "
                      << connection->LastError() << '\n';
            pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        const std::string key = "tinyimx:demo:hello";
        const std::string value = "hello redis";

        if (!connection->Set(key, value)) {
            std::cerr << "redis set failed: "
                      << connection->LastError() << '\n';
            return 1;
        }

        const auto read_value = connection->Get(key);
        if (!read_value.has_value()) {
            std::cerr << "redis get failed: "
                      << connection->LastError() << '\n';
            return 1;
        }

        std::cout << "get " << key
                  << " = " << read_value.value() << '\n';

        const auto counter =
            connection->Incr("tinyimx:demo:counter");

        if (counter.has_value()) {
            std::cout << "counter = "
                      << counter.value() << '\n';
        }

        connection->Expire("tinyimx:demo:counter", 60);
        connection->Del(key);
    }

    std::cout << "available_after_release = "
              << pool.AvailableCount() << '\n';

    pool.Shutdown();

    std::cout << "Redis pool demo finished\n";
    std::cout << "=====================================\n";

    tinyimx::Logger::Instance().Shutdown();

    return 0;
}