# 历史实验目录

[返回实验总索引](INDEX.md)

## native-private10k-100-r1

```json
{
  "directory": "native-private10k-100-r1",
  "relative_path": "benchmark/results/peer-20261017/native-private10k-100-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup-index-hint-plan.json",
    "cleanup.json",
    "client-process.json",
    "long-tail-resource-window.json",
    "outbox-index-audit.json",
    "outbox-query-plans.json",
    "sql-digest-diagnostic.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 100,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 6000,
    "sent": 6000,
    "completed_valid": 6000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## native-timx-pair-smoke

```json
{
  "directory": "native-timx-pair-smoke",
  "relative_path": "benchmark/results/peer-20261017/native-timx-pair-smoke",
  "result_sha256": "da2038fad196629f5a3af6af5d957964ac72e9835a87d7eb64d29dd23ab71f97",
  "status": "PASS",
  "issue_id": "PERF-005",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json",
    "sql-reconciliation.json"
  ]
}
```

## perf001-gfxoff-10k750-r1

```json
{
  "directory": "perf001-gfxoff-10k750-r1",
  "relative_path": "benchmark/results/peer-20261017/perf001-gfxoff-10k750-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "performance-gates.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "FAIL",
    "connections": 10000,
    "rate": 750,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 45000,
    "sent": 3017,
    "completed_valid": 3015,
    "negative_ack": 2,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 1,
    "not_scheduled_after_abort": 41983
  }
}
```

## perf001-graphics-control-r1

```json
{
  "directory": "perf001-graphics-control-r1",
  "relative_path": "benchmark/results/peer-20261017/perf001-graphics-control-r1",
  "result_sha256": "fd92ff913f7e517e3b5288429c5e9a5bd687ff467efe9c427bbc3ab28befdb6c",
  "status": "CONTROL_COMPLETE_GUEST_HEALTH_AND_LOAD_RECHECK_PENDING",
  "issue_id": "PERF-001",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## perf020-abba-a1

```json
{
  "directory": "perf020-abba-a1",
  "relative_path": "benchmark/results/peer-20261017/perf020-abba-a1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "offline-analysis.json",
    "sql-after-cid-recovery.json",
    "sql-after.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "FAIL",
    "connections": 10000,
    "rate": 100,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 6000,
    "sent": 1690,
    "completed_valid": 1518,
    "negative_ack": 172,
    "invalid": 0,
    "timeouts": 172,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 4310
  }
}
```

## perf020-abba-a2

```json
{
  "directory": "perf020-abba-a2",
  "relative_path": "benchmark/results/peer-20261017/perf020-abba-a2",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "performance-gates.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "FAIL",
    "connections": 10000,
    "rate": 100,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 6000,
    "sent": 0,
    "completed_valid": 0,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 6000
  }
}
```

## perf020-abba-a2r2

```json
{
  "directory": "perf020-abba-a2r2",
  "relative_path": "benchmark/results/peer-20261017/perf020-abba-a2r2",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "offline-analysis.json",
    "performance-gates.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 100,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 6000,
    "sent": 6000,
    "completed_valid": 6000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf020-abba-b1

```json
{
  "directory": "perf020-abba-b1",
  "relative_path": "benchmark/results/peer-20261017/perf020-abba-b1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "offline-analysis.json",
    "performance-gates.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 100,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 6000,
    "sent": 6000,
    "completed_valid": 6000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf020-abba-b2

```json
{
  "directory": "perf020-abba-b2",
  "relative_path": "benchmark/results/peer-20261017/perf020-abba-b2",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "offline-analysis.json",
    "performance-gates.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 100,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 6000,
    "sent": 6000,
    "completed_valid": 6000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf020-cap250-r1

```json
{
  "directory": "perf020-cap250-r1",
  "relative_path": "benchmark/results/peer-20261017/perf020-cap250-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "offline-analysis.json",
    "performance-gates.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 250,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 15000,
    "sent": 15000,
    "completed_valid": 15000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf020-cap500-r1

```json
{
  "directory": "perf020-cap500-r1",
  "relative_path": "benchmark/results/peer-20261017/perf020-cap500-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "network-pressure-analysis.json",
    "offline-analysis.json",
    "performance-gates.json",
    "sql-after.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "FAIL",
    "connections": 10000,
    "rate": 500,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 30000,
    "sent": 6577,
    "completed_valid": 5156,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 1421,
    "duplicate_deliveries": 31,
    "not_scheduled_after_abort": 23423
  }
}
```

## perf020-smoke-c2

```json
{
  "directory": "perf020-smoke-c2",
  "relative_path": "benchmark/results/peer-20261017/perf020-smoke-c2",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "performance-gates.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 32,
    "rate": 16,
    "seconds": 2,
    "payload_bytes": 128,
    "planned": 32,
    "sent": 32,
    "completed_valid": 32,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## PERF021-cid-recovery-r1

```json
{
  "directory": "PERF021-cid-recovery-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF021-cid-recovery-r1",
  "result_sha256": "cda0a3b587c62435bd0dd07e78e4af983096df5be52255fdd2ef8e6fb2547c93",
  "status": "PASS",
  "issue_id": "PERF-021",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## perf023-cap500-buf16-r1

```json
{
  "directory": "perf023-cap500-buf16-r1",
  "relative_path": "benchmark/results/peer-20261017/perf023-cap500-buf16-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "network-pressure-analysis.json",
    "offline-analysis.json",
    "performance-gates.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 500,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 30000,
    "sent": 30000,
    "completed_valid": 30000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf023-cap500-buf16-r2

```json
{
  "directory": "perf023-cap500-buf16-r2",
  "relative_path": "benchmark/results/peer-20261017/perf023-cap500-buf16-r2",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "performance-gates.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "FAIL",
    "connections": 10000,
    "rate": 500,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 30000,
    "sent": 0,
    "completed_valid": 0,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 30000
  }
}
```

## perf024-go-cap500-r1

```json
{
  "directory": "perf024-go-cap500-r1",
  "relative_path": "benchmark/results/peer-20261017/perf024-go-cap500-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "offline-analysis.json",
    "performance-gates.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 500,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 30000,
    "sent": 30000,
    "completed_valid": 30000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf024-go-cap750-r1

```json
{
  "directory": "perf024-go-cap750-r1",
  "relative_path": "benchmark/results/peer-20261017/perf024-go-cap750-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "offline-analysis.json",
    "performance-gates.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 750,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 45000,
    "sent": 45000,
    "completed_valid": 45000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf024-go-smoke-r1

```json
{
  "directory": "perf024-go-smoke-r1",
  "relative_path": "benchmark/results/peer-20261017/perf024-go-smoke-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "performance-gates.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 32,
    "rate": 16,
    "seconds": 2,
    "payload_bytes": 128,
    "planned": 32,
    "sent": 32,
    "completed_valid": 32,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf025-cap500-b1

```json
{
  "directory": "perf025-cap500-b1",
  "relative_path": "benchmark/results/peer-20261017/perf025-cap500-b1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "diagnostic-comparison.json",
    "offline-analysis.json",
    "outbox-bottleneck-analysis.json",
    "performance-gates.json",
    "publish-profile.json",
    "relay-restoration.json",
    "sql-waits.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 500,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 30000,
    "sent": 30000,
    "completed_valid": 30000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf025-cap750-b1

```json
{
  "directory": "perf025-cap750-b1",
  "relative_path": "benchmark/results/peer-20261017/perf025-cap750-b1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-before.json",
    "network-pressure-analysis.json",
    "offline-analysis.json",
    "relay-restoration.json",
    "sql-diagnostic.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "FAIL",
    "connections": 10000,
    "rate": 750,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 45000,
    "sent": 6872,
    "completed_valid": 6871,
    "negative_ack": 1,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 38128
  }
}
```

## perf026-sealed-1k500-r1

```json
{
  "directory": "perf026-sealed-1k500-r1",
  "relative_path": "benchmark/results/peer-20261017/perf026-sealed-1k500-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-offsets.json",
    "outbox-bottleneck-analysis.json",
    "performance-gates.json",
    "publish-profile.json",
    "relay-identity.json",
    "relay-restoration.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 1000,
    "rate": 500,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 30000,
    "sent": 30000,
    "completed_valid": 30000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf026-w8-cap750-r1

```json
{
  "directory": "perf026-w8-cap750-r1",
  "relative_path": "benchmark/results/peer-20261017/perf026-w8-cap750-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "performance-gates.json",
    "publish-profile.json",
    "relay-identity.json",
    "relay-restoration.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "FAIL",
    "connections": 10000,
    "rate": 750,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 45000,
    "sent": 8233,
    "completed_valid": 8232,
    "negative_ack": 1,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 36767
  }
}
```

## perf028-b8-w4-cap750-r2

```json
{
  "directory": "perf028-b8-w4-cap750-r2",
  "relative_path": "benchmark/results/peer-20261017/perf028-b8-w4-cap750-r2",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "performance-gates.json",
    "publish-profile.json",
    "relay-restoration.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "FAIL",
    "connections": 10000,
    "rate": 750,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 45000,
    "sent": 4879,
    "completed_valid": 4876,
    "negative_ack": 3,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 1,
    "not_scheduled_after_abort": 40121
  }
}
```

## perf028-b8-w8-1k750-r1

```json
{
  "directory": "perf028-b8-w8-1k750-r1",
  "relative_path": "benchmark/results/peer-20261017/perf028-b8-w8-1k750-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "performance-gates.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "FAIL",
    "connections": 1000,
    "rate": 750,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 45000,
    "sent": 5383,
    "completed_valid": 5382,
    "negative_ack": 1,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 2,
    "not_scheduled_after_abort": 39617
  }
}
```

## perf028-diagnostic-smoke-r1

```json
{
  "directory": "perf028-diagnostic-smoke-r1",
  "relative_path": "benchmark/results/peer-20261017/perf028-diagnostic-smoke-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "mq-observer-exit.json",
    "performance-gates.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 32,
    "rate": 16,
    "seconds": 2,
    "payload_bytes": 128,
    "planned": 32,
    "sent": 32,
    "completed_valid": 32,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf028-sealed-smoke-r3

```json
{
  "directory": "perf028-sealed-smoke-r3",
  "relative_path": "benchmark/results/peer-20261017/perf028-sealed-smoke-r3",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "performance-gates.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 32,
    "rate": 16,
    "seconds": 2,
    "payload_bytes": 128,
    "planned": 32,
    "sent": 32,
    "completed_valid": 32,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf030-p1-1k500-a1

```json
{
  "directory": "perf030-p1-1k500-a1",
  "relative_path": "benchmark/results/peer-20261017/perf030-p1-1k500-a1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-offsets.json",
    "outbox-reconciliation.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "relay-identity.json",
    "relay-restoration.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 1000,
    "rate": 500,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 30000,
    "sent": 30000,
    "completed_valid": 30000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf030-p4-10k500-message-r1

```json
{
  "directory": "perf030-p4-10k500-message-r1",
  "relative_path": "benchmark/results/peer-20261017/perf030-p4-10k500-message-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "mq-observer-exit.json",
    "performance-gates.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "FAIL",
    "connections": 10000,
    "rate": 500,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 30000,
    "sent": 0,
    "completed_valid": 0,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 30000
  }
}
```

## perf030-p4-1k500-b1

```json
{
  "directory": "perf030-p4-1k500-b1",
  "relative_path": "benchmark/results/peer-20261017/perf030-p4-1k500-b1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-offsets.json",
    "network-pressure-analysis.json",
    "offline-analysis.json",
    "outbox-reconciliation.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "relay-identity.json",
    "relay-restoration.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 1000,
    "rate": 500,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 30000,
    "sent": 30000,
    "completed_valid": 30000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf030-p4-1k500-b2

```json
{
  "directory": "perf030-p4-1k500-b2",
  "relative_path": "benchmark/results/peer-20261017/perf030-p4-1k500-b2",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cleanup.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-offsets.json",
    "offline-analysis.json",
    "outbox-reconciliation.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "relay-identity.json",
    "relay-restoration.json",
    "sql-digest-deltas.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 1000,
    "rate": 500,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 30000,
    "sent": 30000,
    "completed_valid": 30000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## PERF031-abba-guest-duplicates-E-r64-attempt2

```json
{
  "directory": "PERF031-abba-guest-duplicates-E-r64-attempt2",
  "relative_path": "benchmark/results/peer-20261017/PERF031-abba-guest-duplicates-E-r64-attempt2",
  "result_sha256": "4beef49c8f8c481ae04199ed018da8002cd77523b7437e6effc34fdb7263204d",
  "status": "E_AND_WORKSPACE_AND_GUEST_FULL_SHA_MATCH_ONLY_EIGHT_COMPLETED_DUPLICATES_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "inspection-execution.json",
    "plan.json",
    "result.json",
    "updates.json",
    "updates.ledger-before.json",
    "verified-c-and-e-mapping.json"
  ]
}
```

## PERF031-aof-headroom-batch-r67

```json
{
  "directory": "PERF031-aof-headroom-batch-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-aof-headroom-batch-r67",
  "result_sha256": "c350db6c56844dc104156bb34268b567161a8f45f695a9bfd9258a70eadeb00b",
  "status": "EXISTING15PCT_PLUS650MIB_RESERVE_RESTORED_BY_VERIFIED_NATIVE_AOF",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before.json",
    "progress.json",
    "r17-execution.json",
    "r18-execution.json",
    "r20-execution.json",
    "r21-execution.json",
    "r22-execution.json",
    "r23-execution.json",
    "r24-execution.json",
    "r26-execution.json",
    "r29-execution.json",
    "r30-execution.json",
    "r31-execution.json",
    "r32-execution.json",
    "r34-execution.json",
    "r35-execution.json",
    "r37-execution.json",
    "r38-execution.json",
    "r39-execution.json",
    "r40-execution.json",
    "r43-execution.json",
    "r44-execution.json",
    "r45-execution.json",
    "r46-execution.json",
    "r47-execution.json",
    "r49-execution.json",
    "r52-execution.json",
    "result.json"
  ]
}
```

## PERF031-apt-binary-cache-E-r99

```json
{
  "directory": "PERF031-apt-binary-cache-E-r99",
  "relative_path": "benchmark/results/peer-20261017/PERF031-apt-binary-cache-E-r99",
  "result_sha256": "0355516c9bfe99296d90424e7efb72856d5031d79b942aa482b481282b1142eb",
  "status": "ONLY_TWO_E_VERIFIED_REGENERABLE_APT_INDEX_CACHES_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "plan.json",
    "reclaim.json",
    "result.json"
  ]
}
```

## PERF031-archived-journal-offload-r132-r1

```json
{
  "directory": "PERF031-archived-journal-offload-r132-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-archived-journal-offload-r132-r1",
  "result_sha256": "fa28b03a53f52c8409c0f4f5f3e97378c1fba7d4da2b8557da8127135772fc44",
  "status": "ARCHIVED_JOURNAL_E_INDEPENDENT_RESTORE_VERIFIED_ACTIVE_LOGS_UNTOUCHED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "plan.execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-binary-pkgcache-offload-r134-r1

```json
{
  "directory": "PERF031-binary-pkgcache-offload-r134-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-binary-pkgcache-offload-r134-r1",
  "result_sha256": "cc8d13d172c026b70ac7c868f9de27c26a6c7570c72a81de862c42224c20685a",
  "status": "TWO_REGENERABLE_BINARY_PKGCACHES_E_RESTORED_APT_LOCKS_HELD_PACKAGES_UNCHANGED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "plan.execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-buildtree-offload-probe-r105-r6

```json
{
  "directory": "PERF031-buildtree-offload-probe-r105-r6",
  "relative_path": "benchmark/results/peer-20261017/PERF031-buildtree-offload-probe-r105-r6",
  "result_sha256": "7ba4d4479a72d15fe44552db2282b576217bb7281ed164aabccfc13b6c074adb",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF031-closed-benchmark-offload-r118-r2

```json
{
  "directory": "PERF031-closed-benchmark-offload-r118-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-benchmark-offload-r118-r2",
  "result_sha256": "7544d32cddf14d477ee544acec9960ef7d575564814cab922928a81ff0321b03",
  "status": "ENDED_UNMOUNTED_COMPONENT_BENCHMARK_EXACT_E_COPY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-closed-broker-log-source-r99-retry1

```json
{
  "directory": "PERF031-closed-broker-log-source-r99-retry1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-broker-log-source-r99-retry1",
  "result_sha256": "eec0a70c6e4610b08e26515021da822af5d08951893bb662f753982cda7b0c0a",
  "status": "SIX_ROTATED_POP_TEXT_LOGS_CLOSED_STABLE_SOURCE_ROLLOVER128MB_CURRENT_BELOW64MB",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-closed-broker-reads-offload-r132-r1

```json
{
  "directory": "PERF031-closed-broker-reads-offload-r132-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-broker-reads-offload-r132-r1",
  "result_sha256": "dca61aa58ea676ab4a8941101067ae5fae8225f5e67571de31b0626909004c3c",
  "status": "CLOSED_BROKER_READ_OUTPUTS_E_VERIFIED_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-map.json",
    "result.json"
  ]
}
```

## PERF031-closed-broker-reads-offload-r134-r1

```json
{
  "directory": "PERF031-closed-broker-reads-offload-r134-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-broker-reads-offload-r134-r1",
  "result_sha256": "29ad6af3db494554c8827300ab65af61c82150927415072e9411958a01de795d",
  "status": "CLOSED_BROKER_READ_OUTPUTS_E_VERIFIED_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-map.json",
    "result.json"
  ]
}
```

## PERF031-closed-components-offload-r132-r1

```json
{
  "directory": "PERF031-closed-components-offload-r132-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-components-offload-r132-r1",
  "result_sha256": "980699f2add0e3ad80a6e0e1aeb3c0a0e167d4ec9957404773ffa99a507960b2",
  "status": "TERMINAL_READERS_AND_ENDED_COMPONENT_FIXTURES_E_VERIFIED_OFFLOADED_OWNER_FIXTURES_PRESERVED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-closed-components-offload-r134-r1

```json
{
  "directory": "PERF031-closed-components-offload-r134-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-components-offload-r134-r1",
  "result_sha256": "78c96c3aed66e19d0405c150ec7c711de38b47420852ccfb176e5766dd659444",
  "status": "TERMINAL_READERS_AND_ENDED_COMPONENT_FIXTURES_E_VERIFIED_OFFLOADED_OWNER_FIXTURES_PRESERVED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-closed-native-fixture-offload-r107-r1

```json
{
  "directory": "PERF031-closed-native-fixture-offload-r107-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-native-fixture-offload-r107-r1",
  "result_sha256": "b8bf3890aa2982604d0207f284c5f2d4b1c32dd45a65a380946c3132604ec58d",
  "status": "CLOSED_NATIVE_FIXTURE_EXACT_E_COPY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-closed-native-fixtures-offload-r110-r1

```json
{
  "directory": "PERF031-closed-native-fixtures-offload-r110-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-native-fixtures-offload-r110-r1",
  "result_sha256": "a494e965b84804a7b77a277fcd9eebf71c79a18ddd40187fd6cbbdfa0b142344",
  "status": "CLOSED_NATIVE_COMPONENT_FIXTURES_EXACT_E_COPY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "perf104-resource-r5-execution.json",
    "perf104-resource-r6-execution.json",
    "perf104-resource-r7-execution.json",
    "perf105-application-r11-archive-membership-proof.json",
    "perf105-application-r11-execution-resume.json",
    "perf105-application-r7-archive-membership-proof.json",
    "perf105-application-r7-execution-resume.json",
    "perf105-application-r7-execution.json",
    "perf105-application-r8-archive-membership-proof.json",
    "perf105-application-r8-execution-resume.json",
    "perf105-application-r9-archive-membership-proof.json",
    "perf105-application-r9-execution-resume.json",
    "result.json"
  ]
}
```

## PERF031-closed-native-fixtures-offload-r118-r1

```json
{
  "directory": "PERF031-closed-native-fixtures-offload-r118-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-native-fixtures-offload-r118-r1",
  "result_sha256": "f7faf879ff5c1f925bd92df6d543cd3261d3987779b076346e08395d0e4d3c28",
  "status": "CLOSED_NATIVE_COMPONENT_FIXTURES_EXACT_E_COPY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "perf105-application-r12-archive-membership-proof.json",
    "perf105-application-r12-execution.json",
    "perf105-application-r16-archive-membership-proof.json",
    "perf105-application-r16-execution.json",
    "result.json"
  ]
}
```

## PERF031-closed-native-test-ELF-r104-r6

```json
{
  "directory": "PERF031-closed-native-test-ELF-r104-r6",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-native-test-ELF-r104-r6",
  "result_sha256": "e116e776fed0af31b03814957f0fc9790b5974f7074cf6d903fa27b3f8b85fad",
  "status": null,
  "issue_id": "PERF-031",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF031-closed-native-test-ELF-r104-r7

```json
{
  "directory": "PERF031-closed-native-test-ELF-r104-r7",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-native-test-ELF-r104-r7",
  "result_sha256": "733bfe6dd111c4b10f6016d966a2ad9581ee31f526b6dab1cea3a473e449a491",
  "status": null,
  "issue_id": "PERF-031",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF031-closed-phase-components-offload-r122-r1

```json
{
  "directory": "PERF031-closed-phase-components-offload-r122-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-phase-components-offload-r122-r1",
  "result_sha256": "d4ffd8e0712c17cc1f2ec3cb7845ddf3d976248cc74ddf378931cad6603634c3",
  "status": "TERMINAL_READERS_AND_ENDED_COMPONENT_FIXTURES_E_VERIFIED_OFFLOADED_OWNER_FIXTURES_PRESERVED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-closed-phase-snapshots-offload-r130-r1

```json
{
  "directory": "PERF031-closed-phase-snapshots-offload-r130-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-phase-snapshots-offload-r130-r1",
  "result_sha256": "ec17774b6488d77550bd11fbfdda469af1bd80297c2a8a303d534b35ee7a22a3",
  "status": "ENDED_DIAGNOSTIC_TSV_DUPLICATES_AND_READERS_E_RESTORE_VERIFIED_OFFLOADED_ALL_SOURCE_AND_OWNERS_RETAINED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-closed-pop-logs-E-r99

```json
{
  "directory": "PERF031-closed-pop-logs-E-r99",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-pop-logs-E-r99",
  "result_sha256": "0fae0c4a878c8fb264668f22726c06c9a9e144bba529062d0df679ae9c03ab40",
  "status": "ONLY6_E_AND_RESTORE_VERIFIED_CLOSED_POP_LOG_ARCHIVES_RECLAIMED_NO_ACTOR_INTERRUPTION",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "after-independent-source-read-retry1.json",
    "after-independent-source-read.json",
    "archive-proof.json",
    "before.json",
    "first-restore-failure.json",
    "independent-restore.json",
    "reclaim.json",
    "result.json"
  ]
}
```

## PERF031-closed-trial-evidence-offload-r113-r1

```json
{
  "directory": "PERF031-closed-trial-evidence-offload-r113-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-trial-evidence-offload-r113-r1",
  "result_sha256": "2dc5b735c25c1bea4f053106f65a6a7a7088e06e873e515d0ce38c39b3d030a3",
  "status": "TEN_ENDED_TRIAL_EVIDENCE_FILES_EXACT_E_COPY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-closed-uploads-offload-r118-r1

```json
{
  "directory": "PERF031-closed-uploads-offload-r118-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-closed-uploads-offload-r118-r1",
  "result_sha256": "1afa1e807e4c6ae69e66a0871fb133d204f6da9123650ab06e23963d6013a8e2",
  "status": "ENDED_UPLOADS_AND_ARCHIVES_EXACT_E_COPY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-completed-evidence-copies-r47

```json
{
  "directory": "PERF031-completed-evidence-copies-r47",
  "relative_path": "benchmark/results/peer-20261017/PERF031-completed-evidence-copies-r47",
  "result_sha256": "33dd3eb04db047d7210727982f4c4cad5869febc189fd225c9b7cf63e33aa2b2",
  "status": "VERIFIED_UNREFERENCED_GENERATED_DUPLICATES_ONLY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-execution.json",
    "archive-validation.json",
    "execution.json",
    "inspection.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-completed-peer-evidence-copies-r47

```json
{
  "directory": "PERF031-completed-peer-evidence-copies-r47",
  "relative_path": "benchmark/results/peer-20261017/PERF031-completed-peer-evidence-copies-r47",
  "result_sha256": "69e63a2ae439c46185737c5d7030d3f8e5542267d54c1b6ad23b2afc34362284",
  "status": "VERIFIED_UNREFERENCED_GENERATED_DUPLICATES_ONLY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-execution.json",
    "archive-validation.json",
    "execution.json",
    "inspection.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-completed-raw-duplicates-E-r75

```json
{
  "directory": "PERF031-completed-raw-duplicates-E-r75",
  "relative_path": "benchmark/results/peer-20261017/PERF031-completed-raw-duplicates-E-r75",
  "result_sha256": "46a3af280745bb7466ccd80c680654fb192aabd18d1ef7f2b1f7d0f8b51208b9",
  "status": "ONLY_ENDED_RAW_C_E_GUEST_SHA_IDENTICAL_COPIES_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "plan.json",
    "restore-map.json",
    "result.json"
  ]
}
```

## PERF031-completed-request-copies-r47

```json
{
  "directory": "PERF031-completed-request-copies-r47",
  "relative_path": "benchmark/results/peer-20261017/PERF031-completed-request-copies-r47",
  "result_sha256": "71eaa59609f1d6255b1ba7b3cfeee8d8c612fc0c6d11510251ef6c003bb131b1",
  "status": "VERIFIED_UNREFERENCED_GENERATED_DUPLICATES_ONLY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-execution.json",
    "archive-validation.json",
    "execution.json",
    "inspection.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-completed-resource-copies-r47

```json
{
  "directory": "PERF031-completed-resource-copies-r47",
  "relative_path": "benchmark/results/peer-20261017/PERF031-completed-resource-copies-r47",
  "result_sha256": "b10c5eb54b970d1df1b2481651ef678f481f106c0c684b4eda1a845efbe04af9",
  "status": "VERIFIED_UNREFERENCED_GENERATED_DUPLICATES_ONLY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-execution.json",
    "archive-validation.json",
    "execution.json",
    "inspection.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-current-r7-guest-duplicates-E-r64

```json
{
  "directory": "PERF031-current-r7-guest-duplicates-E-r64",
  "relative_path": "benchmark/results/peer-20261017/PERF031-current-r7-guest-duplicates-E-r64",
  "result_sha256": "be2f94a95ab14867e304ebd4062a3d6d5f6b9f304921ac066f3981824e6478df",
  "status": "E_AND_WORKSPACE_AND_GUEST_FULL_SHA_MATCH_ONLY_TWO_COMPLETED_R64_DUPLICATES_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "inspection-execution.json",
    "plan.json",
    "result.json",
    "verified-c-and-e-mapping.json"
  ]
}
```

## PERF031-current-rolled-log-probe-r105-r7

```json
{
  "directory": "PERF031-current-rolled-log-probe-r105-r7",
  "relative_path": "benchmark/results/peer-20261017/PERF031-current-rolled-log-probe-r105-r7",
  "result_sha256": "b16421b9ab8d27dfb24478e1af2ff6de546486f347962f44c52646b7e068972a",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF031-E-offload-and-route-recovery-r67

```json
{
  "directory": "PERF031-E-offload-and-route-recovery-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-E-offload-and-route-recovery-r67",
  "result_sha256": "fc9c012341566bcdc02dc317cc802c4e5261b960c3720a0f7b87c024647c56d3",
  "status": "EVIDENCE_ON_E_AND629672283_VERIFIED_GUEST_DUPLICATE_OR_CACHE_BYTES_RETIRED_PROTECTED_STATE_REVERIFIED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ledger-updates.json",
    "ledger-updates.ledger-before.json",
    "result.json"
  ]
}
```

## PERF031-ended-analysis-E-r98-retry1

```json
{
  "directory": "PERF031-ended-analysis-E-r98-retry1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ended-analysis-E-r98-retry1",
  "result_sha256": "7115d8790f05b17d8c49cadcd4c7531c92d9aeec135b1bbbde5d7f6ad0938cb2",
  "status": "EXACT_ENDED_EVIDENCE_COPIES_OFFLOADED_AFTER_E_PROOF",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "approval-review-sequence.json",
    "archive-proof.json",
    "cleanup.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-ended-build-support-E-r90

```json
{
  "directory": "PERF031-ended-build-support-E-r90",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ended-build-support-E-r90",
  "result_sha256": "2e7975e16b928061b57c11ad16a3af862bea323d74392997c5d1039b001ea728",
  "status": "EXACT_ENDED_UNMOUNTED_BUILD_SUPPORT_E_ALL_MEMBER_SHA_METADATA_VERIFIED_GUEST_DUPLICATES_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-ended-raw-evidence-r134-r1

```json
{
  "directory": "PERF031-ended-raw-evidence-r134-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ended-raw-evidence-r134-r1",
  "result_sha256": "a697adf639420be7459d48bbbfa4fab7bcc514bd244745edcff11dfad7783701",
  "status": "ENDED_RAW_BENCHMARK_EVIDENCE_E_INDEPENDENT_RESTORE_VERIFIED_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-ended-request-duplicates-E-r83-retry2

```json
{
  "directory": "PERF031-ended-request-duplicates-E-r83-retry2",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ended-request-duplicates-E-r83-retry2",
  "result_sha256": "d869543fe10ce9e61c2f826c0579acb55d1524d0beda55e3f6cec52d1b7c2b11",
  "status": "EXACT14_ENDED_REQUEST_AND_RESOURCE_DUPLICATES_E_SHA_VERIFIED_GUEST_COPIES_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-backup.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-ended-request-duplicates-E-r90

```json
{
  "directory": "PERF031-ended-request-duplicates-E-r90",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ended-request-duplicates-E-r90",
  "result_sha256": "8533da218e20638c49eb19a86a01c4a0ba5b20e60bd41c0f1a0ea4ec956f8f5f",
  "status": "EXACT8_ENDED_REQUEST_AND_RESOURCE_DUPLICATES_E_SHA_VERIFIED_GUEST_COPIES_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-backup.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-ended-request-duplicates-E-r99

```json
{
  "directory": "PERF031-ended-request-duplicates-E-r99",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ended-request-duplicates-E-r99",
  "result_sha256": "b72caacfca3820d27d43a0c4f1e0fa7ce2fc318cc601a01678257462abd29b78",
  "status": "EXACT4_ENDED_REQUEST_AND_RESOURCE_DUPLICATES_E_SHA_VERIFIED_GUEST_COPIES_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-backup.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-ended-structured-evidence-r134-r1

```json
{
  "directory": "PERF031-ended-structured-evidence-r134-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ended-structured-evidence-r134-r1",
  "result_sha256": "23600dd97dd61ed726092d0fceed43637ac68963ac412880191db9a35692cdf9",
  "status": "ENDED_RAW_BENCHMARK_EVIDENCE_E_INDEPENDENT_RESTORE_VERIFIED_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-ended-task-file-census-r113-r2

```json
{
  "directory": "PERF031-ended-task-file-census-r113-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ended-task-file-census-r113-r2",
  "result_sha256": "ca882ff02da51ca6e5b19a16c5ca449b8f2f865b488dc9d8a29159c5838b696b",
  "status": "READ_ONLY_BOUNDED_ENDED_TASK_FILE_CENSUS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-exact-closed-files-r134-r1

```json
{
  "directory": "PERF031-exact-closed-files-r134-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-exact-closed-files-r134-r1",
  "result_sha256": "ed459c3ade0afafe62fe901c51b0bc4dae8d5a46d99cc85ae8658ca32a92ac5e",
  "status": "EXACT_ENDED_FILES_E_INDEPENDENT_RESTORE_VERIFIED_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-failed073-builds-r46-attempt3

```json
{
  "directory": "PERF031-failed073-builds-r46-attempt3",
  "relative_path": "benchmark/results/peer-20261017/PERF031-failed073-builds-r46-attempt3",
  "result_sha256": "ca6932f690f854af1a988525c613c0e47415082d5c1a8606fde6e2faff5fce84",
  "status": "ARCHIVED_FAILED_BUILD_DUPLICATES_ONLY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-validation.json",
    "execution.json",
    "pack-execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-frozen-source-dedup-E-r70

```json
{
  "directory": "PERF031-frozen-source-dedup-E-r70",
  "relative_path": "benchmark/results/peer-20261017/PERF031-frozen-source-dedup-E-r70",
  "result_sha256": "9ebd12582e04246af9299558a149c62dd8fd09bc8981b2f7b6ef20a26e7b1f98",
  "status": "E_FULL_INDEPENDENT_ARCHIVE_ALL_FROZEN_SOURCE_CONTENT_PATH_MODE_OWNER_MTIME_AND_MANIFESTS_PRESERVED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "backup.json",
    "execution.json",
    "plan.json",
    "restore-map.json",
    "result.json"
  ]
}
```

## PERF031-frozen-source-dedup-r99-retry1

```json
{
  "directory": "PERF031-frozen-source-dedup-r99-retry1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-frozen-source-dedup-r99-retry1",
  "result_sha256": "d3c47b344c935da818ebd96013845d85fae6a5014c10a80fea5a157546f41280",
  "status": "IDENTICAL_FROZEN_SOURCE_INODES_DEDUPED_ALL_PATHS_BYTES_AND_FILE_METADATA_PRESERVED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "deduplicate.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-health-round-offload-r1

```json
{
  "directory": "PERF031-health-round-offload-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-health-round-offload-r1",
  "result_sha256": "db6719d51e846963391ed3ed4d7fbca982823e307b1348b3af8197405b3caeec",
  "status": "CLOSED_ARM_OBSERVER_DUPLICATES_E_RESTORE_VERIFIED_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-map.json",
    "result.json"
  ]
}
```

## PERF031-health-round-offload-r2b

```json
{
  "directory": "PERF031-health-round-offload-r2b",
  "relative_path": "benchmark/results/peer-20261017/PERF031-health-round-offload-r2b",
  "result_sha256": "450b6c6b8966e9dc7a4df21edaf2669df91ff059bd1339eff5797509c1edbaff",
  "status": "CLOSED_ARM_OBSERVER_DUPLICATES_E_RESTORE_VERIFIED_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-map.json",
    "result.json"
  ]
}
```

## PERF031-health-round-offload-r3

```json
{
  "directory": "PERF031-health-round-offload-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF031-health-round-offload-r3",
  "result_sha256": "97c2324b3aeab06c864c1f11bdeaf4957797bff49f19e7f36bf1678fb1d6deec",
  "status": "CLOSED_ARM_OBSERVER_DUPLICATES_E_RESTORE_VERIFIED_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-map.json",
    "result.json"
  ]
}
```

## PERF031-health-round-offload-r4

```json
{
  "directory": "PERF031-health-round-offload-r4",
  "relative_path": "benchmark/results/peer-20261017/PERF031-health-round-offload-r4",
  "result_sha256": "fea81f5eb652b16b86cd567d10d6d56a1f87099cab360a214a22c162438bcaa9",
  "status": "CLOSED_ARM_OBSERVER_DUPLICATES_E_RESTORE_VERIFIED_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-map.json",
    "result.json"
  ]
}
```

## PERF031-historical-archives-E-r71-retry2

```json
{
  "directory": "PERF031-historical-archives-E-r71-retry2",
  "relative_path": "benchmark/results/peer-20261017/PERF031-historical-archives-E-r71-retry2",
  "result_sha256": "6771b76decc023770301fa04285be18a5251df77da3aa1b9d1889df5c0d86758",
  "status": "TWO_HISTORICAL_ARCHIVE_COPIES_MOVED_TO_VERIFIED_E_STORAGE",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-copy-verification.json",
    "earlier-attempts.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-idle-official-peer-binary-E-r90

```json
{
  "directory": "PERF031-idle-official-peer-binary-E-r90",
  "relative_path": "benchmark/results/peer-20261017/PERF031-idle-official-peer-binary-E-r90",
  "result_sha256": "5f8e5de4c2fc18b134519feae0ce21ac58c970db170492a72de824d7cd639a85",
  "status": "IDLE_OFFICIAL_PEER_BINARY_PINNED_SHA_E_VERIFIED_OFFLOADED_TINY_PRODUCTS_AND_ALL_CONTAINER_OBJECTS_UNCHANGED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-backup.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-idle-write-observation-r67

```json
{
  "directory": "PERF031-idle-write-observation-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-idle-write-observation-r67",
  "result_sha256": "d1197004c06f59a255e812cdc760047d410dafa471e170cad0078e9a84d9e525",
  "status": "IDLE15S_ORIGINAL20_WRITE_OBSERVATION_NOT_BENCHMARK",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-immutable-ELF-dedup-probe-r105-r8

```json
{
  "directory": "PERF031-immutable-ELF-dedup-probe-r105-r8",
  "relative_path": "benchmark/results/peer-20261017/PERF031-immutable-ELF-dedup-probe-r105-r8",
  "result_sha256": "8004650b8f6ab733ab30135ee8404216a9921b2301b8b26a986d7de0ac6ed985",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF031-ipch-cleanup-r40

```json
{
  "directory": "PERF031-ipch-cleanup-r40",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ipch-cleanup-r40",
  "result_sha256": "389208d2f846cccbb71b4ad2c56dd88fd1a20c64956245094a394c51043e53bf",
  "status": "STALE_IPCH_ONLY_REMOVED_NO_INDEXER_OR_LOAD_RUNNING_ORIGINAL20_HEALTHY",
  "issue_id": "PERF-031",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "combined-cleanup.json",
    "execution.json",
    "implementation-comparison-before.json",
    "ledger-update.json",
    "ledger-update.ledger-before.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-latest-completed-evidence-copies-r57

```json
{
  "directory": "PERF031-latest-completed-evidence-copies-r57",
  "relative_path": "benchmark/results/peer-20261017/PERF031-latest-completed-evidence-copies-r57",
  "result_sha256": "a2928be80bb09b5e403a6a997d2229151c52b84a7b4a1708775c60eca3c0f1e7",
  "status": "VERIFIED_UNREFERENCED_GENERATED_DUPLICATES_ONLY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-execution.json",
    "archive-validation.json",
    "execution.json",
    "inspection.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-latest-completed-evidence-copies-retry2-r47

```json
{
  "directory": "PERF031-latest-completed-evidence-copies-retry2-r47",
  "relative_path": "benchmark/results/peer-20261017/PERF031-latest-completed-evidence-copies-retry2-r47",
  "result_sha256": "6c3b37dc6628b1e771febaed0a89dedf8521ffad64bade3a46225f05ffad8f67",
  "status": "VERIFIED_UNREFERENCED_GENERATED_DUPLICATES_ONLY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-execution.json",
    "archive-validation.json",
    "execution.json",
    "inspection.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-legacy-codex-E-r53

```json
{
  "directory": "PERF031-legacy-codex-E-r53",
  "relative_path": "benchmark/results/peer-20261017/PERF031-legacy-codex-E-r53",
  "result_sha256": "423315e2d965991e733b28d8a17491eefc7a917bbfc7512c5ad513863d7c8a04",
  "status": "LEGACY_EVIDENCE_VERIFIED_ON_E_GUEST_DUPLICATES_ONLY_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-validation.json",
    "execution.json",
    "inspection.json",
    "pack-execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-legacy-codex-E-r55

```json
{
  "directory": "PERF031-legacy-codex-E-r55",
  "relative_path": "benchmark/results/peer-20261017/PERF031-legacy-codex-E-r55",
  "result_sha256": "fb6ffcb81f4c3d90f8c57f4dc0bc9e34d10fb6aa087ad4de6a3977518738b5c4",
  "status": "LEGACY_EVIDENCE_VERIFIED_ON_E_GUEST_DUPLICATES_ONLY_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-validation.json",
    "execution.json",
    "inspection.json",
    "pack-execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-legacy-codex-E-r59

```json
{
  "directory": "PERF031-legacy-codex-E-r59",
  "relative_path": "benchmark/results/peer-20261017/PERF031-legacy-codex-E-r59",
  "result_sha256": "c175bc7d6a241e66f959cda5fde2a7a9007b96f33ab8ffb79684c824536a61f2",
  "status": "LEGACY_EVIDENCE_VERIFIED_ON_E_GUEST_DUPLICATES_ONLY_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-validation.json",
    "execution.json",
    "inspection.json",
    "pack-execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-legacy-final-evidence-E-r73

```json
{
  "directory": "PERF031-legacy-final-evidence-E-r73",
  "relative_path": "benchmark/results/peer-20261017/PERF031-legacy-final-evidence-E-r73",
  "result_sha256": "6b6dd7a32e038550a2e257e5d9617d503695777b21c9c13c70e78e66de8da793",
  "status": "ALL255_ENDED_LEGACY_EVIDENCE_FILES_E_VERIFIED_AND_GUEST_DUPLICATES_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-backup.json",
    "execution.json",
    "plan.json",
    "result.json",
    "root-owned-completion.json"
  ]
}
```

## PERF031-legacy-generated-r47

```json
{
  "directory": "PERF031-legacy-generated-r47",
  "relative_path": "benchmark/results/peer-20261017/PERF031-legacy-generated-r47",
  "result_sha256": "4296ada722d9e4f775fd33d856972267d02e0f61a12d20bb3362b4988178368d",
  "status": "VERIFIED_UNREFERENCED_GENERATED_DUPLICATES_ONLY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-execution.json",
    "archive-validation.json",
    "execution.json",
    "inspection.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-legacy-raw-evidence-r135-r1

```json
{
  "directory": "PERF031-legacy-raw-evidence-r135-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-legacy-raw-evidence-r135-r1",
  "result_sha256": "7cbf28a77d58b339669457e5597e5912eb27df36ff5c6d2ff43db463be3fb231",
  "status": "ENDED_RAW_BENCHMARK_EVIDENCE_E_INDEPENDENT_RESTORE_VERIFIED_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-legacy-unmounted-ELF-E-r102

```json
{
  "directory": "PERF031-legacy-unmounted-ELF-E-r102",
  "relative_path": "benchmark/results/peer-20261017/PERF031-legacy-unmounted-ELF-E-r102",
  "result_sha256": "8b22c4f8060735f8173c66d90abc90ee74af568e6cb2c97a2ef49271ebff0ce4",
  "status": "ONLY_VERIFIED_UNMOUNTED_ENDED_ELF_GUEST_COPIES_OFFLOADED_ALL1766_CONTAINERS_AND_TAGS_RETAINED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "plan.json",
    "reclaim-execution.json",
    "result.json"
  ]
}
```

## PERF031-legacy-unmounted-ELF-E-r102-third

```json
{
  "directory": "PERF031-legacy-unmounted-ELF-E-r102-third",
  "relative_path": "benchmark/results/peer-20261017/PERF031-legacy-unmounted-ELF-E-r102-third",
  "result_sha256": "0e00b0afdb88f9debbb5d17aa60e5c17f54f46f055d2b90427bb4301d4f12934",
  "status": "ONLY_VERIFIED_UNMOUNTED_ENDED_BUILD_AND_EVIDENCE_GUEST_COPIES_OFFLOADED_ALL1826_CONTAINERS_AND_TAGS_RETAINED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "plan.json",
    "reclaim-execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r17-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r17-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r17-r67",
  "result_sha256": "939bbb90962a83b8a00c04a331c4a18256975904d66cf0948139992cbb77c859",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r18-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r18-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r18-r67",
  "result_sha256": "98d3d2c28403578bb53b796ec0bc5e50536caaeb2759e7d22884b831283f19a7",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r20-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r20-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r20-r67",
  "result_sha256": "b0c9f4e5e0e624bbf0b3d59427674ac48b4aa293a5997becb54cbae8245abf17",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r21-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r21-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r21-r67",
  "result_sha256": "585316466321a4d1b23ea3cf7730009316b0a82084a44cfc44506ad8414048be",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r22-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r22-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r22-r67",
  "result_sha256": "c9418bdd938509115e4194041715e77482f665ae8b5c4064b124d53a5ee1e21b",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r23-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r23-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r23-r67",
  "result_sha256": "7cc97828a8ca88e5d266b97dcf02fb7e2bdb3c98bdaaedbe244d077694c0143e",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r24-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r24-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r24-r67",
  "result_sha256": "aea276b11ad79df87316e7c5959cc493739f39948b8f76701351e20c1545e473",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r26-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r26-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r26-r67",
  "result_sha256": "9e941e809d38516c9b73bdfdf9213b6390fba896e29f8e7494c3da8e6e9e4773",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r29-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r29-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r29-r67",
  "result_sha256": "db5176ecd26ef94e1954ce5ab97e8e0fc52b95f750b7cf306b82b0df9b47afe6",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r30-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r30-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r30-r67",
  "result_sha256": "c74b44b88ecbf720650c3b536e82fdd8dd364fba09eebc8da79cda2b2d526617",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r31-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r31-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r31-r67",
  "result_sha256": "1519d30c7c68ef2f4f883002fc4a3b19855c2099f99c6b748ff59d93ff41d222",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r32-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r32-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r32-r67",
  "result_sha256": "dd53b0950a9e71122b8cfdc28b5f169053b003a0633bbfcddc5e96cc6e01afb4",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r33-r67-verification2

```json
{
  "directory": "PERF031-native-aof-rewrite-r33-r67-verification2",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r33-r67-verification2",
  "result_sha256": "2e37d72e4d1ef3d8862bcff2b23fab21fa1e9f07fcca43368f8c9876663faa45",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r34-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r34-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r34-r67",
  "result_sha256": "1726463abbb3ec8e237ee0e5cfa202bcdfb0fd2218054efbbffcb9b57210cc41",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r35-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r35-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r35-r67",
  "result_sha256": "8797cce28ce19bb2177a085793739995c394e7003ffc2a9fe86a7f6f6a04530c",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r37-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r37-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r37-r67",
  "result_sha256": "bc69b70928ef81969aed2597a4b914dad03b115ba4eef3f8d3e36dfd9fd6745d",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r38-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r38-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r38-r67",
  "result_sha256": "c6e4a1eb221020fe2e91975f69f53265fcbe59e04508e964d4ed5de948655e0b",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r39-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r39-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r39-r67",
  "result_sha256": "801ebcc75ca70cf763154bb6ef3e15124bd5a473ee9837f02b10cd4005586799",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r40-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r40-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r40-r67",
  "result_sha256": "19d721d9e2b84c59aadd31a25e1897e572574b0cdb43e7582559b2a487a7fe39",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r43-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r43-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r43-r67",
  "result_sha256": "5ef7a6dd5ed1eb7b644d660441da97fe8b4cfaccd34890d4511cbcdf21d94b43",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r44-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r44-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r44-r67",
  "result_sha256": "889eb6b1984f644fd473a818b86cfae1fc33604dce27e2cde3d004bb5ea376f7",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r45-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r45-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r45-r67",
  "result_sha256": "51ab9a355f242b5d52a11a1f32708ddb8240f3a43d9cb3bfdacf5f4dabb6d129",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r46-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r46-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r46-r67",
  "result_sha256": "3be71b810b3fa53cf42ab5a77d926ab99ed93e1c46c2b82065ce828a379da396",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r47-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r47-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r47-r67",
  "result_sha256": "8e2016d6292cdfb076bd6c83683587e12a65bdbde83b78852129cd4aa22691c2",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r49-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r49-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r49-r67",
  "result_sha256": "a60f9bb9d5e1914bb3b0324e7f36903134ac8ce7aabfe1b3165b3598c4e3aa42",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r50-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r50-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r50-r67",
  "result_sha256": "5971cf81a63e479e14a92ad5efe36a11213209cbf61abf55782a262d2857cf08",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-native-aof-rewrite-r52-r67

```json
{
  "directory": "PERF031-native-aof-rewrite-r52-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-native-aof-rewrite-r52-r67",
  "result_sha256": "6aadf3123a509056158d92c533be946f4c97cb99cf4d4dec6113fa2a4141c097",
  "status": "E_FULL_BEFORE_AFTER_BACKUPS_NATIVE_AOF_COMPACTION_ALL_CANONICAL_KEYS_AND_RELOAD_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-archive.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-object-cache-E-r65

```json
{
  "directory": "PERF031-object-cache-E-r65",
  "relative_path": "benchmark/results/peer-20261017/PERF031-object-cache-E-r65",
  "result_sha256": "f3b84fbacdec4534d94ce872b497702f6cb71df94f0adeb2e8b673bfc0c8da8d",
  "status": "E_ARCHIVE_EVERY508_MEMBER_SHA_SIZE_MODE_PASS_ONLY_REBUILDABLE_OBJECTS_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-execution.json",
    "restore-map.json",
    "result.json",
    "retire-execution.json"
  ]
}
```

## PERF031-object-caches-r45

```json
{
  "directory": "PERF031-object-caches-r45",
  "relative_path": "benchmark/results/peer-20261017/PERF031-object-caches-r45",
  "result_sha256": "f239026bf974c66a62d27552ee926d823535c67e2b913585527eec0dfd8a7040",
  "status": "ONLY_GENERATED_OBJECT_CACHES_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-old-before-snapshots-r132-r1

```json
{
  "directory": "PERF031-old-before-snapshots-r132-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-old-before-snapshots-r132-r1",
  "result_sha256": "d99f611831ca81af650450a0b1ff1ad13363b4e5b4e404f102e299d1f223a11f",
  "status": "OLD_CLOSED_BEFORE_SNAPSHOTS_E_RESTORED_OFFLOADED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-map.json",
    "result.json"
  ]
}
```

## PERF031-old-debug-fixture-r45

```json
{
  "directory": "PERF031-old-debug-fixture-r45",
  "relative_path": "benchmark/results/peer-20261017/PERF031-old-debug-fixture-r45",
  "result_sha256": "13fe9ab5ad8e873c58e71c9a87dd5ab7f4fd6870368bce3636eae0562ac1e173",
  "status": "ONLY_ARCHIVED_GENERATED_DEBUG_TEST_COPIES_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-relocated-r46.json",
    "archive-validation.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-old-debug-tests-r45-attempt2

```json
{
  "directory": "PERF031-old-debug-tests-r45-attempt2",
  "relative_path": "benchmark/results/peer-20261017/PERF031-old-debug-tests-r45-attempt2",
  "result_sha256": "677f3805b97aff16be8fbeda9aaa0848379a3fb6716d28b955849ec3ecfdbd9e",
  "status": "ONLY_ARCHIVED_GENERATED_DEBUG_TEST_COPIES_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-relocated-r46.json",
    "archive-validation.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-old-object-cleanup-r40

```json
{
  "directory": "PERF031-old-object-cleanup-r40",
  "relative_path": "benchmark/results/peer-20261017/PERF031-old-object-cleanup-r40",
  "result_sha256": "90ac352707e71d609a5e8d457cdefd38156bf2794dacd4333230ca82c2ec2b7b",
  "status": "ONLY_OLD_OWNED_OBJECTS_REMOVED_ALL_SOURCES_BINARIES_LIBRARIES_LOGS_DATA_RETAINED",
  "issue_id": "PERF-031",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "ledger-update.json",
    "ledger-update.ledger-before.json",
    "object-plan.json",
    "result.json"
  ]
}
```

## PERF031-old-object-cleanup-r43

```json
{
  "directory": "PERF031-old-object-cleanup-r43",
  "relative_path": "benchmark/results/peer-20261017/PERF031-old-object-cleanup-r43",
  "result_sha256": "f25f123376317b780bf33b7faf1af105da30e86d6b31517a3658968ddcb6c5ac",
  "status": "ONLY_OLD_OWNED_OBJECTS_REMOVED_ALL_SOURCES_BINARIES_LIBRARIES_LOGS_DATA_RETAINED",
  "issue_id": "PERF-031",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "ledger-update.json",
    "ledger-update.ledger-before.json",
    "object-plan.json",
    "result.json"
  ]
}
```

## PERF031-old-sealed-generated-cache-r46

```json
{
  "directory": "PERF031-old-sealed-generated-cache-r46",
  "relative_path": "benchmark/results/peer-20261017/PERF031-old-sealed-generated-cache-r46",
  "result_sha256": "685fad708c99088f44b8655e660c9c711d0e69c258f9b75d8749a753a35beec9",
  "status": "ARCHIVED_FAILED_BUILD_DUPLICATES_ONLY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-validation.json",
    "execution.json",
    "pack-execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-old-sealed-test-binaries-r46-attempt2

```json
{
  "directory": "PERF031-old-sealed-test-binaries-r46-attempt2",
  "relative_path": "benchmark/results/peer-20261017/PERF031-old-sealed-test-binaries-r46-attempt2",
  "result_sha256": "285838dfb55d84bfeb116c1a67780ed31fd83ff5355833cdccfb6a4120c8b350",
  "status": "ARCHIVED_FAILED_BUILD_DUPLICATES_ONLY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-validation.json",
    "execution.json",
    "pack-execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-package-index-cache-offload-r122-r1

```json
{
  "directory": "PERF031-package-index-cache-offload-r122-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-package-index-cache-offload-r122-r1",
  "result_sha256": "3df9ed3cdb10b1f10cfb62ff4b996345e47412d08157628fb5b999a1cb1472be",
  "status": "REGENERABLE_APT_INDEX_CACHE_E_VERIFIED_REMOVED_UNDER_APT_LOCK_NO_PACKAGES_CHANGED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "plan.execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-peer-duplicates-E-r67

```json
{
  "directory": "PERF031-peer-duplicates-E-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-peer-duplicates-E-r67",
  "result_sha256": "90a6246bab5e30bd3d1b06f7380cd79db52dcfaf948daa69f9b1454515b45ec2",
  "status": "FIVE_ENDED_PEER_COPIES_E_SHA_PASS_RETIRED_WITH_RESTORE_MAP",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-map.json",
    "result.json"
  ]
}
```

## PERF031-placement-guest-duplicates-E-r65

```json
{
  "directory": "PERF031-placement-guest-duplicates-E-r65",
  "relative_path": "benchmark/results/peer-20261017/PERF031-placement-guest-duplicates-E-r65",
  "result_sha256": "bf4369edaa66dfdd9657029173544635cbf8301f3eda6d8af6ffcccb8bc5c81c",
  "status": "E_AND_WORKSPACE_AND_GUEST_FULL_SHA_MATCH_ONLY_TWO_COMPLETED_R65_DUPLICATES_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "inspection-execution.json",
    "plan.json",
    "result.json",
    "verified-c-and-e-mapping.json"
  ]
}
```

## PERF031-placement-guest-duplicates-E-r67

```json
{
  "directory": "PERF031-placement-guest-duplicates-E-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-placement-guest-duplicates-E-r67",
  "result_sha256": "92a63ebfadbff1093081e4b45b28da05fbca9104f989bc3dbc586f3c22589cc9",
  "status": "E_AND_WORKSPACE_AND_GUEST_FULL_SHA_MATCH_ONLY_TWO_COMPLETED_R67_DUPLICATES_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "inspection-execution.json",
    "plan.json",
    "result.json",
    "verified-c-and-e-mapping.json"
  ]
}
```

## PERF031-protected-state-r70

```json
{
  "directory": "PERF031-protected-state-r70",
  "relative_path": "benchmark/results/peer-20261017/PERF031-protected-state-r70",
  "result_sha256": "d65b3d7b04be4cba983d65f2052a08394395d7d2123e22bb0fe12faac7e9ae7e",
  "status": "ALL_ORIGINAL20_PROTECTED_NINE_SQL_SCHEMAS_CURRENT_LIVE_EXIT0_AND_ALL_CONTAINER_IMAGE_IDENTITIES_VERIFIED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-protected-state-r72-retry1

```json
{
  "directory": "PERF031-protected-state-r72-retry1",
  "relative_path": "benchmark/results/peer-20261017/PERF031-protected-state-r72-retry1",
  "result_sha256": "c2b42db305f39004355f141b884c03feff7cc8bcd0d2d920292326756a35e3fd",
  "status": "ALL_ORIGINAL20_PROTECTED_NINE_SQL_SCHEMAS_CURRENT_LIVE_EXIT0_AND_ALL_CONTAINER_IMAGE_IDENTITIES_VERIFIED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-protobuf-debug-E-r87

```json
{
  "directory": "PERF031-protobuf-debug-E-r87",
  "relative_path": "benchmark/results/peer-20261017/PERF031-protobuf-debug-E-r87",
  "result_sha256": "86eea462add2919f046aa61bb89a454aafdd4fb6602de13898f9aa2d58a61f3a",
  "status": "EXACT_THREE_UNMOUNTED_PROTOBUF_DEBUG_STATIC_ARCHIVES_E_VERIFIED_OFFLOADED_RELEASE174_DEPENDENCIES_AND_PRODUCT_BINARIES_UNCHANGED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-backup.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-queue-diagnostic-protected-state-r76

```json
{
  "directory": "PERF031-queue-diagnostic-protected-state-r76",
  "relative_path": "benchmark/results/peer-20261017/PERF031-queue-diagnostic-protected-state-r76",
  "result_sha256": "721521a0a86b0399516442d350c446535cfbeea4d0bca6187c7423d71f954d25",
  "status": "ALL_PREEXISTING1517_OBJECTS_PLUS24_NEW_RETAINED_ORIGINAL20_AND_FULL_SQL_SESSIONS_RESTORED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-r10-build-cache-E-r83

```json
{
  "directory": "PERF031-r10-build-cache-E-r83",
  "relative_path": "benchmark/results/peer-20261017/PERF031-r10-build-cache-E-r83",
  "result_sha256": "05d4456f6a8e701c57c4076f41100f2ff017f185272b28c688f72a6e28aec9f6",
  "status": "ALL_UNMOUNTED_R10_OBJECTS_AND_ENDED_REGRESSION_ELFS_E_VERIFIED_GUEST_COPIES_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-backup.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-r5-test-outputs-r47

```json
{
  "directory": "PERF031-r5-test-outputs-r47",
  "relative_path": "benchmark/results/peer-20261017/PERF031-r5-test-outputs-r47",
  "result_sha256": "2d974beee6a82635aa658ffa2e3cb5353dbea1fe83ed2ef3b8c3f03594c22d05",
  "status": "VERIFIED_UNREFERENCED_GENERATED_DUPLICATES_ONLY_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-execution.json",
    "archive-validation.json",
    "execution.json",
    "inspection.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-r8-build-cache-E-r74

```json
{
  "directory": "PERF031-r8-build-cache-E-r74",
  "relative_path": "benchmark/results/peer-20261017/PERF031-r8-build-cache-E-r74",
  "result_sha256": "4f4c4d937ceeddaec69a0155e07e387ceba8bbae213e324f0eb9d9b867fe346e",
  "status": "ALL_UNMOUNTED_R8_OBJECTS_AND_ENDED_REGRESSION_ELFS_E_VERIFIED_GUEST_COPIES_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-backup.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-r9-build-cache-E-r75

```json
{
  "directory": "PERF031-r9-build-cache-E-r75",
  "relative_path": "benchmark/results/peer-20261017/PERF031-r9-build-cache-E-r75",
  "result_sha256": "ff30bc133d419aea57f6c0c9c822d055bf4d1d2691508e8a7a3e498d94a26cef",
  "status": "ALL_UNMOUNTED_R9_OBJECTS_AND_ENDED_REGRESSION_ELFS_E_VERIFIED_GUEST_COPIES_RETIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-backup.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-ready-batch-protected-state-r83

```json
{
  "directory": "PERF031-ready-batch-protected-state-r83",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ready-batch-protected-state-r83",
  "result_sha256": "feb6bbe4658e8dd57fc93d9e6698f2d387b13588180cb549f7d45308a8bab4e3",
  "status": "ALL_PREEXISTING1517_OBJECTS_PLUS99_RETAINED_ORIGINAL20_AND_FULL_SQL_SESSIONS_RESTORED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-ready-batch-protected-state-r85

```json
{
  "directory": "PERF031-ready-batch-protected-state-r85",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ready-batch-protected-state-r85",
  "result_sha256": "fdf2afd9f12c4861d91b11a30f1c2f9a880d4d3791ce9d0af64b26d4e2e53263",
  "status": "ALL_PREEXISTING1517_OBJECTS_PLUS123_RETAINED_ORIGINAL20_AND_FULL_SQL_SESSIONS_RESTORED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-ready-batch-protected-state-r89

```json
{
  "directory": "PERF031-ready-batch-protected-state-r89",
  "relative_path": "benchmark/results/peer-20261017/PERF031-ready-batch-protected-state-r89",
  "result_sha256": "d767db731e8c298e5ade0d347c66a084ba571484ed06464a3ff8c47d902196ab",
  "status": "ALL_PREEXISTING1517_OBJECTS_PLUS166_RETAINED_ORIGINAL20_AND_FULL_SQL_SESSIONS_RESTORED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-redis-aof-ram-proof-r67-attempt3

```json
{
  "directory": "PERF031-redis-aof-ram-proof-r67-attempt3",
  "relative_path": "benchmark/results/peer-20261017/PERF031-redis-aof-ram-proof-r67-attempt3",
  "result_sha256": "a663862675998e2ccfa6faf8c2dc647cae190cd53de35220bd6d3429cfaabd0e",
  "status": "RAM_REWRITE_RELOAD_AND_FULL_SOURCE_ARCHIVE_UNCHANGED_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before-data-manifest.json",
    "original-after.json",
    "original-before.json",
    "ram-calibration.json",
    "result.json",
    "temporary-cleanup.json",
    "temporary-container.json"
  ]
}
```

## PERF031-redis-persistence-inventory-r67

```json
{
  "directory": "PERF031-redis-persistence-inventory-r67",
  "relative_path": "benchmark/results/peer-20261017/PERF031-redis-persistence-inventory-r67",
  "result_sha256": "165cde8aaa515ffefec6ca6a7c648601365e11f2204acf5db6af5b4b136ce202",
  "status": "READ_ONLY_OWNED_STOPPED_REDIS_DATA_SIZE_INVENTORY",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF031-remaining-debug-static-E-r90

```json
{
  "directory": "PERF031-remaining-debug-static-E-r90",
  "relative_path": "benchmark/results/peer-20261017/PERF031-remaining-debug-static-E-r90",
  "result_sha256": "6ae36adbf234caaa08c25fa488f522b8c5c281f3aa10a839d0d1c0611c01b14d",
  "status": "ALL_REMAINING_UNMOUNTED_DEBUG_STATIC_ARCHIVES_E_VERIFIED_OFFLOADED_RELEASE174_DEPENDENCIES_AND_PRODUCT_BINARIES_UNCHANGED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-backup.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-remaining-grpc-debug-E-r86

```json
{
  "directory": "PERF031-remaining-grpc-debug-E-r86",
  "relative_path": "benchmark/results/peer-20261017/PERF031-remaining-grpc-debug-E-r86",
  "result_sha256": "7fea562a78bb0aca025bbc5a573946d1c1b90d43a6e40fdb7276e35760183985",
  "status": "EXACT_EIGHT_UNMOUNTED_GRPC_DEBUG_STATIC_ARCHIVES_E_VERIFIED_OFFLOADED_RELEASE174_DEPENDENCIES_AND_PRODUCT_BINARIES_UNCHANGED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-backup.json",
    "execution.json",
    "plan.json",
    "result.json"
  ]
}
```

## PERF031-round-review-r70

```json
{
  "directory": "PERF031-round-review-r70",
  "relative_path": "benchmark/results/peer-20261017/PERF031-round-review-r70",
  "result_sha256": "ed803807c2872d5d74b9e93d09eb931a7840060ca86ebe00b7112c40ee0e5abf",
  "status": "SUBSTANTIAL_PROGRESS_CLEANUP_AND_CODE_CORRECTNESS_QUALIFIED_PARITY_AND_MATCHED_AB_OPEN",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "comparison-before-E-close.json",
    "comparison-before.json",
    "E-closure.json",
    "ledger-E-close-updates.json",
    "ledger-E-close-updates.ledger-before.json",
    "ledger-updates.json",
    "ledger-updates.ledger-before.json",
    "legacy-current-execution-r70.json",
    "result.json",
    "review-before-E-close.json"
  ]
}
```

## PERF031-round-review-r72

```json
{
  "directory": "PERF031-round-review-r72",
  "relative_path": "benchmark/results/peer-20261017/PERF031-round-review-r72",
  "result_sha256": "69f10ae45cc86f5389b8ad817063d08e2558df5471427f65d70405f46310cd88",
  "status": "AUTO_RESOURCE_CLEANUP_AND_FIRST_SAME_BINARY84K_AB_COMPLETE_NO_OPTIMIZATION_GAIN_OR_PARITY",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "comparison-before-final.json",
    "E-close-updates.json",
    "E-close-updates.ledger-before.json",
    "E-closure.json",
    "E-partial-copy-cleanup.json",
    "ledger-updates.json",
    "ledger-updates.ledger-before.json",
    "result.json"
  ]
}
```

## PERF031-round-review-r74

```json
{
  "directory": "PERF031-round-review-r74",
  "relative_path": "benchmark/results/peer-20261017/PERF031-round-review-r74",
  "result_sha256": "876a5d6a3ffa7f46a198fade90a6a143ce5be75f801dc500ecf7d3ad43f99866",
  "status": "ACTUAL84K_ROW_IMAGE_AB_RECOVERY_AND_INDEPENDENT_BYTE_ACCOUNTING_COMPLETE_NO_PROMOTION",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ledger-updates.json",
    "ledger-updates.ledger-before.json",
    "result.json"
  ]
}
```

## PERF031-row-image-protected-state-r74

```json
{
  "directory": "PERF031-row-image-protected-state-r74",
  "relative_path": "benchmark/results/peer-20261017/PERF031-row-image-protected-state-r74",
  "result_sha256": "039a98ca2329a4ee56a61471ae21d2e7324af857a52ef116b862021115670e0f",
  "status": "ALL_PREEXISTING1493_OBJECTS_PLUS24_NEW_RETAINED_ORIGINAL20_AND_FULL_SQL_SESSIONS_RESTORED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```
