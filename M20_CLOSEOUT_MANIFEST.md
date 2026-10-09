# TinyIMX M20 Observability Closeout Manifest

Status: **RELEASE CANDIDATE — final acceptance and Git freeze pending**

## Scope closed by M20

M20 establishes the production observability boundary for TinyIMX without changing the durable business ownership model.

Implemented and accepted capabilities:

- OpenTelemetry C++ 1.25.0 integrated through the existing frozen vcpkg baseline with `otlp-grpc` only.
- Process resource identity with `service.namespace`, `service.name`, `service.instance.id`, and deployment environment.
- OTLP/gRPC export to OpenTelemetry Collector.
- Prometheus metrics pipeline through Collector with real scrape/query evidence.
- ThreadPool and TCP server metrics with bounded, low-cardinality attributes.
- W3C `traceparent` / `tracestate` propagation.
- gRPC CLIENT/SERVER tracing and RPC latency metrics across User/Social/Message/Group/File read paths.
- AI Agent, OpenAI-compatible provider, MCP client/server, MCP tool and domain RPC tracing.
- Trace-aware log correlation through `trace_id` and `span_id` without replacing the existing logger ownership model.
- Telemetry bootstrap/teardown for User, Social, Message, Group, File, MCPServer, and AI Agent processes.
- Real multi-process Agent -> MCP -> UserService -> MySQL E2E with one trace ID, strict parent-child span validation, RPC semantic attributes, resource identity, secret redaction, listener cleanup, and log/trace correlation.
- Collector-unavailable behavior remains non-fatal to TinyIMX application correctness.

## Architecture invariants

- MySQL remains durable business truth.
- Redis remains cache/ephemeral state.
- ZooKeeper remains service registry/discovery.
- RocketMQ remains the reliable business-event transport.
- Kafka is not part of the M20/Phase-1 online critical path.
- Telemetry failure must not break login, private/group/reliable messaging, file correctness, MCP read tools, or AI request execution.
- Metrics labels must remain low-cardinality and must not include user/message/group/file/request/trace identifiers, payloads, prompts, raw SQL values, secrets, or arbitrary error strings.
- AI/MCP automatic tools remain read-only under the M19 security boundary.
- `gateway/GatewayPeerTransport.h` is a pre-existing unrelated local delta and MUST remain outside the M20 release commit.

## Explicitly out of M20 scope

M20 does **not** claim completion of production deployment or capacity certification. The following are intentionally moved to the final M21 module:

- Docker / Docker Compose deployment packaging;
- Nginx/TLS ingress and production secret distribution;
- full infrastructure telemetry expansion for MySQL/Redis/RocketMQ/ZooKeeper;
- Alertmanager production notification routing;
- large-scale load/soak testing and capacity numbers;
- fault-injection/recovery certification;
- CI/CD image/release pipeline, rollback and final operations runbooks.

This boundary prevents an already-accepted observability subsystem from being reopened for unrelated deployment work.

## Required final evidence

Before commit/tag, `scripts/run_m20_final_acceptance.sh` must pass. It verifies:

1. exact M20 source set and clean Git index;
2. frozen dependency contract and cardinality/source integrity;
3. retained M20 observability/tracing regression;
4. real Collector -> Prometheus scrape/query E2E;
5. seven-process telemetry bootstrap;
6. real multi-process trace export E2E;
7. protected unrelated Gateway delta preservation;
8. secret hygiene, listener cleanup and no reject/orig patch debris.

The release is frozen only after:

1. `M20_FINAL_ACCEPTANCE=PASS`;
2. `scripts/stage_m20_release.sh` reports `M20_EXACT_STAGING=PASS`;
3. one M20 release commit is created on top of `3f42a43f91da6a83fd7a73b10630d48283a06005`;
4. an annotated `m20-final-YYYYMMDD-rN` tag is created at that commit;
5. branch and peeled tag are pushed to `gitlab`, `origin`, and `gitee`;
6. `scripts/m20_final_freeze_check.sh <tag>` reports `M20_FINAL_FREEZE=PASS`.
