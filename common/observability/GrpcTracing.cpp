#include "common/observability/GrpcTracing.h"

#include "common/observability/Metrics.h"

#include <cstdint>
#include <string>

namespace tinyimx::observability {
namespace {

TraceHeaders ServerHeaders(const grpc::ServerContext* context) {
    TraceHeaders headers;
    if (context == nullptr) return headers;
    for (const auto& item : context->client_metadata()) {
        const std::string key(item.first.data(), item.first.size());
        if (key != "traceparent" && key != "tracestate") continue;
        headers[key] = std::string(item.second.data(), item.second.size());
    }
    return headers;
}

void AddRpcAttributes(
    TraceSpan* span,
    const std::string& service,
    const std::string& method
) {
    if (span == nullptr) return;
    span->SetAttribute("rpc.system.name", "grpc");
    span->SetAttribute("rpc.service", service);
    span->SetAttribute("rpc.method", method);
}

void Finish(
    TraceSpan* span,
    const grpc::Status& status,
    bool client,
    const std::string& service,
    const std::string& method
) noexcept {
    if (span == nullptr) return;
    const int status_code = static_cast<int>(status.error_code());
    span->SetAttribute("rpc.grpc.status_code", static_cast<std::int64_t>(status_code));
    if (status.ok()) {
        span->MarkOk();
    } else {
        span->MarkError("grpc.status." + std::to_string(status_code));
    }
    Metrics::Instance().RecordRpcDuration(
        client,
        service,
        method,
        status.ok(),
        span->ElapsedSeconds(),
        status_code
    );
    span->End();
}

}  // namespace

TraceSpan StartGrpcClientSpan(
    const std::string& service,
    const std::string& method,
    grpc::ClientContext* context
) {
    auto span = StartSpan(service + "/" + method, SpanKind::kClient);
    AddRpcAttributes(&span, service, method);
    if (context != nullptr) {
        TraceHeaders headers;
        InjectCurrentTraceHeaders(&headers);
        for (const auto& [key, value] : headers) {
            context->AddMetadata(key, value);
        }
    }
    return span;
}

TraceSpan StartGrpcServerSpan(
    const std::string& service,
    const std::string& method,
    const grpc::ServerContext* context
) {
    auto span = StartServerSpanFromHeaders(service + "/" + method, ServerHeaders(context));
    AddRpcAttributes(&span, service, method);
    return span;
}

void FinishGrpcClientSpan(
    TraceSpan* span,
    const grpc::Status& status,
    const std::string& service,
    const std::string& method
) noexcept {
    Finish(span, status, true, service, method);
}

void FinishGrpcServerSpan(
    TraceSpan* span,
    const grpc::Status& status,
    const std::string& service,
    const std::string& method
) noexcept {
    Finish(span, status, false, service, method);
}

}  // namespace tinyimx::observability
