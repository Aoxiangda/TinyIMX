#include "services/intelligence/mcp/McpTypes.h"

#include <utility>

namespace tinyimx::mcp {

bool Principal::HasScope(const std::string& scope) const {
    return scopes.find(scope) != scopes.end();
}

bool RequestContext::DeadlineExpired() const noexcept {
    return deadline != std::chrono::steady_clock::time_point{} &&
           std::chrono::steady_clock::now() >= deadline;
}

ToolExecutionError::ToolExecutionError(
    std::string code,
    std::string message,
    Json details
)
    : std::runtime_error(std::move(message)),
      code_(std::move(code)),
      details_(std::move(details)) {}

const std::string& ToolExecutionError::code() const noexcept {
    return code_;
}

const Json& ToolExecutionError::details() const noexcept {
    return details_;
}

Json MakeJsonRpcResult(const Json& id, Json result) {
    return Json{{"jsonrpc", kJsonRpcVersion}, {"id", id}, {"result", std::move(result)}};
}

Json MakeJsonRpcError(const Json& id, int code, std::string message, Json data) {
    Json error{{"code", code}, {"message", std::move(message)}};
    if (!data.is_null()) {
        error["data"] = std::move(data);
    }
    return Json{{"jsonrpc", kJsonRpcVersion}, {"id", id}, {"error", std::move(error)}};
}

Json MakeCompleteResult(Json payload, std::string cache_scope, std::uint64_t ttl_ms) {
    if (!payload.is_object()) {
        payload = Json{{"value", std::move(payload)}};
    }
    payload["resultType"] = "complete";
    payload["cacheScope"] = std::move(cache_scope);
    payload["ttlMs"] = ttl_ms;
    return payload;
}

}  // namespace tinyimx::mcp
