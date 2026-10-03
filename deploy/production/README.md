# TinyIMX M21 Production Runtime

## Topology

NGINX -> Gateway A/B -> ZooKeeper-discovered User/Social/Message/Group/File services.

Reliable event path:
MySQL Outbox -> Outbox Relay -> RocketMQ Proxy -> Unread Projector -> Redis.

Optional AI path:
AI Agent -> MCPServer -> ZooKeeper-discovered gRPC services.

Observability:
Instrumented TinyIMX processes -> OTLP/gRPC Collector -> Prometheus.

## First run

1. `cp deploy/production/.env.example deploy/production/.env`
2. replace every `CHANGE_ME` value;
3. `bash scripts/m21_prepare_workspace.sh`
4. `bash scripts/run_m21_production_contract_gate.sh`
5. `bash scripts/m21_build_runtime_image.sh`
6. `bash scripts/run_m21_production_runtime_gate.sh`

Runtime state defaults to `~/.local/share/tinyimx/m21` and is outside Git.

## TLS

The runtime gate can generate a self-signed certificate for local acceptance.
Replace it with a CA-issued certificate before Internet-facing deployment.

## AI

AI is request-driven, not a fake daemon. After the stack is ready:

`bash scripts/m21_ai_query.sh "show my TinyIMX profile"`

Kafka is not in the synchronous AI/MCP path.

## Shutdown

`bash scripts/m21_production_down.sh`

Named database, queue, ZooKeeper and Prometheus volumes are retained by default.
