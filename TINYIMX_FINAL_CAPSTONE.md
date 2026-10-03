# TinyIMX Final Capstone

Final engineering closeout for the M21 production-release line. This stage does not redesign the frozen IM architecture. It adds fault-aware load generation and a reproducible final acceptance matrix around the existing implementation.

## Frozen invariants

- Gateway Registry / Lease / TTL / Presence / Route: Redis.
- Domain service registration/discovery: ZooKeeper.
- Reliable messaging: at-least-once transport + durable idempotency + receiver dedup + monotonic durable state.
- MySQL remains authoritative truth; Redis is ephemeral/read-model state; RocketMQ is the event backbone.
- Message P0 bulkhead stays frozen: foreground / message / replay runtimes remain isolated.

## Final local acceptance gates

1. `failover-connection`: 10k authenticated connections, SIGKILL Gateway-A, reconnect + re-auth + Redis presence convergence, recovery P50/P95/P99.
2. `failover-message`: same fault while private traffic continues; retry uses the same `client_message_id`; durable rows and receiver-confirmed state must converge without visible duplicates.
3. `user-scale`: single UserService vs two ZooKeeper-registered UserService instances; records the true auth knee and scaling efficiency. Local same-host runs characterize distribution; they do not require >1x capacity unless explicitly enabled.
4. `hotspot-group`: hot-user and hot-group degradation curves using the mature M21 benchmark paths.
5. `file`: FileService SIGKILL/resume/hash correctness plus current-runtime download and concurrent-transfer curves.
6. `mq-fault`: RocketMQ broker outage while messages continue; outbox backlog must grow and later drain without durable-message loss or new quarantined events.
7. `backpressure`: intentional overload followed by a low-rate recovery probe; services must remain bounded and recover without container restarts.
8. `soak`: 10k authenticated clients plus sustained private traffic, heartbeat, P99 and resource recovery checks.

## One-shot local run

After building and deploying the final runtime with clean swap and production preflight:

```bash
./scripts/tinyimx_capstone_local_acceptance.sh all
```

The command is intentionally destructive to individual runtime components: it SIGKILLs Gateway-A and FileService and temporarily stops RocketMQ broker. Each experiment restores its target before returning. The runner stops on the first real failure.

Final evidence is grouped under one `final-capstone-<timestamp>` directory. `FINAL_CAPSTONE_REPORT.json` and `.txt` are generated only after all gates pass.

## Stop condition

After local acceptance passes, rebuild one final runtime/deploy artifact set and run one cloud acceptance on the dedicated Alibaba 8C16G SUT. Do not continue micro-optimizing once the measured connection, recovery, auth scaling, message, group/hotspot, file, MQ/outbox, backpressure and soak gates are closed.
