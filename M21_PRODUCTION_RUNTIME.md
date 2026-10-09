# TinyIMX M21 Production Runtime

Status: **IMPLEMENTATION CANDIDATE — contract/live Compose acceptance pending**

Baseline:
- M20 final commit: `bb3e3a24180f16d7ac68702d2d185f75c06b5319`
- M20 final tag: `m20-final-20260928-r1`
- M21 branch: `feature/m21-production-release-v1`

This batch creates the Phase-1 single-host production runtime:

- host Release build -> reusable TinyIMX runtime image;
- MySQL durable truth;
- Redis cache + Gateway registry;
- ZooKeeper gRPC service discovery;
- RocketMQ 5.5.0 Docker runtime NameServer + Broker + Proxy;
- User / Social / Message / Group / File services;
- Outbox Relay + Unread Projector;
- dual Gateway instances;
- MCPServer;
- optional one-shot AI Agent profile;
- OpenTelemetry Collector Contrib 0.161.0;
- Prometheus 3.14.0;
- NGINX TCP load balancing and TLS termination;
- generated secret-bearing runtime config outside Git;
- contract and live runtime acceptance gates.

Kafka, Kubernetes, WebRTC and service mesh remain outside TinyIMX Phase-1.

Generated runtime state defaults to `~/.local/share/tinyimx/m21`.
