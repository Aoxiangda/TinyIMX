# TinyIMX M20 Distributed Tracing Core

Status: implementation candidate; Ubuntu VM build/test evidence required before promotion.

## Scope

This batch extends the already-accepted M20 metrics foundation with a W3C-compatible tracing core.

Implemented boundaries:

- OpenTelemetry span RAII wrapper with current trace/span ID access.
- W3C `traceparent` / `tracestate` injection and extraction.
- gRPC client/server metadata propagation helper.
- `rpc.client.call.duration` and `rpc.server.call.duration` histograms using low-cardinality RPC attributes.
- Trace/span correlation in TinyIMX formatted logs.
- AI Agent, OpenAI-compatible provider, MCP client, MCP server request, and MCP tool spans.
- Five read-only domain RPC canary paths used by the M19 real AI acceptance chain:
  - UserService/GetUserProfile
  - SocialService/ListFriends
  - MessageService/ListConversations
  - GroupService/ListMyGroups
  - FileService/GetDownloadInfo

## Security/cardinality invariants

Metric attributes are limited to bounded values such as RPC service/method and error type. The batch does not add user IDs, group IDs, file IDs, request IDs, trace IDs, prompts, message content, peer IPs, or arbitrary error text to metric labels.

Trace spans may carry operation metadata, but this batch deliberately does not record user prompt/message content or authentication tokens.

## Architecture invariants

- Tracing failure must never become part of messaging/business correctness.
- Existing protobuf RequestMeta fields remain backward compatible; W3C context is propagated out-of-band via transport metadata/headers.
- MCP actor identity remains principal-derived and is not sourced from trace metadata.
- No new dependency is introduced beyond the existing `opentelemetry-cpp[otlp-grpc]` M20 foundation dependency.
- `gateway/GatewayPeerTransport.h` remains unrelated/protected.

## Acceptance sequence

1. Build tracing core and all touched integration targets with `-j1`.
2. Run tracing context/log-correlation unit gate.
3. Run retained M19 MCP/AI and RPC client unit regressions.
4. Re-run M20 observability foundation gate.
5. Only after focused PASS, enable tracing in process bootstraps and run the real Collector distributed-trace E2E.


## Process telemetry bootstrap and real export gate

The real long-running User/Social/Message/Group/File service demos and MCPServer now initialize
`ProcessTelemetry` from the common `observability` configuration. The production AI agent uses the
same bootstrap through the optional `observability` object in `ai_agent.example.json`; this object is
disabled by default for backward compatibility.

`tests/observability/trace_agent_e2e.cpp` provides a deterministic AgentOrchestrator process for the
real trace gate. It does not fake MCP, gRPC, or MySQL: only the LLM provider is deterministic so the
gate can require exactly one `tinyimx.user.get_self_profile` tool call.

`scripts/run_m20_real_multiprocess_trace_e2e.sh` validates exported Collector JSON evidence for one
continuous trace:

`Agent -> MCP client tools/call -> MCP server tools/call -> MCP tool -> gRPC client -> UserService gRPC server`.

The gate requires one trace ID for the whole chain, exact parentSpanId relationships, and a log
correlation sentinel whose `trace_id`/`span_id` matches the exported Agent root span.
