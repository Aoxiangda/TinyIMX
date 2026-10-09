#include "services/intelligence/ai/AIRuntimeConfig.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

bool WriteFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path);
    if (!output) return false;
    output << content;
    return output.good();
}

std::string BaseConfig(const std::string& observability) {
    return std::string(R"JSON({
  "ai": {
    "provider": "openai_compatible",
    "endpoint": "http://127.0.0.1:11434/v1/chat/completions",
    "api_key_env": "TINYIMX_AI_API_KEY",
    "connect_timeout_ms": 1000,
    "request_timeout_ms": 2000,
    "max_response_bytes": 1048576
  },
  "mcp": {
    "endpoint": "http://127.0.0.1:8080/mcp",
    "token_env": "TINYIMX_MCP_TOKEN",
    "client_name": "m20-config-test",
    "client_version": "m20",
    "connect_timeout_ms": 1000,
    "request_timeout_ms": 2000,
    "max_response_bytes": 1048576
  },
  "agent": {
    "model": "deterministic-test",
    "allowed_tools": ["tinyimx.user.get_self_profile"],
    "max_tool_rounds": 2,
    "max_tool_calls_per_round": 2,
    "max_total_tool_calls": 2,
    "repeated_identical_call_limit": 2
  })JSON") + observability + "\n}\n";
}

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("tinyimx-m20-ai-observability-" + std::to_string(::getpid()));
    std::filesystem::create_directories(root);

    bool ok = true;
    std::string error;

    const auto legacy = root / "legacy.json";
    ok = Expect(WriteFile(legacy, BaseConfig("")), "write legacy config") && ok;
    tinyimx::ai::RuntimeConfig legacy_config;
    ok = Expect(
        tinyimx::ai::LoadRuntimeConfig(legacy.string(), &legacy_config, &error),
        error.c_str()
    ) && ok;
    ok = Expect(!legacy_config.observability.enable, "legacy observability remains disabled") && ok;
    ok = Expect(
        legacy_config.telemetry_identity.service_name == "tinyimx-ai-agent",
        "legacy stable service.name"
    ) && ok;

    const auto traced = root / "traced.json";
    const std::string traced_section = R"JSON(,
  "observability": {
    "enable": true,
    "metrics_enable": false,
    "traces_enable": true,
    "otlp_endpoint": "http://127.0.0.1:14317",
    "metric_export_interval_ms": 1000,
    "export_timeout_ms": 2000,
    "shutdown_timeout_ms": 5000,
    "trace_max_queue_size": 256,
    "trace_max_export_batch_size": 32,
    "trace_schedule_delay_ms": 100,
    "service_instance_id": "m20-ai-agent-test-1",
    "deployment_environment": "test"
  })JSON";
    ok = Expect(WriteFile(traced, BaseConfig(traced_section)), "write traced config") && ok;
    tinyimx::ai::RuntimeConfig traced_config;
    error.clear();
    ok = Expect(
        tinyimx::ai::LoadRuntimeConfig(traced.string(), &traced_config, &error),
        error.c_str()
    ) && ok;
    ok = Expect(traced_config.observability.enable, "traced observability enabled") && ok;
    ok = Expect(!traced_config.observability.metrics_enable, "traced metrics disabled") && ok;
    ok = Expect(traced_config.observability.traces_enable, "traced traces enabled") && ok;
    ok = Expect(
        traced_config.observability.otlp_endpoint == "http://127.0.0.1:14317",
        "traced endpoint parsed"
    ) && ok;
    ok = Expect(
        traced_config.telemetry_identity.service_instance_id == "m20-ai-agent-test-1",
        "traced instance id parsed"
    ) && ok;
    ok = Expect(
        traced_config.telemetry_identity.deployment_environment == "test",
        "traced environment parsed"
    ) && ok;

    const auto invalid = root / "invalid.json";
    const std::string invalid_section = R"JSON(,
  "observability": {
    "enable": true,
    "metrics_enable": false,
    "traces_enable": true,
    "otlp_endpoint": "http://127.0.0.1:14317",
    "trace_max_queue_size": 16,
    "trace_max_export_batch_size": 32
  })JSON";
    ok = Expect(WriteFile(invalid, BaseConfig(invalid_section)), "write invalid config") && ok;
    tinyimx::ai::RuntimeConfig invalid_config;
    error.clear();
    const bool invalid_loaded = tinyimx::ai::LoadRuntimeConfig(
        invalid.string(), &invalid_config, &error);
    ok = Expect(!invalid_loaded, "invalid trace batch must fail") && ok;
    ok = Expect(
        error.find("batch size") != std::string::npos,
        "invalid trace batch diagnostic"
    ) && ok;

    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    if (!ok) return 1;
    std::cout << "M20_AI_OBSERVABILITY_CONFIG_TEST=PASS\n";
    return 0;
}
