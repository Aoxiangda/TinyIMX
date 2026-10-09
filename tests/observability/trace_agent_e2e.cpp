#include "common/config/ConfigTypes.h"
#include "common/logging/LogMacros.h"
#include "common/logging/Logger.h"
#include "common/observability/ObservabilityTypes.h"
#include "common/observability/ProcessTelemetry.h"
#include "services/intelligence/ai/AIProvider.h"
#include "services/intelligence/ai/AgentOrchestrator.h"
#include "services/intelligence/http/HttpClient.h"
#include "services/intelligence/mcp/McpClient.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

class DeterministicTraceProvider final : public tinyimx::ai::IProvider {
public:
    explicit DeterministicTraceProvider(std::string expected_username)
        : expected_username_(std::move(expected_username)) {}

    tinyimx::ai::CompletionResult Complete(
        const tinyimx::ai::CompletionRequest& request
    ) override {
        LOG_INFO(
            "M20_TRACE_LOG_CORRELATION_SENTINEL"
            << ", provider_call=" << calls_
        );

        if (calls_++ == 0) {
            bool published = false;
            for (const auto& tool : request.tools) {
                if (tool.name == "tinyimx.user.get_self_profile") {
                    published = true;
                    break;
                }
            }
            if (!published) {
                return {
                    false, {}, {}, {},
                    "tinyimx.user.get_self_profile is not published"
                };
            }

            tinyimx::ai::ToolCall call;
            call.id = "m20-trace-profile-1";
            call.name = "tinyimx.user.get_self_profile";
            call.arguments = tinyimx::ai::Json::object();

            return {
                true,
                {},
                {std::move(call)},
                "tool_calls",
                {}
            };
        }

        for (auto it = request.messages.rbegin(); it != request.messages.rend(); ++it) {
            if (it->role != "tool") {
                continue;
            }
            try {
                const auto payload = tinyimx::ai::Json::parse(it->content);
                const std::string username =
                    payload.at("structuredContent")
                           .at("profile")
                           .value("username", "");
                if (username != expected_username_) {
                    return {
                        false, {}, {}, {},
                        "unexpected profile username: " + username
                    };
                }
                return {
                    true,
                    "M20_TRACE_AGENT_OK username=" + username,
                    {},
                    "stop",
                    {}
                };
            } catch (const std::exception& e) {
                return {
                    false, {}, {}, {},
                    std::string("invalid tool payload: ") + e.what()
                };
            }
        }

        return {false, {}, {}, {}, "tool response is missing"};
    }

private:
    std::string expected_username_;
    std::size_t calls_{0};
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr
            << "Usage: m20_trace_agent_e2e <mcp_endpoint> <token> "
            << "<otlp_endpoint> <expected_username>\n";
        return 2;
    }

    const std::string mcp_endpoint = argv[1];
    const std::string token = argv[2];
    const std::string otlp_endpoint = argv[3];
    const std::string expected_username = argv[4];

    tinyimx::LoggerConfig logger;
    logger.level = "info";
    logger.console = true;
    logger.file.clear();
    logger.async = false;
    if (!tinyimx::Logger::Instance().Init(logger)) {
        std::cerr << "logger init failed\n";
        return 3;
    }

    tinyimx::ObservabilityConfig observability;
    observability.enable = true;
    observability.metrics_enable = false;
    observability.traces_enable = true;
    observability.otlp_endpoint = otlp_endpoint;
    observability.export_timeout_ms = 2000;
    observability.shutdown_timeout_ms = 5000;
    observability.trace_max_queue_size = 256;
    observability.trace_max_export_batch_size = 32;
    observability.trace_schedule_delay_ms = 100;

    tinyimx::TelemetryIdentity identity;
    identity.service_name = "tinyimx-trace-agent-e2e";
    identity.service_namespace = "tinyimx";
    identity.service_instance_id = "m20-trace-agent-e2e-1";
    identity.deployment_environment = "test";

    tinyimx::ProcessTelemetry process_telemetry;
    if (!process_telemetry.Initialize(observability, identity)) {
        std::cerr << "telemetry init failed\n";
        tinyimx::Logger::Instance().Shutdown();
        return 4;
    }

    auto transport = std::make_shared<tinyimx::intelligence::NativeHttpClient>();

    tinyimx::mcp::ClientOptions mcp_options;
    mcp_options.endpoint = mcp_endpoint;
    mcp_options.bearer_token = token;
    mcp_options.client_name = "tinyimx-m20-trace-e2e";
    mcp_options.client_version = "m20";
    mcp_options.connect_timeout = std::chrono::milliseconds(2000);
    mcp_options.request_timeout = std::chrono::milliseconds(10000);

    auto mcp_client = std::make_shared<tinyimx::mcp::Client>(
        std::move(mcp_options),
        transport
    );
    auto provider = std::make_shared<DeterministicTraceProvider>(
        expected_username
    );

    tinyimx::ai::AgentOptions agent_options;
    agent_options.model = "m20-deterministic-trace-provider";
    agent_options.system_prompt =
        "M20 trace E2E: call the single allowed TinyIMX read-only tool.";
    agent_options.allowed_tools = {"tinyimx.user.get_self_profile"};
    agent_options.max_tool_rounds = 2;
    agent_options.max_tool_calls_per_round = 2;
    agent_options.max_total_tool_calls = 2;
    agent_options.repeated_identical_call_limit = 2;

    tinyimx::ai::AgentOrchestrator agent(
        std::move(agent_options),
        std::move(provider),
        std::move(mcp_client)
    );

    const auto result = agent.Run("Read my own TinyIMX profile.");

    int rc = 0;
    if (!result.ok) {
        std::cerr << "M20 trace agent error: " << result.error << '\n';
        rc = 5;
    } else if (result.tool_calls != 1 || result.tool_rounds != 1) {
        std::cerr
            << "unexpected tool execution shape: rounds=" << result.tool_rounds
            << " calls=" << result.tool_calls << '\n';
        rc = 6;
    } else if (result.answer != "M20_TRACE_AGENT_OK username=" + expected_username) {
        std::cerr << "unexpected final answer: " << result.answer << '\n';
        rc = 7;
    } else {
        std::cout << result.answer << '\n';
        std::cout << "M20_TRACE_AGENT_TOOL_ROUNDS=" << result.tool_rounds << '\n';
        std::cout << "M20_TRACE_AGENT_TOOL_CALLS=" << result.tool_calls << '\n';
        std::cout << "M20_TRACE_AGENT_E2E=PASS\n";
    }

    process_telemetry.Shutdown();
    tinyimx::Logger::Instance().Shutdown();
    return rc;
}
