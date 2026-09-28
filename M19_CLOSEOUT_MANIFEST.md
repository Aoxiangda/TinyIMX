# TinyIMX M19 AI/MCPServer Closeout Manifest

Status: **RELEASE CANDIDATE — final acceptance and Git freeze pending**

## Scope closed by M19

M19 adds an intelligence boundary without making AI part of the core messaging correctness path.

Implemented capabilities:

- Native MCP HTTP server on TinyIMX `EventLoop` / `TcpServer` / `ThreadPool`.
- JSON-RPC dispatch, discovery, deterministic registry, tools/resources/prompts registry support.
- Bearer-token authentication, origin validation, request/header validation, bounded request size and worker queue.
- Eight read-only TinyIMX domain tools backed by existing gRPC clients:
  - `tinyimx.user.get_self_profile`
  - `tinyimx.social.list_friends`
  - `tinyimx.message.list_conversations`
  - `tinyimx.message.list_history`
  - `tinyimx.group.get`
  - `tinyimx.group.list_my_groups`
  - `tinyimx.group.list_members`
  - `tinyimx.file.get_metadata`
- Principal-derived actor identity; tool arguments cannot override `actor_user_id`.
- Existing User/Social/Message/Group/File service ACLs remain authoritative.
- Native TinyIMX HTTP client and MCP client; no new third-party HTTP dependency.
- AI provider abstraction plus OpenAI-compatible provider.
- Agent tool-calling loop with tool allowlist, max-round/max-call bounds and repeated-call protection.
- Local/OpenAI-compatible provider support over `http://` for the M19 native client.

## Architecture invariants

- AI/MCP failure must not break login, private messaging, group messaging, reliable messaging or file correctness.
- MCPServer does not own durable domain truth and does not directly query MySQL.
- MySQL remains durable business truth; Redis remains cache/ephemeral state; ZooKeeper remains service discovery; RocketMQ remains the core reliable business-event transport.
- MCPServer reuses existing domain RPC clients and service authorization.
- M19 exposes read-only AI tools only. Write-capable AI tools require a later explicit confirmation/idempotency/audit design.
- `gateway/GatewayPeerTransport.h` is a pre-existing unrelated local delta and must remain outside the M19 release commit.

## Required final evidence

Before release commit/tag, `scripts/run_m19_final_acceptance.sh` must pass and preserve its artifact directory. The gate covers:

- source integrity and Git-diff checks;
- M19 focused unit tests;
- retained Config/RPC/network/protocol/domain regressions affected by M19;
- fake OpenAI-compatible native HTTP agent smoke;
- real OpenAI-compatible LLM -> Agent -> MCP -> 5 gRPC services -> MySQL E2E;
- Group/File ACL negative paths;
- service/listener cleanup.

The release is frozen only after:

1. final acceptance PASS;
2. exact M19 files are staged with `scripts/stage_m19_release.sh`;
3. accepted tree is committed;
4. an annotated `m19-final-YYYYMMDD-rN` tag is created;
5. branch and peeled tag resolve to the same commit on `gitlab`, `origin`, and `gitee`;
6. `scripts/m19_final_freeze_check.sh <tag>` passes.
