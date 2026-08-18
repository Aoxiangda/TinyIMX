#pragma once

#include "common/config/ConfigTypes.h"

#include <cstdint>
#include <string>

namespace tinyimx {

class Config {
public:
    Config() = default;
    ~Config() = default;

    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    bool LoadFromFile(const std::string& config_path);
    bool LoadFromString(
        const std::string& json_content,
        const std::string& source_name = "<string>"
    );

    bool IsLoaded() const;

    const std::string& LastError() const;
    const std::string& ConfigPath() const;

    const AppConfig& App() const;
    const ServerConfig& Server() const;
    const LoggerConfig& Logger() const;
    const ThreadPoolConfig& ThreadPool() const;
    const ProtocolConfig& Protocol() const;
    const RpcConfig& Rpc() const;
    const MySqlConfig& MySql() const;
    const RedisConfig& Redis() const;

    const GatewayRegistryConfig&
    GatewayRegistry() const;

    const McpConfig& Mcp() const;

    // 兼容当前已有代码的旧接口，
    // 后续模块逐步改用强类型接口。
    std::string ServerName() const;
    std::string ServerHost() const;
    uint16_t ServerPort() const;

    std::string LogLevel() const;
    std::string LogFile() const;
    bool LogConsole() const;

    int ThreadPoolWorkerThreads() const;
    int ThreadPoolQueueCapacity() const;

private:
    void Reset();

    bool ApplyJsonConfig(
        const std::string& json_content
    );

    bool Validate();

    bool SetError(
        const std::string& message
    );

    static QueueFullPolicy
    ParseQueueFullPolicy(
        const std::string& policy
    );

    static std::string
    QueueFullPolicyToString(
        QueueFullPolicy policy
    );

private:
    bool loaded_{false};

    std::string config_path_;
    std::string last_error_;

    AppConfig app_;
    ServerConfig server_;
    LoggerConfig logger_;
    ThreadPoolConfig thread_pool_;
    ProtocolConfig protocol_;
    RpcConfig rpc_;
    MySqlConfig mysql_;
    RedisConfig redis_;

    GatewayRegistryConfig gateway_registry_;

    McpConfig mcp_;
};

}  // namespace tinyimx