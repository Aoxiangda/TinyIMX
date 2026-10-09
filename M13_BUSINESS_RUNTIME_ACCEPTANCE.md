# TinyIMX M13 Business Execution Runtime Acceptance

## 1. Module goal

M13 removes unpredictable blocking business work from Gateway Sub-Reactor
threads while preserving business correctness, session ownership and the M12
Reliable Messaging contract.

Frozen execution path:

```text
Main/Sub Reactor
  -> cheap protocol parse / identity validation / non-blocking admission
  -> bounded BusinessExecutor
  -> blocking MySQL / Redis / synchronous peer or repository work
  -> Completion
  -> original connection EventLoop
  -> SessionEpoch completion fence
  -> Send
```

The Runtime is intentionally a bounded execution layer, not a general-purpose
job system.

## 2. Frozen production baseline

M13-C1 benchmark-finalized baseline:

```text
worker_threads              = 4
max_pending_tasks           = 128
stripe_count                = 64
per_stripe_queue_capacity   = 32
default_deadline_ms         = 3000
shutdown_timeout_ms         = 30000
```

C1 evidence showed that increasing queue capacity did not materially increase
steady-state throughput but increased tail latency almost linearly.  A single
hot ordering key likewise gained no throughput from a larger per-stripe
backlog, so the frozen baseline favors bounded latency and early load shedding.

Worker count remains aligned with the current MySQL/Redis pool concurrency.

## 3. Admission and backpressure contract

`BusinessExecutor::Submit()` is non-blocking with respect to queue capacity.
It may return:

- `kAccepted`
- `kOverloaded` for global lifecycle capacity
- `kHotKeyOverloaded` for a saturated ordering stripe
- `kDeadlineExpired`
- `kShuttingDown`
- `kInvalidArgument`

There is no synchronous MySQL/Redis fallback on Reactor when admission fails.
A hot key is isolated from unrelated ordering domains; global overload fails
fast rather than allowing unbounded queue growth.

## 4. Cancellation and durable responsibility

### kCancelable

Used for work whose value belongs to the current request/session, such as
queries.  Before worker execution the Runtime may skip work when:

- its deadline expired; or
- its `still_valid` probe reports a stale Session/connection.

### kMustRun

Used after the server has accepted durable responsibility.  Session
invalidation does not cancel the business work.  This applies to durable
mutation/reliable paths such as Chat, Read, Friend mutation and Receiver ACK.

A MustRun work item may still have its network Completion fenced if the
original logical Session is stale.

## 5. Completion ownership contract

Work execution and response delivery are separate concerns.

The Runtime uses two logical completion fences:

1. after worker result, before dispatching the callback to the EventLoop;
2. immediately before the queued callback actually executes.

If a completion validity probe cannot prove that the target is still current,
the Runtime fails closed and drops the Completion.  A dropped or throwing
callback must still release all Runtime lifecycle counters.

`pending_completions` is part of the accepted task lifecycle.  Gateway
shutdown must not destroy Gateway/Repository state while a queued Completion
still exists.

## 6. Local ordering contract

Ordering is process-local and implemented with a fixed number of stripes.

- same stripe: FIFO, one drainer, serialized execution;
- different stripes: may execute in parallel;
- per-stripe capacity prevents one hot logical key from consuming all global
  Runtime capacity.

This is **not** a distributed/global ordering guarantee across Gateway
processes.

## 7. Shutdown contract

Shutdown semantics are:

```text
BeginDrain
  -> reject all new admission
  -> gracefully finish already accepted worker work
  -> wait for pending Completion callbacks
  -> lifecycle residue reaches zero
  -> destroy Runtime
```

`shutdown_timeout_ms` is an operational SLA budget, not a force-cancel safety
boundary.  If the budget is exceeded:

- `ShutdownGraceful()` returns `false`;
- the event is logged/observable;
- accepted work and queued Completion lifecycle are still drained safely.

Safety and durable responsibility take precedence over shutdown latency.

