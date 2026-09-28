# TinyIMX M20 Observability Foundation

M20 applications emit OpenTelemetry metrics/traces through OTLP/gRPC to a local
OpenTelemetry Collector. The Collector exposes Prometheus metrics on
`127.0.0.1:9464`; Prometheus scrapes that endpoint.

This directory intentionally contains configuration only. M20 does not add
Docker/Kubernetes deployment ownership.

## Local process order

1. Start `otelcol-contrib` with `otel-collector.yaml`.
2. Start Prometheus with `prometheus.yml`.
3. Run `tinyimx_observability_demo config/observability.example.json`.
4. Inspect `http://127.0.0.1:9464/metrics` or query Prometheus.

Expected metric families contain prefixes such as:

- `tinyimx_thread_pool_tasks`
- `tinyimx_thread_pool_task_duration`
- `tinyimx_thread_pool_queue_size`
- `tinyimx_thread_pool_workers`
- `tinyimx_tcp_connections`

Do not add user IDs, message IDs, group IDs, file IDs, request IDs, trace IDs,
raw IP addresses, prompts, message content, or arbitrary error strings as metric
attributes. Those are high-cardinality or privacy-sensitive dimensions.
