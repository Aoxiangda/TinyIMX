#include "common/observability/ProcessTelemetry.h"

#include "common/config/Config.h"
#include "common/observability/ObservabilityTypes.h"
#include "common/observability/TelemetryRuntime.h"

namespace tinyimx {

ProcessTelemetry::~ProcessTelemetry() {
    Shutdown();
}

bool ProcessTelemetry::Initialize(
    const Config& config,
    const std::string& service_name,
    const std::string& instance_suffix
) {
    if (initialized_) {
        return true;
    }

    TelemetryIdentity identity;
    identity.service_name = service_name;
    identity.service_namespace = "tinyimx";
    identity.service_instance_id = config.App().instance_id;
    if (!instance_suffix.empty()) {
        identity.service_instance_id += "-" + instance_suffix;
    }
    identity.deployment_environment = config.App().env;

    return Initialize(config.Observability(), identity);
}

bool ProcessTelemetry::Initialize(
    const ObservabilityConfig& config,
    const TelemetryIdentity& identity
) {
    if (initialized_) {
        return true;
    }

    if (!TelemetryRuntime::Instance().Initialize(config, identity)) {
        return false;
    }

    initialized_ = true;
    return true;
}

void ProcessTelemetry::Shutdown() noexcept {
    if (!initialized_) {
        return;
    }
    TelemetryRuntime::Instance().Shutdown();
    initialized_ = false;
}

bool ProcessTelemetry::Initialized() const noexcept {
    return initialized_;
}

}  // namespace tinyimx
