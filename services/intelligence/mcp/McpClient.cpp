#include "services/intelligence/mcp/McpClient.h"

#include <sstream>
#include <utility>

namespace tinyimx::mcp {
namespace {

bool HttpSuccess(int status) {
    return status >= 200 && status < 300;
}

std::string JsonRpcErrorText(const Json& body) {
    if (!body.is_object() || !body.contains("error") || !body.at("error").is_object()) {
        return "MCP response does not contain a JSON-RPC result";
    }
    const auto& error = body.at("error");
    std::ostringstream out;
    if (error.contains("code")) out << "code=" << error.at("code").dump() << ' ';
    out << error.value("message", "MCP JSON-RPC error");
    return out.str();
}

}  // namespace

Client::Client(ClientOptions options,
               std::shared_ptr<const intelligence::IHttpTransport> transport)
    : options_(std::move(options)), transport_(std::move(transport)) {}

Json Client::Meta() const {
    return Json{
        {"io.modelcontextprotocol/protocolVersion", kProtocolVersion},
        {"io.modelcontextprotocol/clientInfo",
         Json{{"name", options_.client_name}, {"version", options_.client_version}}},
        {"io.modelcontextprotocol/clientCapabilities", Json::object()}
    };
}

std::uint64_t Client::NextId() noexcept {
    return next_id_.fetch_add(1, std::memory_order_relaxed);
}

Client::RpcResponse Client::Request(
    const std::string& method,
    Json params,
    const std::string& name_header) {
    if (!transport_) return {false, Json::object(), "MCP HTTP transport is not configured"};
    if (options_.endpoint.empty()) return {false, Json::object(), "MCP endpoint is empty"};
    if (options_.bearer_token.empty()) return {false, Json::object(), "MCP bearer token is empty"};
    if (!params.is_object()) return {false, Json::object(), "MCP params must be an object"};

    params["_meta"] = Meta();
    const auto id = NextId();
    const Json body{
        {"jsonrpc", kJsonRpcVersion},
        {"id", id},
        {"method", method},
        {"params", std::move(params)}
    };

    intelligence::HttpRequest request;
    request.method = "POST";
    request.url = options_.endpoint;
    request.connect_timeout = options_.connect_timeout;
    request.request_timeout = options_.request_timeout;
    request.max_response_bytes = options_.max_response_bytes;
    request.body = body.dump();
    request.headers.emplace("Content-Type", "application/json");
    request.headers.emplace("Authorization", "Bearer " + options_.bearer_token);
    request.headers.emplace("MCP-Protocol-Version", kProtocolVersion);
    request.headers.emplace("Mcp-Method", method);
    request.headers.emplace("X-Request-Id", "tinyimx-agent-" + std::to_string(id));
    if (!name_header.empty()) request.headers.emplace("Mcp-Name", name_header);

    const auto http = transport_->Execute(request);
    if (!http.ok) return {false, Json::object(), "MCP HTTP failure: " + http.error};

    Json parsed;
    try {
        parsed = Json::parse(http.response.body);
    } catch (const std::exception& e) {
        return {false, Json::object(), std::string("MCP returned malformed JSON: ") + e.what()};
    }

    if (!HttpSuccess(http.response.status)) {
        return {false, Json::object(),
                "MCP HTTP status " + std::to_string(http.response.status) + ": " + JsonRpcErrorText(parsed)};
    }
    if (!parsed.is_object() || parsed.value("jsonrpc", "") != kJsonRpcVersion) {
        return {false, Json::object(), "MCP returned invalid JSON-RPC envelope"};
    }
    if (!parsed.contains("id") || !parsed.at("id").is_number_unsigned() ||
        parsed.at("id").get<std::uint64_t>() != id) {
        return {false, Json::object(), "MCP JSON-RPC id mismatch"};
    }
    if (parsed.contains("error")) {
        return {false, Json::object(), JsonRpcErrorText(parsed)};
    }
    if (!parsed.contains("result") || !parsed.at("result").is_object()) {
        return {false, Json::object(), "MCP response is missing result"};
    }
    return {true, parsed.at("result"), {}};
}

DiscoverResponse Client::Discover() {
    const auto rpc = Request("server/discover", Json::object());
    if (!rpc.ok) return {false, {}, Json::object(), Json::object(), {}, rpc.error};
    DiscoverResponse out;
    out.ok = true;
    if (rpc.result.contains("supportedVersions") && rpc.result.at("supportedVersions").is_array()) {
        for (const auto& item : rpc.result.at("supportedVersions")) {
            if (item.is_string()) out.supported_versions.push_back(item.get<std::string>());
        }
    }
    out.capabilities = rpc.result.value("capabilities", Json::object());
    out.server_info = rpc.result.value("serverInfo", Json::object());
    out.instructions = rpc.result.value("instructions", "");
    if (out.supported_versions.empty()) {
        out.ok = false;
        out.error = "MCP discovery returned no supportedVersions";
    }
    return out;
}

ListToolsResponse Client::ListTools() {
    const auto rpc = Request("tools/list", Json::object());
    if (!rpc.ok) return {false, {}, rpc.error};
    if (!rpc.result.contains("tools") || !rpc.result.at("tools").is_array()) {
        return {false, {}, "MCP tools/list result is missing tools array"};
    }
    ListToolsResponse out;
    out.ok = true;
    for (const auto& item : rpc.result.at("tools")) {
        if (!item.is_object() || !item.contains("name") || !item.at("name").is_string()) {
            return {false, {}, "MCP tools/list contains invalid tool definition"};
        }
        ClientTool tool;
        tool.name = item.at("name").get<std::string>();
        tool.title = item.value("title", "");
        tool.description = item.value("description", "");
        tool.input_schema = item.value("inputSchema", Json::object());
        if (!tool.input_schema.is_object()) {
            return {false, {}, "MCP tool inputSchema must be an object: " + tool.name};
        }
        out.tools.push_back(std::move(tool));
    }
    return out;
}

CallToolResponse Client::CallTool(const std::string& name, const Json& arguments) {
    if (name.empty()) return {false, false, Json::object(), Json::array(), "tool name is empty"};
    if (!arguments.is_object()) return {false, false, Json::object(), Json::array(), "tool arguments must be an object"};
    const auto rpc = Request(
        "tools/call",
        Json{{"name", name}, {"arguments", arguments}},
        name);
    if (!rpc.ok) return {false, false, Json::object(), Json::array(), rpc.error};

    CallToolResponse out;
    out.ok = true;
    out.is_error = rpc.result.value("isError", false);
    out.structured_content = rpc.result.value("structuredContent", Json::object());
    out.content = rpc.result.value("content", Json::array());
    if (!out.structured_content.is_object() && !out.structured_content.is_array()) {
        return {false, false, Json::object(), Json::array(), "MCP tool structuredContent has unsupported type"};
    }
    return out;
}

}  // namespace tinyimx::mcp
