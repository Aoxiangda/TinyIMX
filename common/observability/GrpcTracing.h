#pragma once

#include "common/observability/Trace.h"

#include <string>

#include <grpcpp/grpcpp.h>

namespace tinyimx::observability {

TraceSpan StartGrpcClientSpan(
    const std::string& service,
    const std::string& method,
    grpc::ClientContext* context
);

TraceSpan StartGrpcServerSpan(
    const std::string& service,
    const std::string& method,
    const grpc::ServerContext* context
);

void FinishGrpcClientSpan(
    TraceSpan* span,
    const grpc::Status& status,
    const std::string& service,
    const std::string& method
) noexcept;
void FinishGrpcServerSpan(
    TraceSpan* span,
    const grpc::Status& status,
    const std::string& service,
    const std::string& method
) noexcept;

}  // namespace tinyimx::observability
