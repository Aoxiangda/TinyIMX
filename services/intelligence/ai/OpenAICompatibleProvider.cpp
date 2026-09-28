#include "services/intelligence/ai/OpenAICompatibleProvider.h"

#include "common/observability/Trace.h"

#include <exception>
#include <string>
#include <utility>

namespace tinyimx::ai {
namespace {

bool HttpSuccess(int status) {
    return status >= 200 && status < 300;
}

Json EncodeToolCall(const ToolCall& call) {
    return Json{
        {"id", call.id},
        {"type", "function"},
        {"function", Json{{"name", call.name}, {"arguments", call.arguments.dump()}}}
    };
}

Json EncodeMessage(const Message& message) {
    Json out{{"role", message.role}};
    if (message.role == "assistant" && !message.tool_calls.empty()) {
        out["content"] = message.content.empty() ? Json(nullptr) : Json(message.content);
        out["tool_calls"] = Json::array();
        for (const auto& call : message.tool_calls) out["tool_calls"].push_back(EncodeToolCall(call));
        return out;
    }
    if (message.role == "tool") {
        out["tool_call_id"] = message.tool_call_id;
        out["content"] = message.content;
        return out;
    }
    out["content"] = message.content;
    return out;
}

}  // namespace

OpenAICompatibleProvider::OpenAICompatibleProvider(
    OpenAICompatibleOptions options,
    std::shared_ptr<const intelligence::IHttpTransport> transport)
    : options_(std::move(options)), transport_(std::move(transport)) {}

Json OpenAICompatibleProvider::EncodeRequest(const CompletionRequest& request) const {
    Json body{{"model", request.model}, {"messages", Json::array()}};
    for (const auto& message : request.messages) body["messages"].push_back(EncodeMessage(message));
    if (!request.tools.empty()) {
        body["tools"] = Json::array();
        for (const auto& tool : request.tools) {
            body["tools"].push_back(Json{
                {"type", "function"},
                {"function", Json{
                    {"name", tool.name},
                    {"description", tool.description},
                    {"parameters", tool.parameters}
                }}
            });
        }
        body["tool_choice"] = "auto";
    }
    return body;
}

CompletionResult OpenAICompatibleProvider::DecodeResponse(const Json& response) const {
    if (!response.is_object() || !response.contains("choices") || !response.at("choices").is_array() ||
        response.at("choices").empty()) {
        return {false, {}, {}, {}, "AI response is missing choices"};
    }
    const auto& choice = response.at("choices").at(0);
    if (!choice.is_object() || !choice.contains("message") || !choice.at("message").is_object()) {
        return {false, {}, {}, {}, "AI response choice is missing message"};
    }
    const auto& message = choice.at("message");
    CompletionResult out;
    out.ok = true;
    out.finish_reason = choice.value("finish_reason", "");
    if (message.contains("content") && message.at("content").is_string()) {
        out.content = message.at("content").get<std::string>();
    }
    if (message.contains("tool_calls")) {
        if (!message.at("tool_calls").is_array()) {
            return {false, {}, {}, {}, "AI response tool_calls must be an array"};
        }
        for (const auto& item : message.at("tool_calls")) {
            if (!item.is_object() || !item.contains("function") || !item.at("function").is_object()) {
                return {false, {}, {}, {}, "AI response contains malformed tool call"};
            }
            const auto& fn = item.at("function");
            ToolCall call;
            call.id = item.value("id", "");
            call.name = fn.value("name", "");
            if (call.id.empty() || call.name.empty()) {
                return {false, {}, {}, {}, "AI response tool call is missing id or name"};
            }
            if (!fn.contains("arguments")) {
                call.arguments = Json::object();
            } else if (fn.at("arguments").is_object()) {
                call.arguments = fn.at("arguments");
            } else if (fn.at("arguments").is_string()) {
                try {
                    const auto text = fn.at("arguments").get<std::string>();
                    call.arguments = text.empty() ? Json::object() : Json::parse(text);
                } catch (const std::exception& e) {
                    return {false, {}, {}, {}, std::string("AI tool arguments are not valid JSON: ") + e.what()};
                }
            } else {
                return {false, {}, {}, {}, "AI tool arguments must be a JSON string or object"};
            }
            if (!call.arguments.is_object()) {
                return {false, {}, {}, {}, "AI tool arguments must decode to an object"};
            }
            out.tool_calls.push_back(std::move(call));
        }
    }
    if (out.content.empty() && out.tool_calls.empty()) {
        return {false, {}, {}, {}, "AI response contains neither content nor tool calls"};
    }
    return out;
}

CompletionResult OpenAICompatibleProvider::Complete(const CompletionRequest& request) {
    if (!transport_) return {false, {}, {}, {}, "AI HTTP transport is not configured"};
    if (options_.endpoint.empty()) return {false, {}, {}, {}, "AI endpoint is empty"};
    if (request.model.empty()) return {false, {}, {}, {}, "AI model is empty"};
    if (request.messages.empty()) return {false, {}, {}, {}, "AI messages are empty"};

    auto provider_span = observability::StartSpan(
        "tinyimx.ai.provider.complete",
        observability::SpanKind::kClient
    );
    provider_span.SetAttribute("ai.provider.type", "openai_compatible");
    provider_span.SetAttribute("ai.model", request.model);
    provider_span.SetDefaultErrorOnEnd("ai.provider.error");

    intelligence::HttpRequest http;
    http.method = "POST";
    http.url = options_.endpoint;
    http.connect_timeout = options_.connect_timeout;
    http.request_timeout = options_.request_timeout;
    http.max_response_bytes = options_.max_response_bytes;
    http.headers.emplace("Content-Type", "application/json");
    if (!options_.api_key.empty()) http.headers.emplace("Authorization", "Bearer " + options_.api_key);
    http.body = EncodeRequest(request).dump();

    const auto response = transport_->Execute(http);
    if (!response.ok) return {false, {}, {}, {}, "AI HTTP failure: " + response.error};

    Json parsed;
    try {
        parsed = Json::parse(response.response.body);
    } catch (const std::exception& e) {
        return {false, {}, {}, {}, std::string("AI returned malformed JSON: ") + e.what()};
    }
    if (!HttpSuccess(response.response.status)) {
        std::string detail;
        if (parsed.is_object() && parsed.contains("error")) detail = parsed.at("error").dump();
        return {false, {}, {}, {}, "AI HTTP status " + std::to_string(response.response.status) +
                                         (detail.empty() ? std::string{} : ": " + detail)};
    }
    auto decoded = DecodeResponse(parsed);
    if (decoded.ok) {
        provider_span.MarkOk();
    }
    return decoded;
}

}  // namespace tinyimx::ai
