#include <cstdint>

#include "common/config/ConfigTypes.h"
#include "common/logging/Logger.h"
#include "services/intelligence/ai/AIRuntimeConfig.h"
#include "services/intelligence/ai/AgentOrchestrator.h"
#include "services/intelligence/ai/OpenAICompatibleProvider.h"
#include "services/intelligence/http/HttpClient.h"
#include "services/intelligence/mcp/McpClient.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace {

std::string Env(const std::string& name) {
    const char* value = std::getenv(name.c_str());
    return value == nullptr ? std::string{} : std::string(value);
}

std::string JoinPrompt(int argc, char** argv, int begin) {
    std::ostringstream out;
    for (int i = begin; i < argc; ++i) {
        if (i != begin) out << ' ';
        out << argv[i];
    }
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    const std::string config_path = argc > 1 ? argv[1] : "config/ai_agent.example.json";
    tinyimx::ai::RuntimeConfig config;
    std::string error;
    if (!tinyimx::ai::LoadRuntimeConfig(config_path, &config, &error)) {
        std::cerr << "AI runtime config error: " << error << '\n';
        return 1;
    }

    config.provider.api_key = Env(config.ai_api_key_env);
    config.mcp.bearer_token = Env(config.mcp_token_env);
    if (config.mcp.bearer_token.empty()) {
        std::cerr << "MCP token environment variable is missing: " << config.mcp_token_env << '\n';
        return 2;
    }

    tinyimx::LoggerConfig logger;
    logger.level = "info";
    logger.console = true;
    logger.file.clear();
    logger.async = false;
    if (!tinyimx::Logger::Instance().Init(logger)) {
        std::cerr << "logger init failed\n";
        return 3;
    }

    auto transport = std::make_shared<tinyimx::intelligence::NativeHttpClient>();
    auto provider = std::make_shared<tinyimx::ai::OpenAICompatibleProvider>(config.provider, transport);
    auto mcp_client = std::make_shared<tinyimx::mcp::Client>(config.mcp, transport);
    tinyimx::ai::AgentOrchestrator agent(config.agent, provider, mcp_client);

    auto run = [&](const std::string& prompt) -> bool {
        const auto result = agent.Run(prompt);
        if (!result.ok) {
            std::cerr << "Agent error: " << result.error << '\n';
            return false;
        }
        std::cout << result.answer << '\n';
        std::cerr << "[tool_rounds=" << result.tool_rounds
                  << " tool_calls=" << result.tool_calls << "]\n";
        return true;
    };

    int rc = 0;
    if (argc > 2) {
        rc = run(JoinPrompt(argc, argv, 2)) ? 0 : 4;
    } else {
        std::cout << "TinyIMX AI Agent (type 'exit' to quit)\n";
        std::string line;
        while (std::cout << "> " && std::getline(std::cin, line)) {
            if (line == "exit" || line == "quit") break;
            if (line.empty()) continue;
            if (!run(line)) rc = 4;
        }
    }

    tinyimx::Logger::Instance().Shutdown();
    return rc;
}
