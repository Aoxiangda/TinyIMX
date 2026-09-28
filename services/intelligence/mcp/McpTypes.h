#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tinyimx::mcp {

using Json = nlohmann::json;

inline constexpr const char* kProtocolVersion = "2026-07-28";
inline constexpr const char* kJsonRpcVersion = "2.0";

inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;
inline constexpr int kHeaderMismatch = -32020;
inline constexpr int kMissingRequiredClientCapability = -32021;
inline constexpr int kUnsupportedProtocolVersion = -32022;
inline constexpr int kUnauthorized = -32040;
inline constexpr int kForbidden = -32041;
inline constexpr int kOverloaded = -32042;

struct Principal {
    std::uint64_t user_id{0};
    std::string subject;
    std::unordered_set<std::string> scopes;

    [[nodiscard]] bool HasScope(const std::string& scope) const;
};

struct RequestContext {
    std::string request_id;
    std::string trace_id;
    Principal principal;
    std::chrono::steady_clock::time_point received_at{
        std::chrono::steady_clock::now()
    };
    std::chrono::steady_clock::time_point deadline{};
    std::string client_name;
    std::string client_version;

    [[nodiscard]] bool DeadlineExpired() const noexcept;
};

struct ToolDefinition {
    std::string name;
    std::string title;
    std::string description;
    Json input_schema{Json::object()};
    std::vector<std::string> required_scopes;
};

struct ResourceDefinition {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type{"application/json"};
    std::vector<std::string> required_scopes;
};

struct PromptDefinition {
    std::string name;
    std::string title;
    std::string description;
    Json arguments{Json::array()};
    std::vector<std::string> required_scopes;
};

struct DispatchResult {
    int http_status{200};
    Json body{Json::object()};
};

class ToolExecutionError final : public std::runtime_error {
public:
    ToolExecutionError(std::string code, std::string message, Json details = Json::object());

    [[nodiscard]] const std::string& code() const noexcept;
    [[nodiscard]] const Json& details() const noexcept;

private:
    std::string code_;
    Json details_;
};

using ToolHandler = std::function<Json(const RequestContext&, const Json&)>;
using ResourceHandler = std::function<Json(const RequestContext&, const Json&)>;
using PromptHandler = std::function<Json(const RequestContext&, const Json&)>;

Json MakeJsonRpcResult(const Json& id, Json result);
Json MakeJsonRpcError(const Json& id, int code, std::string message, Json data = nullptr);
Json MakeCompleteResult(Json payload, std::string cache_scope = "private", std::uint64_t ttl_ms = 0);

}  // namespace tinyimx::mcp
