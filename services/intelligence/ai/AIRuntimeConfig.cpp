#include "services/intelligence/ai/AIRuntimeConfig.h"

#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace tinyimx::ai {
namespace {

using Json = nlohmann::json;

bool PositiveMs(const Json& object, const char* key, std::chrono::milliseconds* value, std::string* error) {
    if (!object.contains(key)) return true;
    if (!object.at(key).is_number_integer()) {
        if (error) *error = std::string(key) + " must be an integer";
        return false;
    }
    const auto raw = object.at(key).get<long long>();
    if (raw <= 0) {
        if (error) *error = std::string(key) + " must be positive";
        return false;
    }
    *value = std::chrono::milliseconds(raw);
    return true;
}

bool PositiveSize(const Json& object, const char* key, std::size_t* value, std::string* error) {
    if (!object.contains(key)) return true;
    if (!object.at(key).is_number_unsigned() && !object.at(key).is_number_integer()) {
        if (error) *error = std::string(key) + " must be an integer";
        return false;
    }
    const auto raw = object.at(key).get<long long>();
    if (raw <= 0) {
        if (error) *error = std::string(key) + " must be positive";
        return false;
    }
    *value = static_cast<std::size_t>(raw);
    return true;
}


bool PositiveInt(const Json& object, const char* key, int* value, std::string* error) {
    if (!object.contains(key)) return true;
    if (!object.at(key).is_number_integer()) {
        if (error) *error = std::string(key) + " must be an integer";
        return false;
    }
    const auto raw = object.at(key).get<long long>();
    if (raw <= 0 || raw > std::numeric_limits<int>::max()) {
        if (error) *error = std::string(key) + " must be a positive int";
        return false;
    }
    *value = static_cast<int>(raw);
    return true;
}

bool LoadObservability(
    const Json& root,
    RuntimeConfig* config,
    std::string* error
) {
    if (!root.contains("observability")) {
        return true;
    }
    const auto& section = root.at("observability");
    if (!section.is_object()) {
        if (error) *error = "observability must be an object";
        return false;
    }

    auto& obs = config->observability;
    obs.enable = section.value("enable", obs.enable);
    obs.metrics_enable = section.value("metrics_enable", obs.metrics_enable);
    obs.traces_enable = section.value("traces_enable", obs.traces_enable);
    obs.otlp_endpoint = section.value("otlp_endpoint", obs.otlp_endpoint);

    if (!PositiveInt(section, "metric_export_interval_ms", &obs.metric_export_interval_ms, error) ||
        !PositiveInt(section, "export_timeout_ms", &obs.export_timeout_ms, error) ||
        !PositiveInt(section, "shutdown_timeout_ms", &obs.shutdown_timeout_ms, error) ||
        !PositiveSize(section, "trace_max_queue_size", &obs.trace_max_queue_size, error) ||
        !PositiveSize(section, "trace_max_export_batch_size", &obs.trace_max_export_batch_size, error) ||
        !PositiveInt(section, "trace_schedule_delay_ms", &obs.trace_schedule_delay_ms, error)) {
        return false;
    }

    config->telemetry_identity.service_instance_id = section.value(
        "service_instance_id", config->telemetry_identity.service_instance_id);
    config->telemetry_identity.deployment_environment = section.value(
        "deployment_environment", config->telemetry_identity.deployment_environment);

    if (obs.enable) {
        if (!obs.metrics_enable && !obs.traces_enable) {
            if (error) *error = "observability requires metrics_enable or traces_enable when enabled";
            return false;
        }
        if (obs.otlp_endpoint.empty()) {
            if (error) *error = "observability.otlp_endpoint is required when enabled";
            return false;
        }
        if (obs.trace_max_export_batch_size > obs.trace_max_queue_size) {
            if (error) *error = "observability trace batch size cannot exceed queue size";
            return false;
        }
        if (config->telemetry_identity.service_instance_id.empty() ||
            config->telemetry_identity.deployment_environment.empty()) {
            if (error) *error = "observability service_instance_id/deployment_environment cannot be empty";
            return false;
        }
    }
    return true;
}

}  // namespace

