# M17-A Closeout Manifest

Status: **CLOSED**

Branch:

`feature/m17-group-domain-foundation-v1`

Pre-freeze HEAD:

`789a9cfde9dcb735b15773fb0e9da8f44c5f3e7e`

Final acceptance artifact:

`artifacts/m17-a-final-20260916-063804`

## Closed Engineering Slices

1. M17-A1 — Group Domain Foundation
2. M17-A2 — Durable Membership + Idempotent Mutation
3. M17-A3 — Gateway Vertical Integration

## Final Acceptance Coverage

- Architecture/security gate
- Focused A3 build
- Focused CTest 5/5
- Real MySQL A1 Group-domain regression
- Real MySQL A2 Membership regression
- Real ZooKeeper Group discovery
- Complete ordinary build with `-j1`
- Ordinary CTest 30/30
- Fresh real TCP Group lifecycle E2E
- Session-authoritative actor spoof rejection
- Cross-layer durable idempotent retry / REUSED
- Permission-negative path
- Role and ownership lifecycle
- Final ACTIVE -> DISBANDED lifecycle

## Frozen Architecture Boundary

M17-A owns the Group management control plane.

Group message persistence, fanout, offline delivery, unread/read state and
cross-Gateway Group message delivery remain outside M17-A.

## Next Engineering Slice

**M17-B — Reliable Group Messaging + Fanout**
