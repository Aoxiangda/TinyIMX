#pragma once

#include <cstddef>
#include <string>

namespace tinyimx {

enum class QueueFullPolicy {
    kBlock,
    kDiscard,
    kOverwrite
};

struct AppConfig {
    std::string name{"TinyIMX-Gateway"};
    std::string env{"dev"};

    std::string instance_id{"tinyimx-gateway-1"};
};

struct ServerConfig {
    std::string host{"0.0.0.0"};
    int port{9000};
    int backlog{128};
    int io_thread_count{2};
};

struct LoggerConfig {
    std::string level{"info"};
    bool console{true};
    std::string file{"logs/tinyimx.log"};

    int max_file_size_mb{100};
    int max_backup_files{5};

    bool async{false};
    bool flush_each_log{false};
};

struct ThreadPoolConfig {
    std::size_t worker_threads{4};
    std::size_t queue_capacity{1024};
    bool enable_dynamic_resize{false};
    std::size_t min_threads{2};
    std::size_t max_threads{8};
    QueueFullPolicy queue_full_policy{QueueFullPolicy::kBlock};

    double scale_up_threshold{0.75};
    double scale_down_threshold{0.20};

    int manager_thread_interval_ms{1000};

    int scale_cooldown_ms{3000};

    int worker_idle_timeout_ms{5000};
};

struct ProtocolConfig {
    std::size_t max_body_size{1024 * 1024};
    int heartbeat_interval_sec{30};
    int heartbeat_timeout_sec{90};
};

struct RpcConfig {
    bool enable{false};
    int connect_timeout_ms{1000};
    int request_timeout_ms{3000};
    std::string compress{"none"};
    bool encrypt{false};
};

struct MySqlConfig {
    bool enable{false};
    std::string host{"127.0.0.1"};
    int port{3306};
    std::string database{"tinyimx"};
    std::string user{"root"};
    std::string password{""};
    int pool_size{4};
};

struct RedisConfig {
    bool enable{false};
    std::string host{"127.0.0.1"};
    int port{6379};
    std::string password{""};
    int db{0};
    int pool_size{4};
};

struct GatewayRegistryConfig {
    bool enable{false};
    std::string advertise_host;
    int lease_ttl_seconds{15};
    int heartbeat_interval_seconds{5};
    int discovery_refresh_interval_seconds{3};
};

struct McpConfig {
    bool enable{false};
    std::string endpoint{"http://127.0.0.1:8080"};
    int timeout_ms{5000};
};

}  // namespace tinyimx