bool LoadRuntimeConfig(const std::string& path, RuntimeConfig* output, std::string* error) {
    if (output == nullptr) {
        if (error) *error = "null runtime config output";
        return false;
    }
    std::ifstream input(path);
    if (!input) {
        if (error) *error = "cannot open AI runtime config: " + path;
        return false;
    }
    Json root;
    try {
        input >> root;
    } catch (const std::exception& e) {
        if (error) *error = std::string("invalid AI runtime config JSON: ") + e.what();
        return false;
    }
    if (!root.is_object() || !root.contains("ai") || !root.at("ai").is_object() ||
        !root.contains("mcp") || !root.at("mcp").is_object() ||
        !root.contains("agent") || !root.at("agent").is_object()) {
        if (error) *error = "AI runtime config requires ai, mcp, and agent objects";
        return false;
    }

    RuntimeConfig config;
    const auto& ai = root.at("ai");
    if (ai.value("provider", "openai_compatible") != "openai_compatible") {
        if (error) *error = "only openai_compatible provider is supported in M19";
        return false;
    }
    config.provider.endpoint = ai.value("endpoint", "");
    config.ai_api_key_env = ai.value("api_key_env", "TINYIMX_AI_API_KEY");
    if (!PositiveMs(ai, "connect_timeout_ms", &config.provider.connect_timeout, error) ||
        !PositiveMs(ai, "request_timeout_ms", &config.provider.request_timeout, error) ||
        !PositiveSize(ai, "max_response_bytes", &config.provider.max_response_bytes, error)) {
        return false;
    }

    const auto& mcp = root.at("mcp");
    config.mcp.endpoint = mcp.value("endpoint", "");
    config.mcp_token_env = mcp.value("token_env", "TINYIMX_MCP_TOKEN");
    config.mcp.client_name = mcp.value("client_name", "tinyimx-ai-agent");
    config.mcp.client_version = mcp.value("client_version", "m19");
    if (!PositiveMs(mcp, "connect_timeout_ms", &config.mcp.connect_timeout, error) ||
        !PositiveMs(mcp, "request_timeout_ms", &config.mcp.request_timeout, error) ||
        !PositiveSize(mcp, "max_response_bytes", &config.mcp.max_response_bytes, error)) {
        return false;
    }

    const auto& agent = root.at("agent");
    config.agent.model = agent.value("model", "");
    config.agent.system_prompt = agent.value("system_prompt", config.agent.system_prompt);
    if (agent.contains("allowed_tools")) {
        if (!agent.at("allowed_tools").is_array()) {
            if (error) *error = "agent.allowed_tools must be an array";
            return false;
        }
        for (const auto& item : agent.at("allowed_tools")) {
            if (!item.is_string() || item.get<std::string>().empty()) {
                if (error) *error = "agent.allowed_tools must contain non-empty strings";
                return false;
            }
            config.agent.allowed_tools.push_back(item.get<std::string>());
        }
    }
    if (!PositiveSize(agent, "max_tool_rounds", &config.agent.max_tool_rounds, error) ||
        !PositiveSize(agent, "max_tool_calls_per_round", &config.agent.max_tool_calls_per_round, error) ||
        !PositiveSize(agent, "max_total_tool_calls", &config.agent.max_total_tool_calls, error) ||
        !PositiveSize(agent, "repeated_identical_call_limit", &config.agent.repeated_identical_call_limit, error)) {
        return false;
    }

    if (!LoadObservability(root, &config, error)) {
        return false;
    }

    if (config.provider.endpoint.empty() || config.mcp.endpoint.empty() || config.agent.model.empty() ||
        config.ai_api_key_env.empty() || config.mcp_token_env.empty() || config.agent.allowed_tools.empty()) {
        if (error) *error = "AI endpoint, MCP endpoint, model, env names, and allowed_tools are required";
        return false;
    }
    *output = std::move(config);
    return true;
}

}  // namespace tinyimx::ai
