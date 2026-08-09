#include "common/config/Config.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <nlohmann/json.hpp>

namespace tinyimx {

namespace {

using json = nlohmann::json;

bool IsValidLogLevel(const std::string& level) {
    return level == "trace" ||
           level == "debug" ||
           level == "info" ||
           level == "warn" ||
           level == "error" ||
           level == "fatal";
}

bool IsValidEnv(const std::string& env) {
    return env == "dev" || env == "test" || env == "prod";
}

bool IsValidCompressType(const std::string& compress) {
    return compress == "none" ||
           compress == "zstd" ||
           compress == "zlib";
}

template <typename T>
void ReadIfExists(const json& section, const char* key, T* output) {
    if (section.contains(key)) {
        *output = section.at(key).get<T>();
    }
}

}  // namespace

bool Config::LoadFromFile(const std::string& config_path) {
    Reset();
    config_path_ = config_path;

    std::ifstream input(config_path);
    if (!input.is_open()) {
        return SetError("failed to open config file: " + config_path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();

    return LoadFromString(buffer.str(), config_path);
}

bool Config::LoadFromString(const std::string& json_content,
                            const std::string& source_name) {
    Reset();
    config_path_ = source_name;

    if (!ApplyJsonConfig(json_content)) {
        return false;
    }

    if (!Validate()) {
        return false;
    }

    loaded_ = true;
    return true;
}

bool Config::IsLoaded() const {
    return loaded_;
}

const std::string& Config::LastError() const {
    return last_error_;
}

const std::string& Config::ConfigPath() const {
    return config_path_;
}

const AppConfig& Config::App() const {
    return app_;
}

const ServerConfig& Config::Server() const {
    return server_;
}

const LoggerConfig& Config::Logger() const {
    return logger_;
}

const ThreadPoolConfig& Config::ThreadPool() const {
    return thread_pool_;
}

const ProtocolConfig& Config::Protocol() const {
    return protocol_;
}

const RpcConfig& Config::Rpc() const {
    return rpc_;
}

const MySqlConfig& Config::MySql() const {
    return mysql_;
}

const RedisConfig& Config::Redis() const {
    return redis_;
}

const McpConfig& Config::Mcp() const {
    return mcp_;
}

std::string Config::ServerName() const {
    return app_.name;
}

std::string Config::ServerHost() const {
    return server_.host;
}

uint16_t Config::ServerPort() const {
    return static_cast<uint16_t>(server_.port);
}

std::string Config::LogLevel() const {
    return logger_.level;
}

std::string Config::LogFile() const {
    return logger_.file;
}

bool Config::LogConsole() const {
    return logger_.console;
}

int Config::ThreadPoolWorkerThreads() const {
    return static_cast<int>(thread_pool_.worker_threads);
}

int Config::ThreadPoolQueueCapacity() const {
    return static_cast<int>(thread_pool_.queue_capacity);
}

void Config::Reset() {
    loaded_ = false;
    last_error_.clear();

    app_ = AppConfig{};
    server_ = ServerConfig{};
    logger_ = LoggerConfig{};
    thread_pool_ = ThreadPoolConfig{};
    protocol_ = ProtocolConfig{};
    rpc_ = RpcConfig{};
    mysql_ = MySqlConfig{};
    redis_ = RedisConfig{};
    mcp_ = McpConfig{};
}

bool Config::ApplyJsonConfig(const std::string& json_content) {
    try {
        json root = json::parse(json_content);

        if (root.contains("app")) {
            const auto& section = root.at("app");
            ReadIfExists(section, "name", &app_.name);
            ReadIfExists(section, "env", &app_.env);
        }

        if (root.contains("server")) {
            const auto& section = root.at("server");

            ReadIfExists(section, "host", &server_.host);
            ReadIfExists(section, "port", &server_.port);
            ReadIfExists(section, "backlog", &server_.backlog);
            ReadIfExists(section, "io_thread_count", &server_.io_thread_count);
        }

        if (root.contains("logger")) {
            const auto& section = root.at("logger");
            ReadIfExists(section, "level", &logger_.level);
            ReadIfExists(section, "console", &logger_.console);
            ReadIfExists(section, "file", &logger_.file);
            ReadIfExists(section, "max_file_size_mb", &logger_.max_file_size_mb);
            ReadIfExists(section, "max_backup_files", &logger_.max_backup_files);
            ReadIfExists(section, "async", &logger_.async);
            ReadIfExists(section, "flush_each_log", &logger_.flush_each_log);
        }

        if (root.contains("thread_pool")) {
            const auto& section = root.at("thread_pool");

            ReadIfExists(section, "worker_threads", &thread_pool_.worker_threads);
            ReadIfExists(section, "queue_capacity", &thread_pool_.queue_capacity);
            ReadIfExists(section, "enable_dynamic_resize",
                         &thread_pool_.enable_dynamic_resize);
            ReadIfExists(section, "min_threads", &thread_pool_.min_threads);
            ReadIfExists(section, "max_threads", &thread_pool_.max_threads);

            std::string policy;
            ReadIfExists(section, "queue_full_policy", &policy);
            if (!policy.empty()) { // 读取成功才解析
                thread_pool_.queue_full_policy = ParseQueueFullPolicy(policy);
            }
            ReadIfExists(section, "scale_up_threshold",
                         &thread_pool_.scale_up_threshold);
            ReadIfExists(section, "scale_down_threshold",
                         &thread_pool_.scale_down_threshold);
            ReadIfExists(section, "manager_thread_interval_ms",
                         &thread_pool_.manager_thread_interval_ms);
            ReadIfExists(section, "scale_cooldown_ms",
                            &thread_pool_.scale_cooldown_ms);
            ReadIfExists(section, "worker_idle_timeout_ms",
                            &thread_pool_.worker_idle_timeout_ms);

        }

        if (root.contains("protocol")) {
            const auto& section = root.at("protocol");
            ReadIfExists(section, "max_body_size", &protocol_.max_body_size);
            ReadIfExists(section, "heartbeat_interval_sec",
                         &protocol_.heartbeat_interval_sec);
            ReadIfExists(section, "heartbeat_timeout_sec",
                         &protocol_.heartbeat_timeout_sec);
        }

        if (root.contains("rpc")) {
            const auto& section = root.at("rpc");
            ReadIfExists(section, "enable", &rpc_.enable);
            ReadIfExists(section, "connect_timeout_ms", &rpc_.connect_timeout_ms);
            ReadIfExists(section, "request_timeout_ms", &rpc_.request_timeout_ms);
            ReadIfExists(section, "compress", &rpc_.compress);
            ReadIfExists(section, "encrypt", &rpc_.encrypt);
        }

        if (root.contains("mysql")) {
            const auto& section = root.at("mysql");
            ReadIfExists(section, "enable", &mysql_.enable);
            ReadIfExists(section, "host", &mysql_.host);
            ReadIfExists(section, "port", &mysql_.port);
            ReadIfExists(section, "database", &mysql_.database);
            ReadIfExists(section, "user", &mysql_.user);
            ReadIfExists(section, "password", &mysql_.password);
            ReadIfExists(section, "pool_size", &mysql_.pool_size);
        }

        if (root.contains("redis")) {
            const auto& section = root.at("redis");
            ReadIfExists(section, "enable", &redis_.enable);
            ReadIfExists(section, "host", &redis_.host);
            ReadIfExists(section, "port", &redis_.port);
            ReadIfExists(section, "password", &redis_.password);
            ReadIfExists(section, "db", &redis_.db);
            ReadIfExists(section, "pool_size", &redis_.pool_size);
        }

        if (root.contains("mcp")) {
            const auto& section = root.at("mcp");
            ReadIfExists(section, "enable", &mcp_.enable);
            ReadIfExists(section, "endpoint", &mcp_.endpoint);
            ReadIfExists(section, "timeout_ms", &mcp_.timeout_ms);
        }

        return true;


    } catch (const json::parse_error& e) {
        return SetError("json parse error: " + std::string(e.what()));
    } catch (const json::type_error& e) {
        return SetError("json type error: " + std::string(e.what()));
    } catch (const json::out_of_range& e) {
        return SetError("json missing field error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return SetError("config load error: " + std::string(e.what()));
    }
}

bool Config::Validate() {
    if (app_.name.empty()) {
        return SetError("app.name cannot be empty");
    }

    if (!IsValidEnv(app_.env)) {
        return SetError("app.env must be one of: dev, test, prod");
    }

    if (server_.host.empty()) {
        return SetError("server.host cannot be empty");
    }

    if (server_.port <= 0 || server_.port > 65535) {
        return SetError("server.port must be in range 1-65535");
    }

    if (server_.backlog <= 0) {
        return SetError("server.backlog must be greater than 0");
    }

    if (server_.io_thread_count < 0) {
        return SetError("server.io_thread_count cannot be negative");
    }

    if (!IsValidLogLevel(logger_.level)) {
        return SetError("logger.level must be one of: trace, debug, info, warn, error, fatal");
    }

    if (logger_.file.empty()) {
        return SetError("logger.file cannot be empty");
    }

    if (logger_.max_file_size_mb <= 0) {
        return SetError("logger.max_file_size_mb must be greater than 0");
    }

    if (logger_.max_backup_files < 0) {
        return SetError("logger.max_backup_files cannot be negative");
    }

    if (thread_pool_.worker_threads == 0) {
        return SetError("thread_pool.worker_threads must be greater than 0");
    }

    if (thread_pool_.queue_capacity == 0) {
        return SetError("thread_pool.queue_capacity must be greater than 0");
    }

    if (thread_pool_.min_threads == 0) {
        return SetError("thread_pool.min_threads must be greater than 0");
    }

    if (thread_pool_.max_threads == 0) {
        return SetError("thread_pool.max_threads must be greater than 0");
    }

    if (thread_pool_.max_threads < thread_pool_.min_threads) {
        return SetError(
            "thread_pool.max_threads must be greater than or equal to min_threads"
        );
    }

    if (thread_pool_.enable_dynamic_resize) {
        if (thread_pool_.worker_threads < thread_pool_.min_threads ||
            thread_pool_.worker_threads > thread_pool_.max_threads) {
            return SetError(
                "thread_pool.worker_threads must be between min_threads and max_threads when dynamic resize is enabled"
            );
        }
    }

    if (thread_pool_.scale_up_threshold <= 0.0 ||
        thread_pool_.scale_up_threshold > 1.0) {
        return SetError("thread_pool.scale_up_threshold must be in (0, 1]");
    }

    if (thread_pool_.scale_down_threshold < 0.0 ||
        thread_pool_.scale_down_threshold >= 1.0) {
        return SetError("thread_pool.scale_down_threshold must be in [0, 1)");
    }

    if (thread_pool_.scale_down_threshold >=
        thread_pool_.scale_up_threshold) {
        return SetError(
            "thread_pool.scale_down_threshold must be less than scale_up_threshold"
        );
    }

    if (thread_pool_.manager_thread_interval_ms <= 0) {
        return SetError(
            "thread_pool.manager_check_interval_ms must be greater than 0"
        );
    }

    if (thread_pool_.scale_cooldown_ms < 0) {
        return SetError("thread_pool.scale_cooldown_ms cannot be negative");
    }

    if (thread_pool_.worker_idle_timeout_ms <= 0) {
        return SetError(
            "thread_pool.worker_idle_timeout_ms must be greater than 0"
        );
    }

    if (protocol_.max_body_size == 0) {
        return SetError("protocol.max_body_size must be greater than 0");
    }

    if (protocol_.heartbeat_interval_sec <= 0) {
        return SetError("protocol.heartbeat_interval_sec must be greater than 0");
    }

    if (protocol_.heartbeat_timeout_sec <= protocol_.heartbeat_interval_sec) {
        return SetError("protocol.heartbeat_timeout_sec must be greater than heartbeat_interval_sec");
    }

    if (rpc_.connect_timeout_ms <= 0) {
        return SetError("rpc.connect_timeout_ms must be greater than 0");
    }

    if (rpc_.request_timeout_ms <= 0) {
        return SetError("rpc.request_timeout_ms must be greater than 0");
    }

    if (!IsValidCompressType(rpc_.compress)) {
        return SetError("rpc.compress must be one of: none, zstd, zlib");
    }

    if (mysql_.enable) {
        if (mysql_.host.empty()) {
            return SetError("mysql.host cannot be empty when mysql.enable=true");
        }

        if (mysql_.port <= 0 || mysql_.port > 65535) {
            return SetError("mysql.port must be in range 1-65535");
        }

        if (mysql_.database.empty()) {
            return SetError("mysql.database cannot be empty when mysql.enable=true");
        }

        if (mysql_.user.empty()) {
            return SetError("mysql.user cannot be empty when mysql.enable=true");
        }

        if (mysql_.pool_size <= 0) {
            return SetError("mysql.pool_size must be greater than 0");
        }
    }

    if (redis_.enable) {
        if (redis_.host.empty()) {
            return SetError("redis.host cannot be empty when redis.enable=true");
        }

        if (redis_.port <= 0 || redis_.port > 65535) {
            return SetError("redis.port must be in range 1-65535");
        }

        if (redis_.db < 0) {
            return SetError("redis.db cannot be negative");
        }

        if (redis_.pool_size <= 0) {
            return SetError("redis.pool_size must be greater than 0");
        }
    }

    if (mcp_.enable) {
        if (mcp_.endpoint.empty()) {
            return SetError("mcp.endpoint cannot be empty when mcp.enable=true");
        }

        if (mcp_.timeout_ms <= 0) {
            return SetError("mcp.timeout_ms must be greater than 0");
        }
    }

    unsigned int hardware_threads = std::thread::hardware_concurrency();
    if (hardware_threads > 0 &&
        thread_pool_.worker_threads > static_cast<std::size_t>(hardware_threads * 4)) {
        return SetError("thread_pool.worker_threads is too large for current hardware");
    }
    if (hardware_threads > 0 && server_.io_thread_count >
            static_cast<int>( hardware_threads * 4)) {
        return SetError("server.io_thread_count is too large for current hardware");
    }
    return true;
}

bool Config::SetError(const std::string& message) {
    last_error_ = message;
    loaded_ = false;
    return false;
}

QueueFullPolicy Config::ParseQueueFullPolicy(const std::string& policy) {
    if (policy == "block" || policy == "BLOCK") {
        return QueueFullPolicy::kBlock;
    }

    if (policy == "discard" || policy == "DISCARD") {
        return QueueFullPolicy::kDiscard;
    }

    if (policy == "overwrite" || policy == "OVERWRITE") {
        return QueueFullPolicy::kOverwrite;
    }

    throw std::invalid_argument(
        "invalid queue_full_policy: " + policy +
        ", valid values: block, discard, overwrite"
    );
}

std::string Config::QueueFullPolicyToString(QueueFullPolicy policy) {
    switch (policy) {
        case QueueFullPolicy::kBlock:
            return "block";
        case QueueFullPolicy::kDiscard:
            return "discard";
        case QueueFullPolicy::kOverwrite:
            return "overwrite";
        default:
            return "unknown";
    }
}

}  // namespace tinyimx