#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace tinyimx::ai {

using Json = nlohmann::json;

struct ToolCall {
    std::string id;
    std::string name;
    Json arguments{Json::object()};
};

struct Message {
    std::string role;
    std::string content;
    std::string tool_call_id;
    std::vector<ToolCall> tool_calls;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    Json parameters{Json::object()};
};

struct CompletionRequest {
    std::string model;
    std::vector<Message> messages;
    std::vector<ToolDefinition> tools;
};

struct CompletionResult {
    bool ok{false};
    std::string content;
    std::vector<ToolCall> tool_calls;
    std::string finish_reason;
    std::string error;
};

struct AgentOptions {
    std::string model;
    std::string system_prompt{
        "You are the TinyIMX assistant. Use only the provided read-only tools when "
        "TinyIMX account, social, message, group, or file context is required. "
        "Never invent tool results or claim an action was performed when no tool confirms it."};
    std::vector<std::string> allowed_tools;
    std::size_t max_tool_rounds{4};
    std::size_t max_tool_calls_per_round{8};
    std::size_t max_total_tool_calls{16};
    std::size_t repeated_identical_call_limit{3};
};

struct AgentResult {
    bool ok{false};
    std::string answer;
    std::size_t tool_rounds{0};
    std::size_t tool_calls{0};
    std::string error;
};

}  // namespace tinyimx::ai