## 8. Runtime observability

The Runtime exposes/prints at least:

- submitted / accepted / completed
- global overload / hot-key / deadline / shutdown / invalid rejection
- deadline-expired-before-start / cancelled-before-start
- worker exception / completion exception / completion dropped
- current and peak pending/active tasks
- pending completions
- average/max queue wait and execution time
- final `within_budget`

A normal Gateway shutdown acceptance requires zero final lifecycle residue:

```text
current_pending=0
current_active=0
pending_completions=0
worker_exceptions=0
completion_exceptions=0
```

## 9. Fault/acceptance matrix

| Case | Fault / pressure | Required behavior |
|---|---|---|
| Production baseline | Frozen 4/128/64/32 | Starts, drains, zero residue |
| Invalid Runtime config | per-stripe > global | Runtime refuses to start |
| Hot key | One stripe saturated | Hot-key reject; unrelated key still executes |
| Global overload | Accepted lifecycle full | Fast `kOverloaded`, no blocking admission |
| Stale cancelable work | Session invalid while queued | Work skipped before consuming dependency |
| Queued deadline | Deadline expires before worker start | Work skipped and counted |
| MustRun + stale completion | Durable work accepted, Session stale | Work runs; old Completion is dropped |
| Drain admission fence | BeginDrain with queued work | New submit rejected; accepted work finishes |
| Shutdown budget | Long accepted work exceeds SLA | `within_budget=false`, work not force-dropped |
| Pending Completion | Worker done, EventLoop callback delayed | Shutdown waits callback lifecycle |
| Dispatcher/completion exception | Callback layer throws | Exception isolated; counters converge |
| Slow business dependency | History worker delayed 500ms | Heartbeat remains responsive |
| Real Gateway overload | Small temporary Runtime + slow History burst | History load shed; heartbeat not blocked |
| M12 final regression | Reliable Messaging fault matrix | 19/19 still passes |

## 10. Final release gate

From the repository root:

```bash
bash scripts/run_m13_final_acceptance.sh
```

The script must exit `0` and finish with:

```text
[M13 PASS] Business Execution Runtime final regression and acceptance gate passed.
```

The script stores logs under:

```text
artifacts/m13-final-YYYYMMDD-HHMMSS/
```

Temporary fault-test configs are created outside the artifact directory with
mode 0600 and deleted on exit so database/cache credentials are not copied into
release evidence.

## 11. Relationship to M12

M13 does not change the frozen M12 guarantee.  Reliable Messaging remains:

```text
At-Least-Once delivery attempts
+ Idempotency
+ Receiver Dedup
+ Monotonic Durable State
```

The final M13 gate reruns the complete M12 regression.  M13 must not introduce
or claim system-wide Exactly-Once delivery.

## 12. Non-goals / deferred work

The following are intentionally not required for M13 closure:

- distributed/global ordering across Gateway processes
- asynchronous MySQL/Redis client replacement
- coroutine scheduler / work stealing / dynamic worker resizing
- adaptive queue sizing
- full MySQL/Redis HA or storage failover testing
- Prometheus/OpenTelemetry production export and dashboards
- Docker/Kubernetes deployment hardening
- RocketMQ transactional/outbox integration
- system-wide Exactly-Once delivery

These belong to later observability/deployment/distributed-system stages and
must not be presented as already implemented by M13.

## 13. Closure criterion

M13 is CLOSED/FROZEN only when the final release gate verifies:

- Config Business Runtime tests all pass;
- Business Runtime unit tests 13/13;
- C2 Runtime acceptance tests all pass;
- Concurrency tests 18/18;
- Gateway tests 32/32;
- production baseline benchmark sanity passes;
- synthetic hot-key and global overload gates pass;
- real slow-business Reactor isolation passes;
- real Gateway overload/load-shedding gate passes;
- shutdown lifecycle residue is zero;
- M12 final regression remains 19/19;
- M13 semantic/config consistency scan passes.
