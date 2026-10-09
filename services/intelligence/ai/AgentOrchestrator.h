#pragma once

#include "services/intelligence/ai/AIProvider.h"
#include "services/intelligence/mcp/McpClient.h"

#include <memory>
#include <string>

namespace tinyimx::ai {

class AgentOrchestrator final {
public:
    AgentOrchestrator(
        AgentOptions options,
        std::shared_ptr<IProvider> provider,
        std::shared_ptr<mcp::IToolClient> mcp_client);

    [[nodiscard]] AgentResult Run(const std::string& user_prompt);

private:
    [[nodiscard]] std::vector<ToolDefinition> BuildAllowedTools(
        const std::vector<mcp::ClientTool>& tools,
        std::string* error) const;
    [[nodiscard]] bool IsAllowed(const std::string& tool_name) const;
    [[nodiscard]] std::string ToolSignature(const ToolCall& call) const;

private:
    AgentOptions options_;
    std::shared_ptr<IProvider> provider_;
    std::shared_ptr<mcp::IToolClient> mcp_client_;
};

}  // namespace tinyimx::ai
