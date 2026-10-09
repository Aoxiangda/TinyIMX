# TinyIMX M12 Reliable Messaging — Final Acceptance

## 1. Module status

**Target status:** CLOSED after the final regression script passes.

M12 provides reliable private-message delivery semantics for Same-Gateway and Cross-Gateway paths. The final system guarantee is intentionally described as:

> **At-Least-Once delivery attempts + Idempotency + Receiver Dedup + Monotonic Durable State**

TinyIMX M12 does **not** claim system-level Exactly-Once delivery.

---

## 2. Why M12 exists

A successful `TcpConnection::Send()` only proves that the server submitted bytes to its local TCP send path. It does not prove that the receiver application received or applied the message.

M12 therefore separates:

- logical-send identity;
- durable business-message identity;
- individual network/delivery attempts;
- transport submission from receiver confirmation;
- process-local duplicate suppression from durable delivery state.

The goal is that retries, reconnects, response loss, Gateway restarts and cross-Gateway failures converge on the same durable business message without duplicating business side effects.

---

## 3. Frozen identity model

| Identity | Meaning | Retry behavior |
|---|---|---|
| `client_message_id` | Client logical-send identity | Stable across sender retries/resume |
| server `message_id` | Durable business-message identity | Stable for the lifetime of the business message |
| `Packet.seq` | One network/RPC/delivery-attempt identity | Fresh attempt may use a new seq |

Example:

```text
client_message_id = C1
server message_id = M347
receiver attempts  = D1 -> D2 -> D3
```

A late ACK for a real old attempt may confirm the same stable `M347`; an ACK for an unknown attempt must not.

---

## 4. Frozen sender semantics

A sender `chat_ack` has two independent meanings:

- `success=true`: the server accepted responsibility for the logical send;
- `delivered=true`: durable Receiver application confirmation has occurred.

Therefore the normal online first response may be:

```text
success=true
delivered=false
reason=local_delivery_awaiting_receiver_ack
```

After Receiver ACK and durable confirmation, the same logical send may be retried and return:

```text
reused=true
delivered=true
reason=local_already_receiver_confirmed
```

Cross-Gateway retries use the equivalent remote receiver-confirmed semantics.

---

## 5. Frozen durable state machine

```text
Pending (0)
   |
   | Receiver application ACK
   v
ReceiverConfirmed (1)
   |
   | Read Request
   v
Read (2)

Failed (3) is terminal failure state.
```

State transitions are monotonic. A late ACK must not roll `Read` back to `ReceiverConfirmed`, and transport submission must never directly advance `Pending` to `ReceiverConfirmed`.

---

## 6. Receiver ACK model

Gateway sends:

```text
kChatDelivery
Packet.seq = current delivery attempt D
body.message_id = stable business M
```

Receiver replies:

```text
kChatDeliveryAck
Packet.seq = the real attempt D being acknowledged
body.message_id = stable business M
```

Receiver identity is taken from the authenticated session rather than trusted from client-supplied identity fields.

Only this application-level ACK may advance durable `Pending -> ReceiverConfirmed`.

---

## 7. Retry and dedup model

### Sender retry

Same `client_message_id` must reuse the same durable `message_id`.

Database idempotency is protected by the logical-send uniqueness rule `(from_user_id, client_message_id)` and reconciliation after concurrent unique-key races.

### Receiver retry

ACK timeout may cause a fresh delivery attempt:

```text
same M
fresh D
```

Receiver business code deduplicates by stable `message_id`, not by `Packet.seq`.

### Peer retry

Cross-Gateway retry follows the same principle:

```text
same durable M
fresh peer seq
```

Peer-layer historical `kDelivered` naming is not itself sufficient to make Sender `delivered=true`; shared durable ReceiverConfirmed state remains authoritative.

---

## 8. Persistent backlog replay

Persistent offline replay uses keyset pagination by stable `message_id`:

```sql
WHERE to_user_id = ?
  AND delivery_status = 0
  AND message_id > after_message_id
ORDER BY message_id ASC
LIMIT ?
```

This avoids the liveness bug where `LIMIT 100` replayed only the first page and also avoids a naive loop repeatedly reading the same still-Pending page before asynchronous ACKs arrive.

Validated boundary:

```text
105 Pending
-> page 1 = 100
-> page 2 = 5
-> one receiver login
-> 105 unique stable M observed
-> 105 receiver ACKs
-> 0 duplicate business messages
```

`offline_count` is now based on `COUNT(*)` rather than the size of a truncated 100-row list result.

---

## 9. Client logical-send state model

The client retry demos freeze these states:

```text
Created
InFlight
RetryWait
Accepted
Rejected
Uncertain
```

Important rules:

- deterministic rejection is terminal and cannot be resurrected by a late success ACK;
- ambiguous network outcome is not deterministic rejection;
- retry-budget exhaustion enters `Uncertain`;
- explicit resume of the same logical send may reconcile `Uncertain -> Accepted`;
- completion occurs once at the logical-send state-machine level.

---

## 10. Failure / regression matrix

