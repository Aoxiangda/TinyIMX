#pragma once

#include <memory>
#include <vector>
#include <opentelemetry/sdk/metrics/aggregation/aggregation_config.h>
#include <opentelemetry/sdk/metrics/view/view_registry.h>

namespace tinyimx::observability {
// Values and exported units remain seconds. The SDK default 5,10,... buckets
// cannot resolve millisecond RPC tails. Match exact owned instruments only.
inline std::unique_ptr<opentelemetry::sdk::metrics::ViewRegistry>
MakeDurationHistogramViews() {
    namespace sdk = opentelemetry::sdk::metrics;
    auto registry = std::make_unique<sdk::ViewRegistry>();
    for (const char* name : {"rpc.client.call.duration", "rpc.server.call.duration",
                             "tinyimx.thread_pool.task.duration"}) {
        auto config = std::make_shared<sdk::HistogramAggregationConfig>();
        config->boundaries_ = {0.0, 0.001, 0.0025, 0.005, 0.01, 0.02, 0.03,
            0.04, 0.05, 0.06, 0.075, 0.09, 0.1, 0.125, 0.15, 0.2, 0.25,
            0.5, 0.75, 1.0, 2.5, 5.0, 10.0};
        registry->AddView(
            std::make_unique<sdk::InstrumentSelector>(sdk::InstrumentType::kHistogram, name, "s"),
            std::make_unique<sdk::MeterSelector>("tinyimx.observability", "m20", ""),
            std::make_unique<sdk::View>(name, "", sdk::AggregationType::kHistogram, config));
    }
    return registry;
}
}  // namespace tinyimx::observability
