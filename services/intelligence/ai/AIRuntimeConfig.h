#pragma once

#include "common/config/ConfigTypes.h"
#include "common/observability/ObservabilityTypes.h"
#include "services/intelligence/ai/AITypes.h"
#include "services/intelligence/ai/OpenAICompatibleProvider.h"
#include "services/intelligence/mcp/McpClient.h"

#include <string>

namespace tinyimx::ai {

struct RuntimeConfig {
    OpenAICompatibleOptions provider;
    mcp::ClientOptions mcp;
    AgentOptions agent;
    ObservabilityConfig observability;
    TelemetryIdentity telemetry_identity{
        "tinyimx-ai-agent",
        "tinyimx",
        "tinyimx-ai-agent-1",
        "dev"
    };
    std::string ai_api_key_env{"TINYIMX_AI_API_KEY"};
    std::string mcp_token_env{"TINYIMX_MCP_TOKEN"};
};

[[nodiscard]] bool LoadRuntimeConfig(
    const std::string& path,
    RuntimeConfig* output,
    std::string* error = nullptr);

}  // namespace tinyimx::ai
