# TinyIMX Final Performance Candidate

## Scope

This candidate starts from the already-validated Auth A1 baseline and closes the
remaining performance work before the final cloud acceptance. It intentionally
preserves PBKDF2-HMAC-SHA256 at 100000 iterations and all frozen reliable-message
semantics.

### Auth / Login
- Keep credential verification authoritative; move `last_login_at` to a bounded,
  best-effort background executor.
- Configure the synchronous UserService gRPC server so aggregate max pollers track
  CPU capacity instead of the default 1 CQ / 2 pollers.
- Remove synchronous MessageService `CountPending` from Login ACK critical path.
- Expose Login response latency and explicit deadline/unresolved failure counters
  from the load generator.

### Queue isolation
- Keep latency-sensitive Login/Chat on `gateway-foreground-runtime`.
- Run persistent/group replay on an independent bounded
  `gateway-replay-runtime` while preserving per-user ordering.

### Connection plane
- Propagate production `server.backlog` into `listen(2)` instead of hardcoding 128.
- Production backlog: 16384 from rendered Gateway config through NGINX and kernel.
- Raise container nofile for Gateway/NGINX.
- Track active and peak TCP connections; accepted/closed event counters remain the
  existing OTel counters.
- Downgrade per-connection accept/remove logs to DEBUG to avoid a 10k-connection
  logging storm.

### Production correctness
- Fresh MySQL initialization includes file-domain migrations 009/010.
- Existing volumes can be repaired idempotently with
  `scripts/tinyimx_final_schema_apply.sh`.

## Local acceptance order

1. `sudo scripts/tinyimx_final_host_tune.sh`
2. Re-login/reboot, then `scripts/tinyimx_final_preflight.sh benchmark`
3. `scripts/tinyimx_final_verify_vmware.sh <repo>`
4. `scripts/tinyimx_final_build_artifacts.sh <repo>`
5. `scripts/tinyimx_final_local_deploy.sh <repo>`
6. Copy the release `tinyimx_im_loadgen` to external WSL/Linux LoadGen.
7. Run, in order:
   - `tinyimx_final_loadgen_suite.sh connections`
   - `tinyimx_final_loadgen_suite.sh auth`
   - `tinyimx_final_loadgen_suite.sh message`
   - `tinyimx_final_loadgen_suite.sh soak10k`
8. `TINYIMX_FINAL_DRAIN=1 scripts/tinyimx_final_collect_sut.sh <repo>`

## Hard acceptance intent

No performance number is assumed in advance. The candidate is accepted only from
measured evidence. Primary goals are:
- 10k authenticated steady long connections without unexpected disconnects,
  protocol/server errors, FD exhaustion, memory creep, or heartbeat loss.
- `accepted_unaccounted=0` for foreground and replay executors.
- No silent Login timeout: explicit deadline responses are counted separately from
  unresolved failures.
- Auth admission knee moves materially right from the measured >=100 login/s
  baseline.
- A reproducible private-message sustainable floor with throughput and
  P50/P95/P99 evidence.

20k connections are a stretch test only after 10k is stable and the external
LoadGen is not the bottleneck.
