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

struct BusinessRuntimeConfig {
    // Fixed worker count for blocking business work.
    std::size_t worker_threads{4};

    // Global accepted task lifecycle capacity.
    std::size_t max_pending_tasks{128};

    // Fixed ordering stripe count.
    std::size_t stripe_count{64};

    // Per-stripe backlog bound for hot-key isolation.
    std::size_t per_stripe_queue_capacity{32};

    int default_deadline_ms{3000};
    int shutdown_timeout_ms{30000};
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

struct RocketMQConfig {
    bool enable{false};
    std::string endpoint{"127.0.0.1:8081"};
    std::string message_topic{"tinyimx-message-events"};
    int request_timeout_ms{3000};
    bool tls{false};
    std::string access_key;
    std::string access_secret;
};

struct OutboxRelayConfig {
    bool enable{false};
    std::string instance_id{"outbox-relay-1"};
    std::size_t batch_size{32};
    std::size_t worker_threads{4};
    std::size_t max_inflight{128};
    int poll_interval_ms{100};
    int lease_ms{30000};
    int lease_renew_interval_ms{5000};
    int retry_base_ms{200};
    int retry_max_ms{30000};
    int published_retention_hours{168};
    int cleanup_interval_ms{60000};
    std::size_t cleanup_batch_size{1000};
    int shutdown_timeout_ms{10000};
};

struct UnreadProjectionConfig {
    bool enable{false};
    std::string owner{"gateway"};
    bool shadow_mode{false};
    std::string consumer_group{"tinyimx-unread-projector-v1"};
    std::size_t batch_size{16};
    int invisible_duration_ms{30000};
    int await_duration_ms{5000};
    int receive_error_backoff_ms{500};
};

struct GatewayRegistryConfig {
    bool enable{false};
    std::string advertise_host;
    int lease_ttl_seconds{15};
    int heartbeat_interval_seconds{5};
    int discovery_refresh_interval_seconds{3};
};

struct ZooKeeperConfig {
    bool enable{false};
    std::string connect_string{"127.0.0.1:2181"};
    int session_timeout_ms{10000};
    int connect_timeout_ms{5000};
    std::string service_root{"/tinyimx/services"};
    std::string advertise_host{"127.0.0.1"};
    std::string service_version{"v1"};
};

struct ServiceDiscoveryConfig {
    std::string provider{"static"};
    int initial_sync_timeout_ms{5000};
    int snapshot_stale_after_ms{30000};
    bool retain_last_known_good{true};
};

struct McpConfig {
    bool enable{false};
    std::string endpoint{"http://127.0.0.1:8080"};
    int timeout_ms{5000};
};

}  // namespace tinyimx