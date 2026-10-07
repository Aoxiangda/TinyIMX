#include "common/observability/TelemetryRuntime.h"

#include "common/observability/Metrics.h"
#include "common/observability/DurationHistogramViews.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_exporter_options.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h>
#include <opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_options.h>
#include <opentelemetry/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h>
#include <opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h>
#include <opentelemetry/sdk/metrics/meter_provider.h>
#include <opentelemetry/sdk/metrics/provider.h>
#include <opentelemetry/sdk/resource/resource.h>
#include <opentelemetry/sdk/trace/batch_span_processor_factory.h>
#include <opentelemetry/sdk/trace/batch_span_processor_options.h>
#include <opentelemetry/sdk/trace/provider.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>
#include <opentelemetry/sdk/trace/tracer_provider_factory.h>
#include <opentelemetry/trace/tracer_provider.h>

namespace tinyimx {
namespace {

namespace metric_sdk = opentelemetry::sdk::metrics;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace resource_sdk = opentelemetry::sdk::resource;
namespace otlp = opentelemetry::exporter::otlp;

resource_sdk::Resource BuildResource(const TelemetryIdentity& identity) {
    return resource_sdk::Resource::Create({
        {"service.name", identity.service_name},
        {"service.namespace", identity.service_namespace},
        {"service.instance.id", identity.service_instance_id},
        {"deployment.environment.name", identity.deployment_environment}
    });
}

bool ValidIdentity(const TelemetryIdentity& identity) {
    return !identity.service_name.empty() &&
           !identity.service_namespace.empty() &&
           !identity.service_instance_id.empty() &&
           !identity.deployment_environment.empty();
}

}  // namespace

TelemetryRuntime& TelemetryRuntime::Instance() {
    static TelemetryRuntime* runtime = new TelemetryRuntime();
    return *runtime;
}

bool TelemetryRuntime::Initialize(
    const ObservabilityConfig& config,
    const TelemetryIdentity& identity
) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return true;
    }

    shutdown_timeout_ms_ = config.shutdown_timeout_ms;

    if (!config.enable) {
        initialized_ = true;
        enabled_ = false;
        return true;
    }

    if (!ValidIdentity(identity)) {
        std::cerr << "observability init failed: invalid telemetry identity\n";
        return false;
    }

    if (config.otlp_endpoint.empty()) {
        std::cerr << "observability init failed: empty OTLP endpoint\n";
        return false;
    }

    const auto resource = BuildResource(identity);

    if (config.metrics_enable) {
        otlp::OtlpGrpcMetricExporterOptions exporter_options;
        exporter_options.endpoint = config.otlp_endpoint;
        exporter_options.use_ssl_credentials = false;
        exporter_options.timeout = std::chrono::milliseconds(config.export_timeout_ms);

        auto exporter = otlp::OtlpGrpcMetricExporterFactory::Create(exporter_options);

        metric_sdk::PeriodicExportingMetricReaderOptions reader_options;
        reader_options.export_interval_millis =
            std::chrono::milliseconds(config.metric_export_interval_ms);
        reader_options.export_timeout_millis =
            std::chrono::milliseconds(config.export_timeout_ms);

        auto reader = metric_sdk::PeriodicExportingMetricReaderFactory::Create(
            std::move(exporter),
            reader_options
        );

        auto provider = std::make_shared<metric_sdk::MeterProvider>(
            observability::MakeDurationHistogramViews(),
            resource
        );
        provider->AddMetricReader(
            std::shared_ptr<metric_sdk::MetricReader>(std::move(reader))
        );

        std::shared_ptr<opentelemetry::metrics::MeterProvider> api_provider = provider;
        metric_sdk::Provider::SetMeterProvider(api_provider);
        meter_provider_ = std::move(provider);

        auto meter = api_provider->GetMeter("tinyimx.observability", "m20");
        Metrics::Instance().Initialize(meter);
    }

    if (config.traces_enable) {
        otlp::OtlpGrpcExporterOptions exporter_options;
        exporter_options.endpoint = config.otlp_endpoint;
        exporter_options.use_ssl_credentials = false;
        exporter_options.timeout = std::chrono::milliseconds(config.export_timeout_ms);

        auto exporter = otlp::OtlpGrpcExporterFactory::Create(exporter_options);

        trace_sdk::BatchSpanProcessorOptions processor_options{};
        processor_options.max_queue_size = config.trace_max_queue_size;
        processor_options.max_export_batch_size = config.trace_max_export_batch_size;
        processor_options.schedule_delay_millis =
            std::chrono::milliseconds(config.trace_schedule_delay_ms);
        processor_options.export_timeout =
            std::chrono::milliseconds(config.export_timeout_ms);

        auto processor = trace_sdk::BatchSpanProcessorFactory::Create(
            std::move(exporter),
            processor_options
        );
        auto provider_unique = trace_sdk::TracerProviderFactory::Create(
            std::move(processor),
            resource
        );
        auto provider = std::shared_ptr<trace_sdk::TracerProvider>(
            std::move(provider_unique)
        );
        std::shared_ptr<opentelemetry::trace::TracerProvider> api_provider = provider;
        trace_sdk::Provider::SetTracerProvider(api_provider);
        tracer_provider_ = std::move(provider);
    }

    initialized_ = true;
    enabled_ = true;
    return true;
}

bool TelemetryRuntime::ForceFlush() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !enabled_) {
        return true;
    }

    const auto timeout = std::chrono::milliseconds(shutdown_timeout_ms_);
    bool ok = true;

    if (meter_provider_) {
        ok = meter_provider_->ForceFlush(timeout) && ok;
    }
    if (tracer_provider_) {
        ok = tracer_provider_->ForceFlush(timeout) && ok;
    }

    return ok;
}

void TelemetryRuntime::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return;
    }

    const auto timeout = std::chrono::milliseconds(shutdown_timeout_ms_);

    if (meter_provider_) {
        meter_provider_->ForceFlush(timeout);
    }
    if (tracer_provider_) {
        tracer_provider_->ForceFlush(timeout);
    }

    Metrics::Instance().Shutdown();

    meter_provider_.reset();
    tracer_provider_.reset();

    std::shared_ptr<opentelemetry::metrics::MeterProvider> no_meter_provider;
    metric_sdk::Provider::SetMeterProvider(no_meter_provider);
    std::shared_ptr<opentelemetry::trace::TracerProvider> no_tracer_provider;
    trace_sdk::Provider::SetTracerProvider(no_tracer_provider);

    enabled_ = false;
    initialized_ = false;
}

bool TelemetryRuntime::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

bool TelemetryRuntime::IsEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_ && enabled_;
}

}  // namespace tinyimx
