#include "common/concurrency/ThreadPool.h"
#include "common/observability/TelemetryRuntime.h"

#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <thread>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "CHECK failed: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    auto& runtime = tinyimx::TelemetryRuntime::Instance();
    runtime.Shutdown();

    tinyimx::TelemetryIdentity identity;
    identity.service_name = "tinyimx-m20-runtime-test";
    identity.service_instance_id = "m20-runtime-test-1";
    identity.deployment_environment = "test";

    {
        tinyimx::ObservabilityConfig config;
        config.enable = false;
        Check(runtime.Initialize(config, identity), "disabled runtime initializes as no-op");
        Check(runtime.IsInitialized(), "disabled runtime records initialized state");
        Check(!runtime.IsEnabled(), "disabled runtime remains disabled");
        runtime.Shutdown();
        Check(!runtime.IsInitialized(), "shutdown clears disabled runtime state");
    }

    {
        tinyimx::ObservabilityConfig config;
        config.enable = true;
        config.metrics_enable = true;
        config.traces_enable = false;
        config.otlp_endpoint = "http://127.0.0.1:65534";
        config.metric_export_interval_ms = 100;
        config.export_timeout_ms = 50;
        config.shutdown_timeout_ms = 200;

        Check(runtime.Initialize(config, identity),
              "runtime initialization does not depend on collector availability");
        Check(runtime.IsEnabled(), "runtime enabled");

        tinyimx::ThreadPoolOptions options;
        options.name = "m20-runtime-test-pool";
        options.worker_threads = 2;
        options.queue_capacity = 16;
        options.queue_full_policy = tinyimx::QueueFullPolicy::kBlock;
        options.enable_dynamic_resize = false;

        tinyimx::ThreadPool pool(options);
        Check(pool.Start(), "instrumented thread pool starts");

        auto future = pool.Submit([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return 42;
        });
        Check(future.get() == 42, "instrumented task executes");
        pool.Shutdown(tinyimx::ShutdownMode::kGraceful);

        // A missing Collector may make ForceFlush return false. The acceptance
        // contract here is that the application remains correct and shutdown-safe.
        (void)runtime.ForceFlush();
        runtime.Shutdown();
        Check(!runtime.IsInitialized(), "runtime shuts down with unavailable collector");
    }

    if (failures != 0) {
        return 1;
    }

    std::cout << "M20_OBSERVABILITY_RUNTIME_TESTS=PASS\n";
    return 0;
}
