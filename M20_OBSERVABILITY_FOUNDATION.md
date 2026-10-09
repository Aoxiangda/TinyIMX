# TinyIMX M20 Observability Foundation

Status: implementation candidate; not yet accepted or frozen.

## Scope of this vertical slice

- OpenTelemetry C++ SDK integration using the frozen vcpkg baseline.
- OTLP/gRPC metrics exporter and trace-provider foundation.
- Process resource identity (`service.name`, `service.namespace`,
  `service.instance.id`, `deployment.environment.name`).
- Low-cardinality ThreadPool metrics.
- Low-cardinality TcpServer connection metrics.
- OpenTelemetry Collector configuration exposing Prometheus metrics.
- Prometheus scrape/rule configuration.
- Collector-unavailable safety test.

## Explicitly deferred to the next M20 slice

- gRPC client/server trace propagation.
- W3C `traceparent` / `tracestate` injection and extraction.
- MySQL/Redis/RocketMQ spans and metrics.
- MCP/AI request and provider spans/metrics.
- log `trace_id` / `span_id` correlation.
- dashboards and production alert tuning.

## Cardinality contract

Metrics MUST NOT use these as attributes: user_id, message_id,
client_message_id, group_id, file_id, request_id, trace_id, raw IP address,
message content, LLM prompt, or arbitrary error message.

Initial allowed attributes are bounded component identifiers such as
`pool.name`, `server.name`, lifecycle `event`, and success/error `result`.

## Dependency contract

The M20 branch keeps the existing vcpkg baseline and adds only
`opentelemetry-cpp[otlp-grpc]`. The dependency must be dry-run audited before
installation; no script in this slice performs an implicit dependency install.
