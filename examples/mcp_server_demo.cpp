#include "common/config/Config.h"
#include "common/logging/LogMacros.h"
#include "common/logging/Logger.h"
#include "common/net/EventLoop.h"
#include "services/intelligence/mcp/McpAuth.h"
#include "services/intelligence/mcp/McpDispatcher.h"
#include "services/intelligence/mcp/McpDomainTools.h"
#include "services/intelligence/mcp/McpRegistry.h"
#include "services/intelligence/mcp/McpServer.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperServiceDiscovery.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "services/rpc/ZooKeeperServiceEndpointProvider.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>

namespace {
tinyimx::EventLoop* g_loop = nullptr;
void OnSignal(int sig) {
    if ((sig == SIGINT || sig == SIGTERM) && g_loop != nullptr) g_loop->Quit();
}

std::string EnvOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
}

}  // namespace

int main(int argc, char** argv) {
    const std::string config_path = argc > 1 ? argv[1] : "config/mcp_server.example.json";
    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "config load failed: " << config.LastError() << '\n';
        return 1;
    }
    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "logger init failed\n";
        return 1;
    }
    const auto& mcp = config.Mcp();
    if (!mcp.enable) {
        LOG_ERROR("mcp.enable=false");
        return 2;
    }
    const char* token_env = std::getenv(mcp.auth_token_env.c_str());
    if (token_env == nullptr || *token_env == '\0') {
        LOG_ERROR("MCP token environment variable is missing: " << mcp.auth_token_env);
        return 3;
    }

    std::shared_ptr<tinyimx::registry::zookeeper::ZooKeeperClient> zk_client;
    std::shared_ptr<tinyimx::registry::zookeeper::ZooKeeperServiceDiscovery> zk_discovery;
    std::shared_ptr<const tinyimx::rpc::ServiceEndpointProvider> endpoint_provider;

    if (config.ServiceDiscovery().provider == "zookeeper") {
        zk_client = std::make_shared<tinyimx::registry::zookeeper::ZooKeeperClient>();
        if (!zk_client->Start(config.ZooKeeper())) {
            LOG_ERROR("MCP ZooKeeper client start failed: " << zk_client->LastError());
            tinyimx::Logger::Instance().Shutdown();
            return 4;
        }
        zk_discovery = std::make_shared<tinyimx::registry::zookeeper::ZooKeeperServiceDiscovery>(
            zk_client, config.ZooKeeper().service_root, config.ServiceDiscovery());
        if (!zk_discovery->Start(std::chrono::milliseconds(config.ServiceDiscovery().initial_sync_timeout_ms))) {
            LOG_ERROR("MCP ZooKeeper discovery initial sync failed: " << zk_discovery->LastError());
            zk_discovery->Stop();
            zk_client->Stop();
            tinyimx::Logger::Instance().Shutdown();
            return 5;
        }
        endpoint_provider = std::make_shared<tinyimx::rpc::ZooKeeperServiceEndpointProvider>(
            zk_discovery, config.ServiceDiscovery());
        LOG_INFO("MCP domain RPC discovery enabled, provider=zookeeper");
    } else {
        endpoint_provider = std::make_shared<tinyimx::rpc::StaticServiceEndpointProvider>(
            EnvOrEmpty("TINYIMX_SOCIAL_RPC_TARGET"),
            EnvOrEmpty("TINYIMX_USER_RPC_TARGET"),
            EnvOrEmpty("TINYIMX_MESSAGE_RPC_TARGET"),
            EnvOrEmpty("TINYIMX_GROUP_RPC_TARGET"),
            EnvOrEmpty("TINYIMX_FILE_RPC_TARGET"));
        LOG_INFO("MCP domain RPC discovery enabled, provider=static");
    }

    auto registry = std::make_shared<tinyimx::mcp::Registry>();
    std::string error;
    if (!registry->RegisterTool(
        {"tinyimx.system.echo", "Echo", "M19 transport smoke-test tool",
         tinyimx::mcp::Json{{"type", "object"}, {"properties", tinyimx::mcp::Json{{"text", tinyimx::mcp::Json{{"type", "string"}}}}},
                            {"required", tinyimx::mcp::Json::array({"text"})}, {"additionalProperties", false}},
         {"mcp.read"}},
        [](const tinyimx::mcp::RequestContext& ctx, const tinyimx::mcp::Json& args) {
            return tinyimx::mcp::Json{{"text", args.value("text", "")}, {"principal", ctx.principal.subject}};
        }, &error)) {
        LOG_ERROR("MCP echo tool setup failed: " << error);
        if (zk_discovery) zk_discovery->Stop();
        if (zk_client) zk_client->Stop();
        tinyimx::Logger::Instance().Shutdown();
        return 6;
    }

    auto domain_backend = tinyimx::mcp::MakeRpcDomainBackend(endpoint_provider);
    if (!tinyimx::mcp::RegisterReadOnlyDomainTools(registry.get(), domain_backend, &error) ||
        !registry->Freeze(&error)) {
        LOG_ERROR("MCP registry setup failed: " << error);
        if (zk_discovery) zk_discovery->Stop();
        if (zk_client) zk_client->Stop();
        tinyimx::Logger::Instance().Shutdown();
        return 7;
    }

    auto dispatcher = std::make_shared<tinyimx::mcp::Dispatcher>(registry);
    auto verifier = std::make_shared<tinyimx::mcp::StaticTokenVerifier>(
        token_env, mcp.static_user_id, mcp.static_subject,
        std::unordered_set<std::string>{"mcp.read"});

    tinyimx::mcp::ServerOptions options;
    options.host = mcp.listen_host;
    options.port = mcp.listen_port;
    options.endpoint_path = mcp.endpoint_path;
    options.io_threads = mcp.io_threads;
    options.worker_threads = mcp.worker_threads;
    options.queue_capacity = mcp.queue_capacity;
    options.max_request_bytes = mcp.max_request_bytes;
    options.request_timeout = std::chrono::milliseconds(mcp.timeout_ms);
    options.allowed_origins.insert(mcp.allowed_origins.begin(), mcp.allowed_origins.end());

    tinyimx::EventLoop loop;
    g_loop = &loop;
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    tinyimx::mcp::Server server(&loop, std::move(options), dispatcher, verifier);
    if (!server.Start()) {
        LOG_ERROR("MCP server failed to start");
        if (zk_discovery) zk_discovery->Stop();
        if (zk_client) zk_client->Stop();
        tinyimx::Logger::Instance().Shutdown();
        return 8;
    }
    loop.Loop();
    server.Stop();
    if (zk_discovery) zk_discovery->Stop();
    if (zk_client) zk_client->Stop();
    g_loop = nullptr;
    tinyimx::Logger::Instance().Shutdown();
    return 0;
}
