# TinyIMX M17-A Final Acceptance

## Status

**PASS / CLOSED**

M17-A is frozen after A1, A2 and A3 acceptance.

## Scope

M17-A establishes the reliable Group management control plane:

- Group domain foundation
- Durable membership
- Owner / Admin / Member authorization
- Durable idempotent group mutations
- Group lifecycle and membership lifecycle
- GroupService gRPC boundary
- Gateway Group control-plane integration
- Static and ZooKeeper service discovery
- Session-authoritative actor identity
- Real TCP vertical integration

## Final Evidence

Artifact directory:

`artifacts/m17-a-final-20260916-063804`

Validated:

- A1 Group Domain retained regression: PASS
- A2 Durable Membership retained regression: PASS
- A3 Gateway Vertical Integration: PASS
- ZooKeeper Group discovery: PASS
- Full ordinary build (-j1): PASS
- Ordinary CTest regression: 30/30 PASS
- Fresh TCP Group E2E: PASS
- Client actor spoof protection: PASS
- Durable mutation retry / REUSED: PASS
- Permission negative path: PASS
- Role mutation: PASS
- Ownership transfer: PASS
- Previous owner leave after transfer: PASS
- New owner disband: PASS

## Frozen Correctness Boundaries

- MySQL remains the authoritative Group durability boundary.
- Gateway does not own Group durable state.
- Gateway actor identity comes from the authenticated Session.
- `client_operation_id` is the durable mutation identity.
- `Packet.seq` remains transport identity.
- Group mutation correctness does not depend on Gateway ordering.
- ZooKeeper provides discovery, not business correctness.
- Redis remains derived/cache state only.
- MQ remains event transport only.

## Explicitly Deferred to M17-B

M17-A does not implement:

- SendGroupMessage
- Group message persistence path
- Per-recipient fanout
- Offline group delivery
- Group unread projection
- Group read cursor
- Multi-Gateway group message delivery

Those belong to **M17-B — Reliable Group Messaging + Fanout**.

## Git Rule

M17-A1, M17-A2 and M17-A3 are committed together only after this closeout.
