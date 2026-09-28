#include "services/intelligence/ai/AgentOrchestrator.h"

#include "common/observability/Trace.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tinyimx::ai {

AgentOrchestrator::AgentOrchestrator(
    AgentOptions options,
    std::shared_ptr<IProvider> provider,
    std::shared_ptr<mcp::IToolClient> mcp_client)
    : options_(std::move(options)), provider_(std::move(provider)), mcp_client_(std::move(mcp_client)) {}

bool AgentOrchestrator::IsAllowed(const std::string& tool_name) const {
    return std::find(options_.allowed_tools.begin(), options_.allowed_tools.end(), tool_name) !=
           options_.allowed_tools.end();
}

std::string AgentOrchestrator::ToolSignature(const ToolCall& call) const {
    return call.name + "\n" + call.arguments.dump();
}

std::vector<ToolDefinition> AgentOrchestrator::BuildAllowedTools(
    const std::vector<mcp::ClientTool>& tools,
    std::string* error) const {
    std::unordered_map<std::string, const mcp::ClientTool*> available;
    for (const auto& tool : tools) available.emplace(tool.name, &tool);

    std::vector<ToolDefinition> out;
    out.reserve(options_.allowed_tools.size());
    std::unordered_set<std::string> seen;
    for (const auto& name : options_.allowed_tools) {
        if (name.empty() || !seen.insert(name).second) continue;
        const auto it = available.find(name);
        if (it == available.end()) {
            if (error) *error = "allowed tool is not published by MCPServer: " + name;
            return {};
        }
        out.push_back(ToolDefinition{
            it->second->name,
            it->second->description,
            it->second->input_schema
        });
    }
    if (out.empty()) {
        if (error) *error = "agent tool allowlist is empty";
    }
    return out;
}

AgentResult AgentOrchestrator::Run(const std::string& user_prompt) {
    if (!provider_) return {false, {}, 0, 0, "AI provider is not configured"};
    if (!mcp_client_) return {false, {}, 0, 0, "MCP client is not configured"};
    if (user_prompt.empty()) return {false, {}, 0, 0, "user prompt is empty"};
    if (options_.model.empty()) return {false, {}, 0, 0, "agent model is empty"};
    if (options_.max_tool_rounds == 0 || options_.max_tool_calls_per_round == 0 ||
        options_.max_total_tool_calls == 0 || options_.repeated_identical_call_limit == 0) {
        return {false, {}, 0, 0, "agent safety limits must be positive"};
    }

    auto agent_span = observability::StartSpan(
        "tinyimx.ai.agent.run",
        observability::SpanKind::kInternal
    );
    agent_span.SetAttribute("ai.model", options_.model);
    agent_span.SetDefaultErrorOnEnd("agent.error");

    const auto discovery = mcp_client_->Discover();
    if (!discovery.ok) return {false, {}, 0, 0, "MCP discovery failed: " + discovery.error};
    if (std::find(discovery.supported_versions.begin(), discovery.supported_versions.end(),
                  mcp::kProtocolVersion) == discovery.supported_versions.end()) {
        return {false, {}, 0, 0, "MCPServer does not advertise required protocol version"};
    }

    const auto listed = mcp_client_->ListTools();
    if (!listed.ok) return {false, {}, 0, 0, "MCP tools/list failed: " + listed.error};
    std::string tool_error;
    const auto tools = BuildAllowedTools(listed.tools, &tool_error);
    if (tools.empty()) return {false, {}, 0, 0, tool_error};

    CompletionRequest request;
    request.model = options_.model;
    request.tools = tools;
    request.messages.push_back(Message{"system", options_.system_prompt, {}, {}});
    request.messages.push_back(Message{"user", user_prompt, {}, {}});

    std::unordered_map<std::string, std::size_t> signature_counts;
    std::size_t total_tool_calls = 0;

    for (std::size_t round = 0; round <= options_.max_tool_rounds; ++round) {
        auto completion = provider_->Complete(request);
        if (!completion.ok) {
            return {false, {}, round, total_tool_calls, "AI completion failed: " + completion.error};
        }
        if (completion.tool_calls.empty()) {
            if (completion.content.empty()) {
                return {false, {}, round, total_tool_calls, "AI completion returned an empty final answer"};
            }
            agent_span.SetAttribute(
                "ai.tool_calls",
                static_cast<std::int64_t>(total_tool_calls)
            );
            agent_span.SetAttribute(
                "ai.tool_rounds",
                static_cast<std::int64_t>(round)
            );
            agent_span.MarkOk();
            return {true, completion.content, round, total_tool_calls, {}};
        }
        if (round == options_.max_tool_rounds) {
            return {false, {}, round, total_tool_calls, "maximum tool-call rounds exceeded"};
        }
        if (completion.tool_calls.size() > options_.max_tool_calls_per_round) {
            return {false, {}, round, total_tool_calls, "AI requested too many tools in one round"};
        }
        if (total_tool_calls + completion.tool_calls.size() > options_.max_total_tool_calls) {
            return {false, {}, round, total_tool_calls, "maximum total tool calls exceeded"};
        }

        Message assistant;
        assistant.role = "assistant";
        assistant.content = completion.content;
        assistant.tool_calls = completion.tool_calls;
        request.messages.push_back(std::move(assistant));

        for (const auto& call : completion.tool_calls) {
            if (!IsAllowed(call.name)) {
                return {false, {}, round, total_tool_calls,
                        "AI requested a tool outside the execution allowlist: " + call.name};
            }
            if (!call.arguments.is_object()) {
                return {false, {}, round, total_tool_calls,
                        "AI requested non-object arguments for tool: " + call.name};
            }
            const std::string signature = ToolSignature(call);
            auto& repeated = signature_counts[signature];
            ++repeated;
            if (repeated >= options_.repeated_identical_call_limit) {
                return {false, {}, round, total_tool_calls,
                        "repeated identical tool-call limit reached for: " + call.name};
            }

            const auto tool = mcp_client_->CallTool(call.name, call.arguments);
            if (!tool.ok) {
                return {false, {}, round, total_tool_calls,
                        "MCP tools/call failed for " + call.name + ": " + tool.error};
            }
            ++total_tool_calls;

            Json tool_payload{
                {"tool", call.name},
                {"isError", tool.is_error},
                {"structuredContent", tool.structured_content}
            };
            if (!tool.content.empty()) tool_payload["content"] = tool.content;
            request.messages.push_back(Message{
                "tool",
                tool_payload.dump(),
                call.id,
                {}
            });
        }
    }

    return {false, {}, options_.max_tool_rounds, total_tool_calls, "agent loop terminated unexpectedly"};
}

}  // namespace tinyimx::ai
