#include "services/intelligence/mcp/McpRegistry.h"

#include <utility>
#include <cctype>

namespace tinyimx::mcp {
namespace {

bool ValidName(const std::string& value) {
    if (value.empty() || value.size() > 128) return false;
    for (const unsigned char c : value) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == ':' || c == '/')) {
            return false;
        }
    }
    return true;
}

Json ScopesJson(const std::vector<std::string>& scopes) {
    Json out = Json::array();
    for (const auto& scope : scopes) out.push_back(scope);
    return out;
}

}  // namespace

bool Registry::RegisterTool(ToolDefinition definition, ToolHandler handler, std::string* error) {
    std::unique_lock lock(mutex_);
    if (frozen_) { if (error) *error = "registry is frozen"; return false; }
    if (!ValidName(definition.name) || !handler) { if (error) *error = "invalid tool definition"; return false; }
    const std::string key = definition.name;
    if (!tools_.emplace(key, Entry<ToolDefinition, ToolHandler>{std::move(definition), std::move(handler)}).second) {
        if (error) *error = "duplicate tool"; return false;
    }
    return true;
}

bool Registry::RegisterResource(ResourceDefinition definition, ResourceHandler handler, std::string* error) {
    std::unique_lock lock(mutex_);
    if (frozen_) { if (error) *error = "registry is frozen"; return false; }
    if (definition.uri.empty() || !handler) { if (error) *error = "invalid resource definition"; return false; }
    const std::string key = definition.uri;
    if (!resources_.emplace(key, Entry<ResourceDefinition, ResourceHandler>{std::move(definition), std::move(handler)}).second) {
        if (error) *error = "duplicate resource"; return false;
    }
    return true;
}

bool Registry::RegisterPrompt(PromptDefinition definition, PromptHandler handler, std::string* error) {
    std::unique_lock lock(mutex_);
    if (frozen_) { if (error) *error = "registry is frozen"; return false; }
    if (!ValidName(definition.name) || !handler) { if (error) *error = "invalid prompt definition"; return false; }
    const std::string key = definition.name;
    if (!prompts_.emplace(key, Entry<PromptDefinition, PromptHandler>{std::move(definition), std::move(handler)}).second) {
        if (error) *error = "duplicate prompt"; return false;
    }
    return true;
}

bool Registry::Freeze(std::string*) {
    std::unique_lock lock(mutex_);
    frozen_ = true;
    return true;
}

bool Registry::IsFrozen() const noexcept {
    std::shared_lock lock(mutex_);
    return frozen_;
}

Json Registry::ListTools() const {
    std::shared_lock lock(mutex_);
    Json out = Json::array();
    for (const auto& [name, entry] : tools_) {
        const auto& d = entry.definition;
        Json item{{"name", d.name}, {"description", d.description}, {"inputSchema", d.input_schema}};
        if (!d.title.empty()) item["title"] = d.title;
        if (!d.required_scopes.empty()) item["_meta"] = Json{{"io.tinyimx/requiredScopes", ScopesJson(d.required_scopes)}};
        out.push_back(std::move(item));
    }
    return out;
}

Json Registry::ListResources() const {
    std::shared_lock lock(mutex_);
    Json out = Json::array();
    for (const auto& [uri, entry] : resources_) {
        const auto& d = entry.definition;
        Json item{{"uri", d.uri}, {"name", d.name}, {"description", d.description}, {"mimeType", d.mime_type}};
        if (!d.required_scopes.empty()) item["_meta"] = Json{{"io.tinyimx/requiredScopes", ScopesJson(d.required_scopes)}};
        out.push_back(std::move(item));
    }
    return out;
}

Json Registry::ListPrompts() const {
    std::shared_lock lock(mutex_);
    Json out = Json::array();
    for (const auto& [name, entry] : prompts_) {
        const auto& d = entry.definition;
        Json item{{"name", d.name}, {"description", d.description}, {"arguments", d.arguments}};
        if (!d.title.empty()) item["title"] = d.title;
        if (!d.required_scopes.empty()) item["_meta"] = Json{{"io.tinyimx/requiredScopes", ScopesJson(d.required_scopes)}};
        out.push_back(std::move(item));
    }
    return out;
}

std::optional<std::pair<ToolDefinition, ToolHandler>> Registry::FindTool(const std::string& name) const {
    std::shared_lock lock(mutex_);
    auto it = tools_.find(name);
    if (it == tools_.end()) return std::nullopt;
    return std::make_pair(it->second.definition, it->second.handler);
}

std::optional<std::pair<ResourceDefinition, ResourceHandler>> Registry::FindResource(const std::string& uri) const {
    std::shared_lock lock(mutex_);
    auto it = resources_.find(uri);
    if (it == resources_.end()) return std::nullopt;
    return std::make_pair(it->second.definition, it->second.handler);
}

std::optional<std::pair<PromptDefinition, PromptHandler>> Registry::FindPrompt(const std::string& name) const {
    std::shared_lock lock(mutex_);
    auto it = prompts_.find(name);
    if (it == prompts_.end()) return std::nullopt;
    return std::make_pair(it->second.definition, it->second.handler);
}

}  // namespace tinyimx::mcp
