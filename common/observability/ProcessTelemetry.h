#pragma once

#include "common/config/ConfigTypes.h"
#include "common/observability/ObservabilityTypes.h"

#include <string>

namespace tinyimx {

class Config;

class ProcessTelemetry final {
public:
    ProcessTelemetry() = default;
    ~ProcessTelemetry();

    ProcessTelemetry(const ProcessTelemetry&) = delete;
    ProcessTelemetry& operator=(const ProcessTelemetry&) = delete;

    bool Initialize(
        const Config& config,
        const std::string& service_name,
        const std::string& instance_suffix
    );

    bool Initialize(
        const ObservabilityConfig& config,
        const TelemetryIdentity& identity
    );

    void Shutdown() noexcept;
    bool Initialized() const noexcept;

private:
    bool initialized_{false};
};

}  // namespace tinyimx
