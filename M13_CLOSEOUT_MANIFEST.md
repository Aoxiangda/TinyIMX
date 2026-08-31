# TinyIMX M13 Business Execution Runtime Closeout Manifest

## Status

**PENDING FINAL GATE**

This manifest records the frozen M13 engineering contract and the evidence already observed in the user's real Ubuntu environment. M13 becomes **CLOSED / FROZEN** only after `scripts/run_m13_final_acceptance.sh` completes with the final `[M13 PASS]` line after the repository-root `M13_BUSINESS_RUNTIME_ACCEPTANCE.md` is present.

## Frozen production baseline

```text
worker_threads              = 4
max_pending_tasks           = 128
stripe_count                = 64
per_stripe_queue_capacity   = 32
default_deadline_ms         = 3000
shutdown_timeout_ms         = 30000
```

## Frozen execution contract

```text
Main/Sub Reactor
  -> cheap parse / validation / non-blocking admission
  -> bounded BusinessExecutor
  -> blocking MySQL / Redis / synchronous peer or repository work
  -> Completion
  -> original EventLoop
  -> SessionEpoch completion fence
  -> Send
```

- `kCancelable`: stale/deadline-expired request-scoped work may be skipped before dependency execution.
- `kMustRun`: once admitted durable responsibility must continue even if the original session becomes stale.
- Completion delivery is independently fenced from durable work execution.
- Ordering is process-local and striped; it is not a distributed global ordering guarantee.
- Global and per-stripe capacities provide bounded backpressure and hot-key isolation.
- Shutdown stops admission, drains accepted work and pending completions, and reports SLA overrun without silently discarding accepted `kMustRun` work.

## Reliable Messaging boundary inherited from M12

```text
At-Least-Once delivery attempts
+ Idempotency
+ Receiver Dedup
+ Monotonic Durable State
```

M13 does **not** claim system-wide Exactly-Once delivery.

## Real acceptance evidence already observed

The following passed in the user's Ubuntu environment before the final documentation semantic scan:

```text
Config Business Runtime tests       PASS
Business Runtime unit tests         13/13
M13-C2 Runtime acceptance           10/10
Concurrency tests                   18/18
Gateway tests                       32/32
Production baseline benchmark       PASS
Synthetic hot-key gate              PASS
Synthetic global-overload gate      PASS
Real slow-history isolation         PASS
Real Gateway overload/load shedding PASS
M12 final regression                19/19
```

Observed real-Gateway evidence:

```text
slow history ~505 ms
heartbeat during slow history ~0 ms
heartbeat_not_blocked=1

history_overload_count=8
heartbeat during overload ~2 ms
runtime_overload_observed=1
heartbeat_not_blocked=1
```

Representative production baseline evidence:

```text
workers=4
max_pending=128
stripes=64
per_stripe=32
submitted=128
accepted=128
completed=128
worker_exceptions=0
completion_errors=0
current_pending=0
pending_completion=0
within_shutdown_budget=1
```

## Final closure command

From repository root:

```bash
bash scripts/run_m13_final_acceptance.sh
```

M13 is closed only if the command exits `0` and ends with:

```text
[M13] final acceptance gate completed
[M13] PASS=<N> FAIL=0
[M13 PASS] Business Execution Runtime final regression and acceptance gate passed.
```

## Required repository-root release artifacts

```text
M13_BUSINESS_RUNTIME_ACCEPTANCE.md
scripts/run_m13_final_acceptance.sh
```

The acceptance document is part of the release contract, not optional prose. The final semantic/config scan intentionally verifies its reliability wording and the frozen runtime parameters.

## Non-goals / deferred work

M13 closure does not imply completion of:

- distributed/global ordering across Gateway processes;
- asynchronous MySQL/Redis client replacement;
- coroutine scheduler/work stealing/dynamic worker resizing;
- full storage/cache HA and failover;
- Prometheus/OpenTelemetry production dashboards;
- Docker/Kubernetes deployment hardening;
- RocketMQ transactional/outbox integration;
- system-wide Exactly-Once delivery.

These belong to later TinyIMX stages and must not be presented as already implemented by M13.
