#pragma once

#include "common/config/ConfigTypes.h"
#include "common/observability/ObservabilityTypes.h"

#include <memory>
#include <mutex>

#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>

namespace tinyimx {

class TelemetryRuntime {
public:
    static TelemetryRuntime& Instance();

    bool Initialize(
        const ObservabilityConfig& config,
        const TelemetryIdentity& identity
    );

    bool ForceFlush();
    void Shutdown();

    bool IsInitialized() const;
    bool IsEnabled() const;

private:
    TelemetryRuntime() = default;
    ~TelemetryRuntime() = default;
    TelemetryRuntime(const TelemetryRuntime&) = delete;
    TelemetryRuntime& operator=(const TelemetryRuntime&) = delete;

private:
    mutable std::mutex mutex_;
    bool initialized_{false};
    bool enabled_{false};
    int shutdown_timeout_ms_{5000};

    std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> meter_provider_;
    std::shared_ptr<opentelemetry::sdk::trace::TracerProvider> tracer_provider_;
};

}  // namespace tinyimx
