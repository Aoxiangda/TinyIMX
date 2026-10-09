#pragma once

#include "services/intelligence/mcp/McpTypes.h"

#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>

namespace tinyimx::mcp {

class Registry final {
public:
    bool RegisterTool(ToolDefinition definition, ToolHandler handler, std::string* error);
    bool RegisterResource(ResourceDefinition definition, ResourceHandler handler, std::string* error);
    bool RegisterPrompt(PromptDefinition definition, PromptHandler handler, std::string* error);

    bool Freeze(std::string* error = nullptr);
    [[nodiscard]] bool IsFrozen() const noexcept;

    [[nodiscard]] Json ListTools() const;
    [[nodiscard]] Json ListResources() const;
    [[nodiscard]] Json ListPrompts() const;

    [[nodiscard]] std::optional<std::pair<ToolDefinition, ToolHandler>> FindTool(const std::string& name) const;
    [[nodiscard]] std::optional<std::pair<ResourceDefinition, ResourceHandler>> FindResource(const std::string& uri) const;
    [[nodiscard]] std::optional<std::pair<PromptDefinition, PromptHandler>> FindPrompt(const std::string& name) const;

private:
    template <class Definition, class Handler>
    struct Entry {
        Definition definition;
        Handler handler;
    };

    mutable std::shared_mutex mutex_;
    bool frozen_{false};
    std::map<std::string, Entry<ToolDefinition, ToolHandler>> tools_;
    std::map<std::string, Entry<ResourceDefinition, ResourceHandler>> resources_;
    std::map<std::string, Entry<PromptDefinition, PromptHandler>> prompts_;
};

}  // namespace tinyimx::mcp
