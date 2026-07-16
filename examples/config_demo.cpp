#include "common/config/Config.h"

#include <iostream>
#include <string>

namespace {

void PrintConfig(const tinyimx::Config& config) {

    const auto& app = config.App();
    const auto& server = config.Server();
    const auto& logger = config.Logger();
    const auto& thread_pool = config.ThreadPool();
    const auto& protocol = config.Protocol();
    const auto& rpc = config.Rpc();
    const auto& mysql = config.MySql();
    const auto& redis = config.Redis();
    const auto& mcp = config.Mcp();

    std::cout << "[ConfigDemo] app.name=" << app.name << '\n';
    std::cout << "[ConfigDemo] app.env=" << app.env << '\n';

    std::cout << "[ConfigDemo] server.host=" << server.host << '\n';
    std::cout << "[ConfigDemo] server.port=" << server.port << '\n';
    std::cout << "[ConfigDemo] server.backlog=" << server.backlog << '\n';

    std::cout << "[ConfigDemo] logger.level=" << logger.level << '\n';
    std::cout << "[ConfigDemo] logger.console="
              << (logger.console ? "true" : "false") << '\n';
    std::cout << "[ConfigDemo] logger.file=" << logger.file << '\n';
    std::cout << "[ConfigDemo] logger.max_file_size_mb="
              << logger.max_file_size_mb << '\n';
    std::cout << "[ConfigDemo] logger.async="
              << (logger.async ? "true" : "false") << '\n';

    std::cout << "[ConfigDemo] thread_pool.worker_threads="
              << thread_pool.worker_threads << '\n';
    std::cout << "[ConfigDemo] thread_pool.queue_capacity="
              << thread_pool.queue_capacity << '\n';
    std::cout << "[ConfigDemo] thread_pool.enable_dynamic_resize="
              << (thread_pool.enable_dynamic_resize ? "true" : "false") << '\n';
    std::cout << "[ConfigDemo] thread_pool.min_threads="
              << thread_pool.min_threads << '\n';
    std::cout << "[ConfigDemo] thread_pool.max_threads="
              << thread_pool.max_threads << '\n';
    std::cout << "[ConfigDemo] thread_pool.enable_dynamic_resize="
            << (thread_pool.enable_dynamic_resize ? "true" : "false")
            << '\n';

    std::cout << "[ConfigDemo] thread_pool.min_threads="
            << thread_pool.min_threads << '\n';

    std::cout << "[ConfigDemo] thread_pool.max_threads="
            << thread_pool.max_threads << '\n';

    std::cout << "[ConfigDemo] thread_pool.scale_up_threshold="
            << thread_pool.scale_up_threshold << '\n';

    std::cout << "[ConfigDemo] thread_pool.scale_down_threshold="
            << thread_pool.scale_down_threshold << '\n';

    std::cout << "[ConfigDemo] thread_pool.manager_thread_interval_ms="
            << thread_pool.manager_thread_interval_ms << '\n';

    std::cout << "[ConfigDemo] thread_pool.scale_cooldown_ms="
            << thread_pool.scale_cooldown_ms << '\n';

    std::cout << "[ConfigDemo] thread_pool.worker_idle_timeout_ms="
            << thread_pool.worker_idle_timeout_ms << '\n';
    std::cout << "[ConfigDemo] protocol.max_body_size="
              << protocol.max_body_size << '\n';
    std::cout << "[ConfigDemo] protocol.heartbeat_interval_sec="
              << protocol.heartbeat_interval_sec << '\n';
    std::cout << "[ConfigDemo] protocol.heartbeat_timeout_sec="
              << protocol.heartbeat_timeout_sec << '\n';

    std::cout << "[ConfigDemo] rpc.enable="
              << (rpc.enable ? "true" : "false") << '\n';
    std::cout << "[ConfigDemo] rpc.connect_timeout_ms="
              << rpc.connect_timeout_ms << '\n';
    std::cout << "[ConfigDemo] rpc.request_timeout_ms="
              << rpc.request_timeout_ms << '\n';
    std::cout << "[ConfigDemo] rpc.compress=" << rpc.compress << '\n';
    std::cout << "[ConfigDemo] rpc.encrypt="
              << (rpc.encrypt ? "true" : "false") << '\n';

    std::cout << "[ConfigDemo] mysql.enable="
              << (mysql.enable ? "true" : "false") << '\n';
    std::cout << "[ConfigDemo] mysql.host=" << mysql.host << '\n';
    std::cout << "[ConfigDemo] mysql.port=" << mysql.port << '\n';
    std::cout << "[ConfigDemo] mysql.database=" << mysql.database << '\n';
    std::cout << "[ConfigDemo] mysql.pool_size=" << mysql.pool_size << '\n';

    std::cout << "[ConfigDemo] redis.enable="
              << (redis.enable ? "true" : "false") << '\n';
    std::cout << "[ConfigDemo] redis.host=" << redis.host << '\n';
    std::cout << "[ConfigDemo] redis.port=" << redis.port << '\n';
    std::cout << "[ConfigDemo] redis.db=" << redis.db << '\n';
    std::cout << "[ConfigDemo] redis.pool_size=" << redis.pool_size << '\n';

    std::cout << "[ConfigDemo] mcp.enable="
              << (mcp.enable ? "true" : "false") << '\n';
    std::cout << "[ConfigDemo] mcp.endpoint=" << mcp.endpoint << '\n';
    std::cout << "[ConfigDemo] mcp.timeout_ms=" << mcp.timeout_ms << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "[ConfigDemo] load failed: "
                  << config.LastError() << std::endl;
        return 1;
    }

    PrintConfig(config);

    std::cout << "[ConfigDemo] config validation passed" << std::endl;
    return 0;
}