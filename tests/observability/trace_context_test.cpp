#include "common/config/ConfigTypes.h"
#include "common/logging/LogFormatter.h"
#include "common/logging/LogMessage.h"
#include "common/observability/TelemetryRuntime.h"
#include "common/observability/Trace.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main() {
    tinyimx::ObservabilityConfig config;
    config.enable = true;
    config.metrics_enable = false;
    config.traces_enable = true;
    config.otlp_endpoint = "127.0.0.1:65534";
    config.export_timeout_ms = 20;
    config.trace_schedule_delay_ms = 10;
    config.trace_max_queue_size = 64;
    config.trace_max_export_batch_size = 16;
    config.shutdown_timeout_ms = 100;

    tinyimx::TelemetryIdentity identity;
    identity.service_name = "tinyimx-trace-unit";
    identity.service_namespace = "tinyimx";
    identity.service_instance_id = "trace-unit-1";
    identity.deployment_environment = "test";

    auto& runtime = tinyimx::TelemetryRuntime::Instance();
    if (!runtime.Initialize(config, identity)) {
        std::cerr << "telemetry init failed\n";
        return 1;
    }

    auto parent = tinyimx::observability::StartSpan(
        "trace.unit.parent",
        tinyimx::observability::SpanKind::kInternal
    );
    if (!parent.Valid()) {
        std::cerr << "parent span invalid\n";
        return 2;
    }

    const auto parent_ids = parent.Ids();

    // An inbound server request without W3C trace headers must start a new
    // root trace instead of accidentally inheriting an ambient in-process span.
    auto independent_server = tinyimx::observability::StartServerSpanFromHeaders(
        "trace.unit.server.root",
        {}
    );
    if (!independent_server.Valid()) {
        std::cerr << "independent server span invalid\n";
        return 3;
    }
    const auto independent_ids = independent_server.Ids();
    if (independent_ids.trace_id == parent_ids.trace_id) {
        std::cerr << "missing remote context incorrectly inherited ambient parent\n";
        return 4;
    }
    independent_server.MarkOk();
    independent_server.End();

    const auto after_independent = tinyimx::observability::CurrentTraceIds();
    if (after_independent.trace_id != parent_ids.trace_id ||
        after_independent.span_id != parent_ids.span_id) {
        std::cerr << "ambient parent not restored after independent server span\n";
        return 5;
    }

    tinyimx::observability::TraceHeaders headers;
    tinyimx::observability::InjectCurrentTraceHeaders(&headers);
    if (!headers.contains("traceparent")) {
        std::cerr << "traceparent not injected\n";
        return 6;
    }

    auto server = tinyimx::observability::StartServerSpanFromHeaders(
        "trace.unit.server",
        headers
    );
    if (!server.Valid()) {
        std::cerr << "server span invalid\n";
        return 7;
    }
    const auto server_ids = server.Ids();
    if (server_ids.trace_id != parent_ids.trace_id ||
        server_ids.span_id == parent_ids.span_id) {
        std::cerr << "trace parent/child identity mismatch\n";
        return 8;
    }

    tinyimx::LogMessage message;
    message.timestamp = std::chrono::system_clock::now();
    message.level = tinyimx::LogLevel::kInfo;
    message.thread_id = std::this_thread::get_id();
    message.source = {"trace_context_test.cpp", 1, "main"};
    message.message = "trace-log-correlation";
    tinyimx::DefaultLogFormatter formatter;
    const auto formatted = formatter.Format(message);
    if (formatted.find("trace_id=" + server_ids.trace_id) == std::string::npos ||
        formatted.find("span_id=" + server_ids.span_id) == std::string::npos) {
        std::cerr << "log trace correlation missing\n";
        return 9;
    }

    server.MarkOk();
    server.End();

    const auto resumed = tinyimx::observability::CurrentTraceIds();
    if (resumed.trace_id != parent_ids.trace_id ||
        resumed.span_id != parent_ids.span_id) {
        std::cerr << "parent context was not restored\n";
        return 10;
    }

    parent.MarkOk();
    parent.End();
    runtime.Shutdown();

    std::cout << "M20_TRACE_CONTEXT_TEST=PASS\n";
    return 0;
}