| Regression | Failure model / purpose | Required invariant |
|---|---|---|
| Unit CTest | Core state-machine and protocol regressions | All unit suites pass |
| Session Happy Path | Normal private chat | `Pending -> ReceiverConfirmed -> Read` |
| Same-Gateway Idempotency | Same logical send retried | same C -> same M, unread side effect not duplicated |
| Client Retry | Sender timeout / retry | late valid ACK can complete once; receiver effect not duplicated |
| Sender ACK Loss | Server/Receiver succeed but sender response is lost | reconnect same C -> same M, `reused=true`, `delivered=true` |
| Retry Exhaustion | Multiple ambiguous sender failures | retry budget -> `Uncertain`, explicit resume -> `Accepted` |
| Offline Replay | Receiver offline then login | persistent M replayed and confirmed |
| Offline Restart Recovery | Gateway process restart while M is Pending | DB Pending survives process state loss and is replayed |
| Receiver Reconnect/Dedup | Receiver applies M then disconnects before ACK | replay same M with fresh D; business apply count remains one |
| 105 Pagination | Backlog larger than page size | one login replays 100 + 5 without starvation or duplicate M |
| Peer Duplicate | Same peer business M with fresh peer seq | duplicate peer RPC does not repeat receiver business delivery |
| Peer Durable Replay | Gateway B restart loses in-memory dedup | durable ReceiverConfirmed suppresses replay after restart |
| Cross-Gateway | A on Gateway A, B on Gateway B | stable M through peer forwarding and retry |
| Cross-Gateway Reconnect | Remote receiver disconnect/reconnect | stable M, fresh D, receiver dedup |
| Peer Response Loss | Gateway B executed but first peer response is dropped | Gateway A retry recovers same M without second business effect |
| Gateway B SIGKILL | B crashes before Receiver ACK | durable Pending survives; restart replay recovers same M |
| Semantic Scan | Contract/document drift | no stale `local_delivered`, `local_already_delivered`, or positive Exactly-Once claim |

---

## 11. Final automated release gate

Run from the repository root:

```bash
bash scripts/run_m12_reliability_regression.sh
```

The script:

1. verifies required tools and free Gateway ports;
2. configures/builds M12 release targets;
3. runs the four CTest suites;
4. starts/stops Gateway A/B itself;
5. runs Same-Gateway reliability cases;
6. creates a real Pending fixture, restarts Gateway A and validates durable recovery;
7. runs Cross-Gateway cases;
8. obtains Gateway A's current Redis registry lease token for peer-auth regression;
9. runs peer duplicate and Receiver ACK, then restarts Gateway B and validates durable duplicate suppression;
10. restarts Gateway B with first-peer-response-drop fault injection and validates response-loss recovery;
11. automates Gateway B SIGKILL recovery, asserts MySQL remains Pending, waits for the crashed Gateway B Redis registry lease TTL to expire naturally (preserving lease-token fencing), then restarts B and resumes the existing crash demo;
12. performs the final semantic scan;
13. writes per-case logs and `summary.tsv` under `artifacts/m12-regression-<timestamp>/`.

The script never uses `DELETE`, `TRUNCATE`, `FLUSHDB` or equivalent destructive cleanup for test fixtures.

---

## 12. Last hardening evidence before final release gate

The 2026-08-27 hardening run already verified:

- all selected M12 production/demo build targets compiled successfully;
- Session Happy Path: `M348`, first Sender ACK `delivered=false`, Receiver ACK, then Read;
- Sender ACK Loss: `M349`, reconnect same C, same M, `reused=true`, `delivered=true`, completion count 1;
- Retry Exhaustion: `M350`, three ambiguous attempts -> `Uncertain`, explicit resume -> `Accepted`, completion count 1;
- Receiver Reconnect/Dedup: stable `M351`, `D4 -> D5`, receiver business apply count remained 1;
- Pagination: `M352..M456`, exact `offline_count=105`, 105 unique M, 105 ACKs, duplicate business count 0.

These are hardening evidence; the release gate still reruns the broader M12 matrix before the module is marked closed.

---

## 13. Non-goals / deferred work

The following do not block M12 closure and belong to later runtime/performance stages:

- moving blocking MySQL/Redis work off EventLoop/Sub-Reactor threads (M13 Business Execution Runtime);
- long-lived ReceiverDeliveryTracker memory governance;
- Prometheus/OpenTelemetry reliability metrics;
- broader timeout/backoff configurability;
- global historical API renames such as every internal `kDelivered` symbol;
- system-level Exactly-Once claims;
- RocketMQ/Transactional Outbox business-MQ integration;
- Group, Media, MCP, Docker/K8s and later deployment/benchmark work.

---

## 14. Closure criterion

M12 is **CLOSED** only when the final regression script exits with code `0` and reports:

```text
[M12 PASS] Reliable Messaging final regression and acceptance gate passed.
```

At that point the frozen module statement is:

> TinyIMX M12 implements reliable private-message delivery using At-Least-Once delivery attempts, stable logical/business identities, idempotent persistence, receiver-side deduplication, explicit Receiver ACK, monotonic durable delivery state, retry/replay, and cross-Gateway failure recovery.

The next development module is **M13 Business Execution Runtime**.
