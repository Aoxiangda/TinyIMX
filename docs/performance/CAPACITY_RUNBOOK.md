# Audited capacity validation

Use `benchmark/local_capacity/capacity_run.py` against the verified candidate image and real nginx port 9000. Existing private configs and baseline image remain preserved. Never call legacy swap-reset or fixture-reset helpers as part of this runner.

The coordinator verifies runtime identity and owned synthetic user identities before starting. It inserts only explicitly audited missing ring relations; existing rows are not modified. It starts epoll workers with distinct loopback source addresses, ramps 100 users/second in total, waits until every user is authenticated, and starts the offered-load window at a common future monotonic time.

Each run has an immutable unique directory under `.local/codex/capacity-<run>`. Raw ledgers, finite histograms with overflow and raw maximum, worker logs, process IDs, all-online evidence, container identities, memory/pressure samples, and SQL reconciliation are retained on success and failure. SQL is read while recipients remain connected. Positive ACK identity, durable message identity, actual wire delivery, and ReceiverConfirmed state must agree. Snapshot absence must be investigated for late commits and is never called permanent loss without evidence.

Required private gates: all expected users authenticated, no unexpected disconnects, heartbeat success at least 99.99%, all offered requests sent with no scheduler skips, no negative ACK, all sends positively acknowledged, positive ACK P99 at most 100ms, active positive ACK throughput at least 95% of offered load, and every sent logical message durable and receiver-confirmed. Scheduled-to-ACK latency is also retained so client scheduling delay remains visible. The bin value reported for P99 is an upper bound rounded to 0.1ms.

This runner covers normal plaintext private chat and connection hold. It does not establish group/file/MCP/TLS/failover/soak or mixed-workflow capacity. Those coverage obligations remain NOT_RUN until their own explicit scenario and reconciliation pass at the claimed user count.

Packaging regression: source compilation under umask 077 can produce 0700 executable files. Docker COPY preserves that mode and root ownership, preventing the UID1000 service from executing. Normalize image-copy binary modes to 0755 and library modes to 0644, then verify all ten binaries and dependency resolution as UID1000 before deployment. Preserve the failed image and logs. Apply only audited application services with no dependency recreation; keep the original image and private configuration hashes for rollback.
