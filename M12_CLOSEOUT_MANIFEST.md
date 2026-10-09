# TinyIMX M12 Final Closeout Package

## Files

```text
scripts/run_m12_reliability_regression.sh
docs/M12_RELIABLE_MESSAGING_ACCEPTANCE.md
examples/gateway_peer_duplicate_client_demo.cpp
examples/gateway_peer_durable_replay_client_demo.cpp
```

## Why two peer demo files are included

The production M12 semantics were already hardened. During final closeout audit, the standalone peer-duplicate fixture was still missing Receiver application ACK, while the durable-replay demo assumes a durable receiver-confirmed fixture after Gateway B restart.

The replacement `gateway_peer_duplicate_client_demo.cpp` now:

- keeps the existing durable fixture creation and peer authentication/duplicate tests;
- after the first real Receiver delivery, sends `kChatDeliveryAck(M,D)`;
- allows MySQL to advance `Pending -> ReceiverConfirmed` through the real Gateway path;
- prints `fixture_message_text=` so the final regression script can feed the same fixture into durable-replay validation after Gateway B restart.

The replacement `gateway_peer_durable_replay_client_demo.cpp` changes comments only:

- persisted delivery truth is described as `ReceiverConfirmed`, not generic durable `Delivered`;
- the internal in-memory dedup state may retain its historical `Delivered` naming.

No production Gateway/Repository/Protocol code is modified by this closeout package.

## Existing hardening files

Keep the production and demo replacements already applied from the previous M12 Final Semantic Hardening package. This package is incremental to that successful hardening run; it does not revert or replace those files.

## Install

From the TinyIMX repository root, copy:

```bash
cp <package>/scripts/run_m12_reliability_regression.sh scripts/run_m12_reliability_regression.sh
chmod +x scripts/run_m12_reliability_regression.sh

cp <package>/docs/M12_RELIABLE_MESSAGING_ACCEPTANCE.md docs/M12_RELIABLE_MESSAGING_ACCEPTANCE.md

cp <package>/examples/gateway_peer_duplicate_client_demo.cpp examples/gateway_peer_duplicate_client_demo.cpp
cp <package>/examples/gateway_peer_durable_replay_client_demo.cpp examples/gateway_peer_durable_replay_client_demo.cpp
```

Then run:

```bash
bash scripts/run_m12_reliability_regression.sh
```

Do not keep manually started Gateway A/B processes on ports 9001/9002 while running the script; the regression gate owns their lifecycle.

## v2 closeout fix: offline restart recovery demo

The first full release-gate run correctly exposed one stale regression assumption in
`gateway_offline_recovery_client_demo.cpp`.

The old demo performed the first post-restart replay, closed the Receiver connection
without sending `kChatDeliveryAck(M,D)`, and then expected the same Pending message not
to replay on the second login. That contradicts final M12 semantics: transport replay
alone does not advance durable state. Without Receiver application ACK the row must
remain `Pending` and replay again.

The v2 replacement now:

- keeps a persistent input `Buffer` per TCP connection so half packets survive across
  `WaitForPackets()` calls;
- after the first recovered `kChatDelivery`, sends the real Receiver application ACK
  using `body.message_id = stable M` and `Packet.seq = actual delivery attempt D`;
- waits for durable `Pending -> ReceiverConfirmed` convergence;
- only then disconnects/re-logins and asserts that the receiver-confirmed target M is
  not replayed;
- emits stable verification identity lines for debugging/audit.

No production Gateway, Repository, Protocol, schema, retry, routing or registry logic
is changed by this v2 fix. The existing release script is intentionally unchanged: its
post-condition already requires the recovery fixture to become delivery_status 1 or 2.

## v3 crash-recovery lease fencing fix

The final gate exposed a second orchestration issue in `cross-gateway-b-crash-recovery`.
The crash demo intentionally sends `SIGKILL` to Gateway B. Unlike a graceful stop,
SIGKILL cannot run `GatewayRegistryLease::Stop()` / `GatewayRegistry::UnregisterIfMatch()`.
The Redis registry key therefore correctly remains until its lease TTL expires.

The previous script waited only for TCP port 9002 to close and then immediately
started a new Gateway B using the same `gateway_id`. That restart was correctly
rejected with `gateway_id owned by another lease` while the old lease key was still alive.

v3 fixes only the release-gate orchestration:

- adds Redis `EXISTS` / `TTL` helpers;
- after the SIGKILL and MySQL Pending assertion, waits for
  `tinyimx:gateway:registry:gateway-b` to expire naturally;
- treats `TTL=-1` as an invariant violation;
- never deletes the lease key manually;
- starts the replacement Gateway B only after the old lease fencing window has closed.

No Gateway/Registry production code is changed by v3. This preserves the intended
lease-token fencing semantics and makes the crash test validate them instead of bypassing them.
