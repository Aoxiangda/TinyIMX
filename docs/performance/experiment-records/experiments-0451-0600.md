# 历史实验目录

[返回实验总索引](INDEX.md)

## perf087-exact-key-10k700-r71-off

```json
{
  "directory": "perf087-exact-key-10k700-r71-off",
  "relative_path": "benchmark/results/peer-20261017/perf087-exact-key-10k700-r71-off",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf087-exact-key-10k700-r72-on

```json
{
  "directory": "perf087-exact-key-10k700-r72-on",
  "relative_path": "benchmark/results/peer-20261017/perf087-exact-key-10k700-r72-on",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## PERF087-exact-key-AB-r72

```json
{
  "directory": "PERF087-exact-key-AB-r72",
  "relative_path": "benchmark/results/peer-20261017/PERF087-exact-key-AB-r72",
  "result_sha256": "1175f76c5241986d54415789d1c711a0cfdf553e636a116f1eee956866c32c8f",
  "status": "FIRST_SAME_R8_GATEWAY_OFF_ON_PAIR_FULL84K_CORRECT_REVERSE_REPEAT_REQUIRED",
  "issue_id": "PERF-087",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF087-exact-key-trial-plan-r71

```json
{
  "directory": "PERF087-exact-key-trial-plan-r71",
  "relative_path": "benchmark/results/peer-20261017/PERF087-exact-key-trial-plan-r71",
  "result_sha256": "6e4ad1040b95b87bab2f65c533b6419e66d06d69070fb7b1b2120359dbf2c9fb",
  "status": "FORMAL_SAME_R8_GATEWAY_EXACT_KEY_POINT_COLLECTED_REVERSE_REPEAT_PENDING",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF087-exact-key-trial-plan-r72

```json
{
  "directory": "PERF087-exact-key-trial-plan-r72",
  "relative_path": "benchmark/results/peer-20261017/PERF087-exact-key-trial-plan-r72",
  "result_sha256": "53132b423d677809ba40364f3348df33b8a0d3399250343fe406290720c578d0",
  "status": "FORMAL_SAME_R8_GATEWAY_EXACT_KEY_POINT_COLLECTED_REVERSE_REPEAT_PENDING",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF089-actual-row-image-binlogs-r74

```json
{
  "directory": "PERF089-actual-row-image-binlogs-r74",
  "relative_path": "benchmark/results/peer-20261017/PERF089-actual-row-image-binlogs-r74",
  "result_sha256": "64b1b8456fe793893aee0d8bda1899d07a9fc15f5b8ddd1d17aabc618ef75317",
  "status": "BOTH_IMMUTABLE_OWNED_PREFIXES_DECODED_READ_ONLY",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "r73-result.json",
    "r74-result.json",
    "result.json",
    "source-identity.json"
  ]
}
```

## PERF089-binlog-accounting-qualification-r73

```json
{
  "directory": "PERF089-binlog-accounting-qualification-r73",
  "relative_path": "benchmark/results/peer-20261017/PERF089-binlog-accounting-qualification-r73",
  "result_sha256": "6280f8e02e59f69108d80ee2309685ac41d89240b1b53377beddcfc3433c0916",
  "status": "VENDOR_PSEUDOROW_AND_INDEPENDENT_BINARY_CRC_TABLE_OP_ACCOUNTING_EXACTLY_AGREE",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF089-binlog-analysis-r73-retry2

```json
{
  "directory": "PERF089-binlog-analysis-r73-retry2",
  "relative_path": "benchmark/results/peer-20261017/PERF089-binlog-analysis-r73-retry2",
  "result_sha256": "4a8a8bf1594af261b06e1efc89ee70a7be83d11584ad68e04660452e1191d3f6",
  "status": "BOTH_IMMUTABLE_OWNED_PREFIXES_DECODED_READ_ONLY",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "r71-accounting-before-payload-check.json",
    "r71-result.json",
    "r72-result.json",
    "result.json",
    "source-identity.json"
  ]
}
```

## PERF089-binlog-analysis-r73-retry3

```json
{
  "directory": "PERF089-binlog-analysis-r73-retry3",
  "relative_path": "benchmark/results/peer-20261017/PERF089-binlog-analysis-r73-retry3",
  "result_sha256": "809a09bf4b3163f80f8011fff4b175809951ab03b930fb910ce2916b28ff4579",
  "status": "BOTH_IMMUTABLE_OWNED_PREFIXES_DECODED_READ_ONLY",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "r71-accounting-before-payload-check.json",
    "r71-result.json",
    "r72-result.json",
    "result.json",
    "source-identity.json"
  ]
}
```

## PERF089-binlog-raw-oracle-r73

```json
{
  "directory": "PERF089-binlog-raw-oracle-r73",
  "relative_path": "benchmark/results/peer-20261017/PERF089-binlog-raw-oracle-r73",
  "result_sha256": "4b5ad5bff19e033f736843979d1109c12972b44fabb04ae7dea3f65c871b50c2",
  "status": "BOTH_RAW_PREFIXES_FRAMING_AND_CRC_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-prefix-backup.json",
    "execution.json",
    "r71.json",
    "r72.json",
    "result.json"
  ]
}
```

## PERF089-current-raw-oracle-r74

```json
{
  "directory": "PERF089-current-raw-oracle-r74",
  "relative_path": "benchmark/results/peer-20261017/PERF089-current-raw-oracle-r74",
  "result_sha256": "8b9f40fe450c23e59a8de09ce3a6da6281df9e1d5540849d20628134bbe91b86",
  "status": "BOTH_RAW_PREFIXES_FRAMING_AND_CRC_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-prefix-backup.json",
    "execution.json",
    "r73.json",
    "r74.json",
    "result.json"
  ]
}
```

## perf089-row-image-10k700-r73-full

```json
{
  "directory": "perf089-row-image-10k700-r73-full",
  "relative_path": "benchmark/results/peer-20261017/perf089-row-image-10k700-r73-full",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "binlog-end.json",
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "mysql-row-image.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf089-row-image-10k700-r74-minimal

```json
{
  "directory": "perf089-row-image-10k700-r74-minimal",
  "relative_path": "benchmark/results/peer-20261017/perf089-row-image-10k700-r74-minimal",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "binlog-end.json",
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "mysql-row-image.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## PERF089-row-image-AB-r74

```json
{
  "directory": "PERF089-row-image-AB-r74",
  "relative_path": "benchmark/results/peer-20261017/PERF089-row-image-AB-r74",
  "result_sha256": "870fe0ed5c6c29fd8f9a127bc8ec5e51f54c159333da4aabf406e131d6551255",
  "status": "FIRST_SAME_BINARY_FULL_MINIMAL84K_PAIR_ALL_STATE_GATES_PASS_REVERSE_REPEAT_REQUIRED",
  "issue_id": "PERF-089",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF089-row-image-conclusion-r74

```json
{
  "directory": "PERF089-row-image-conclusion-r74",
  "relative_path": "benchmark/results/peer-20261017/PERF089-row-image-conclusion-r74",
  "result_sha256": "c03737323907a6c012088f46944d7e432b7e6fdef67490bb9a1395c0efba5d99",
  "status": "ACTUAL84K_FULL_MINIMAL_ALL_BYTES_CRC_ROW_BITMAPS_REPLAY_AND_BUSINESS_GATES_PASS_NO_LATENCY_CPU_GAIN",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF089-row-image-replay-r73

```json
{
  "directory": "PERF089-row-image-replay-r73",
  "relative_path": "benchmark/results/peer-20261017/PERF089-row-image-replay-r73",
  "result_sha256": "14a48e03618f39fbd84782de89750fc7cd0b3ca5693ebce0be7cd03fcaeda364",
  "status": "FULL_AND_MINIMAL_IDENTICAL_SCHEMA_BASE_PLUS_VENDOR_BINLOG_REPLAY_EXACT_ALL_ROW_HASH_PASS",
  "issue_id": "PERF-089",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "base-codex_perf089_20261019_r73_full_dst.json",
    "base-codex_perf089_20261019_r73_full_src.json",
    "base-codex_perf089_20261019_r73_mini_dst.json",
    "base-codex_perf089_20261019_r73_mini_src.json",
    "execution.json",
    "full-cid-conflict.json",
    "full-cid-retry.json",
    "mini-cid-conflict.json",
    "mini-cid-retry.json",
    "replay-full.json",
    "replay-mini.json",
    "result.json",
    "source-sha256.json",
    "transitions-full.json",
    "transitions-mini.json",
    "write-full.json",
    "write-mini.json"
  ]
}
```

## PERF089-row-image-trial-plan-r73

```json
{
  "directory": "PERF089-row-image-trial-plan-r73",
  "relative_path": "benchmark/results/peer-20261017/PERF089-row-image-trial-plan-r73",
  "result_sha256": "5323f631b3c957c9b3ce88b4e1e44883ea732cc7df50b0e0e01c90308e7936eb",
  "status": "FORMAL_SAME_R8_FULL_MINIMAL_ROW_IMAGE_POINT_COLLECTED_REVERSE_REPEAT_PENDING",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF089-row-image-trial-plan-r74

```json
{
  "directory": "PERF089-row-image-trial-plan-r74",
  "relative_path": "benchmark/results/peer-20261017/PERF089-row-image-trial-plan-r74",
  "result_sha256": "962ab77533af2d3140d706617ed86698a54f6cd2031b57612188da8a6ad6fe35",
  "status": "FORMAL_SAME_R8_FULL_MINIMAL_ROW_IMAGE_POINT_COLLECTED_REVERSE_REPEAT_PENDING",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF089-row-log-review-r70

```json
{
  "directory": "PERF089-row-log-review-r70",
  "relative_path": "benchmark/results/peer-20261017/PERF089-row-log-review-r70",
  "result_sha256": "5b267e70db2ce0b14d7f90c619fec62b88efb38bcaf3aaf0ac6aeea6a98b0016",
  "status": "FULL_ROW_IMAGE_AND_IMMUTABLE_OUTBOX_PAYLOAD_VERIFIED_BYTE_CAUSE_AND_AB_PENDING",
  "issue_id": "PERF-089",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## perf094-ready-batch-10k700-r80-off

```json
{
  "directory": "perf094-ready-batch-10k700-r80-off",
  "relative_path": "benchmark/results/peer-20261017/perf094-ready-batch-10k700-r80-off",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf094-ready-batch-10k700-r81-on

```json
{
  "directory": "perf094-ready-batch-10k700-r81-on",
  "relative_path": "benchmark/results/peer-20261017/perf094-ready-batch-10k700-r81-on",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf094-ready-batch-10k700-r82-on

```json
{
  "directory": "perf094-ready-batch-10k700-r82-on",
  "relative_path": "benchmark/results/peer-20261017/perf094-ready-batch-10k700-r82-on",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf094-ready-batch-10k700-r83-off

```json
{
  "directory": "perf094-ready-batch-10k700-r83-off",
  "relative_path": "benchmark/results/peer-20261017/perf094-ready-batch-10k700-r83-off",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf094-ready-batch-10k700-r87-off

```json
{
  "directory": "perf094-ready-batch-10k700-r87-off",
  "relative_path": "benchmark/results/peer-20261017/perf094-ready-batch-10k700-r87-off",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf094-ready-batch-10k700-r88-on

```json
{
  "directory": "perf094-ready-batch-10k700-r88-on",
  "relative_path": "benchmark/results/peer-20261017/perf094-ready-batch-10k700-r88-on",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## PERF094-ready-batch-first-comparison-r83

```json
{
  "directory": "PERF094-ready-batch-first-comparison-r83",
  "relative_path": "benchmark/results/peer-20261017/PERF094-ready-batch-first-comparison-r83",
  "result_sha256": "3db8ebebe25f65aa4e31e6835cbaaa4bd005e06c900e13ba392f3dc87a82c737",
  "status": "SAME_ALL10_BINARY_FULL_84000_COMPARED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF094-ready-batch-four-comparison-r83

```json
{
  "directory": "PERF094-ready-batch-four-comparison-r83",
  "relative_path": "benchmark/results/peer-20261017/PERF094-ready-batch-four-comparison-r83",
  "result_sha256": "66208a66a894dafc584e588982dc2cb67b22bc7004f963fdd38f5a93d6f2e351",
  "status": "SAME_ALL10_BINARY_FULL_168000_COMPARED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF094-ready-batch-trial-plan-r80

```json
{
  "directory": "PERF094-ready-batch-trial-plan-r80",
  "relative_path": "benchmark/results/peer-20261017/PERF094-ready-batch-trial-plan-r80",
  "result_sha256": "313faebc07491e49eea84e18e066299861733afd9f16e76a5f8ebb72029efe84",
  "status": "FORMAL_ALL10_R10_READY_BATCH_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF094-ready-batch-trial-plan-r81

```json
{
  "directory": "PERF094-ready-batch-trial-plan-r81",
  "relative_path": "benchmark/results/peer-20261017/PERF094-ready-batch-trial-plan-r81",
  "result_sha256": "3e172867fe34c66c4384d60c608b45dad3bea4ff33fd56cfc626cd38653a1163",
  "status": "FORMAL_ALL10_R10_READY_BATCH_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF094-ready-batch-trial-plan-r82

```json
{
  "directory": "PERF094-ready-batch-trial-plan-r82",
  "relative_path": "benchmark/results/peer-20261017/PERF094-ready-batch-trial-plan-r82",
  "result_sha256": "d7ffc0f6a41cc040dc387df9c2e296d451b5fb0ac2fd02b8cb26bbea25e58b8f",
  "status": "FORMAL_ALL10_R10_READY_BATCH_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF094-ready-batch-trial-plan-r83

```json
{
  "directory": "PERF094-ready-batch-trial-plan-r83",
  "relative_path": "benchmark/results/peer-20261017/PERF094-ready-batch-trial-plan-r83",
  "result_sha256": "174bf299c94cdf27fd59bd7997750f2693b4cd9165a35347183883c0bbecec22",
  "status": "FORMAL_ALL10_R10_READY_BATCH_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF094-ready-batch-trial-plan-r87

```json
{
  "directory": "PERF094-ready-batch-trial-plan-r87",
  "relative_path": "benchmark/results/peer-20261017/PERF094-ready-batch-trial-plan-r87",
  "result_sha256": "c177e44ee52b451eff1bd5b5f7313d68926acb49111aa593b9eb9cc811d3a0e3",
  "status": "FORMAL_ALL10_R10_READY_BATCH_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF094-ready-batch-trial-plan-r88

```json
{
  "directory": "PERF094-ready-batch-trial-plan-r88",
  "relative_path": "benchmark/results/peer-20261017/PERF094-ready-batch-trial-plan-r88",
  "result_sha256": "1b97aa67172eabcbefab7c5571307259e389ef87411ac71d73b385e1cf65675e",
  "status": "FORMAL_ALL10_R10_READY_BATCH_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF094-ready-sql-resource-r77

```json
{
  "directory": "PERF094-ready-sql-resource-r77",
  "relative_path": "benchmark/results/peer-20261017/PERF094-ready-sql-resource-r77",
  "result_sha256": "45091040f7b0f8b7393e3bb64711514fdb2ee90c40ba23a148a429ea8e188d39",
  "status": "BUILDING_TEST_ONLY",
  "issue_id": null,
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF094-ready-sql-resource-r77-retry2

```json
{
  "directory": "PERF094-ready-sql-resource-r77-retry2",
  "relative_path": "benchmark/results/peer-20261017/PERF094-ready-sql-resource-r77-retry2",
  "result_sha256": "f0a5383976e8f3301d3e20429bd9de9ca45559de1c29916d4790928c84fbee45",
  "status": "REAL_R10_SQL_BATCH_ATOMICITY_CID_COLLATION_FULL_ROLLBACK_PRECOMMIT_DISCONNECT_POSTCOMMIT_READ_RECOVERY_POOL1_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF094-ready8-batch-first-comparison-r89

```json
{
  "directory": "PERF094-ready8-batch-first-comparison-r89",
  "relative_path": "benchmark/results/peer-20261017/PERF094-ready8-batch-first-comparison-r89",
  "result_sha256": "af59b840d7d1c46d020223e163be5ea6924868200ca10668c5c423b64d1520fd",
  "status": "SAME_ALL10_BINARY_FULL_84000_COMPARED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF094-source-driven-implementation-r80

```json
{
  "directory": "PERF094-source-driven-implementation-r80",
  "relative_path": "benchmark/results/peer-20261017/PERF094-source-driven-implementation-r80",
  "result_sha256": "92d758ff47329e5c42e824afd6c3fc59f48e3ed39e6de32527a78e180df30e5d",
  "status": "EXACT_PINNED_PEER_TO_PRODUCT_CANDIDATE_WITH_REAL_QUALIFICATION",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF095-complete-release-view-r90

```json
{
  "directory": "PERF095-complete-release-view-r90",
  "relative_path": "benchmark/results/peer-20261017/PERF095-complete-release-view-r90",
  "result_sha256": "01236e1e80bd20c6da7971cd63ae40a1617f66a9edcf8f885a74f9da432dc3f7",
  "status": "COMPLETE_RELEASE_VIEW_QUALIFICATION_FAILURE_RETAINED",
  "issue_id": "PERF-095",
  "error": "AssertionError(117)",
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF095-complete-release-view-r90-retry2

```json
{
  "directory": "PERF095-complete-release-view-r90-retry2",
  "relative_path": "benchmark/results/peer-20261017/PERF095-complete-release-view-r90-retry2",
  "result_sha256": "70eeb7fe3929f8c1ea589f500cd7bec4a742201678960fe7f13f5dc97077de45",
  "status": "COMPLETE_RELEASE_VIEW_QUALIFICATION_FAILURE_RETAINED",
  "issue_id": "PERF-095",
  "error": "AssertionError(117)",
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF095-complete-release-view-r90-retry3

```json
{
  "directory": "PERF095-complete-release-view-r90-retry3",
  "relative_path": "benchmark/results/peer-20261017/PERF095-complete-release-view-r90-retry3",
  "result_sha256": "f9ad28c7ef424ed81afa08819a19b87b9b59d1fc3e1e7e18e5b2afd015ad495c",
  "status": "COMPLETE_RELEASE_VIEW_FROZEN_PROJECT174_INPUTS_AND_REAL_GRPC_PROTOBUF_COMPILE_LINK_RUNTIME_PASS",
  "issue_id": "PERF-095",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF095-protobuf-release-view-r87

```json
{
  "directory": "PERF095-protobuf-release-view-r87",
  "relative_path": "benchmark/results/peer-20261017/PERF095-protobuf-release-view-r87",
  "result_sha256": "49d3fceebde3cead85031a4a621a85809414573ba2b81d5813900757a69fe559",
  "status": "DETACHED_RELEASE_PROTOBUF_IMPORT_COMPILE_SERIALIZE_AND_PROTOC_PASS_ORIGINAL_CONFIGS_UNCHANGED",
  "issue_id": "PERF-095",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF096-native-packet-comparison-r90

```json
{
  "directory": "PERF096-native-packet-comparison-r90",
  "relative_path": "benchmark/results/peer-20261017/PERF096-native-packet-comparison-r90",
  "result_sha256": "a7a189bab7a7bc7b40b5faca353ec2a2959f0cfb59ffcf3d56940242f0a61a60",
  "status": "REAL_THREE_MODE_ABCCBA_BOUNDED_MULTIROW_PROTOCOL_PASS_SMALL_BATCH_NO_GAIN_FULL_IM_NOT_PROVEN",
  "issue_id": "PERF-096",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF096-native-sql-comparison-r89

```json
{
  "directory": "PERF096-native-sql-comparison-r89",
  "relative_path": "benchmark/results/peer-20261017/PERF096-native-sql-comparison-r89",
  "result_sha256": "066fca85c1cd42e4d704e726360bfb8bdc5cc8d0849dd8d3293fc7382cf8217d",
  "status": "NATIVE_SQL_WORK_REDUCTION_PROVEN_LARGER_BATCH_RESOURCE_MEANS_LOWER_FULL_SERVICE_NOT_PROVEN",
  "issue_id": "PERF-096",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF096-native-sql-resource-r89-retry2

```json
{
  "directory": "PERF096-native-sql-resource-r89-retry2",
  "relative_path": "benchmark/results/peer-20261017/PERF096-native-sql-resource-r89-retry2",
  "result_sha256": "9a567c7b1277c4daed31c9e7c071d2773892decb807640dabdb069cfa55efb70",
  "status": "REAL_NATIVE_SQL_CONTRACTS_PACKET_NATIVE_ABBA_AND_EXACT_SQL_COUNTS_PASS",
  "issue_id": "PERF-096",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF096-native-sql-resource-r90

```json
{
  "directory": "PERF096-native-sql-resource-r90",
  "relative_path": "benchmark/results/peer-20261017/PERF096-native-sql-resource-r90",
  "result_sha256": "ca4d12636677aa980cbb5c9e83eeeb3f5937a07b8524fe00ddbc904bb2147444",
  "status": "REAL_NATIVE_SQL_THREE_MODES_ABCCBA_PROTOCOL_BOUNDS_CONTRACTS_AND_EXACT_COUNTS_PASS",
  "issue_id": "PERF-096",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF096-sql-shape-source-gap-r82

```json
{
  "directory": "PERF096-sql-shape-source-gap-r82",
  "relative_path": "benchmark/results/peer-20261017/PERF096-sql-shape-source-gap-r82",
  "result_sha256": "f0fd472e0a6703d4f9e5daa78a8ba732f631095b624653d7cb2fe0640d80b71e",
  "status": "PINNED_SOURCE_SQL_SHAPE_AND_REAL84K_STATEMENT_COUNTS_RECONCILED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF098-go-windows-clock-r91

```json
{
  "directory": "PERF098-go-windows-clock-r91",
  "relative_path": "benchmark/results/peer-20261017/PERF098-go-windows-clock-r91",
  "result_sha256": "d720cf4412df69604399a1c220e4635aa92cbf3e9438c300126cab6fdc8b3875",
  "status": "ACTUAL_WINDOWS_GO_MONOTONIC_VS_QPC_CALIBRATED",
  "issue_id": "PERF-098",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "raw.json",
    "result.json"
  ]
}
```

## PERF098-peer-qpc-client-build-r92

```json
{
  "directory": "PERF098-peer-qpc-client-build-r92",
  "relative_path": "benchmark/results/peer-20261017/PERF098-peer-qpc-client-build-r92",
  "result_sha256": "a266bbd07cbc1427041130f8223ec21dea2636cae7d6097abdcedd014d582147",
  "status": "NATIVE_WINDOWS_QPC_DUAL_CLOCK_AND_SETUP_RETRY_TESTS_PASS_WIRE_CALIBRATION_PENDING",
  "issue_id": "PERF-098",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF098-peer-qpc-client-preparation-r92-retry2

```json
{
  "directory": "PERF098-peer-qpc-client-preparation-r92-retry2",
  "relative_path": "benchmark/results/peer-20261017/PERF098-peer-qpc-client-preparation-r92-retry2",
  "result_sha256": "92e2fd15ef889aaa4301b41aae4d3a3aabd413c2cd0ea928b5cf9784edfc28a4",
  "status": "QPC_CLIENT_SOURCE_PREPARED_SAME_PINNED_SDK_AND_SHARED_TINY_QPC_HELPER_BUILD_AND_WIRE_PENDING",
  "issue_id": "PERF-098",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF098-qpc-server-helpers-r92

```json
{
  "directory": "PERF098-qpc-server-helpers-r92",
  "relative_path": "benchmark/results/peer-20261017/PERF098-qpc-server-helpers-r92",
  "result_sha256": "49a8576782859c1914c6004a5f35573a6c8db6a2863cfe0ae0e6d957a2143b6b",
  "status": "ONLY_NEW_VERSIONED_QPC_HELPERS_STAGED_SHA_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF099-controller-preparation-r95

```json
{
  "directory": "PERF099-controller-preparation-r95",
  "relative_path": "benchmark/results/peer-20261017/PERF099-controller-preparation-r95",
  "result_sha256": "f46a545777c8a290869f334260658c115d0356badec36358b42f2bcc1f1b735e",
  "status": "VERSIONED_LOCAL_PERMISSION_CONTROLLER_AND_EXACT95_98_FIXTURE_EXTENSION_COMPILE_PASS_NOT_EXECUTED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "java-scope-guard-repair.json",
    "live-read-count-correction.json",
    "result-revised.json",
    "result.json",
    "upload.json"
  ]
}
```

## PERF099-full-pair-preparation-r97

```json
{
  "directory": "PERF099-full-pair-preparation-r97",
  "relative_path": "benchmark/results/peer-20261017/PERF099-full-pair-preparation-r97",
  "result_sha256": "e3aec301c8649fd7dcc69e8da6c2709efd90d80ab5e40abacca3aa8d9e1c8e87",
  "status": "FULL_PAIR_SOURCE_FROZEN_AFTER_REAL_SQL_AND_BOTH_LIVE_GATES",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "preparation-metadata-error.json",
    "result.json"
  ]
}
```

## perf099-local-permission-10k700-r91-on

```json
{
  "directory": "perf099-local-permission-10k700-r91-on",
  "relative_path": "benchmark/results/peer-20261017/perf099-local-permission-10k700-r91-on",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf099-local-permission-10k700-r92-off

```json
{
  "directory": "perf099-local-permission-10k700-r92-off",
  "relative_path": "benchmark/results/peer-20261017/perf099-local-permission-10k700-r92-off",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf099-local-permission-10k700-r97-off

```json
{
  "directory": "perf099-local-permission-10k700-r97-off",
  "relative_path": "benchmark/results/peer-20261017/perf099-local-permission-10k700-r97-off",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf099-local-permission-10k700-r98-on

```json
{
  "directory": "perf099-local-permission-10k700-r98-on",
  "relative_path": "benchmark/results/peer-20261017/perf099-local-permission-10k700-r98-on",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## PERF099-local-permission-abba-r100

```json
{
  "directory": "PERF099-local-permission-abba-r100",
  "relative_path": "benchmark/results/peer-20261017/PERF099-local-permission-abba-r100",
  "result_sha256": "e373c1b1a4c2d62475939853a6ce5bbb55bcd66c118e81b687f43ce85887c80a",
  "status": "FULL_SAME_R11_168000_OFF_ON_ON_OFF_ALL_GATES_PASS",
  "issue_id": "PERF-099",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF099-local-permission-build-r94

```json
{
  "directory": "PERF099-local-permission-build-r94",
  "relative_path": "benchmark/results/peer-20261017/PERF099-local-permission-build-r94",
  "result_sha256": "5eb170f664ce775e4e89ce494e35ffe2a973b30f5e3aaa70e46e4bfa9444a8c1",
  "status": "FRESH_GATEWAY_LOCAL_PERMISSION_NORMAL_SANITIZED_AND_DEFAULT_WIRE_PASS_REAL_SQL_LIVE_AND_AB_PENDING",
  "issue_id": "PERF-099",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "host-preflight.json",
    "result.json"
  ]
}
```

## PERF099-local-permission-code-and-contract-r97

```json
{
  "directory": "PERF099-local-permission-code-and-contract-r97",
  "relative_path": "benchmark/results/peer-20261017/PERF099-local-permission-code-and-contract-r97",
  "result_sha256": "2e066201a0a3c4ca070a98f119f23b56892a76940f31d242f342381a0580bb5f",
  "status": "SAME_ELF_FULL84K_OFF_ON_COMPLETE_REVERSE_AND_PHASE_CHECK_PENDING",
  "issue_id": "PERF-099",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF099-local-permission-full-pair-r98

```json
{
  "directory": "PERF099-local-permission-full-pair-r98",
  "relative_path": "benchmark/results/peer-20261017/PERF099-local-permission-full-pair-r98",
  "result_sha256": "33119b02005025f99120e5fa4b0d0ec938bd5c953db3fc62964be9185bbf8880",
  "status": "ONE_ORDERED_FULL_PAIR_COMPLETE_NOT_PROMOTED",
  "issue_id": "PERF-099",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF099-local-permission-reverse-plan-r91

```json
{
  "directory": "PERF099-local-permission-reverse-plan-r91",
  "relative_path": "benchmark/results/peer-20261017/PERF099-local-permission-reverse-plan-r91",
  "result_sha256": "95e3e88e1376bd2adbf4b040e9c8478902fde372c9c7679b5a09d48bcde8ec41",
  "status": "FORMAL_R11_LOCAL_PERMISSION_FULL_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF099-local-permission-reverse-plan-r92

```json
{
  "directory": "PERF099-local-permission-reverse-plan-r92",
  "relative_path": "benchmark/results/peer-20261017/PERF099-local-permission-reverse-plan-r92",
  "result_sha256": "8c6b6caaaf03bfc91054304bb7ec32a8da3189f56c45cf2bcabcce7813794df1",
  "status": "FORMAL_R11_LOCAL_PERMISSION_FULL_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF099-local-permission-trial-plan-r97

```json
{
  "directory": "PERF099-local-permission-trial-plan-r97",
  "relative_path": "benchmark/results/peer-20261017/PERF099-local-permission-trial-plan-r97",
  "result_sha256": "cdd7fd2628f7cc6838cc50b8b9b03b82e06135b1a7acd7b0697d26ffb1e561b4",
  "status": "FORMAL_R11_LOCAL_PERMISSION_FULL_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF099-local-permission-trial-plan-r98-retry2

```json
{
  "directory": "PERF099-local-permission-trial-plan-r98-retry2",
  "relative_path": "benchmark/results/peer-20261017/PERF099-local-permission-trial-plan-r98-retry2",
  "result_sha256": "744da49f28307aeb11aa9d1279c5aa45dc0aafab81221556880c576eab6a249a",
  "status": "FORMAL_R11_LOCAL_PERMISSION_FULL_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF099-real-permission-sql-r94-retry2

```json
{
  "directory": "PERF099-real-permission-sql-r94-retry2",
  "relative_path": "benchmark/results/peer-20261017/PERF099-real-permission-sql-r94-retry2",
  "result_sha256": "ff7493b24cf46d61c474f45d6aae1e23fd9ec5ca6f1b8c65942fab620826e44b",
  "status": "REAL_PERMISSION_SQL_RPC_DUAL_INSTANCE_REVOKE_FAILURE_DEADLINE_DRAIN_PASS",
  "issue_id": "PERF-099",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF099-reverse-preparation-r99

```json
{
  "directory": "PERF099-reverse-preparation-r99",
  "relative_path": "benchmark/results/peer-20261017/PERF099-reverse-preparation-r99",
  "result_sha256": "e8904949adfd0e21753a95760444383d682d151414224aa2351fb76cd6f8645f",
  "status": "FINITE_UNUSED_R91_R92_SCOPES_AND_UNCHANGED_R11_REVERSE_DRIVER_PREPARED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF1000-identity-build-failure-followthrough-r1

```json
{
  "directory": "PERF1000-identity-build-failure-followthrough-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1000-identity-build-failure-followthrough-r1",
  "result_sha256": "30f0899d2ea2a7e10980c17edc12a582f80721e2deb9d6dad303dfa7b0c9f3ec",
  "status": "FAILED_BUILD_BEFORE_PRIVATE_CONFIG_OR_TESTS_OR_SQL_START_OR_BUSINESS_PRESERVED_ORIGINAL20_RESTORED",
  "issue_id": "PERF-1000",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF1001-current-identity-acceptance-checkpoint-r1

```json
{
  "directory": "PERF1001-current-identity-acceptance-checkpoint-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1001-current-identity-acceptance-checkpoint-r1",
  "result_sha256": "34ae0f40aadd90f155357cf14aa197f34b481b7efd429b3fea22efa5ef482e6c",
  "status": "CURRENT_IDENTITY_REAL_SQL_AND207_BUSINESS_ACCEPTANCE_CLOSED_RAW_SOURCE_E_ACTUAL_RESTORE_PASS",
  "issue_id": "PERF-1001",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "artifact-manifest.json",
    "continuation.json",
    "result.json"
  ]
}
```

## PERF1002-fixed-peer-common4-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF1002-fixed-peer-common4-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF1002-fixed-peer-common4-p1-perf921-q2-local-users20k",
  "result_sha256": "497e2d345e5cce98d9da64e311aa47ddb3498375e28c2a0fd52d74192122bf91",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-1002",
  "error": "Current complete business qualification failed; raw receipts preserved",
  "performance_claim": false,
  "root_json_files": [
    "boot-SSH.json",
    "final-state.json",
    "finally-original20.json",
    "finally-peer-normal-stop-only.json",
    "finally-SSH.json",
    "handoff-closed.json",
    "idle-greeter-cleanup.json",
    "new-quiescent-retention-epoch.json",
    "original-identities-before.json",
    "owned-case-finally-stop.json",
    "peer-boot-receipt.json",
    "postboot-identities-and-volume.json",
    "quiesce-applications.json",
    "quiescent-data-after.json",
    "quiescent-data-before.json",
    "restore-required-background4-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF1005-private-pipeline-pretraffic-cost-r1

```json
{
  "directory": "PERF1005-private-pipeline-pretraffic-cost-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1005-private-pipeline-pretraffic-cost-r1",
  "result_sha256": "a9be5f93c2a1055cc360ae356f21a1cd94733682385e2876ccb2bb830816a943",
  "status": "EXISTING_PASSIVE_PRETRAFFIC_AND_HOT30_PRIVATE_PIPELINE_COMPONENT_COST_SAVED",
  "issue_id": "PERF-1005",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF1006-pinned-proxy-send-batch-code-r1

```json
{
  "directory": "PERF1006-pinned-proxy-send-batch-code-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1006-pinned-proxy-send-batch-code-r1",
  "result_sha256": "61cb399072cc4bebc898a18e92b887f8cc35e9765a0c4da200911ad2ce232cfd",
  "status": "PINNED_PROXY_PRIMARY_BYTECODE_SAVED_READ_ONLY",
  "issue_id": "PERF-1006",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF1006-pinned-proxy-send-batch-code-r2

```json
{
  "directory": "PERF1006-pinned-proxy-send-batch-code-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF1006-pinned-proxy-send-batch-code-r2",
  "result_sha256": "35ff4f391c9aac931f1bae52a056b4c0c4155897e8e1ae24041701de26486afe",
  "status": "PINNED_PROXY_PRIMARY_BYTECODE_SAVED_READ_ONLY",
  "issue_id": "PERF-1006",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF1007-batch-receipt-native-windows-r1

```json
{
  "directory": "PERF1007-batch-receipt-native-windows-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1007-batch-receipt-native-windows-r1",
  "result_sha256": "e01f4af6f1ba227600800b229cb56562eeb962820ddda3c6682b9add63250ac4",
  "status": "NATIVE_WINDOWS_CPP14_STRICT_BUILD_BOUNDED_PARTIAL_UNKNOWN_DUPLICATE_TIMEOUT_RECEIPT_POLICY_PASS",
  "issue_id": "PERF-1007",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF1011-fresh-identity-reverse-p3-perf921-q2-local-users20k

```json
{
  "directory": "PERF1011-fresh-identity-reverse-p3-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF1011-fresh-identity-reverse-p3-perf921-q2-local-users20k",
  "result_sha256": "5d61292b01a11b1c8e325aca9da90bb9a54be611698f8b1e38ad0489e5de35b6",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-1011",
  "error": "Current complete business qualification failed; raw receipts preserved",
  "performance_claim": false,
  "root_json_files": [
    "boot-SSH.json",
    "final-state.json",
    "finally-original20.json",
    "finally-SSH.json",
    "handoff-closed.json",
    "idle-greeter-cleanup.json",
    "new-quiescent-retention-epoch.json",
    "original-identities-before.json",
    "owned-case-finally-stop.json",
    "postboot-identities-and-volume.json",
    "quiesce-applications.json",
    "quiescent-data-after.json",
    "quiescent-data-before.json",
    "restore-required-background4-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## perf1011-p3-identity-users20k

```json
{
  "directory": "perf1011-p3-identity-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf1011-p3-identity-users20k",
  "result_sha256": "4f80b048cecbe8aef134d99e45e902ed1d04bebd07b902e341715d86b841b72b",
  "status": "FAILED",
  "issue_id": "PERF-1011",
  "error": "\nCLOSED_AUDIT:AssertionError('closed-audit:   File \"<stdin>\", line 9\\n    runtime=json.loads((folder/\\'runtime-result.json\\').read_bytes());assert runtime[\\'error\\']is None and runtime[\\'original_identities_states_equal\\']\\nIndentationError: unexpected indent\\n')",
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "barrier-contract.json",
    "checks.json",
    "client-command.json",
    "client-ports.json",
    "client-records.json",
    "client-vmware-memory.json",
    "clock-after.json",
    "clock-before.json",
    "closed-audit.json",
    "formal-window-analysis-r1013.json",
    "functional-resource-result.json",
    "grant.json",
    "headroom-grant.json",
    "host-preflight.json",
    "host-samples.json",
    "identity.json",
    "immediate-projection-snapshot.json",
    "namespace.json",
    "namespace.json.intent.json",
    "namespace.json.routing.json",
    "resource-monitor-terminal.json",
    "resource-policy.json",
    "result.json",
    "runtime-result.json",
    "spec.json",
    "sql-work-after-offer.json",
    "sql-work-before-offer.json",
    "sql-work-delta-r1013.json",
    "suite.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 20000,
    "rate": 1000,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 60000,
    "sent": 60000,
    "completed_valid": 60000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0,
    "client_revision": "R819_KERNEL_TCP_BUFFERS_CACHED_ENUM"
  }
}
```

## PERF1012-stopped-diagnostic-text-cold-r1

```json
{
  "directory": "PERF1012-stopped-diagnostic-text-cold-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1012-stopped-diagnostic-text-cold-r1",
  "result_sha256": "1846a3f98eef6a679e604cc1d64704c269707cb97641d4df94f2fb2a87f69805",
  "status": "CLOSED_DIAGNOSTIC_TEXT_LOGS_E_RESTORED_AND_COLD_ARCHIVED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "closed-diagnostic-plan.json",
    "execution.json",
    "normal-stop14-before-diagnostic-archive.json",
    "original20-restored.json",
    "restore-original20.json",
    "restore-proof.json",
    "result.json",
    "source-proof.json"
  ]
}
```

## PERF1014-closed-journal-cache-r1

```json
{
  "directory": "PERF1014-closed-journal-cache-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1014-closed-journal-cache-r1",
  "result_sha256": "d15c41986bc6202dbc58c01173343f42b4d2ab0bc24cc833fa7ea374c0c93eb7",
  "status": "SEALED_JOURNALS_AND_REGENERATABLE_APT_INDEX_AND_DEB_CACHE_E_ACTUAL_RESTORE_RECLAIM_ORIGINAL3175_20_PASS",
  "issue_id": "PERF-1014",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "plan-execution.json",
    "plan.json",
    "reclaim-execution.json",
    "result.json"
  ]
}
```

## PERF1015-reverse-postload-audit-correction-r1

```json
{
  "directory": "PERF1015-reverse-postload-audit-correction-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1015-reverse-postload-audit-correction-r1",
  "result_sha256": "54cb852a6836fb87f97e63f180f6d3ef0e67ffcd478113673b4dbed94bc2aaae",
  "status": "ACTUAL60000_REVERSE_FULL_LOAD_RESOURCE_AND_READ_ONLY_CORRECTED_CLOSE_ALL_PASS",
  "issue_id": "PERF-1015",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "corrected-closed-audit.json",
    "result.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF1016-identity-four-point-adoption-r1

```json
{
  "directory": "PERF1016-identity-four-point-adoption-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1016-identity-four-point-adoption-r1",
  "result_sha256": "1d484e574891c9901616368f3adb5b575991472c1a0284e33f2a77aae1f1203b",
  "status": "FOUR_FULL60000_POINTS_AND_SQL_AUTHORITY_BUSINESS_CLOSURE_PASS_RETAIN_IMMUTABLE_IDENTITY_CACHE_IN_NEXT_BENCHMARK_PROFILE",
  "issue_id": "PERF-1016",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json",
    "selected-next-benchmark-profile.json"
  ]
}
```

## PERF1017-exact-cold-peer-fixture-recovery-r1

```json
{
  "directory": "PERF1017-exact-cold-peer-fixture-recovery-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1017-exact-cold-peer-fixture-recovery-r1",
  "result_sha256": "eed92150e7819ad20a5792739fd4346fc8367991c2b04b75a257cdeff9ac83b6",
  "status": "ZERO_TRAFFIC_NO_PEER_STARTED_SEED178_EXACT_AND_ARCHIVED_PRIVATE_FIXTURE_RESTORED",
  "issue_id": "PERF-1017",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF1018-fixed-peer-common4-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF1018-fixed-peer-common4-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF1018-fixed-peer-common4-p1-perf921-q2-local-users20k",
  "result_sha256": "6cf7540fc59be1befdee42fa165ff796e47d46740e31bc8e5d3c0dff7d31f2aa",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-1018",
  "error": "Current complete business qualification failed; raw receipts preserved",
  "performance_claim": false,
  "root_json_files": [
    "boot-SSH.json",
    "exact-fixture-preflight-before-any-stop.json",
    "final-state.json",
    "finally-original20.json",
    "finally-peer-normal-stop-only.json",
    "finally-SSH.json",
    "handoff-closed.json",
    "idle-greeter-cleanup.json",
    "new-quiescent-retention-epoch.json",
    "original-identities-before.json",
    "owned-case-finally-stop.json",
    "peer-boot-receipt.json",
    "postboot-identities-and-volume.json",
    "quiesce-applications.json",
    "quiescent-data-after.json",
    "quiescent-data-before.json",
    "restore-required-background4-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## perf102-commit-packet-10k700-r103-off

```json
{
  "directory": "perf102-commit-packet-10k700-r103-off",
  "relative_path": "benchmark/results/peer-20261017/perf102-commit-packet-10k700-r103-off",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf102-commit-packet-10k700-r104-on

```json
{
  "directory": "perf102-commit-packet-10k700-r104-on",
  "relative_path": "benchmark/results/peer-20261017/perf102-commit-packet-10k700-r104-on",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf102-commit-packet-10k700-r105-on

```json
{
  "directory": "perf102-commit-packet-10k700-r105-on",
  "relative_path": "benchmark/results/peer-20261017/perf102-commit-packet-10k700-r105-on",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## perf102-commit-packet-10k700-r106-off

```json
{
  "directory": "perf102-commit-packet-10k700-r106-off",
  "relative_path": "benchmark/results/peer-20261017/perf102-commit-packet-10k700-r106-off",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "cache-values-final.json",
    "cleanup.json",
    "client-process.json",
    "client-setup-control.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "memory-window-r42.json",
    "mq-observer-exit.json",
    "mq-observer-resources.json",
    "mq-observer-scope.json",
    "mq-offsets.json",
    "nondiag-collection.json",
    "nondiag-collector-execution.json",
    "offline-analysis.json",
    "operating-point-costs.json",
    "outbox-progress-analysis.json",
    "outbox-progress.json",
    "outbox-reconciliation.json",
    "owned-final-followup.json",
    "performance-gates.json",
    "pipeline-summary.json",
    "placement-before-traffic.json",
    "relay-identity.json",
    "relay-restoration.json",
    "route-census-execution.json",
    "route-census.json",
    "snapshot-runtime-execution.json",
    "snapshot-runtime.json",
    "sql-digests-after.json",
    "sql-digests-before.json",
    "sql-reconciliation.json",
    "tcp-after.json",
    "tcp-auth9000.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "trial.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 700,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 42000,
    "sent": 42000,
    "completed_valid": 42000,
    "negative_ack": 0,
    "invalid": 0,
    "timeouts": 0,
    "duplicate_deliveries": 0,
    "not_scheduled_after_abort": 0
  }
}
```

## PERF102-commit-packet-build-r1

```json
{
  "directory": "PERF102-commit-packet-build-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF102-commit-packet-build-r1",
  "result_sha256": "a3e7364031d7240486ed55fb5e25ac53f84f8c1c72ef4a6a6e7492179cc8b8b2",
  "status": "BUILD_OR_REAL_SQL_FAILED_PRESERVED",
  "issue_id": "PERF-102",
  "error": "wire_loss failed; raw log retained",
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host-preflight.json",
    "result.json"
  ]
}
```

## PERF102-commit-packet-build-r2

```json
{
  "directory": "PERF102-commit-packet-build-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF102-commit-packet-build-r2",
  "result_sha256": "7dcf2b30c18fc4e5755201c4ed5ebd990661de659e25a4b0abbc92002524e4ad",
  "status": "FRESH_MESSAGE_REAL_SQL_OFF_ON_SANITIZERS_AND_REAL_COMMIT_OK_LOSS_PASS_FULL_IM_AB_PENDING",
  "issue_id": "PERF-102",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host-preflight.json",
    "proxy-parser-boundaries.json",
    "result.json"
  ]
}
```

## PERF102-commit-packet-plan-r103

```json
{
  "directory": "PERF102-commit-packet-plan-r103",
  "relative_path": "benchmark/results/peer-20261017/PERF102-commit-packet-plan-r103",
  "result_sha256": "2cf8b50c9a3833d9a5fcf6b6cb4fff67adf1695c657eed66dc31e41c982a0deb",
  "status": "FORMAL_R12_COMMIT_PACKET_FULL_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF102-commit-packet-plan-r104

```json
{
  "directory": "PERF102-commit-packet-plan-r104",
  "relative_path": "benchmark/results/peer-20261017/PERF102-commit-packet-plan-r104",
  "result_sha256": "fe71588da1473002b5daca6ab0381d9d1f55fa2dae8cdd17c1009ebeb2da36c1",
  "status": "FORMAL_R12_COMMIT_PACKET_FULL_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF102-commit-packet-plan-r105

```json
{
  "directory": "PERF102-commit-packet-plan-r105",
  "relative_path": "benchmark/results/peer-20261017/PERF102-commit-packet-plan-r105",
  "result_sha256": "118661862d26c492f35c0ed7894518d5ef4e05ae3cae0bc8c1ba8bc4cd3929db",
  "status": "FORMAL_R12_COMMIT_PACKET_FULL_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF102-commit-packet-plan-r106

```json
{
  "directory": "PERF102-commit-packet-plan-r106",
  "relative_path": "benchmark/results/peer-20261017/PERF102-commit-packet-plan-r106",
  "result_sha256": "0e8576e22b940f73eecd946ddae6c009e31cc3a0ad669ec19b04e7dc46926245",
  "status": "FORMAL_R12_COMMIT_PACKET_FULL_POINT_COLLECTED_REPEAT_AND_PEER_REQUIRED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "ablation.json",
    "business.exit.json",
    "collection.exit.json",
    "costs.exit.json",
    "final.exit.json",
    "host-headroom-preflight.json",
    "memory.exit.json",
    "namespace.exit.json",
    "result.json",
    "same-config-normal-maintenance.exit.json",
    "state-after.json",
    "state-before.json"
  ]
}
```

## PERF102-commit-packet-resource-r3

```json
{
  "directory": "PERF102-commit-packet-resource-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF102-commit-packet-resource-r3",
  "result_sha256": "ad10bb19498b6b82972c084907aeb21357d92dc06b306802106fbfb8c25af4b9",
  "status": "SAME_RESOURCE_ELF_2000_REAL_SQL_OFF_ON_ON_OFF_EXACT_ROWS_AND_COMMANDS_PASS",
  "issue_id": "PERF-102",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "comparison.json",
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF102-completion-receipt-r10

```json
{
  "directory": "PERF102-completion-receipt-r10",
  "relative_path": "benchmark/results/peer-20261017/PERF102-completion-receipt-r10",
  "result_sha256": "96cd7b2d3ed697f73923db1ce3215d951118b51deaf10b8bb4f187c020fab4ee",
  "status": "SOURCE_GAPS_AND_R12_FULL168K_ABBA_CLOSED_IN_E_PROTECTED1838_RESTORED_GOAL_UNFINISHED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF102-current-E-closure-r8

```json
{
  "directory": "PERF102-current-E-closure-r8",
  "relative_path": "benchmark/results/peer-20261017/PERF102-current-E-closure-r8",
  "result_sha256": "faf2f3329466e6b3b7042d5046812357b538947dcfeb24378793d48587aab369",
  "status": "CURRENT_SOURCE_AUDIT_R12_QUALIFIED_AND_FORMAL_EVIDENCE_INDEPENDENT_E_VERIFIED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF102-current-result-r6

```json
{
  "directory": "PERF102-current-result-r6",
  "relative_path": "benchmark/results/peer-20261017/PERF102-current-result-r6",
  "result_sha256": "11ecf5115c6c6bfa229269dde7ed84543d0aca8929fb9dfa453c398adcefdd26",
  "status": "R12_FULL168K_ABBA_COLLECTED_ALL_GATES_PASS_DEFAULT_OFF_E_CLOSURE",
  "issue_id": "PERF-102",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF102-formal-driver-preparation-r5

```json
{
  "directory": "PERF102-formal-driver-preparation-r5",
  "relative_path": "benchmark/results/peer-20261017/PERF102-formal-driver-preparation-r5",
  "result_sha256": "e177cef0f0e48837afc527cf20606381d992277d97e6b8ed850c74b60dfca85e",
  "status": "SAME_R12_OFF_ON_ON_OFF_FORMAL_DRIVER_CREATED_NOT_RUN",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF102-full-abba-comparison-r9

```json
{
  "directory": "PERF102-full-abba-comparison-r9",
  "relative_path": "benchmark/results/peer-20261017/PERF102-full-abba-comparison-r9",
  "result_sha256": "10e3ae58e29c00155121636f16af6f14ec22561f3ceb27e0c3d355a6fa9521f4",
  "status": "R12_SAME_ALL10_ELFS_FULL168000_OFF_ON_ON_OFF_ALL_GATES_PASS",
  "issue_id": "PERF-102",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF102-full-pair-comparison-r7

```json
{
  "directory": "PERF102-full-pair-comparison-r7",
  "relative_path": "benchmark/results/peer-20261017/PERF102-full-pair-comparison-r7",
  "result_sha256": "17b919d73e8bd759213f24cbf6199c770175af619d99ab27ee1e21804a6b3817",
  "status": "R12_SAME_ALL10_ELFS_FULL84000_OFF_ON_ALL_GATES_COLLECTED_NOT_PROMOTED",
  "issue_id": "PERF-102",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF102-harness-preparation-r4

```json
{
  "directory": "PERF102-harness-preparation-r4",
  "relative_path": "benchmark/results/peer-20261017/PERF102-harness-preparation-r4",
  "result_sha256": "7e6e3731931598eb34078ad2798edf755866787b91651f6b9704da6a3d8174c5",
  "status": "VERSIONED_R12_CONTROLLER_FINITE101_106_SCOPES_AND_LIVE_GATES_READY_NOT_RUN",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF1022-pinned-MQ-real-batch-wire-r1

```json
{
  "directory": "PERF1022-pinned-MQ-real-batch-wire-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1022-pinned-MQ-real-batch-wire-r1",
  "result_sha256": "2c2b29f4b0848108906e5245140563e1bd0dcbc806524c3b4286595908493ffe",
  "status": "FAILED_RAW_PROOF_SAVED_ORIGINAL20_RESTORED",
  "issue_id": "PERF-1022",
  "error": "AssertionError(('real-grpc2-16-invalid-and-broker-history', 'Exception in thread \"main\" java.lang.IllegalStateException: Selected-queue message order changed\\n\\tat GrpcBatchSendProbeR1022.require(GrpcBatchSendProbeR1022.java:25)\\n\\tat GrpcBatchSendProbeR1022.main(GrpcBatchSendProbeR1022.java:96)\\n'))",
  "performance_claim": false,
  "root_json_files": [
    "execution.json",
    "host-samples.json",
    "input-manifest.json",
    "result.json"
  ]
}
```

## PERF1025-long-path-cache-archive-followthrough-r1

```json
{
  "directory": "PERF1025-long-path-cache-archive-followthrough-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1025-long-path-cache-archive-followthrough-r1",
  "result_sha256": "174cbc56ac56e292931110a4b2676e9b99e52286693e3cb32ce499e1a8542ac5",
  "status": "CLOSED_OBSERVER_OUTPUT_AND_REBUILDABLE_CMAKE_CACHE_E_RESTORED_RECLAIMED",
  "issue_id": "PERF-1025",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "reclaim.json",
    "result.json"
  ]
}
```

## PERF1028-existing-batch-history-only-r1

```json
{
  "directory": "PERF1028-existing-batch-history-only-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1028-existing-batch-history-only-r1",
  "result_sha256": "aec7c828b5544ae85e6a88f101e195243860f28805d943875616c8386b1d49c4",
  "status": "FAILED_RAW_PROOF_SAVED_ORIGINAL20_RESTORED",
  "issue_id": "PERF-1028",
  "error": "AssertionError(('read-existing18-and-reconcile-response-IDs', 'Exception in thread \"main\" org.apache.rocketmq.client.exception.MQClientException: Can not find Message Queue for this topic, codex_perf1022_events_20261025_r1\\nSee https://rocketmq.apache.org/docs/bestPractice/06FAQ for further details.\\n\\tat org.apache.rocketmq.client.impl.MQAdminImpl.fetchSubscribeMessageQueues(MQAdminImpl.java:182)\\n\\tat org.apache.rocketmq.client.impl.consumer.DefaultMQPullConsumerImpl.fetchSubscribeMessageQueues(DefaultMQPullConsumerImpl.java:147)\\n\\tat org.apache.rocketmq.client.consumer.DefaultMQPullConsumer.fetchSubscribeMessageQueues(DefaultMQPullConsumer.java:313)\\n\\tat GrpcBatchSendAuditR1028.main(GrpcBatchSendAuditR1028.java:30)\\nCaused by: org.apache.rocketmq.client.exception.MQClientException: CODE: 17  DESC: No topic route info in name server for the topic: codex_perf1022_events_20261025_r1\\nSee https://rocketmq.apache.org/docs/bestPractice/06FAQ for further details.\\n\\tat org.apache.rocketmq.client.impl.MQClientAPIImpl.getTopicRouteInfoFromNameServer(MQClientAPIImpl.java:2115)\\n\\tat org.apache.rocketmq.client.impl.MQClientAPIImpl.getTopicRouteInfoFromNameServer(MQClientAPIImpl.java:2086)\\n\\tat org.apache.rocketmq.client.impl.MQAdminImpl.fetchSubscribeMessageQueues(MQAdminImpl.java:171)\\n\\t... 3 more\\n'))",
  "performance_claim": false,
  "root_json_files": [
    "execution.json",
    "host-samples.json",
    "input-manifest.json",
    "result.json"
  ]
}
```

## PERF1029-batch-receipt-native-windows-r1

```json
{
  "directory": "PERF1029-batch-receipt-native-windows-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1029-batch-receipt-native-windows-r1",
  "result_sha256": "51bde19ffd88fdb66bb0e43baa4e6bd56e803cfdecf22ccc96260326779c4fdd",
  "status": "NATIVE_CPP14_GENERIC_AND_PINNED_JOINED_RECEIPT_COMPLETE_PARTIAL_DUPLICATE_UNKNOWN_TIMEOUT_PASS",
  "issue_id": "PERF-1029",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF103-ack-work-boundary-source-r1

```json
{
  "directory": "PERF103-ack-work-boundary-source-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF103-ack-work-boundary-source-r1",
  "result_sha256": "c15f844b676867fcb109d19c4546a49cf0f4a3fa6572509cd416eb4f2fa97636",
  "status": "ACTUAL_SOURCES_SHOW_DIFFERENT_ACK_PREREQUISITES_NOT_A_PERFORMANCE_ABLATION",
  "issue_id": "PERF-103",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF1031-receipt-policy-CXX11-14-r1

```json
{
  "directory": "PERF1031-receipt-policy-CXX11-14-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1031-receipt-policy-CXX11-14-r1",
  "result_sha256": "0450d0780211b52e043e8202febf22ff330c1e19459db3b080b2b689baee5726",
  "status": "EXACT_SHARED_POLICY_CPP11_AND_CPP14_STRICT_NATIVE63_CASES_PASS",
  "issue_id": "PERF-1031",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF1034-existing-MQ-history-cold-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF1034-existing-MQ-history-cold-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF1034-existing-MQ-history-cold-p1-perf921-q2-local-users20k",
  "result_sha256": "a3f2db05b5446a0739f9353e2a74f5414dd71b13d960649c3a3e7882cadc5d6c",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-1034",
  "error": "quiescent-data-before: Traceback (most recent call last):\n  File \"<stdin>\", line 120, in <module>\n  File \"<stdin>\", line 23, in data\n  File \"<stdin>\", line 19, in sql\nAssertionError: SQL preservation read failed\n",
  "performance_claim": false,
  "root_json_files": [
    "exact-fixture-preflight-before-any-stop.json",
    "final-state.json",
    "finally-original20.json",
    "finally-peer-normal-stop-only.json",
    "finally-SSH.json",
    "handoff-closed.json",
    "original-identities-before.json",
    "owned-case-finally-stop.json",
    "quiesce-applications.json",
    "quiescent-data-before.json",
    "result.json"
  ]
}
```

## PERF1035-preservation-read-followthrough-r1

```json
{
  "directory": "PERF1035-preservation-read-followthrough-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1035-preservation-read-followthrough-r1",
  "result_sha256": "750f27d6761345623225efaf92b2691274bacf33b143e1de7592da0a01ec3742",
  "status": "ORIGINAL_PRE_REBOOT_READ_FAILURE_PRESERVED_ORIGINAL20_RESTORED_LOGGED_BOUNDED_READ_CONTROL_STAGED",
  "issue_id": "PERF-1035",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF1036-existing-MQ-history-cold-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF1036-existing-MQ-history-cold-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF1036-existing-MQ-history-cold-p1-perf921-q2-local-users20k",
  "result_sha256": "439557b2cd0c4bd82551195c982ff7d9d91d5f2a24560b52de8c5fb4b3151bc2",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-1036",
  "error": "quiescent-data-before: Traceback (most recent call last):\n  File \"<stdin>\", line 131, in <module>\n  File \"<stdin>\", line 34, in data\n  File \"<stdin>\", line 30, in sql\nRuntimeError: Preservation read failed after3 logged fresh read-only attempts: ERROR 1049 (42000) at line 1: Unknown database 'codex_perf041_20261027_r2'\n\n",
  "performance_claim": false,
  "root_json_files": [
    "exact-fixture-preflight-before-any-stop.json",
    "final-state.json",
    "finally-original20.json",
    "finally-peer-normal-stop-only.json",
    "finally-SSH.json",
    "handoff-closed.json",
    "original-identities-before.json",
    "owned-case-finally-stop.json",
    "quiesce-applications.json",
    "quiescent-data-before.json",
    "result.json"
  ]
}
```

## PERF1037-protected-identifier-regression-r1

```json
{
  "directory": "PERF1037-protected-identifier-regression-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1037-protected-identifier-regression-r1",
  "result_sha256": "9de22c0aa05b3f5151b502111e22f76e0385fbab5543188d4d0b35df14ff6639",
  "status": "GLOBAL_NUMERIC_REPLACEMENT_CAUSE_REPRODUCED_EXPLICIT_IDENTIFIER_ONLY_COLD_PARENT_PROTECTED_DATA_BODY_UNCHANGED_REGRESSION_PASS",
  "issue_id": "PERF-1037",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF1039-existing-batch-history-cold-r1

```json
{
  "directory": "PERF1039-existing-batch-history-cold-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1039-existing-batch-history-cold-r1",
  "result_sha256": "20779ca0f4bcf8dcf65a9a68026ad09710cff34fb461073a31bf3df3b916c1ad",
  "status": "EXISTING18_REAL_WIRE_HISTORY_RECEIPT_AND_BUSINESS_CONTRACT_CLOSED_PHASE4_PASS_NO_RESEND",
  "issue_id": "PERF-1039",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "execution.json",
    "host-samples.json",
    "input-manifest.json",
    "result.json"
  ]
}
```

## PERF1039-existing-MQ-history-cold-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF1039-existing-MQ-history-cold-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF1039-existing-MQ-history-cold-p1-perf921-q2-local-users20k",
  "result_sha256": "63bb45ec0b29f0517c1651c8aa58b96994e89c0db8933dbed0515e47f59cb709",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-1039",
  "error": "exact-fixture-preflight-before-any-stop: Traceback (most recent call last):\n  File \"<stdin>\", line 11, in <module>\nAssertionError\n",
  "performance_claim": false,
  "root_json_files": [
    "exact-fixture-preflight-before-any-stop.json",
    "protected-schemas-and-existing18-before-any-stop.json",
    "result.json"
  ]
}
```

## PERF1039-existing-MQ-history-cold-p1-perf921-q2-local-users20k-r2

```json
{
  "directory": "PERF1039-existing-MQ-history-cold-p1-perf921-q2-local-users20k-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF1039-existing-MQ-history-cold-p1-perf921-q2-local-users20k-r2",
  "result_sha256": "d0a47bd2533353decbf0c16eaab0fed99564f51e2e64dc1f47cdbbb7ac906fcf",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-1039",
  "error": "quiescent-data-after: Traceback (most recent call last):\n  File \"<stdin>\", line 126, in <module>\n  File \"<stdin>\", line 20, in sql\nAssertionError\n",
  "performance_claim": false,
  "root_json_files": [
    "boot-SSH.json",
    "final-state.json",
    "finally-original20.json",
    "finally-owned-privateMQ-b-only.json",
    "finally-peer-normal-stop-only.json",
    "finally-SSH.json",
    "handoff-closed.json",
    "original-identities-before.json",
    "owned-case-finally-stop.json",
    "postboot-identities-and-volume.json",
    "protected-schemas-and-existing18-before-any-stop.json",
    "quiesce-applications.json",
    "quiescent-data-after.json",
    "quiescent-data-before.json",
    "restore-required-background4-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF104-E-closure-r8

```json
{
  "directory": "PERF104-E-closure-r8",
  "relative_path": "benchmark/results/peer-20261017/PERF104-E-closure-r8",
  "result_sha256": "0b5140cddfcab606eb7d500571740fb23d884e8e2db11fd9b254e06e840812a7",
  "status": "SOURCE_FAILED_AND_SUCCESSFUL_COMPONENT_TRIALS_RECOVERY_AND_RUNTIME_E_VERIFIED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "completion-receipt.json",
    "result.json"
  ]
}
```

## PERF104-local-store-dependency-r1

```json
{
  "directory": "PERF104-local-store-dependency-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF104-local-store-dependency-r1",
  "result_sha256": "e07a0b1dba035de35ef10550df581604aa6d6536e26dcc3acd7c0a76fff57e9b",
  "status": "OFFICIAL_PINNED_RELEASE_DOWNLOADED_NOT_BUILT",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json",
    "tag.json"
  ]
}
```

## PERF104-rocksdb-release-build-r2

```json
{
  "directory": "PERF104-rocksdb-release-build-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF104-rocksdb-release-build-r2",
  "result_sha256": "053c914d9437a1524cbaa0f4aef19643b0b5a1f1e63eebd5aae93427122abe36",
  "status": "PINNED_NATIVE_ROCKSDB_RELEASE_STATIC_AND_HEADERS_BUILT_NOT_INTEGRATED",
  "issue_id": "PERF-104",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF104-source-and-decision-r5

```json
{
  "directory": "PERF104-source-and-decision-r5",
  "relative_path": "benchmark/results/peer-20261017/PERF104-source-and-decision-r5",
  "result_sha256": "f2da80cf841d3da44636ef0917e0478a9c2c694dd70532d1111179d447fa6e7f",
  "status": "PHYSICAL_REPOSITORY_16000_OPS_ABBA_BOTH_WORKLOADS_EXACT_STATE_PASS_NOT_FULL_IM",
  "issue_id": "PERF-104",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF104-source-and-decision-r6

```json
{
  "directory": "PERF104-source-and-decision-r6",
  "relative_path": "benchmark/results/peer-20261017/PERF104-source-and-decision-r6",
  "result_sha256": "5b2f7fb6865fcc0cd52ddcb7125c72d73bb51fbff543f35d251949195b5d57a2",
  "status": "NATIVE_EXACT_LOCK_16000_OPS_PHASED_ABBA_STATE_PASS_NOT_FULL_IM",
  "issue_id": "PERF-104",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF104-source-and-decision-r7

```json
{
  "directory": "PERF104-source-and-decision-r7",
  "relative_path": "benchmark/results/peer-20261017/PERF104-source-and-decision-r7",
  "result_sha256": "a150d5735693f240f87df03dc28a39feb70c624e001fdc6aab5a9b4cecca61d0",
  "status": "PHYSICAL_REPOSITORY_16000_OPS_ABBA_BOTH_WORKLOADS_EXACT_STATE_PASS_NOT_FULL_IM",
  "issue_id": "PERF-104",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF104-store-build-r3

```json
{
  "directory": "PERF104-store-build-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF104-store-build-r3",
  "result_sha256": "70b0459a6b649183b7f6b525c4ec269396b60de5e5244dea1952273d9377ede6",
  "status": "NATIVE_STORE_QUALIFICATION_FAILURE_PRESERVED",
  "issue_id": "PERF-104",
  "error": "build failed; original log retained",
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF104-store-build-r4

```json
{
  "directory": "PERF104-store-build-r4",
  "relative_path": "benchmark/results/peer-20261017/PERF104-store-build-r4",
  "result_sha256": "0ccb04df53ace6dbd45dbef60f4ccd417846a8fc50d6573e25f070282e4728a4",
  "status": "NATIVE_STORE_QUALIFICATION_FAILURE_PRESERVED",
  "issue_id": "PERF-104",
  "error": "Fault boundary not reached; no crash result claimed",
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF104-store-build-r5

```json
{
  "directory": "PERF104-store-build-r5",
  "relative_path": "benchmark/results/peer-20261017/PERF104-store-build-r5",
  "result_sha256": "9e95d65eb1f37b6db9ac046ce647c9750aa7a104a66eebedefdb4a3c717b3b3d",
  "status": "RELEASE_AND_INSTRUMENTED_OWNER_CONTRACTS_AND_REAL_SIGKILL_RECOVERY_PASS_NOT_SERVICE_INTEGRATED",
  "issue_id": "PERF-104",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF104-store-build-r6

```json
{
  "directory": "PERF104-store-build-r6",
  "relative_path": "benchmark/results/peer-20261017/PERF104-store-build-r6",
  "result_sha256": "973b43735a7d82fd28be9caa99699b2d9153b2e5a422769f68951666336bde46",
  "status": "RELEASE_AND_INSTRUMENTED_OWNER_CONTRACTS_AND_REAL_SIGKILL_RECOVERY_PASS_NOT_SERVICE_INTEGRATED",
  "issue_id": "PERF-104",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF104-store-resource-r5

```json
{
  "directory": "PERF104-store-resource-r5",
  "relative_path": "benchmark/results/peer-20261017/PERF104-store-resource-r5",
  "result_sha256": "a9b675026c24ab4a04caa7167c9ef19865f0c72cd28c2d620cda2f95e63da43f",
  "status": "PHYSICAL_REPOSITORY_16000_OPS_ABBA_BOTH_WORKLOADS_EXACT_STATE_PASS_NOT_FULL_IM",
  "issue_id": "PERF-104",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF104-store-resource-r6

```json
{
  "directory": "PERF104-store-resource-r6",
  "relative_path": "benchmark/results/peer-20261017/PERF104-store-resource-r6",
  "result_sha256": "43653057770a1afdaf69ca082f75c4ee5b56b2040824a95656812e7d8943ea73",
  "status": "NATIVE_EXACT_LOCK_16000_OPS_PHASED_ABBA_STATE_PASS_NOT_FULL_IM",
  "issue_id": "PERF-104",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF104-store-resource-r7

```json
{
  "directory": "PERF104-store-resource-r7",
  "relative_path": "benchmark/results/peer-20261017/PERF104-store-resource-r7",
  "result_sha256": "5056efcc2d74a5f30777ed0edaf2cfcab551ade9e65c6ba4b9c7c41b93dbc56e",
  "status": "PHYSICAL_REPOSITORY_16000_OPS_ABBA_BOTH_WORKLOADS_EXACT_STATE_PASS_NOT_FULL_IM",
  "issue_id": "PERF-104",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF1040-audit-parent-unused-peer-assertion-fix-r1

```json
{
  "directory": "PERF1040-audit-parent-unused-peer-assertion-fix-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1040-audit-parent-unused-peer-assertion-fix-r1",
  "result_sha256": "72c9e5e2e71bb0e8362c3e5a626cd0b1c22f54185164bdbc6e8180f226b0e787",
  "status": "UNRELATED_PEER_NEW_DIRECTORY_ASSERTION_REMOVED_BEFORE_ANY_VM_MUTATION_RETRY_READONLY_AUDIT",
  "issue_id": "PERF-1040",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF1043-postboot-existing18-audit-resume-r1

```json
{
  "directory": "PERF1043-postboot-existing18-audit-resume-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1043-postboot-existing18-audit-resume-r1",
  "result_sha256": "3afb204dccb8747f4f54ebbd88d9f8d8f2f2fa6c4b632bdf6d84b65c3182e57f",
  "status": "EXISTING18_READONLY_AUDIT_POSTBOOT_RECONCILIATION_RESOURCE_AND_ORIGINAL20_ALL_PASS",
  "issue_id": "PERF-1043",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "current-original20-after.json",
    "current-original20-before.json",
    "exact-recorded-base-and-all-data-reconcile.json",
    "final-source-guard.json",
    "normal-privateMQ-close.json",
    "normal-quiesce-to-common4.json",
    "restore-original20.json",
    "result.json"
  ]
}
```

## PERF1044-exact-safe-batch-send-overlay-r1

```json
{
  "directory": "PERF1044-exact-safe-batch-send-overlay-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1044-exact-safe-batch-send-overlay-r1",
  "result_sha256": "29edb899119637967182d06507a91c0a45319e5ce1e9a05cd266feb784cc3db3",
  "status": "EXACT_SAFE_SOURCE_PATCH_IDEMPOTENCE_FOREIGN_EDIT_GUARD_PUBLIC_CPP11_API_PASS_BUILD_METADATA_COLLECTED",
  "issue_id": "PERF-1044",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "actual-build-metadata.json",
    "first.json",
    "idempotent.json",
    "metadata-execution.json",
    "result.json"
  ]
}
```

## PERF1045-fixed-peer-common4-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF1045-fixed-peer-common4-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF1045-fixed-peer-common4-p1-perf921-q2-local-users20k",
  "result_sha256": "b79d496ff16374607905ea0b6148545a92da57b331b629c5051f19b85b5c7309",
  "status": "COLD_INFRA4_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-1045",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "boot-SSH.json",
    "exact-fixture-preflight-before-any-stop.json",
    "final-state.json",
    "finally-original20.json",
    "finally-peer-normal-stop-only.json",
    "finally-SSH.json",
    "handoff-closed.json",
    "idle-greeter-cleanup.json",
    "new-quiescent-retention-epoch.json",
    "original-identities-before.json",
    "owned-case-finally-stop.json",
    "peer-boot-receipt.json",
    "postboot-identities-and-volume.json",
    "protected-schema-identifiers-before-any-stop.json",
    "quiesce-applications.json",
    "quiescent-data-after.json",
    "quiescent-data-before.json",
    "restore-required-background4-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF1045-identifier-safe-fixed-peer-staging-r1

```json
{
  "directory": "PERF1045-identifier-safe-fixed-peer-staging-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1045-identifier-safe-fixed-peer-staging-r1",
  "result_sha256": "d0637524efa9ca748013defcf38ed0b689bd0feba93dbd7c86edaa85a96f7201",
  "status": "PINNED_PEER_SAME_HASHES_SEED178_PROTECTED_DATE_NAMES_AND_RESOURCE_GATES_STAGED",
  "issue_id": "PERF-1045",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF1046-closed-native-EOF-allocation-r1

```json
{
  "directory": "PERF1046-closed-native-EOF-allocation-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1046-closed-native-EOF-allocation-r1",
  "result_sha256": "f505a60e60cb503dfc62ee40be48c67b95aba4274885eaac0af17cb14cd03dac",
  "status": "CLOSED_LOCKED_NATIVE_WAL_EOF_ONLY_ALLOCATION_RECLAIM_ALL_LOGICAL_BYTES_LENGTHS_INODES_PATHS_PRESERVED",
  "issue_id": "PERF-1046",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "plan.json",
    "restore-proof.json",
    "result.json",
    "same-size-EOF-reclaim.json"
  ]
}
```

## PERF1047-SDK-build-metadata-exact-recovery-r1

```json
{
  "directory": "PERF1047-SDK-build-metadata-exact-recovery-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1047-SDK-build-metadata-exact-recovery-r1",
  "result_sha256": "3ac0ca875750f7319d29ac30b273cb5d78cdaf8d3152e1ba5e7bda3e51bed75e",
  "status": "EXACT_ARCHIVED_BUILD_METADATA_RECOVERED_SHA_MATCH_NO_REGENERATION_OR_DOWNLOAD",
  "issue_id": "PERF-1047",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "exact-compile-commands.json",
    "result.json",
    "three-compile-rows.json"
  ]
}
```

## PERF1048-closed-journal-cache-r1

```json
{
  "directory": "PERF1048-closed-journal-cache-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF1048-closed-journal-cache-r1",
  "result_sha256": "6e2efd8951e95607300652614c24b1f95dea1aad7c88d58c21c4a3df79dc4eac",
  "status": "SEALED_JOURNALS_AND_REGENERATABLE_APT_INDEX_AND_DEB_CACHE_E_ACTUAL_RESTORE_RECLAIM_ORIGINAL3187_20_PASS",
  "issue_id": "PERF-1048",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "plan-execution.json",
    "plan.json",
    "reclaim-execution.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r1

```json
{
  "directory": "PERF105-application-qualification-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r1",
  "result_sha256": "dac68380d313b20d06280d92b504e1663162a718560fbde6b2610270f98752a7",
  "status": "APPLICATION_QUALIFICATION_FAILURE_PRESERVED",
  "issue_id": "PERF-105",
  "error": "striped-release-contracts failed; raw evidence retained",
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r10

```json
{
  "directory": "PERF105-application-qualification-r10",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r10",
  "result_sha256": "e177ed9d12de91e87b2bfa09ba7cc99bcbd054a77b5ce61414a2264b513d9d89",
  "status": "REAL_SQL_UCA_PARITY_APPLICATION_BATCH_AND_OWNER_SIGKILL_RECOVERY_PASS_NOT_FULL_IM",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r11

```json
{
  "directory": "PERF105-application-qualification-r11",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r11",
  "result_sha256": "9f88630eb8bb3facb880f69b067dcf7bcc998f474a2d7905c35ad64d3745811d",
  "status": "REAL_SQL_UCA_PARITY_APPLICATION_BATCH_AND_OWNER_SIGKILL_RECOVERY_PASS_NOT_FULL_IM",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r12

```json
{
  "directory": "PERF105-application-qualification-r12",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r12",
  "result_sha256": "11c498719f24c44cd83eae3d58f6950a237ff93ffdc54eb59b73e8d481738366",
  "status": "REAL_SQL_UCA_PARITY_APPLICATION_BATCH_AND_OWNER_SIGKILL_RECOVERY_PASS_NOT_FULL_IM",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r13

```json
{
  "directory": "PERF105-application-qualification-r13",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r13",
  "result_sha256": "6c7ac3fd145462d015ab770b30eb9cf5535f0628cf25724b5b9f7dd749519a8a",
  "status": "QUALIFIED_CORE_REUSED_SELECTED_RUNTIME_PRODUCTS_REBUILT_NOT_REEXECUTED",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r14

```json
{
  "directory": "PERF105-application-qualification-r14",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r14",
  "result_sha256": "cf85a63d5c92b50647a7ec1f7f7668e8c00b03e0e7eb313b276388db749999a8",
  "status": "QUALIFIED_CORE_REUSED_SELECTED_RUNTIME_PRODUCTS_REBUILT_NOT_REEXECUTED",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r16

```json
{
  "directory": "PERF105-application-qualification-r16",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r16",
  "result_sha256": "fe45f4f5e43caa676497613e277b4db9513a82d07a3bbc47390919fd536150d5",
  "status": "REAL_SQL_UCA_PARITY_APPLICATION_BATCH_AND_OWNER_SIGKILL_RECOVERY_PASS_NOT_FULL_IM",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r2

```json
{
  "directory": "PERF105-application-qualification-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r2",
  "result_sha256": "7e912663fe73b42466abfad82602b53146b2a8e0e8328f9117b54b06064d089c",
  "status": "REAL_SQL_UCA_PARITY_APPLICATION_BATCH_AND_OWNER_SIGKILL_RECOVERY_PASS_NOT_FULL_IM",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r20

```json
{
  "directory": "PERF105-application-qualification-r20",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r20",
  "result_sha256": "43fa74204e70f62ce252321cf1acb9db7b368300cc6cea19d88364156ca83a7c",
  "status": "APPLICATION_QUALIFICATION_FAILURE_PRESERVED",
  "issue_id": "PERF-105",
  "error": "Unrelated native semantics changed; full native qualification required",
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r21

```json
{
  "directory": "PERF105-application-qualification-r21",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r21",
  "result_sha256": "d595deab437d4ac3a65788c44c5770f2a12910b1079c075cf0a3a8fad97af697",
  "status": "APPLICATION_QUALIFICATION_FAILURE_PRESERVED",
  "issue_id": "PERF-105",
  "error": "group_repository_integration_tests failed; raw evidence retained",
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r22

```json
{
  "directory": "PERF105-application-qualification-r22",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r22",
  "result_sha256": "8f2772695827cc7a3bd5113341827dd832ee2f530d3441273d6e1f62d85cb7a2",
  "status": "APPLICATION_QUALIFICATION_FAILURE_PRESERVED",
  "issue_id": "PERF-105",
  "error": "Owned SQL observer failed; private diagnostics withheld",
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r23

```json
{
  "directory": "PERF105-application-qualification-r23",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r23",
  "result_sha256": "415c842bdd349dcc2bfd590923762d85b8817c401c361213ae8b552feefd138a",
  "status": "GROUP_TOPIC_FACTORY_ADAPTER_REAL_SQL_TRANSACTIONS_AND_SANITIZERS_PASS_NATIVE_R16_REUSED_FULL_MQ_PENDING",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r24

```json
{
  "directory": "PERF105-application-qualification-r24",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r24",
  "result_sha256": "6fd545fb41259caeb7d2c6b45107e169d5a2879bca70c699f1d5be78fe95fb04",
  "status": "REAL_SQL_UCA_PARITY_APPLICATION_BATCH_AND_OWNER_SIGKILL_RECOVERY_PASS_NOT_FULL_IM",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r25

```json
{
  "directory": "PERF105-application-qualification-r25",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r25",
  "result_sha256": "826653a2d0e82d3559acd12666b556566b4326b1ad74a7f80c494116633b6b79",
  "status": "REAL_SQL_UCA_PARITY_APPLICATION_BATCH_AND_OWNER_SIGKILL_RECOVERY_PASS_NOT_FULL_IM",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r26

```json
{
  "directory": "PERF105-application-qualification-r26",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r26",
  "result_sha256": "eb3ef54f48164bd5a10c090120d69879867878e122f9bd0af1612b863ae44c9d",
  "status": "REAL_SQL_UCA_PARITY_APPLICATION_BATCH_AND_OWNER_SIGKILL_RECOVERY_PASS_NOT_FULL_IM",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r29

```json
{
  "directory": "PERF105-application-qualification-r29",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r29",
  "result_sha256": "c875a16f11d217e92ad290dcf5930ede27b6fdc3990331019529b18caf5e2628",
  "status": "REAL_SQL_UCA_PARITY_APPLICATION_BATCH_AND_OWNER_SIGKILL_RECOVERY_PASS_NOT_FULL_IM",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```

## PERF105-application-qualification-r30

```json
{
  "directory": "PERF105-application-qualification-r30",
  "relative_path": "benchmark/results/peer-20261017/PERF105-application-qualification-r30",
  "result_sha256": "0c34d232f256e373321a59d0d17d58fd28a88f127732d096d3ad54356959fe3d",
  "status": "REAL_SQL_UCA_PARITY_APPLICATION_BATCH_AND_OWNER_SIGKILL_RECOVERY_PASS_NOT_FULL_IM",
  "issue_id": "PERF-105",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "E-proof.json",
    "execution.json",
    "host.json",
    "result.json"
  ]
}
```
