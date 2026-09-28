#include "services/intelligence/mcp/McpDispatcher.h"

#include <exception>

namespace tinyimx::mcp {
namespace {

DispatchResult ErrorResult(const Json& id, int code, const std::string& message, int http_status = 200) {
    return {http_status, MakeJsonRpcError(id, code, message)};
}

std::string MetaString(const Json& meta, const char* key) {
    if (!meta.is_object() || !meta.contains(key) || !meta.at(key).is_string()) return {};
    return meta.at(key).get<std::string>();
}

}  // namespace

Dispatcher::Dispatcher(std::shared_ptr<const Registry> registry)
    : registry_(std::move(registry)) {}

DispatchResult Dispatcher::Dispatch(const RequestContext& context,
                                    const DispatchMetadata& metadata,
                                    const Json& request) const {
    const Json null_id = nullptr;
    if (!request.is_object()) return ErrorResult(null_id, kInvalidRequest, "request must be an object", 400);
    if (request.value("jsonrpc", "") != kJsonRpcVersion) return ErrorResult(request.value("id", null_id), kInvalidRequest, "jsonrpc must be 2.0", 400);
    if (!request.contains("id")) return ErrorResult(null_id, kInvalidRequest, "notifications are not accepted on this endpoint", 400);
    if (!request.contains("method") || !request.at("method").is_string()) return ErrorResult(request.at("id"), kInvalidRequest, "method must be a string", 400);

    const Json id = request.at("id");
    const std::string method = request.at("method").get<std::string>();
    const Json params = request.value("params", Json::object());
    if (!params.is_object()) return ErrorResult(id, kInvalidParams, "params must be an object");

    const Json meta = params.value("_meta", Json::object());
    const std::string body_protocol = MetaString(meta, "io.modelcontextprotocol/protocolVersion");
    const std::string protocol = !metadata.protocol_version.empty() ? metadata.protocol_version : body_protocol;
    if (protocol != kProtocolVersion) return ErrorResult(id, kUnsupportedProtocolVersion, "unsupported MCP protocol version", 400);
    if (!body_protocol.empty() && !metadata.protocol_version.empty() && body_protocol != metadata.protocol_version) {
        return ErrorResult(id, kHeaderMismatch, "MCP-Protocol-Version does not match request _meta", 400);
    }
    if (metadata.method_header.empty() || metadata.method_header != method) {
        return ErrorResult(id, kHeaderMismatch,
            metadata.method_header.empty() ? "Mcp-Method header is required" :
                                             "Mcp-Method does not match JSON-RPC method",
            400);
    }

    if (method == "tools/call" || method == "prompts/get" || method == "resources/read") {
        const char* field = method == "resources/read" ? "uri" : "name";
        const std::string body_name = params.value(field, "");
        if (metadata.name_header.empty() || metadata.name_header != body_name) {
            return ErrorResult(id, kHeaderMismatch,
                metadata.name_header.empty() ? "Mcp-Name header is required" :
                                               "Mcp-Name does not match request body",
                400);
        }
    }
    return DispatchRequest(context, metadata, request, id, method, params);
}

DispatchResult Dispatcher::DispatchRequest(const RequestContext& context,
                                           const DispatchMetadata& metadata,
                                           const Json&,
                                           const Json& id,
                                           const std::string& method,
                                           const Json& params) const {
    if (context.DeadlineExpired()) return ErrorResult(id, kInternalError, "request deadline exceeded", 408);

    if (method == "server/discover") {
        Json capabilities = Json::object();
        if (!registry_->ListTools().empty()) capabilities["tools"] = Json::object();
        if (!registry_->ListResources().empty()) capabilities["resources"] = Json::object();
        if (!registry_->ListPrompts().empty()) capabilities["prompts"] = Json::object();
        Json result = MakeCompleteResult(Json{
            {"supportedVersions", Json::array({kProtocolVersion})},
            {"capabilities", std::move(capabilities)},
            {"serverInfo", Json{{"name", "TinyIMX MCPServer"}, {"version", "m19"}, {"description", "TinyIMX AI-safe microservice capability gateway"}}},
            {"instructions", "Use TinyIMX tools only for the authenticated principal. Durable business ownership remains in domain services."}
        }, "public", 30000);
        return {200, MakeJsonRpcResult(id, std::move(result))};
    }

    if (method == "tools/list") {
        return {200, MakeJsonRpcResult(id, MakeCompleteResult(Json{{"tools", registry_->ListTools()}}, "private", 30000))};
    }
    if (method == "resources/list") {
        return {200, MakeJsonRpcResult(id, MakeCompleteResult(Json{{"resources", registry_->ListResources()}}, "private", 30000))};
    }
    if (method == "prompts/list") {
        return {200, MakeJsonRpcResult(id, MakeCompleteResult(Json{{"prompts", registry_->ListPrompts()}}, "private", 30000))};
    }

    try {
        if (method == "tools/call") {
            const std::string name = params.value("name", "");
            if (name.empty()) return ErrorResult(id, kInvalidParams, "tools/call requires name");
            const auto found = registry_->FindTool(name);
            if (!found) return ErrorResult(id, kMethodNotFound, "unknown tool: " + name);
            std::string missing;
            if (!Authorized(context.principal, found->first.required_scopes, &missing)) return ErrorResult(id, kForbidden, "missing scope: " + missing, 403);
            Json content = found->second(context, params.value("arguments", Json::object()));
            Json result = MakeCompleteResult(Json{{"content", Json::array({Json{{"type", "text"}, {"text", content.dump()}}})}, {"structuredContent", std::move(content)}, {"isError", false}}, "private", 0);
            return {200, MakeJsonRpcResult(id, std::move(result))};
        }

        if (method == "resources/read") {
            const std::string uri = params.value("uri", "");
            const auto found = registry_->FindResource(uri);
            if (!found) return ErrorResult(id, kMethodNotFound, "unknown resource: " + uri);
            std::string missing;
            if (!Authorized(context.principal, found->first.required_scopes, &missing)) return ErrorResult(id, kForbidden, "missing scope: " + missing, 403);
            Json data = found->second(context, params);
            Json result = MakeCompleteResult(Json{{"contents", Json::array({Json{{"uri", uri}, {"mimeType", found->first.mime_type}, {"text", data.dump()}}})}}, "private", 0);
            return {200, MakeJsonRpcResult(id, std::move(result))};
        }

        if (method == "prompts/get") {
            const std::string name = params.value("name", "");
            const auto found = registry_->FindPrompt(name);
            if (!found) return ErrorResult(id, kMethodNotFound, "unknown prompt: " + name);
            std::string missing;
            if (!Authorized(context.principal, found->first.required_scopes, &missing)) return ErrorResult(id, kForbidden, "missing scope: " + missing, 403);
            Json result = found->second(context, params.value("arguments", Json::object()));
            result = MakeCompleteResult(std::move(result), "private", 0);
            return {200, MakeJsonRpcResult(id, std::move(result))};
        }
    } catch (const ToolExecutionError& e) {
        Json tool_error{{"error", Json{{"code", e.code()}, {"message", e.what()}}}};
        if (!e.details().empty()) {
            tool_error["error"]["details"] = e.details();
        }
        Json result = MakeCompleteResult(Json{
            {"content", Json::array({Json{{"type", "text"}, {"text", tool_error.dump()}}})},
            {"structuredContent", std::move(tool_error)},
            {"isError", true}
        }, "private", 0);
        return {200, MakeJsonRpcResult(id, std::move(result))};
    } catch (const std::exception& e) {
        return ErrorResult(id, kInternalError, std::string("handler failure: ") + e.what(), 500);
    } catch (...) {
        return ErrorResult(id, kInternalError, "handler failure", 500);
    }

    return ErrorResult(id, kMethodNotFound, "unsupported MCP method: " + method);
}

bool Dispatcher::Authorized(const Principal& principal,
                            const std::vector<std::string>& required_scopes,
                            std::string* missing_scope) {
    for (const auto& scope : required_scopes) {
        if (!principal.HasScope(scope)) {
            if (missing_scope) *missing_scope = scope;
            return false;
        }
    }
    return true;
}

}  // namespace tinyimx::mcp
