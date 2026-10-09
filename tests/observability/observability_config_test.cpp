#include "common/config/Config.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "CHECK failed: " << message << '\n';
        ++failures;
    }
}

std::string BaseConfig(const std::string& observability_json) {
    return std::string(R"JSON({
      "app": {
        "name": "TinyIMX-M20-Test",
        "env": "test",
        "instance_id": "m20-test-1"
      },
      "logger": {
        "level": "info",
        "console": true,
        "file": "logs/m20-test.log"
      },
      "observability": )JSON") + observability_json + "\n}";
}

}  // namespace

int main() {
    {
        tinyimx::Config config;
        Check(config.LoadFromString(R"JSON({
          "app": {
            "name": "TinyIMX-M20-Compatibility",
            "env": "test",
            "instance_id": "m20-compat"
          }
        })JSON"), "config without observability remains backward compatible");
        Check(!config.Observability().enable, "observability defaults disabled");
    }

    {
        tinyimx::Config config;
        Check(config.LoadFromString(BaseConfig(R"JSON({
          "enable": true,
          "metrics_enable": true,
          "traces_enable": false,
          "otlp_endpoint": "http://127.0.0.1:4317",
          "metric_export_interval_ms": 1000,
          "export_timeout_ms": 500,
          "shutdown_timeout_ms": 1000,
          "trace_max_queue_size": 256,
          "trace_max_export_batch_size": 64,
          "trace_schedule_delay_ms": 500
        })JSON")), "valid observability config loads");
        const auto& obs = config.Observability();
        Check(obs.enable, "observability enabled");
        Check(obs.metrics_enable, "metrics enabled");
        Check(!obs.traces_enable, "traces disabled");
        Check(obs.metric_export_interval_ms == 1000, "metric interval parsed");
    }

    {
        tinyimx::Config config;
        Check(!config.LoadFromString(BaseConfig(R"JSON({
          "enable": true,
          "metrics_enable": false,
          "traces_enable": false
        })JSON")), "enabled observability requires at least one signal");
    }

    {
        tinyimx::Config config;
        Check(!config.LoadFromString(BaseConfig(R"JSON({
          "enable": true,
          "metrics_enable": true,
          "otlp_endpoint": "",
          "metric_export_interval_ms": 1000,
          "export_timeout_ms": 500,
          "shutdown_timeout_ms": 1000
        })JSON")), "enabled observability rejects empty endpoint");
    }

    {
        tinyimx::Config config;
        Check(!config.LoadFromString(BaseConfig(R"JSON({
          "enable": true,
          "metrics_enable": true,
          "otlp_endpoint": "http://127.0.0.1:4317",
          "trace_max_queue_size": 16,
          "trace_max_export_batch_size": 32
        })JSON")), "trace batch cannot exceed trace queue");
    }

    if (failures != 0) {
        return 1;
    }

    std::cout << "M20_OBSERVABILITY_CONFIG_TESTS=PASS\n";
    return 0;
}
