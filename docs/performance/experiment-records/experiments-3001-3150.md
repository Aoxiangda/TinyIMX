# 历史实验目录

[返回实验总索引](INDEX.md)

## PERF905-local-permission-preparation-r2

```json
{
  "directory": "PERF905-local-permission-preparation-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF905-local-permission-preparation-r2",
  "result_sha256": "953be962a68eac8528838693d4a3298dc594c33acbcee214e2baa4b342a10e46",
  "status": "FINITE_RETRY_CASES_ADDED_NO_OWNER_RESET",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF906-memory-business-restart-r1

```json
{
  "directory": "PERF906-memory-business-restart-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF906-memory-business-restart-r1",
  "result_sha256": "04ebd4008883b65ca168fa899104feb63b9cf5571e3766516b553c1714759399",
  "status": "NORMAL_SOFT_REBOOT_ORIGINAL2955_20_8C16G_DISKS_AND_QUIESCENT_DATA_EQUAL_PASS",
  "issue_id": "PERF-906",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "boot-SSH.json",
    "final-state.json",
    "finally-original20.json",
    "finally-SSH.json",
    "idle-greeter-cleanup.json",
    "new-quiescent-retention-epoch.json",
    "original-identities-before.json",
    "postboot-identities-and-volume.json",
    "quiesce-applications.json",
    "quiescent-data-after.json",
    "quiescent-data-before.json",
    "restore-infrastructure-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF909-inflight-local-permission-r1

```json
{
  "directory": "PERF909-inflight-local-permission-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF909-inflight-local-permission-r1",
  "result_sha256": "e6eaefd0b1fb80352b941d9625a0509c03983f1e92156b4f671a3f0ba7e590ac",
  "status": "FAIL_OR_PARTIAL_NO_ADOPTION",
  "issue_id": "PERF-909",
  "error": "RuntimeError('Quiesced8GiB10s gate not met; original services must restore, no load')\nTraceback (most recent call last):\n  File \"<WORKSPACE>\\benchmark\\peer_compare\\run_inflight_local_permission_r909.py\", line 192, in main\n    gate = wait_for_headroom(sampler.sample, lambda j:\n           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n  File \"<WORKSPACE>\\benchmark\\peer_compare\\quiesced_preload_gate_r627.py\", line 15, in wait_for_headroom\n    if t-began>=timeout:raise RuntimeError('Quiesced8GiB10s gate not met; original services must restore, no load')\n                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\nRuntimeError: Quiesced8GiB10s gate not met; original services must restore, no load\n",
  "performance_claim": false,
  "root_json_files": [
    "before.json",
    "broker-common85.json",
    "broker-restored.json",
    "contracts.json",
    "host-preflight.json",
    "owned-redis-start.json",
    "owned-redis-stopped.json",
    "result.json"
  ]
}
```

## PERF909-inflight-local-permission-r2

```json
{
  "directory": "PERF909-inflight-local-permission-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF909-inflight-local-permission-r2",
  "result_sha256": "4b01e9ce472d185b9e81df6812caf3c88eff2e8767fbbea72d383c0f9d6245c8",
  "status": "FAIL_OR_PARTIAL_NO_ADOPTION",
  "issue_id": "PERF-909",
  "error": "RuntimeError('Quiesced8GiB10s gate not met; original services must restore, no load')\nTraceback (most recent call last):\n  File \"<WORKSPACE>\\benchmark\\peer_compare\\run_inflight_local_permission_r909.py\", line 192, in main\n    gate = wait_for_headroom(sampler.sample, lambda j:\n           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n  File \"<WORKSPACE>\\benchmark\\peer_compare\\quiesced_preload_gate_r627.py\", line 15, in wait_for_headroom\n    if t-began>=timeout:raise RuntimeError('Quiesced8GiB10s gate not met; original services must restore, no load')\n                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\nRuntimeError: Quiesced8GiB10s gate not met; original services must restore, no load\n",
  "performance_claim": false,
  "root_json_files": [
    "before.json",
    "broker-common85.json",
    "broker-restored.json",
    "contracts.json",
    "host-preflight.json",
    "owned-redis-start.json",
    "owned-redis-stopped.json",
    "result.json"
  ]
}
```

## PERF909-inflight-local-permission-r3

```json
{
  "directory": "PERF909-inflight-local-permission-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF909-inflight-local-permission-r3",
  "result_sha256": "4c3792813f476ab692a6393149f0262c914027d351ac805c658d37ba5823ed71",
  "status": "EXTENDED_REAL_SESSION_BUSINESS_AND_CLOSURE_PASS_NOT_PERFORMANCE",
  "issue_id": "PERF-909",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "before.json",
    "broker-common85.json",
    "broker-restored.json",
    "closed-audit.json",
    "contracts.json",
    "deadline-locker.json",
    "deadline-response.json",
    "functional-host-resource.json",
    "headroom-grant.json",
    "host-preflight.json",
    "owned-redis-start.json",
    "owned-redis-stopped.json",
    "real-sql-wait-deadline.json",
    "real-sql-wait-shutdown.json",
    "result.json",
    "route-census.json",
    "shutdown-locker.json",
    "stop-with-inflight.json"
  ]
}
```

## PERF909-inflight-preparation-r2

```json
{
  "directory": "PERF909-inflight-preparation-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF909-inflight-preparation-r2",
  "result_sha256": "ad1dc6c2cf653a6c2ed593263d0bdcaadfe2b3af66420fa29a20af59e320c568",
  "status": "QUALIFIED_LOG_VISIBLE_ASYNC_SAME_OWNER_REAL_INFLIGHT_FAULT_READY",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF910-closed-analysis-json-r2

```json
{
  "directory": "PERF910-closed-analysis-json-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF910-closed-analysis-json-r2",
  "result_sha256": "fbe40891c79eb9672ce8115fd167b32f02b1b4cc77b9b3d8c2403496613a6268",
  "status": "VERIFIED_ENDED_UNBOUND_ANALYSIS_JSON_COLD_ARCHIVED_NO_DB_OR_CONTAINER_REMOVAL",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-execution.json",
    "archive-proof.json",
    "plan.json",
    "reclaim-execution.json",
    "result.json"
  ]
}
```

## PERF911-memory-business-restart-r1

```json
{
  "directory": "PERF911-memory-business-restart-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF911-memory-business-restart-r1",
  "result_sha256": "0b798c167b3d99c73c131139b0fa9d442badf8769a1dd8463ba8cf0ac1f5b6c7",
  "status": "NORMAL_SOFT_REBOOT_ORIGINAL2977_20_8C16G_DISKS_AND_QUIESCENT_DATA_EQUAL_PASS",
  "issue_id": "PERF-911",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "boot-SSH.json",
    "final-state.json",
    "finally-original20.json",
    "finally-SSH.json",
    "idle-greeter-cleanup.json",
    "new-quiescent-retention-epoch.json",
    "original-identities-before.json",
    "postboot-identities-and-volume.json",
    "quiesce-applications.json",
    "quiescent-data-after.json",
    "quiescent-data-before.json",
    "restore-infrastructure-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF912-local-permission-review-r1

```json
{
  "directory": "PERF912-local-permission-review-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF912-local-permission-review-r1",
  "result_sha256": "e78ea6790828d92602dd7b8663c66c675d4096bafb52dd57c143ab21ebc522c3",
  "status": "SOURCE_FIX_REAL_CONTRACTS_ISSUES_AND_NEXT_AB_RECORDED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF913-business-source-checkpoint-r1

```json
{
  "directory": "PERF913-business-source-checkpoint-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF913-business-source-checkpoint-r1",
  "result_sha256": "7aed676dc9a188a60c2400571e4106683dd4f594d6f643e4be7a549ba9111728",
  "status": "SOURCE_RESULTS_PUBLIC_BUSINESS_LOGS_RPC_PROBE_E_ARCHIVE_ACTUALLY_RESTORED_ALL_SHA_EQUAL",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "continuation.json",
    "execution.json",
    "final-state.json",
    "guest-archive-execution.json",
    "result.json"
  ]
}
```

## PERF913-business-source-checkpoint-r2

```json
{
  "directory": "PERF913-business-source-checkpoint-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF913-business-source-checkpoint-r2",
  "result_sha256": "7aed676dc9a188a60c2400571e4106683dd4f594d6f643e4be7a549ba9111728",
  "status": "SOURCE_RESULTS_PUBLIC_BUSINESS_LOGS_RPC_PROBE_E_ARCHIVE_ACTUALLY_RESTORED_ALL_SHA_EQUAL",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "continuation.json",
    "execution.json",
    "final-state.json",
    "guest-archive-execution.json",
    "result.json"
  ]
}
```

## PERF914-functional-fault-profile-r1

```json
{
  "directory": "PERF914-functional-fault-profile-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF914-functional-fault-profile-r1",
  "result_sha256": "3aa30cbf774caafd1012baae1e4f967511e88f770af845f0f3fd8e0a4cfab42a",
  "status": "DECLARED_TWO_REQUEST_FUNCTIONAL_PROFILE_ONLY_FORMAL8GIB_UNCHANGED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF915-idle-system-panels-memory-r1

```json
{
  "directory": "PERF915-idle-system-panels-memory-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF915-idle-system-panels-memory-r1",
  "result_sha256": "19fa67c2d74db664e99b242cc0d77754e92baefc1df3c2321dd973adc8662937",
  "status": "NO_VERIFIED_IDLE_SYSTEM_PANEL_ACTION_AVAILABLE_OR_TARGET_ALREADY_MET",
  "issue_id": "PERF-915",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF916-20k-fixture-tools-r1

```json
{
  "directory": "PERF916-20k-fixture-tools-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF916-20k-fixture-tools-r1",
  "result_sha256": "39a3282f8c3c1234f09a37f5613fb3fbff61a96508f80600c62cbbc38f666561",
  "status": "TWO_FRESH_20K_TOOLSETS_STAGED_OFFICIAL_INSTALLED_MQ_SDK_COMPILED_NO_SQL_OR_MQ_MUTATION",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF917-closed-analysis-json-r1

```json
{
  "directory": "PERF917-closed-analysis-json-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF917-closed-analysis-json-r1",
  "result_sha256": "d697e72742a18334b34304280e82eff633b97eabe4caa6f3a6fde82fa859cbc0",
  "status": "VERIFIED_ENDED_UNBOUND_ANALYSIS_JSON_COLD_ARCHIVED_NO_DB_OR_CONTAINER_REMOVAL",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-execution.json",
    "archive-proof.json",
    "plan.json",
    "reclaim-execution.json",
    "result.json"
  ]
}
```

## PERF918-client-and-embedded-programs-r1

```json
{
  "directory": "PERF918-client-and-embedded-programs-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF918-client-and-embedded-programs-r1",
  "result_sha256": "c86c2b6621e82e7683f62a7bcf8cd6d294ec8849c8b2c44c9a555c4de72ee723",
  "status": "FROZEN_R819_WINDOWS_CLIENT_AND_EMBEDDED_PREPARATION_SYNTAX_PASS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF918-formal-driver-staging-r2

```json
{
  "directory": "PERF918-formal-driver-staging-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF918-formal-driver-staging-r2",
  "result_sha256": "459cf5f90c9eae3b0b6e78cbd5f34b8bfac18fdaff773f410d0e1be81e8e7bab",
  "status": "FORMAL20K_LOCAL_PERMISSION_ASYNC_AB_DRIVER_READY_NOT_EXECUTED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF918-formal-driver-staging-r3

```json
{
  "directory": "PERF918-formal-driver-staging-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF918-formal-driver-staging-r3",
  "result_sha256": "793d833942f9775aac861ee01ff4b68bacdbdf4c31791428012f782683c70b2b",
  "status": null,
  "issue_id": "PERF-923",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF918-formal-local-permission-preparation-r1

```json
{
  "directory": "PERF918-formal-local-permission-preparation-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF918-formal-local-permission-preparation-r1",
  "result_sha256": "0ff82f13f1074da3e0a03708592b99f3ce2d35b4d69ec99132f0e92483165d0c",
  "status": "SAME_C97_COMPLETE_PRODUCTS20K_PERMISSION_ASYNC0_1_FORMAL_TOOLS_STAGED_NOT_MEASURED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF918-formal20k-preflight-r4

```json
{
  "directory": "PERF918-formal20k-preflight-r4",
  "relative_path": "benchmark/results/peer-20261017/PERF918-formal20k-preflight-r4",
  "result_sha256": "8f08cdc4dbbc2aa4f80a9c5a50149dac69d9e77f89df6a6f7b522b4621e2486e",
  "status": "BOTH20K_ACTUAL_COMPLETE_PRODUCT_LOADERS_CONFIGS_AND_FRESH_DATA_GUARDS_PASS_NO_TRAFFIC",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "r9161-actual-loaders.json",
    "r9161-cache-start.json",
    "r9161-cache-stopped.json",
    "r9162-actual-loaders.json",
    "r9162-cache-start.json",
    "r9162-cache-stopped.json",
    "result.json"
  ]
}
```

## PERF918-handoff-read-only-qualification-r1

```json
{
  "directory": "PERF918-handoff-read-only-qualification-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF918-handoff-read-only-qualification-r1",
  "result_sha256": "62137b4d16e21c51ee31db1caa337a1c2dcad78e156c72524f2e8b99d87c6ff9",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "import-path-fixed.json",
    "result.json"
  ]
}
```

## PERF918-required-legacy-desktop-metadata-restore-r1

```json
{
  "directory": "PERF918-required-legacy-desktop-metadata-restore-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF918-required-legacy-desktop-metadata-restore-r1",
  "result_sha256": "7c64a20e17b13f769be53b71ed1a4a538efb31332a19bf331d2f29e096d41520",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "preflight-receipts-preserved.json",
    "result.json"
  ]
}
```

## perf920-q2-local-users20k

```json
{
  "directory": "perf920-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf920-q2-local-users20k",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "admission-idle-panels.json",
    "barrier-contract.json",
    "client-ports.json",
    "client-process.json",
    "clock-after.json",
    "clock-before.json",
    "final-restoration.json",
    "formal-window-analysis-r924.json",
    "fresh-post-cache.json",
    "host-preflight.json",
    "host-samples.json",
    "point-result.json",
    "preparation-memory.json",
    "quiesced-host-headroom-grant.json",
    "resource-monitor-terminal.json",
    "restoration-control.json",
    "route-census-execution.json",
    "route-census.json",
    "spec.json",
    "tcp-after.json",
    "tcp-before.json"
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

## perf921-q2-local-users20k

```json
{
  "directory": "perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf921-q2-local-users20k",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "admission-idle-panels.json",
    "barrier-contract.json",
    "client-ports.json",
    "client-process.json",
    "clock-after.json",
    "clock-before.json",
    "final-restoration.json",
    "formal-window-analysis-r924.json",
    "fresh-post-cache.json",
    "host-preflight.json",
    "host-samples.json",
    "point-result.json",
    "preparation-memory.json",
    "quiesced-host-headroom-grant.json",
    "resource-monitor-terminal.json",
    "restoration-control.json",
    "route-census-execution.json",
    "route-census.json",
    "spec.json",
    "tcp-after.json",
    "tcp-before.json"
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

## PERF922-prequiesced-perf920-q1-local-users20k

```json
{
  "directory": "PERF922-prequiesced-perf920-q1-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF922-prequiesced-perf920-q1-local-users20k",
  "result_sha256": "8c85f319ddf88795d5f906a35ac206d5606ed80f1e909e5f94d6233b200c8113",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-922",
  "error": "prequiesced-handoff-ready: Traceback (most recent call last):\n  File \"<stdin>\", line 80, in <module>\nModuleNotFoundError: No module named 'original_outbox_guard_r871'\n",
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
    "prequiesced-handoff-ready.json",
    "quiesce-applications.json",
    "quiescent-data-after.json",
    "quiescent-data-before.json",
    "restore-infrastructure-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF922-prequiesced-perf920-q1-local-users20k-r2

```json
{
  "directory": "PERF922-prequiesced-perf920-q1-local-users20k-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF922-prequiesced-perf920-q1-local-users20k-r2",
  "result_sha256": "8884bd5fe92ce71788f3fbe0fcbdaf341468eb5d9f2936b6ff6e930f6c793269",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-922",
  "error": "Formal point failed or no traffic; receipts preserved",
  "performance_claim": false,
  "root_json_files": [
    "boot-SSH.json",
    "broker-common85.json",
    "broker-original-restored.json",
    "final-state.json",
    "finally-original20.json",
    "finally-SSH.json",
    "handoff-closed.json",
    "idle-greeter-cleanup.json",
    "new-quiescent-retention-epoch.json",
    "original-identities-before.json",
    "owned-case-finally-stop.json",
    "postboot-identities-and-volume.json",
    "prequiesced-handoff-ready.json",
    "quiesce-applications.json",
    "quiescent-data-after.json",
    "quiescent-data-before.json",
    "restore-infrastructure-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF922-prequiesced-perf920-q2-local-users20k-r3

```json
{
  "directory": "PERF922-prequiesced-perf920-q2-local-users20k-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF922-prequiesced-perf920-q2-local-users20k-r3",
  "result_sha256": "f7c3aac3f92321ddd2d63106027fe866a6fde6e477c3bac2b50d104f147901e5",
  "status": "PREQUIESCED_FORMAL_POINT_TERMINAL_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-922",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "boot-SSH.json",
    "broker-common85.json",
    "broker-original-restored.json",
    "final-state.json",
    "finally-original20.json",
    "finally-SSH.json",
    "handoff-closed.json",
    "idle-greeter-cleanup.json",
    "new-quiescent-retention-epoch.json",
    "original-identities-before.json",
    "owned-case-finally-stop.json",
    "postboot-identities-and-volume.json",
    "prequiesced-handoff-ready.json",
    "quiesce-applications.json",
    "quiescent-data-after.json",
    "quiescent-data-before.json",
    "restore-infrastructure-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF922-prequiesced-perf921-q2-local-users20k-r3

```json
{
  "directory": "PERF922-prequiesced-perf921-q2-local-users20k-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF922-prequiesced-perf921-q2-local-users20k-r3",
  "result_sha256": "c90a79846f9bca6561cba2f11705729e1d070d3631c6ee788fa6e6ec1abde9d3",
  "status": "PREQUIESCED_FORMAL_POINT_TERMINAL_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-922",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "boot-SSH.json",
    "broker-common85.json",
    "broker-original-restored.json",
    "final-state.json",
    "finally-original20.json",
    "finally-SSH.json",
    "handoff-closed.json",
    "idle-greeter-cleanup.json",
    "new-quiescent-retention-epoch.json",
    "original-identities-before.json",
    "owned-case-finally-stop.json",
    "postboot-identities-and-volume.json",
    "prequiesced-handoff-ready.json",
    "quiesce-applications.json",
    "quiescent-data-after.json",
    "quiescent-data-before.json",
    "restore-infrastructure-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF923-stopped-original-controller-repair-r2

```json
{
  "directory": "PERF923-stopped-original-controller-repair-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF923-stopped-original-controller-repair-r2",
  "result_sha256": "da4193330d4b15285fa059f2e7d844989c2acee53374df8d899a32d1c3a3987b",
  "status": "FORMAL_TOOL_FIX_STAGED_ORIGINAL20_AND_BOTH20K_FIXTURES_PRESERVED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF925-current-root-write-budget-r2

```json
{
  "directory": "PERF925-current-root-write-budget-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF925-current-root-write-budget-r2",
  "result_sha256": "0c8a035efff94358a68cd2d440bd733b0cc72f80e8ae7f3369331610bddeade1",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF925-unused-test-build-artifacts-r1

```json
{
  "directory": "PERF925-unused-test-build-artifacts-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF925-unused-test-build-artifacts-r1",
  "result_sha256": "5f91bc5f000f95286fa18de50dcf7fe92d664b46442b2674f205faf4f94d5f82",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF925-unused-test-hardlink-groups-r1

```json
{
  "directory": "PERF925-unused-test-hardlink-groups-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF925-unused-test-hardlink-groups-r1",
  "result_sha256": "b26256199bd096cd099c83a6f577cdb1fed1782adc533cdccf5dcd1ff791748c",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF926-perf920-q2-native-closure-r1

```json
{
  "directory": "PERF926-perf920-q2-native-closure-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF926-perf920-q2-native-closure-r1",
  "result_sha256": "cbf1b2d03fc5dd545465babed34192462684062debd86bd39a72692c1656ae03",
  "status": "FORMAL_FULL60000_NATIVE_CLIENT_FIFO_INDICES_LOGICAL_REFERENCES_AND_NO_FK_VERIFIED",
  "issue_id": "PERF-926",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "guest-stderr.json",
    "result.json",
    "stream-proof.json"
  ]
}
```

## PERF926-perf921-q2-native-closure-r1

```json
{
  "directory": "PERF926-perf921-q2-native-closure-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF926-perf921-q2-native-closure-r1",
  "result_sha256": "bcd89811dd6852028310d1b496fe2d4436405394d72d9e4b6ea9b4f30080f8c7",
  "status": "FORMAL_FULL60000_NATIVE_CLIENT_FIFO_INDICES_LOGICAL_REFERENCES_AND_NO_FK_VERIFIED",
  "issue_id": "PERF-926",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "guest-stderr.json",
    "result.json",
    "stream-proof.json"
  ]
}
```

## PERF928-identical-closed-SST-inventory-r1

```json
{
  "directory": "PERF928-identical-closed-SST-inventory-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF928-identical-closed-SST-inventory-r1",
  "result_sha256": "295e424a11cffff06439d4e4b4df4f7427f7635b52a83f91c75291ec6b9314d9",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF929-closed-container-stdout-inventory-r1

```json
{
  "directory": "PERF929-closed-container-stdout-inventory-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF929-closed-container-stdout-inventory-r1",
  "result_sha256": "5986ce0fe440ad11566a3a4a0b8a094211cfd379d6cf0479d96ee0d9d082e4e5",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF929-closed-container-stdout-inventory-r2

```json
{
  "directory": "PERF929-closed-container-stdout-inventory-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF929-closed-container-stdout-inventory-r2",
  "result_sha256": "3949659fb234ee53db2f6cc70857d469b335ff7dffa57c3408f5a21261c430f6",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF930-ordinary-evidence-cold-r1

```json
{
  "directory": "PERF930-ordinary-evidence-cold-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF930-ordinary-evidence-cold-r1",
  "result_sha256": "13ea306887d40472866991a4826b6e9647891efc517e874203e220558bf54154",
  "status": "CLOSED_ORDINARY_EVIDENCE_ACTUALLY_RESTORED_AND_COLD_ARCHIVED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-proof.json",
    "result.json",
    "source-proof.json"
  ]
}
```

## PERF931-perf920-q2-projection-r1

```json
{
  "directory": "PERF931-perf920-q2-projection-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF931-perf920-q2-projection-r1",
  "result_sha256": "4bf4b3cb9f1240e4d7d1cacc5ac2160b9008bbcba29c56a1c36987c078815ee1",
  "status": "ALL60000_NATIVE_BROKER_EVENTS_SQL0_POP_TAIL_AND_FULL_REDIS_PROJECTION_MATCH",
  "issue_id": "PERF-129",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "build.json",
    "compile.execution.json",
    "E-proof.json",
    "exit-probe.json",
    "exit-verify.json",
    "expected.json",
    "prepare-scope.execution.json",
    "redis-full-snapshot.execution.json",
    "redis-values.json",
    "result-probe.json",
    "result-verify.json",
    "result.json"
  ]
}
```

## PERF931-perf921-q2-projection-r1

```json
{
  "directory": "PERF931-perf921-q2-projection-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF931-perf921-q2-projection-r1",
  "result_sha256": "91c049d2392d65b2900c90c59766f453a88623060dbec62cf3b6894db95d844c",
  "status": "ALL60000_NATIVE_BROKER_EVENTS_SQL0_POP_TAIL_AND_FULL_REDIS_PROJECTION_MATCH",
  "issue_id": "PERF-129",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "build.json",
    "compile.execution.json",
    "E-proof.json",
    "exit-probe.json",
    "exit-verify.json",
    "expected.json",
    "prepare-scope.execution.json",
    "redis-full-snapshot.execution.json",
    "redis-values.json",
    "result-probe.json",
    "result-verify.json",
    "result.json"
  ]
}
```

## PERF932-closed-stdout-cold-r2

```json
{
  "directory": "PERF932-closed-stdout-cold-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF932-closed-stdout-cold-r2",
  "result_sha256": "3c904046543e63124f4ed47e079675b499c526c4b1b184cd4255e577c77c5a04",
  "status": "CLOSED_STDOUT_COLD_ARCHIVE_ALL_CONTAINERS_LAYERS_AND_DATA_RETAINED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF933-closed-stdout-cold-r1

```json
{
  "directory": "PERF933-closed-stdout-cold-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF933-closed-stdout-cold-r1",
  "result_sha256": "6815dc9a56b6a48ad42fb219007a44f17259a7c4436726f85fd535921bb9569f",
  "status": "CLOSED_STDOUT_COLD_ARCHIVE_ALL_CONTAINERS_LAYERS_AND_DATA_RETAINED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "canonical-count-addendum-r2.json",
    "execution.json",
    "restore-proof.json",
    "result.json",
    "source-proof.json"
  ]
}
```

## PERF934-ordinary-evidence-cold-r1

```json
{
  "directory": "PERF934-ordinary-evidence-cold-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF934-ordinary-evidence-cold-r1",
  "result_sha256": "ba83257aa4dfaa10e161ae326440ffc7056419962490413b1ea35b1c0f1ccff6",
  "status": "CLOSED_ORDINARY_EVIDENCE_ACTUALLY_RESTORED_AND_COLD_ARCHIVED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-proof.json",
    "result.json",
    "source-proof.json"
  ]
}
```

## PERF935-cold-reference-SQL-digest-r1

```json
{
  "directory": "PERF935-cold-reference-SQL-digest-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF935-cold-reference-SQL-digest-r1",
  "result_sha256": "9e99aa6507277048e0c25515f9e89b073218a165e82c001ea51865a21fac1611",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF936-reference-ready-qualification-r1

```json
{
  "directory": "PERF936-reference-ready-qualification-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF936-reference-ready-qualification-r1",
  "result_sha256": "283e31eaba2fabffaa0fb70feb4bc4bdf54cc8629b11d809e6df7e256caca3ee",
  "status": "REAL_READY_REFERENCE_HELPER_RELEASE_ASAN_UBSAN_AND_INTEGRATED_SOURCE_COMPILE_PASS",
  "issue_id": "PERF-936",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "execution.json",
    "input-manifest.json",
    "result.json"
  ]
}
```

## PERF936-reference-ready-qualification-r2

```json
{
  "directory": "PERF936-reference-ready-qualification-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF936-reference-ready-qualification-r2",
  "result_sha256": "1a291c83e3c2d76cdc99438ef4d93efaac43c46643a64d98772840a53c4f02b3",
  "status": "REAL_READY_REFERENCE_HELPER_RELEASE_ASAN_UBSAN_AND_INTEGRATED_SOURCE_COMPILE_PASS",
  "issue_id": "PERF-936",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "execution.json",
    "input-manifest.json",
    "result.json"
  ]
}
```

## PERF936-reference-ready-qualification-r3

```json
{
  "directory": "PERF936-reference-ready-qualification-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF936-reference-ready-qualification-r3",
  "result_sha256": "25526e8910e6f1667ecdad7b37680dfcb3fad570942d1290cece3f08abff1034",
  "status": "REAL_READY_REFERENCE_HELPER_RELEASE_ASAN_UBSAN_AND_INTEGRATED_SOURCE_COMPILE_PASS",
  "issue_id": "PERF-936",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "execution.json",
    "input-manifest.json",
    "result.json"
  ]
}
```

## PERF937-reference-ready-real-SQL-r3

```json
{
  "directory": "PERF937-reference-ready-real-SQL-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF937-reference-ready-real-SQL-r3",
  "result_sha256": "1e13ca1bffe02fe13cf084ebc9db0bcaaedbd92c4e1990e5ce112aafba268330",
  "status": "FAILED",
  "issue_id": "PERF-937",
  "error": "AssertionError('link-release: /usr/bin/ld: 找不到 libtinyimx_local_private_store.a: 没有那个文件或目录\\n/usr/bin/ld: 找不到 libtinyimx_message_core.a: 没有那个文件或目录\\n/usr/bin/ld: 找不到 libtinyimx_outbox.a: 没有那个文件或目录\\n/usr/bin/ld: 找不到 libtinyimx_eventing.a: 没有那个文件或目录\\ncollect2: error: ld returned 1 exit status\\n')\nTraceback (most recent call last):\n  File \"<stdin>\", line 48, in <module>\n  File \"<stdin>\", line 27, in run\nAssertionError: link-release: /usr/bin/ld: 找不到 libtinyimx_local_private_store.a: 没有那个文件或目录\n/usr/bin/ld: 找不到 libtinyimx_message_core.a: 没有那个文件或目录\n/usr/bin/ld: 找不到 libtinyimx_outbox.a: 没有那个文件或目录\n/usr/bin/ld: 找不到 libtinyimx_eventing.a: 没有那个文件或目录\ncollect2: error: ld returned 1 exit status\n\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "input-manifest.json",
    "preflight-memory.json",
    "resource-policy.json",
    "result.json"
  ]
}
```

## PERF937-reference-ready-real-SQL-r4

```json
{
  "directory": "PERF937-reference-ready-real-SQL-r4",
  "relative_path": "benchmark/results/peer-20261017/PERF937-reference-ready-real-SQL-r4",
  "result_sha256": "7fadf2eec2322c06bbde1861cea85ab355a8ced3d5f7bcfc24ea3d452a5b4bd5",
  "status": "FAILED",
  "issue_id": "PERF-937",
  "error": "AssertionError('sql-r7-contracts: /probe: error while loading shared libraries: libasan.so.6: cannot open shared object file: No such file or directory\\n')\nTraceback (most recent call last):\n  File \"<stdin>\", line 106, in <module>\n  File \"<stdin>\", line 27, in run\nAssertionError: sql-r7-contracts: /probe: error while loading shared libraries: libasan.so.6: cannot open shared object file: No such file or directory\n\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "input-manifest.json",
    "preflight-memory.json",
    "resource-policy.json",
    "result.json"
  ]
}
```

## PERF937-reference-ready-real-SQL-r5

```json
{
  "directory": "PERF937-reference-ready-real-SQL-r5",
  "relative_path": "benchmark/results/peer-20261017/PERF937-reference-ready-real-SQL-r5",
  "result_sha256": "933e143ca43aef2db4bf5764117b686b414ccdd85fff736d943585e3fb52cdbf",
  "status": "ALL9_ACTUAL_SQL_OFF_ON_CONTRACTS_INDEPENDENCE_4_CRASH_RECOVERIES_SANITIZER_AND_REAL_BATCH_ISOLATION_PASS",
  "issue_id": "PERF-937",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "case7.json",
    "case8.json",
    "case9.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "preflight-memory.json",
    "private-runtime-source-proof.json",
    "result.json"
  ]
}
```

## PERF938-maintenance-only-perf921-q2-local-users20k

```json
{
  "directory": "PERF938-maintenance-only-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF938-maintenance-only-perf921-q2-local-users20k",
  "result_sha256": "b3638ba0b2deab7d8cd349b84cb817fe6b3d556271afda9edacaa73e8dcee3cc",
  "status": "NORMAL_MAINTENANCE_SOFT_REBOOT_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-938",
  "error": null,
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
    "restore-infrastructure-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF938-maintenance-only-perf921-q2-local-users20k-r2

```json
{
  "directory": "PERF938-maintenance-only-perf921-q2-local-users20k-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF938-maintenance-only-perf921-q2-local-users20k-r2",
  "result_sha256": "18e99746097f02c127bd4e85e454e9e3450ee1363f13906aa85202b187d36d11",
  "status": "NORMAL_MAINTENANCE_SOFT_REBOOT_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-938",
  "error": null,
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
    "restore-infrastructure-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF940-idle-system-panels-memory-r1

```json
{
  "directory": "PERF940-idle-system-panels-memory-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF940-idle-system-panels-memory-r1",
  "result_sha256": "6dd724632cc99a4c926caa9760f7e1006bb2f7256424ed21614490276a3ff123",
  "status": "NO_VERIFIED_IDLE_SYSTEM_PANEL_ACTION_AVAILABLE_OR_TARGET_ALREADY_MET",
  "issue_id": "PERF-940",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF941-bounded-component-resource-declaration-r1

```json
{
  "directory": "PERF941-bounded-component-resource-declaration-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF941-bounded-component-resource-declaration-r1",
  "result_sha256": "fb3cc644e17a2e5492e64ed527537237d2515cc0360afd4de928dfdf942fb17b",
  "status": "PROSPECTIVE_FUNCTIONAL_PROFILE_DECLARED_NO_CAPACITY_RECLASSIFICATION",
  "issue_id": "PERF-941",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF942-isolated-MQ-newvol-staging-r1

```json
{
  "directory": "PERF942-isolated-MQ-newvol-staging-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF942-isolated-MQ-newvol-staging-r1",
  "result_sha256": "0b0439e2d031b618b6fb485dd08e974638102fcd5ecec324a8d810779c26d9af",
  "status": "SIX_STOPPED_ISOLATED_MQ_ACTORS_READY_ON_APPROVED_NEW_VOLUME_NOT_STARTED",
  "issue_id": "PERF-942",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF943-complete-reference-cohort-r1

```json
{
  "directory": "PERF943-complete-reference-cohort-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF943-complete-reference-cohort-r1",
  "result_sha256": "14e42a3046c52f43bb137e1237f4bf7461aeea03f13f43e894e5ea0b65101fc7",
  "status": "FAILED",
  "issue_id": "PERF-943",
  "error": "AssertionError(('configure-reproduction', '-- The CXX compiler identification is GNU 11.4.0\\n-- Detecting CXX compiler ABI info\\n-- Detecting CXX compiler ABI info - done\\n-- Check for working CXX compiler: /usr/bin/c++ - skipped\\n-- Detecting CXX compile features\\n-- Detecting CXX compile features - done\\n-- Found nlohmann_json: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/share/nlohmann_json/nlohmann_jsonConfig.cmake (found version \"3.12.0\") \\n-- Looking for C++ include pthread.h\\n-- Looking for C++ include pthread.h - found\\n-- Performing Test CMAKE_HAVE_LIBC_PTHREAD\\n-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success\\n-- Found Threads: TRUE  \\n-- Found OpenSSL: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/lib/libcrypto.a (found version \"3.6.1\")  \\n-- Found ZLIB: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/lib/libz.a (found version \"1.3.1\") \\n-- Found Protobuf: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/tools/protobuf/protoc (found version \"33.4.0\") \\n-- Found c-ares: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/share/c-ares/c-ares-config.cmake (found version \"1.34.6\") \\n-- Found RE2 via CMake.\\n-- Found opentelemetry-cpp: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/share/opentelemetry-cpp/opentelemetry-cpp-config.cmake (found version \"1.25.0\") \\n-- Configuring done\\nCMake Error at /home/jackson7/projects/TinyIMX_publish/toolchains/vcpkg-tinyimx/scripts/buildsystems/vcpkg.cmake:612 (_add_executable):\\n  Cannot find source file:\\n\\n    /home/jackson7/projects/tinyimx-interview-20261017/fk-free-qualification-r146/legacy-reference-source/LocalPrivateStore.cpp\\n\\n  Tried extensions .c .C .c++ .cc .cpp .cxx .cu .mpp .m .M .mm .ixx .cppm .h\\n  .hh .h++ .hm .hpp .hxx .in .txx .f .F .for .f77 .f90 .f95 .f03 .hip .ispc\\nCall Stack (most recent call first):\\n  cmake/TinyIMXLocalStore.cmake:29 (add_executable)\\n  CMakeLists.txt:1581 (include)\\n\\n\\nCMake Error at /home/jackson7/projects/TinyIMX_publish/toolchains/vcpkg-tinyimx/scripts/buildsystems/vcpkg.cmake:612 (_add_executable):\\n  No SOURCES given to target: fk_upgrade_legacy_tests\\nCall Stack (most recent call first):\\n  cmake/TinyIMXLocalStore.cmake:29 (add_executable)\\n  CMakeLists.txt:1581 (include)\\n\\n\\nCMake Generate step failed.  Build files cannot be regenerated correctly.\\n'))\nTraceback (most recent call last):\n  File \"<stdin>\", line 55, in <module>\n  File \"<stdin>\", line 43, in run\nAssertionError: ('configure-reproduction', '-- The CXX compiler identification is GNU 11.4.0\\n-- Detecting CXX compiler ABI info\\n-- Detecting CXX compiler ABI info - done\\n-- Check for working CXX compiler: /usr/bin/c++ - skipped\\n-- Detecting CXX compile features\\n-- Detecting CXX compile features - done\\n-- Found nlohmann_json: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/share/nlohmann_json/nlohmann_jsonConfig.cmake (found version \"3.12.0\") \\n-- Looking for C++ include pthread.h\\n-- Looking for C++ include pthread.h - found\\n-- Performing Test CMAKE_HAVE_LIBC_PTHREAD\\n-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success\\n-- Found Threads: TRUE  \\n-- Found OpenSSL: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/lib/libcrypto.a (found version \"3.6.1\")  \\n-- Found ZLIB: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/lib/libz.a (found version \"1.3.1\") \\n-- Found Protobuf: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/tools/protobuf/protoc (found version \"33.4.0\") \\n-- Found c-ares: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/share/c-ares/c-ares-config.cmake (found version \"1.34.6\") \\n-- Found RE2 via CMake.\\n-- Found opentelemetry-cpp: /mnt/tinyimx-benchmark/async-local-gateway-perf902-r1/release-dependencies/x64-linux/share/opentelemetry-cpp/opentelemetry-cpp-config.cmake (found version \"1.25.0\") \\n-- Configuring done\\nCMake Error at /home/jackson7/projects/TinyIMX_publish/toolchains/vcpkg-tinyimx/scripts/buildsystems/vcpkg.cmake:612 (_add_executable):\\n  Cannot find source file:\\n\\n    /home/jackson7/projects/tinyimx-interview-20261017/fk-free-qualification-r146/legacy-reference-source/LocalPrivateStore.cpp\\n\\n  Tried extensions .c .C .c++ .cc .cpp .cxx .cu .mpp .m .M .mm .ixx .cppm .h\\n  .hh .h++ .hm .hpp .hxx .in .txx .f .F .for .f77 .f90 .f95 .f03 .hip .ispc\\nCall Stack (most recent call first):\\n  cmake/TinyIMXLocalStore.cmake:29 (add_executable)\\n  CMakeLists.txt:1581 (include)\\n\\n\\nCMake Error at /home/jackson7/projects/TinyIMX_publish/toolchains/vcpkg-tinyimx/scripts/buildsystems/vcpkg.cmake:612 (_add_executable):\\n  No SOURCES given to target: fk_upgrade_legacy_tests\\nCall Stack (most recent call first):\\n  cmake/TinyIMXLocalStore.cmake:29 (add_executable)\\n  CMakeLists.txt:1581 (include)\\n\\n\\nCMake Generate step failed.  Build files cannot be regenerated correctly.\\n')\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "build-host-resource.json",
    "build-policy.json",
    "checks.json",
    "configure-command.json",
    "execution.json",
    "original-source-manifest.json",
    "result.json",
    "source-input-manifest.json"
  ]
}
```

## PERF943-complete-reference-cohort-r2

```json
{
  "directory": "PERF943-complete-reference-cohort-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF943-complete-reference-cohort-r2",
  "result_sha256": "2e5e91ea26109e3759b34e01721714bb01f1a09c007e286939b35d9e5a6ca00d",
  "status": "FAILED",
  "issue_id": "PERF-943",
  "error": "AssertionError(('configure-complete-current-source', '-- Found RE2 via CMake.\\n-- Found Python3: /usr/bin/python3.10 (found version \"3.10.12\") found components: Interpreter \\nCMake Error at cmake/TinyIMXRocketMQ.cmake:87 (message):\\n  RocketMQ SDK safe-send build verification failed.  Rebuild with\\n  scripts/bootstrap_rocketmq_cpp_client.sh.  /usr/bin/python3.10: can\\'t open\\n  file\\n  \\'/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/scripts/verify_rocketmq_sync_send_toolchain.py\\':\\n  [Errno 2] No such file or directory\\n\\nCall Stack (most recent call first):\\n  CMakeLists.txt:1488 (include)\\n\\n\\n-- Configuring incomplete, errors occurred!\\nSee also \"/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/build/CMakeFiles/CMakeOutput.log\".\\n'))\nTraceback (most recent call last):\n  File \"<stdin>\", line 68, in <module>\n  File \"<stdin>\", line 43, in run\nAssertionError: ('configure-complete-current-source', '-- Found RE2 via CMake.\\n-- Found Python3: /usr/bin/python3.10 (found version \"3.10.12\") found components: Interpreter \\nCMake Error at cmake/TinyIMXRocketMQ.cmake:87 (message):\\n  RocketMQ SDK safe-send build verification failed.  Rebuild with\\n  scripts/bootstrap_rocketmq_cpp_client.sh.  /usr/bin/python3.10: can\\'t open\\n  file\\n  \\'/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/scripts/verify_rocketmq_sync_send_toolchain.py\\':\\n  [Errno 2] No such file or directory\\n\\nCall Stack (most recent call first):\\n  CMakeLists.txt:1488 (include)\\n\\n\\n-- Configuring incomplete, errors occurred!\\nSee also \"/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/build/CMakeFiles/CMakeOutput.log\".\\n')\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "build-host-resource.json",
    "build-policy.json",
    "checks.json",
    "configure-command.json",
    "execution.json",
    "original-source-manifest.json",
    "result.json",
    "source-diff-from-reproduced-c97.json",
    "source-input-manifest.json"
  ]
}
```

## PERF943-complete-reference-cohort-r3

```json
{
  "directory": "PERF943-complete-reference-cohort-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF943-complete-reference-cohort-r3",
  "result_sha256": "3b22db7f4944a70134b3bdf8b9a55de9a53e5950a96bfa0896230f6a24945f35",
  "status": "FAILED",
  "issue_id": "PERF-943",
  "error": "AssertionError(('build-all9-complete-current-graph', 'nce-cohort-perf943-r2/source/common/observability/SnapshotRpcTiming.h:88:28: note: candidate: ‘template<class Fn> std::invoke_result_t<Releaser> tinyimx::diagnostics::SnapshotRpcTiming::Measure(Fn&&)’\\n   88 |     template<class Fn>auto Measure(Fn&& fn)->std::invoke_result_t<Fn> {\\n      |                            ^~~~~~~\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/common/observability/SnapshotRpcTiming.h:88:28: note:   template argument deduction/substitution failed:\\nIn file included from /usr/include/c++/11/bits/move.h:57,\\n                 from /usr/include/c++/11/bits/stl_pair.h:59,\\n                 from /usr/include/c++/11/bits/stl_algobase.h:64,\\n                 from /usr/include/c++/11/bits/char_traits.h:39,\\n                 from /usr/include/c++/11/string:40,\\n                 from /mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadProjectionReader.h:4,\\n                 from /mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadSnapshotRpc.h:2,\\n                 from /mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadSnapshotRpc.cpp:1:\\n/usr/include/c++/11/type_traits: In substitution of ‘template<class _Fn, class ... _Args> using invoke_result_t = typename std::invoke_result::type [with _Fn = tinyimx::projection::unread::UnreadSnapshotRpcClient::LoadDialogSnapshots(const std::vector<std::pair<long unsigned int, long unsigned int> >&)::<lambda()>; _Args = {}]’:\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/common/observability/SnapshotRpcTiming.h:88:28:   required by substitution of ‘template<class Fn> std::invoke_result_t<Releaser> tinyimx::diagnostics::SnapshotRpcTiming::Measure(Fn&&) [with Fn = tinyimx::projection::unread::UnreadSnapshotRpcClient::LoadDialogSnapshots(const std::vector<std::pair<long unsigned int, long unsigned int> >&)::<lambda()>]’\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadSnapshotRpc.cpp:159:36:   required from here\\n/usr/include/c++/11/type_traits:3016:11: error: no type named ‘type’ in ‘struct std::invoke_result<tinyimx::projection::unread::UnreadSnapshotRpcClient::LoadDialogSnapshots(const std::vector<std::pair<long unsigned int, long unsigned int> >&)::<lambda()> >’\\n 3016 |     using invoke_result_t = typename invoke_result<_Fn, _Args...>::type;\\n      |           ^~~~~~~~~~~~~~~\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadSnapshotRpc.cpp:175:33: error: ‘r’ was not declared in this scope\\n  175 |     if(static_cast<std::size_t>(r.snapshots_size())!=dialogs.size())return fail(\"invalid owner batch response count\");\\n      |                                 ^\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadSnapshotRpc.cpp:179:24: error: ‘r’ was not declared in this scope\\n  179 |         const auto&row=r.snapshots(static_cast<int>(i));\\n      |                        ^\\ngmake[3]: *** [CMakeFiles/tinyimx_unread_snapshot_rpc.dir/build.make:76：CMakeFiles/tinyimx_unread_snapshot_rpc.dir/services/projection/unread/UnreadSnapshotRpc.cpp.o] 错误 1\\ngmake[2]: *** [CMakeFiles/Makefile2:10397：CMakeFiles/tinyimx_unread_snapshot_rpc.dir/all] 错误 2\\ngmake[1]: *** [CMakeFiles/Makefile2:5655：CMakeFiles/message_service_demo.dir/rule] 错误 2\\ngmake: *** [Makefile:2331：message_service_demo] 错误 2\\n'))\nTraceback (most recent call last):\n  File \"<stdin>\", line 92, in <module>\n  File \"<stdin>\", line 45, in run\nAssertionError: ('build-all9-complete-current-graph', 'nce-cohort-perf943-r2/source/common/observability/SnapshotRpcTiming.h:88:28: note: candidate: ‘template<class Fn> std::invoke_result_t<Releaser> tinyimx::diagnostics::SnapshotRpcTiming::Measure(Fn&&)’\\n   88 |     template<class Fn>auto Measure(Fn&& fn)->std::invoke_result_t<Fn> {\\n      |                            ^~~~~~~\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/common/observability/SnapshotRpcTiming.h:88:28: note:   template argument deduction/substitution failed:\\nIn file included from /usr/include/c++/11/bits/move.h:57,\\n                 from /usr/include/c++/11/bits/stl_pair.h:59,\\n                 from /usr/include/c++/11/bits/stl_algobase.h:64,\\n                 from /usr/include/c++/11/bits/char_traits.h:39,\\n                 from /usr/include/c++/11/string:40,\\n                 from /mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadProjectionReader.h:4,\\n                 from /mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadSnapshotRpc.h:2,\\n                 from /mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadSnapshotRpc.cpp:1:\\n/usr/include/c++/11/type_traits: In substitution of ‘template<class _Fn, class ... _Args> using invoke_result_t = typename std::invoke_result::type [with _Fn = tinyimx::projection::unread::UnreadSnapshotRpcClient::LoadDialogSnapshots(const std::vector<std::pair<long unsigned int, long unsigned int> >&)::<lambda()>; _Args = {}]’:\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/common/observability/SnapshotRpcTiming.h:88:28:   required by substitution of ‘template<class Fn> std::invoke_result_t<Releaser> tinyimx::diagnostics::SnapshotRpcTiming::Measure(Fn&&) [with Fn = tinyimx::projection::unread::UnreadSnapshotRpcClient::LoadDialogSnapshots(const std::vector<std::pair<long unsigned int, long unsigned int> >&)::<lambda()>]’\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadSnapshotRpc.cpp:159:36:   required from here\\n/usr/include/c++/11/type_traits:3016:11: error: no type named ‘type’ in ‘struct std::invoke_result<tinyimx::projection::unread::UnreadSnapshotRpcClient::LoadDialogSnapshots(const std::vector<std::pair<long unsigned int, long unsigned int> >&)::<lambda()> >’\\n 3016 |     using invoke_result_t = typename invoke_result<_Fn, _Args...>::type;\\n      |           ^~~~~~~~~~~~~~~\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadSnapshotRpc.cpp:175:33: error: ‘r’ was not declared in this scope\\n  175 |     if(static_cast<std::size_t>(r.snapshots_size())!=dialogs.size())return fail(\"invalid owner batch response count\");\\n      |                                 ^\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/projection/unread/UnreadSnapshotRpc.cpp:179:24: error: ‘r’ was not declared in this scope\\n  179 |         const auto&row=r.snapshots(static_cast<int>(i));\\n      |                        ^\\ngmake[3]: *** [CMakeFiles/tinyimx_unread_snapshot_rpc.dir/build.make:76：CMakeFiles/tinyimx_unread_snapshot_rpc.dir/services/projection/unread/UnreadSnapshotRpc.cpp.o] 错误 1\\ngmake[2]: *** [CMakeFiles/Makefile2:10397：CMakeFiles/tinyimx_unread_snapshot_rpc.dir/all] 错误 2\\ngmake[1]: *** [CMakeFiles/Makefile2:5655：CMakeFiles/message_service_demo.dir/rule] 错误 2\\ngmake: *** [Makefile:2331：message_service_demo] 错误 2\\n')\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "build-host-resource.json",
    "build-policy.json",
    "checks.json",
    "configure-command.json",
    "execution.json",
    "original-source-manifest.json",
    "qualified-sdk-provenance.json",
    "result.json",
    "source-diff-from-reproduced-c97.json",
    "source-input-manifest.json"
  ]
}
```

## PERF943-complete-reference-cohort-r4

```json
{
  "directory": "PERF943-complete-reference-cohort-r4",
  "relative_path": "benchmark/results/peer-20261017/PERF943-complete-reference-cohort-r4",
  "result_sha256": "85193adb7cb28a868848ba124b23eb031991e94fa48af56d47f1efe9262c5174",
  "status": "FAILED",
  "issue_id": "PERF-943",
  "error": "AssertionError(('build-all9-complete-current-graph', 'l_core\\n[100%] Built target tinyimx_social_core\\nConsolidate compiler generated dependencies of target tinyimx_social_grpc\\n[100%] Building CXX object CMakeFiles/tinyimx_social_grpc.dir/services/social/service/SocialServiceImpl.cpp.o\\n[100%] Building CXX object CMakeFiles/tinyimx_social_grpc.dir/services/social/server/SocialServiceServer.cpp.o\\n[100%] Linking CXX static library libtinyimx_social_grpc.a\\n[100%] Built target tinyimx_social_grpc\\nConsolidate compiler generated dependencies of target social_service_demo\\n[100%] Building CXX object CMakeFiles/social_service_demo.dir/examples/social_service_demo.cpp.o\\n[100%] Linking CXX executable social_service_demo\\n[100%] Built target social_service_demo\\n[  0%] Built target tinyimx_config\\n[  5%] Built target tinyimx_observability\\n[ 11%] Built target tinyimx_rpc_codegen\\n[ 29%] Built target tinyimx_rpc_proto\\n[ 29%] Built target tinyimx_logging\\nConsolidate compiler generated dependencies of target tinyimx_unread_snapshot_rpc\\n[ 35%] Building CXX object CMakeFiles/tinyimx_unread_snapshot_rpc.dir/services/projection/unread/UnreadSnapshotRpc.cpp.o\\n[ 35%] Linking CXX static library libtinyimx_unread_snapshot_rpc.a\\n[ 35%] Built target tinyimx_unread_snapshot_rpc\\n[ 41%] Built target tinyimx_db\\n[ 41%] Built target tinyimx_security\\n[ 41%] Built target tinyimx_repository\\n[ 41%] Building CXX object CMakeFiles/tinyimx_eventing.dir/services/eventing/DomainEvent.cpp.o\\n[ 41%] Building CXX object CMakeFiles/tinyimx_eventing.dir/services/eventing/EventCodec.cpp.o\\n[ 41%] Building CXX object CMakeFiles/tinyimx_eventing.dir/services/eventing/EventPublisher.cpp.o\\n[ 47%] Linking CXX static library libtinyimx_eventing.a\\n[ 47%] Built target tinyimx_eventing\\n[ 52%] Building CXX object CMakeFiles/tinyimx_outbox.dir/services/outbox/OutboxRepository.cpp.o\\n[ 52%] Linking CXX static library libtinyimx_outbox.a\\n[ 52%] Built target tinyimx_outbox\\n[ 58%] Building CXX object CMakeFiles/tinyimx_message_core.dir/services/message/application/MessageApplicationService.cpp.o\\n[ 58%] Building CXX object CMakeFiles/tinyimx_message_core.dir/services/message/application/MessageEventFactory.cpp.o\\n[ 58%] Building CXX object CMakeFiles/tinyimx_message_core.dir/services/message/repository/MessageRepositoryAdapter.cpp.o\\n[ 58%] Linking CXX static library libtinyimx_message_core.a\\n[ 58%] Built target tinyimx_message_core\\n[ 64%] Building CXX object CMakeFiles/tinyimx_rocketmq_transport.dir/services/eventing/rocketmq/RocketMQProducer.cpp.o\\n[ 64%] Building CXX object CMakeFiles/tinyimx_rocketmq_transport.dir/services/eventing/rocketmq/RocketMQSimpleConsumer.cpp.o\\nIn file included from /mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/eventing/rocketmq/RocketMQSimpleConsumer.cpp:5:\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/eventing/rocketmq/OptionalWireAckBatch.h:3:10: fatal error: patches/rocketmq-cpp-5.1.1/TinyIMXBatchAckApi.h: 没有那个文件或目录\\n    3 | #include \"patches/rocketmq-cpp-5.1.1/TinyIMXBatchAckApi.h\"\\n      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\\ncompilation terminated.\\ngmake[3]: *** [CMakeFiles/tinyimx_rocketmq_transport.dir/build.make:90：CMakeFiles/tinyimx_rocketmq_transport.dir/services/eventing/rocketmq/RocketMQSimpleConsumer.cpp.o] 错误 1\\ngmake[2]: *** [CMakeFiles/Makefile2:6969：CMakeFiles/tinyimx_rocketmq_transport.dir/all] 错误 2\\ngmake[1]: *** [CMakeFiles/Makefile2:5655：CMakeFiles/message_service_demo.dir/rule] 错误 2\\ngmake: *** [Makefile:2331：message_service_demo] 错误 2\\n'))\nTraceback (most recent call last):\n  File \"<stdin>\", line 97, in <module>\n  File \"<stdin>\", line 45, in run\nAssertionError: ('build-all9-complete-current-graph', 'l_core\\n[100%] Built target tinyimx_social_core\\nConsolidate compiler generated dependencies of target tinyimx_social_grpc\\n[100%] Building CXX object CMakeFiles/tinyimx_social_grpc.dir/services/social/service/SocialServiceImpl.cpp.o\\n[100%] Building CXX object CMakeFiles/tinyimx_social_grpc.dir/services/social/server/SocialServiceServer.cpp.o\\n[100%] Linking CXX static library libtinyimx_social_grpc.a\\n[100%] Built target tinyimx_social_grpc\\nConsolidate compiler generated dependencies of target social_service_demo\\n[100%] Building CXX object CMakeFiles/social_service_demo.dir/examples/social_service_demo.cpp.o\\n[100%] Linking CXX executable social_service_demo\\n[100%] Built target social_service_demo\\n[  0%] Built target tinyimx_config\\n[  5%] Built target tinyimx_observability\\n[ 11%] Built target tinyimx_rpc_codegen\\n[ 29%] Built target tinyimx_rpc_proto\\n[ 29%] Built target tinyimx_logging\\nConsolidate compiler generated dependencies of target tinyimx_unread_snapshot_rpc\\n[ 35%] Building CXX object CMakeFiles/tinyimx_unread_snapshot_rpc.dir/services/projection/unread/UnreadSnapshotRpc.cpp.o\\n[ 35%] Linking CXX static library libtinyimx_unread_snapshot_rpc.a\\n[ 35%] Built target tinyimx_unread_snapshot_rpc\\n[ 41%] Built target tinyimx_db\\n[ 41%] Built target tinyimx_security\\n[ 41%] Built target tinyimx_repository\\n[ 41%] Building CXX object CMakeFiles/tinyimx_eventing.dir/services/eventing/DomainEvent.cpp.o\\n[ 41%] Building CXX object CMakeFiles/tinyimx_eventing.dir/services/eventing/EventCodec.cpp.o\\n[ 41%] Building CXX object CMakeFiles/tinyimx_eventing.dir/services/eventing/EventPublisher.cpp.o\\n[ 47%] Linking CXX static library libtinyimx_eventing.a\\n[ 47%] Built target tinyimx_eventing\\n[ 52%] Building CXX object CMakeFiles/tinyimx_outbox.dir/services/outbox/OutboxRepository.cpp.o\\n[ 52%] Linking CXX static library libtinyimx_outbox.a\\n[ 52%] Built target tinyimx_outbox\\n[ 58%] Building CXX object CMakeFiles/tinyimx_message_core.dir/services/message/application/MessageApplicationService.cpp.o\\n[ 58%] Building CXX object CMakeFiles/tinyimx_message_core.dir/services/message/application/MessageEventFactory.cpp.o\\n[ 58%] Building CXX object CMakeFiles/tinyimx_message_core.dir/services/message/repository/MessageRepositoryAdapter.cpp.o\\n[ 58%] Linking CXX static library libtinyimx_message_core.a\\n[ 58%] Built target tinyimx_message_core\\n[ 64%] Building CXX object CMakeFiles/tinyimx_rocketmq_transport.dir/services/eventing/rocketmq/RocketMQProducer.cpp.o\\n[ 64%] Building CXX object CMakeFiles/tinyimx_rocketmq_transport.dir/services/eventing/rocketmq/RocketMQSimpleConsumer.cpp.o\\nIn file included from /mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/eventing/rocketmq/RocketMQSimpleConsumer.cpp:5:\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/services/eventing/rocketmq/OptionalWireAckBatch.h:3:10: fatal error: patches/rocketmq-cpp-5.1.1/TinyIMXBatchAckApi.h: 没有那个文件或目录\\n    3 | #include \"patches/rocketmq-cpp-5.1.1/TinyIMXBatchAckApi.h\"\\n      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\\ncompilation terminated.\\ngmake[3]: *** [CMakeFiles/tinyimx_rocketmq_transport.dir/build.make:90：CMakeFiles/tinyimx_rocketmq_transport.dir/services/eventing/rocketmq/RocketMQSimpleConsumer.cpp.o] 错误 1\\ngmake[2]: *** [CMakeFiles/Makefile2:6969：CMakeFiles/tinyimx_rocketmq_transport.dir/all] 错误 2\\ngmake[1]: *** [CMakeFiles/Makefile2:5655：CMakeFiles/message_service_demo.dir/rule] 错误 2\\ngmake: *** [Makefile:2331：message_service_demo] 错误 2\\n')\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "build-host-resource.json",
    "build-policy.json",
    "checks.json",
    "configure-command.json",
    "execution.json",
    "original-source-manifest.json",
    "qualified-sdk-provenance.json",
    "result.json",
    "source-diff-from-reproduced-c97.json",
    "source-input-manifest.json"
  ]
}
```

## PERF943-complete-reference-cohort-r5

```json
{
  "directory": "PERF943-complete-reference-cohort-r5",
  "relative_path": "benchmark/results/peer-20261017/PERF943-complete-reference-cohort-r5",
  "result_sha256": "931b119750ae422a3e31c4dfa5b1f00d40f71fa8268cfd83f91f34f26c5bc458",
  "status": "FAILED",
  "issue_id": "PERF-943",
  "error": "AssertionError(('build-business-regressions', '[100%] Building CXX object CMakeFiles/native_reference_ready_batch_tests.dir/tests/message/native_reference_ready_batch_test.cpp.o\\n[100%] Linking CXX executable native_reference_ready_batch_tests\\n[100%] Built target native_reference_ready_batch_tests\\n[  0%] Built target tinyimx_config\\n[ 16%] Built target tinyimx_observability\\n[ 16%] Built target tinyimx_logging\\n[ 33%] Built target tinyimx_db\\n[ 33%] Built target tinyimx_security\\n[ 33%] Built target tinyimx_repository\\n[ 50%] Built target tinyimx_eventing\\n[ 66%] Built target tinyimx_outbox\\n[ 83%] Built target tinyimx_message_core\\nConsolidate compiler generated dependencies of target tinyimx_local_private_store\\n[100%] Built target tinyimx_local_private_store\\n[100%] Building CXX object CMakeFiles/native_private_persist_batch_tests.dir/tests/message/native_private_persist_batch_test.cpp.o\\n[100%] Linking CXX executable native_private_persist_batch_tests\\n[100%] Built target native_private_persist_batch_tests\\n[  0%] Built target tinyimx_config\\n[ 16%] Built target tinyimx_observability\\n[ 16%] Built target tinyimx_logging\\n[ 33%] Built target tinyimx_db\\n[ 33%] Built target tinyimx_security\\n[ 33%] Built target tinyimx_repository\\n[ 50%] Built target tinyimx_eventing\\n[ 66%] Built target tinyimx_outbox\\n[ 83%] Built target tinyimx_message_core\\n[100%] Built target tinyimx_local_private_store\\n[100%] Building CXX object CMakeFiles/native_receiver_confirmation_batch_tests.dir/tests/message/native_receiver_confirmation_batch_test.cpp.o\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/tests/message/native_receiver_confirmation_batch_test.cpp:3:10: fatal error: rocksdb/db.h: 没有那个文件或目录\\n    3 | #include <rocksdb/db.h>\\n      |          ^~~~~~~~~~~~~~\\ncompilation terminated.\\ngmake[3]: *** [CMakeFiles/native_receiver_confirmation_batch_tests.dir/build.make:76：CMakeFiles/native_receiver_confirmation_batch_tests.dir/tests/message/native_receiver_confirmation_batch_test.cpp.o] 错误 1\\ngmake[2]: *** [CMakeFiles/Makefile2:9645：CMakeFiles/native_receiver_confirmation_batch_tests.dir/all] 错误 2\\ngmake[1]: *** [CMakeFiles/Makefile2:9652：CMakeFiles/native_receiver_confirmation_batch_tests.dir/rule] 错误 2\\ngmake: *** [Makefile:3943：native_receiver_confirmation_batch_tests] 错误 2\\n'))\nTraceback (most recent call last):\n  File \"<stdin>\", line 112, in <module>\n  File \"<stdin>\", line 45, in run\nAssertionError: ('build-business-regressions', '[100%] Building CXX object CMakeFiles/native_reference_ready_batch_tests.dir/tests/message/native_reference_ready_batch_test.cpp.o\\n[100%] Linking CXX executable native_reference_ready_batch_tests\\n[100%] Built target native_reference_ready_batch_tests\\n[  0%] Built target tinyimx_config\\n[ 16%] Built target tinyimx_observability\\n[ 16%] Built target tinyimx_logging\\n[ 33%] Built target tinyimx_db\\n[ 33%] Built target tinyimx_security\\n[ 33%] Built target tinyimx_repository\\n[ 50%] Built target tinyimx_eventing\\n[ 66%] Built target tinyimx_outbox\\n[ 83%] Built target tinyimx_message_core\\nConsolidate compiler generated dependencies of target tinyimx_local_private_store\\n[100%] Built target tinyimx_local_private_store\\n[100%] Building CXX object CMakeFiles/native_private_persist_batch_tests.dir/tests/message/native_private_persist_batch_test.cpp.o\\n[100%] Linking CXX executable native_private_persist_batch_tests\\n[100%] Built target native_private_persist_batch_tests\\n[  0%] Built target tinyimx_config\\n[ 16%] Built target tinyimx_observability\\n[ 16%] Built target tinyimx_logging\\n[ 33%] Built target tinyimx_db\\n[ 33%] Built target tinyimx_security\\n[ 33%] Built target tinyimx_repository\\n[ 50%] Built target tinyimx_eventing\\n[ 66%] Built target tinyimx_outbox\\n[ 83%] Built target tinyimx_message_core\\n[100%] Built target tinyimx_local_private_store\\n[100%] Building CXX object CMakeFiles/native_receiver_confirmation_batch_tests.dir/tests/message/native_receiver_confirmation_batch_test.cpp.o\\n/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source/tests/message/native_receiver_confirmation_batch_test.cpp:3:10: fatal error: rocksdb/db.h: 没有那个文件或目录\\n    3 | #include <rocksdb/db.h>\\n      |          ^~~~~~~~~~~~~~\\ncompilation terminated.\\ngmake[3]: *** [CMakeFiles/native_receiver_confirmation_batch_tests.dir/build.make:76：CMakeFiles/native_receiver_confirmation_batch_tests.dir/tests/message/native_receiver_confirmation_batch_test.cpp.o] 错误 1\\ngmake[2]: *** [CMakeFiles/Makefile2:9645：CMakeFiles/native_receiver_confirmation_batch_tests.dir/all] 错误 2\\ngmake[1]: *** [CMakeFiles/Makefile2:9652：CMakeFiles/native_receiver_confirmation_batch_tests.dir/rule] 错误 2\\ngmake: *** [Makefile:3943：native_receiver_confirmation_batch_tests] 错误 2\\n')\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "build-host-resource.json",
    "build-policy.json",
    "checks.json",
    "configure-command.json",
    "execution.json",
    "original-source-manifest.json",
    "qualified-sdk-provenance.json",
    "result.json",
    "source-diff-from-reproduced-c97.json",
    "source-input-manifest.json"
  ]
}
```

## PERF943-complete-reference-cohort-r6

```json
{
  "directory": "PERF943-complete-reference-cohort-r6",
  "relative_path": "benchmark/results/peer-20261017/PERF943-complete-reference-cohort-r6",
  "result_sha256": "dfeac40b6fb27efa04b9248da980994e34f46ae8972b848183862a91d1e4ba0e",
  "status": "FAILED",
  "issue_id": "PERF-943",
  "error": "AssertionError(('contracts-native_private_persist_batch_tests', ''))\nTraceback (most recent call last):\n  File \"<stdin>\", line 113, in <module>\n  File \"<stdin>\", line 45, in run\nAssertionError: ('contracts-native_private_persist_batch_tests', '')\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "build-host-resource.json",
    "build-policy.json",
    "checks.json",
    "configure-command.json",
    "execution.json",
    "original-source-manifest.json",
    "qualified-sdk-provenance.json",
    "result.json",
    "source-diff-from-reproduced-c97.json",
    "source-input-manifest.json"
  ]
}
```

## PERF944-cold-public-file-restore-r1

```json
{
  "directory": "PERF944-cold-public-file-restore-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF944-cold-public-file-restore-r1",
  "result_sha256": "aec8f0a94462451c570e5c4dd52f3a2014c0aba6f7b1cc98620ab402e3fc1d58",
  "status": "EXACT_PREVIOUSLY_COLD_PUBLIC_FILE_RESTORED",
  "issue_id": "PERF-944",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF945-component-cost-and-root-write-analysis-r1

```json
{
  "directory": "PERF945-component-cost-and-root-write-analysis-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF945-component-cost-and-root-write-analysis-r1",
  "result_sha256": "62e2d575e0e14f49aa6ef7068800e4f1419cf8a382288b2e49152ce2f6f2bbd8",
  "status": "EXISTING_SAME_CLOCK_COMPONENT_CPU_AND_CGROUP_WRITE_PATHS_SAVED",
  "issue_id": "PERF-945",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF948-complete-cohort-contracts-r2

```json
{
  "directory": "PERF948-complete-cohort-contracts-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF948-complete-cohort-contracts-r2",
  "result_sha256": "2f3117900635aac3e1805f59482fd5c5fc9dfeda33d73fe9c6eabf35d7fd0ab7",
  "status": "FULL9_FROZEN_PRODUCTS_13_STORAGE_RPC_PERMISSION_FIFO_STOP_CONTRACT_RUNS_PASS",
  "issue_id": "PERF-948",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "checks.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "input-manifest.json",
    "preflight-memory.json",
    "resource-policy.json",
    "result.json"
  ]
}
```

## PERF949-complete-cohort-acceptance-r1

```json
{
  "directory": "PERF949-complete-cohort-acceptance-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF949-complete-cohort-acceptance-r1",
  "result_sha256": "7adcc5b424657bc2ef257fea469a05d8b9047f399bc5009491899018c01acba2",
  "status": "COMPLETE9_CMAKE_GRAPH_LOADER_13_CONTRACT_RUNS_AND_MATCHED_REFERENCE_SQL_COMPONENT_ACCEPTED",
  "issue_id": "PERF-949",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF951-isolated-MQ-runtime-r2

```json
{
  "directory": "PERF951-isolated-MQ-runtime-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF951-isolated-MQ-runtime-r2",
  "result_sha256": "615b742d780bf521cac9b0f38f5270b9abcdeb69e62363b4add1ca9a60c15c68",
  "status": "FAILED",
  "issue_id": "PERF-951",
  "error": "AssertionError(('compile-private-namespace', 'nedIsolatedMqR951.java:31: error: cannot find symbol\\n    var expectedGroup=template.getJSONObject(\"original_consumer_group_config\");\\n    ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:36: error: cannot find symbol\\n    var admin=new DefaultMQAdminExt();admin.setNamesrvAddr(args[0]);\\n    ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:42: error: cannot find symbol\\n      Path intent=Path.of(args[3]+\".intent.json\");\\n                      ^\\n  symbol:   method of(String)\\n  location: interface Path\\n/probe/OwnedIsolatedMqR951.java:44: error: cannot find symbol\\n      if(Files.exists(intent))require(JSON.parseObject(Files.readString(intent)).equals(plan),\"Exact owned intent required\");\\n                                                            ^\\n  symbol:   method readString(Path)\\n  location: class Files\\n/probe/OwnedIsolatedMqR951.java:45: error: cannot find symbol\\n      else{require(!hasTopic&&!hasGroup,\"Fresh private namespace required\");Files.writeString(intent,plan.toJSONString(),StandardOpenOption.CREATE_NEW);}\\n                                                                                 ^\\n  symbol:   method writeString(Path,String,StandardOpenOption)\\n  location: class Files\\n/probe/OwnedIsolatedMqR951.java:46: error: cannot find symbol\\n      var t=RemotingSerializable.fromJson(expectedTopic.toJSONString(),TopicConfig.class);\\n      ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:47: error: cannot find symbol\\n      var g=RemotingSerializable.fromJson(expectedGroup.toJSONString(),SubscriptionGroupConfig.class);\\n      ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:51: error: cannot find symbol\\n      var actualTopic=object(admin.examineTopicConfig(args[1],topic));\\n      ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:52: error: cannot find symbol\\n      var actualGroup=object(admin.examineSubscriptionGroupConfig(args[1],group));\\n      ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:54: error: cannot find symbol\\n      var result=new JSONObject();result.put(\"status\",\"PRIVATE_TOPIC_GROUP_EXACT_PUBLIC_TEMPLATE_MATCH\");\\n      ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n15 errors\\n'))\nTraceback (most recent call last):\n  File \"<stdin>\", line 54, in <module>\n  File \"<stdin>\", line 28, in run\nAssertionError: ('compile-private-namespace', 'nedIsolatedMqR951.java:31: error: cannot find symbol\\n    var expectedGroup=template.getJSONObject(\"original_consumer_group_config\");\\n    ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:36: error: cannot find symbol\\n    var admin=new DefaultMQAdminExt();admin.setNamesrvAddr(args[0]);\\n    ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:42: error: cannot find symbol\\n      Path intent=Path.of(args[3]+\".intent.json\");\\n                      ^\\n  symbol:   method of(String)\\n  location: interface Path\\n/probe/OwnedIsolatedMqR951.java:44: error: cannot find symbol\\n      if(Files.exists(intent))require(JSON.parseObject(Files.readString(intent)).equals(plan),\"Exact owned intent required\");\\n                                                            ^\\n  symbol:   method readString(Path)\\n  location: class Files\\n/probe/OwnedIsolatedMqR951.java:45: error: cannot find symbol\\n      else{require(!hasTopic&&!hasGroup,\"Fresh private namespace required\");Files.writeString(intent,plan.toJSONString(),StandardOpenOption.CREATE_NEW);}\\n                                                                                 ^\\n  symbol:   method writeString(Path,String,StandardOpenOption)\\n  location: class Files\\n/probe/OwnedIsolatedMqR951.java:46: error: cannot find symbol\\n      var t=RemotingSerializable.fromJson(expectedTopic.toJSONString(),TopicConfig.class);\\n      ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:47: error: cannot find symbol\\n      var g=RemotingSerializable.fromJson(expectedGroup.toJSONString(),SubscriptionGroupConfig.class);\\n      ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:51: error: cannot find symbol\\n      var actualTopic=object(admin.examineTopicConfig(args[1],topic));\\n      ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:52: error: cannot find symbol\\n      var actualGroup=object(admin.examineSubscriptionGroupConfig(args[1],group));\\n      ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n/probe/OwnedIsolatedMqR951.java:54: error: cannot find symbol\\n      var result=new JSONObject();result.put(\"status\",\"PRIVATE_TOPIC_GROUP_EXACT_PUBLIC_TEMPLATE_MATCH\");\\n      ^\\n  symbol:   class var\\n  location: class OwnedIsolatedMqR951\\n15 errors\\n')\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "checks.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "preflight-memory.json",
    "resource-policy.json",
    "result.json"
  ]
}
```

## PERF951-isolated-MQ-runtime-r3

```json
{
  "directory": "PERF951-isolated-MQ-runtime-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF951-isolated-MQ-runtime-r3",
  "result_sha256": "15bfb38f366f483798b3c7068ca4efd29115af6c2db6a8280ce3e899e9d2b094",
  "status": "FAILED",
  "issue_id": "PERF-951",
  "error": "AssertionError(('private listener readiness', '172.18.0.7', 8081))\nTraceback (most recent call last):\n  File \"<stdin>\", line 35, in wait_port\n  File \"/usr/lib/python3.10/socket.py\", line 845, in create_connection\n    raise err\n  File \"/usr/lib/python3.10/socket.py\", line 833, in create_connection\n    sock.connect(sa)\nTimeoutError: timed out\n\nDuring handling of the above exception, another exception occurred:\n\nTraceback (most recent call last):\n  File \"<stdin>\", line 69, in <module>\n  File \"<stdin>\", line 36, in wait_port\nAssertionError: ('private listener readiness', '172.18.0.7', 8081)\n\nRESTORATION:AssertionError()\nTraceback (most recent call last):\n  File \"<stdin>\", line 109, in <module>\nAssertionError\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "checks.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "preflight-memory.json",
    "resource-policy.json",
    "result.json"
  ]
}
```

## PERF951-isolated-MQ-runtime-r4

```json
{
  "directory": "PERF951-isolated-MQ-runtime-r4",
  "relative_path": "benchmark/results/peer-20261017/PERF951-isolated-MQ-runtime-r4",
  "result_sha256": "904bf4becf88f387f3d76bda19873e562b4d2f1f5f406d262673c58142d063d2",
  "status": "FAILED",
  "issue_id": "PERF-951",
  "error": "JSONDecodeError('Expecting property name enclosed in double quotes: line 1 column 699 (char 698)')\nTraceback (most recent call last):\n  File \"<stdin>\", line 79, in <module>\n  File \"/usr/lib/python3.10/json/__init__.py\", line 346, in loads\n    return _default_decoder.decode(s)\n  File \"/usr/lib/python3.10/json/decoder.py\", line 337, in decode\n    obj, end = self.raw_decode(s, idx=_w(s, 0).end())\n  File \"/usr/lib/python3.10/json/decoder.py\", line 353, in raw_decode\n    obj, end = self.scan_once(s, idx)\njson.decoder.JSONDecodeError: Expecting property name enclosed in double quotes: line 1 column 699 (char 698)\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "checks.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "preflight-memory.json",
    "resource-policy.json",
    "result.json"
  ]
}
```

## PERF951-isolated-MQ-runtime-r5

```json
{
  "directory": "PERF951-isolated-MQ-runtime-r5",
  "relative_path": "benchmark/results/peer-20261017/PERF951-isolated-MQ-runtime-r5",
  "result_sha256": "066a92064461b97fded9835fe03cb71b7996c9cb9d6d9ef756326a3603b5448e",
  "status": "FAILED",
  "issue_id": "PERF-951",
  "error": "AssertionError(('real-MQ-Redis-RPC-a', 'PASS owned physical native/MQ fixture\\nFAIL exact owned broker/temporary Redis namespace\\n'))\nTraceback (most recent call last):\n  File \"<stdin>\", line 103, in <module>\n  File \"<stdin>\", line 31, in run\nAssertionError: ('real-MQ-Redis-RPC-a', 'PASS owned physical native/MQ fixture\\nFAIL exact owned broker/temporary Redis namespace\\n')\n",
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "checks.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "preflight-memory.json",
    "resource-policy.json",
    "result.json"
  ]
}
```

## PERF951-isolated-MQ-runtime-r6

```json
{
  "directory": "PERF951-isolated-MQ-runtime-r6",
  "relative_path": "benchmark/results/peer-20261017/PERF951-isolated-MQ-runtime-r6",
  "result_sha256": "6d65fcde66b19d87f3f7ed00d1428bcfe12012f7e2c2a882937e52a0cb6e44d9",
  "status": "BOTH_PRIVATE_BROKERS_REAL13_EVENT_NATIVE_MQ_REDIS_RPC_REBUILD_AND_DRAIN_PASS",
  "issue_id": "PERF-951",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "arms.json",
    "artifact-manifest.json",
    "checks.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "preflight-memory.json",
    "resource-policy.json",
    "result.json"
  ]
}
```

## PERF953-private-MQ-guest-syntax-r1

```json
{
  "directory": "PERF953-private-MQ-guest-syntax-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF953-private-MQ-guest-syntax-r1",
  "result_sha256": "313ccebbf3732a9aa054f5b240a5eca6eb035279b8786669f13051974f12be0d",
  "status": "FULL_GENERATED_GUEST_PROGRAM_COMPILES_OFFLINE",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF954-private-MQ-readiness-and-restore-r1

```json
{
  "directory": "PERF954-private-MQ-readiness-and-restore-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF954-private-MQ-readiness-and-restore-r1",
  "result_sha256": "6906b4fa59b51d9679ab79a6067c9c07f5b3c74c7d8d5360495755aab5c544f9",
  "status": "ORIGINAL3012_CONFIGS_IMAGES_EQUAL_ORIGINAL20_HEALTHY_FIRST_START_FALSE_NULL_ONLY",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF955-install-cache-cold-r2

```json
{
  "directory": "PERF955-install-cache-cold-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF955-install-cache-cold-r2",
  "result_sha256": "13c58f0f08d706cc563869b3e79aaf4685108f44632143cf471426ce64824d66",
  "status": "ORDINARY_INSTALLER_CACHES_E_RESTORED_THEN_SOURCE_COPIES_RECLAIMED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-proof.json",
    "result.json",
    "source-proof.json"
  ]
}
```

## PERF955-ordinary-cache-inventory-r1

```json
{
  "directory": "PERF955-ordinary-cache-inventory-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF955-ordinary-cache-inventory-r1",
  "result_sha256": "8fa3b23713bb4d739f530aaef4eef1c16a496d2e5bfa8efd562dfe3402f76be2",
  "status": "READ_ONLY_ORDINARY_DOWNLOAD_AND_APT_INDEX_CACHE_INVENTORY",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF958-closed-diagnostic-log-inventory-r2

```json
{
  "directory": "PERF958-closed-diagnostic-log-inventory-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF958-closed-diagnostic-log-inventory-r2",
  "result_sha256": "87ad61930e6f4d8407b6e35be48e432ed684097eba63d3db12ed742bc1c3b295",
  "status": "CLOSED_DIAGNOSTIC_TEXT_LOGS_INSIDE_LOG_VOLUME_ONLY_NO_OPEN_LOGS",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF958-closed-MQ-log-cold-r4

```json
{
  "directory": "PERF958-closed-MQ-log-cold-r4",
  "relative_path": "benchmark/results/peer-20261017/PERF958-closed-MQ-log-cold-r4",
  "result_sha256": "791335b87dfbf0ca6e44f911dc4ac001c89f0d0b3eacfa172e4d8360576b58b9",
  "status": "CLOSED_DIAGNOSTIC_TEXT_LOGS_E_RESTORED_AND_COLD_ARCHIVED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "restore-proof.json",
    "result.json",
    "source-proof.json"
  ]
}
```

## PERF958-rotated-MQ-logs-and-old-helper-inventory-r1

```json
{
  "directory": "PERF958-rotated-MQ-logs-and-old-helper-inventory-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF958-rotated-MQ-logs-and-old-helper-inventory-r1",
  "result_sha256": "83e3e30d780815b046bad1959cb942dca00de6f6345f4029e399aa442e9eea3b",
  "status": "CLOSED_ROTATED_DIAGNOSTIC_LOGS_READONLY_INVENTORY",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "legacy-helper-candidates.json",
    "result.json",
    "system-catalog-cache-inventory.json"
  ]
}
```

## PERF959-complete-graph-checkpoint-r2

```json
{
  "directory": "PERF959-complete-graph-checkpoint-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF959-complete-graph-checkpoint-r2",
  "result_sha256": "3cc59b6ce6931d92988d0c949b2342d517d45533d7a37ae10f103f0cf5d8dcaa",
  "status": "COMPLETE_SOURCE_RESULTS_AND_CONTINUATION_E_ACTUALLY_RESTORED_ALL_SHA_EQUAL",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "continuation.json",
    "execution.json",
    "final-state.json",
    "result.json"
  ]
}
```

## PERF960-cold-infra6-current-SQL-perf921-q2-local-users20k

```json
{
  "directory": "PERF960-cold-infra6-current-SQL-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF960-cold-infra6-current-SQL-perf921-q2-local-users20k",
  "result_sha256": "fa5365d62e6c61eac0db2de9818c9c02d04aa9670456d9f664a65a683a71fe89",
  "status": "COLD_INFRA6_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-960",
  "error": null,
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF961-current-graph-isolated-SQL-r1

```json
{
  "directory": "PERF961-current-graph-isolated-SQL-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF961-current-graph-isolated-SQL-r1",
  "result_sha256": "26b6410810b5a0b4682e4657b3fdb77b83b79c37ddcbfc3cb131863f1bd0e39f",
  "status": "CURRENT_COMPLETE_GRAPH8_REAL_SQL_OFF_ON_INDEPENDENCE4_CRASH_RECOVERIES_BATCH_ISOLATION_PASS",
  "issue_id": "PERF-961",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "cases.json",
    "checks.json",
    "database-profile.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "preflight-memory.json",
    "resource-policy.json",
    "result.json"
  ]
}
```

## PERF962-current-peer-pipeline-work-audit-r1

```json
{
  "directory": "PERF962-current-peer-pipeline-work-audit-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF962-current-peer-pipeline-work-audit-r1",
  "result_sha256": "0b8da0a0da681a41ab415a8cd98a3a0f156c76b815dc891a98b3e4acd8292e39",
  "status": "EXACT_CACHED_SOURCE_DIFFERENCES_AND_SINGLE_FACTOR_DECISIONS_SAVED",
  "issue_id": "PERF-962",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF963-current-full-business-a-r1

```json
{
  "directory": "PERF963-current-full-business-a-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF963-current-full-business-a-r1",
  "result_sha256": "c94c1f1b370a4728213443d7412ff7a7ba960b5ba946d6d3f86d8066132b643d",
  "status": "COMPLETE_CURRENT9_PLUS_DESKTOP_REAL_TCP_SQL_MQ_CACHE_GROUP_FILE_AUTHORITY_BUSINESS_PASS",
  "issue_id": "PERF-963",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "checks.json",
    "client-records.json",
    "closed-audit.json",
    "functional-resource-result.json",
    "grant.json",
    "headroom-grant.json",
    "host-preflight.json",
    "host-samples.json",
    "http-status-summary.json",
    "identity.json",
    "namespace.json",
    "namespace.json.intent.json",
    "namespace.json.routing.json",
    "owned-part-corruption.json",
    "permission-available.json",
    "permission-block_a.json",
    "permission-block_b.json",
    "permission-remove_a.json",
    "permission-restore.json",
    "permission-snapshot.json",
    "permission-unavailable.json",
    "permission-unblock.json",
    "read-projection-converged.json",
    "resource-policy.json",
    "result.json",
    "route-census.json",
    "runtime-result.json",
    "suite.json"
  ]
}
```

## PERF963-current-full-business-b-r1

```json
{
  "directory": "PERF963-current-full-business-b-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF963-current-full-business-b-r1",
  "result_sha256": "dbf69d5d2a3a53e7dd0b20fdec22b48a7b05ad2930f4c32d22ce3ca304c8cc8b",
  "status": "COMPLETE_CURRENT9_PLUS_DESKTOP_REAL_TCP_SQL_MQ_CACHE_GROUP_FILE_AUTHORITY_BUSINESS_PASS",
  "issue_id": "PERF-963",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "checks.json",
    "client-records.json",
    "closed-audit.json",
    "functional-resource-result.json",
    "grant.json",
    "headroom-grant.json",
    "host-preflight.json",
    "host-samples.json",
    "http-status-summary.json",
    "identity.json",
    "namespace.json",
    "namespace.json.intent.json",
    "namespace.json.routing.json",
    "owned-part-corruption.json",
    "permission-available.json",
    "permission-block_a.json",
    "permission-block_b.json",
    "permission-remove_a.json",
    "permission-restore.json",
    "permission-snapshot.json",
    "permission-unavailable.json",
    "permission-unblock.json",
    "read-projection-converged.json",
    "resource-policy.json",
    "result.json",
    "route-census.json",
    "runtime-result.json",
    "suite.json"
  ]
}
```

## PERF964-cold-infra6-full-business-a-perf921-q2-local-users20k

```json
{
  "directory": "PERF964-cold-infra6-full-business-a-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF964-cold-infra6-full-business-a-perf921-q2-local-users20k",
  "result_sha256": "36e32abc8d02a84c50da78d88c4ef2ac9b69c117a4b63e69294da34f6c0d624b",
  "status": "COLD_INFRA6_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-964",
  "error": null,
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF964-cold-infra6-full-business-b-perf921-q2-local-users20k

```json
{
  "directory": "PERF964-cold-infra6-full-business-b-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF964-cold-infra6-full-business-b-perf921-q2-local-users20k",
  "result_sha256": "dab573814ad1d1af36c76860eb9089b232537150047cc766e9360bec742be278",
  "status": "COLD_INFRA6_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-964",
  "error": null,
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF965-current-business-acceptance-r1

```json
{
  "directory": "PERF965-current-business-acceptance-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF965-current-business-acceptance-r1",
  "result_sha256": "f4ec72871ff416ceec941b9193bf9024027c41ab49b377819041ab62181e3499",
  "status": "CURRENT_COMPLETE9_REFERENCE_OFF_ON414_REAL_BUSINESS_ASSERTIONS8_CURRENT_SQL_CASES4_REAL_CRASH_RECOVERIES_PASS",
  "issue_id": "PERF-965",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF966-cold-infra6-formal-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF966-cold-infra6-formal-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF966-cold-infra6-formal-p1-perf921-q2-local-users20k",
  "result_sha256": "e5b4724a0c4a9ca43061d89d1df2035a5f1f8105d924313a6e2514051d259e4d",
  "status": "COLD_INFRA6_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-966",
  "error": null,
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF966-cold-infra6-formal-p2-perf921-q2-local-users20k

```json
{
  "directory": "PERF966-cold-infra6-formal-p2-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF966-cold-infra6-formal-p2-perf921-q2-local-users20k",
  "result_sha256": "11bc09871084fb1e0d0c531280cf093f8877270476beff129855e07456633640",
  "status": "COLD_INFRA6_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-966",
  "error": null,
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## perf966-p1-reference-users20k

```json
{
  "directory": "perf966-p1-reference-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf966-p1-reference-users20k",
  "result_sha256": "06fe0f37c96392e8a7fe054a10c50f705aa77d546a8b4a09ff12f4469b3f0623",
  "status": "COMPLETE60000_CLIENT_RESOURCE_AND_SERVER_CLOSURE_PASS_NOT_CAPACITY",
  "issue_id": "PERF-966",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "barrier-contract.json",
    "checks.json",
    "client-command.json",
    "client-ports.json",
    "client-records.json",
    "clock-after.json",
    "clock-before.json",
    "closed-audit.json",
    "formal-window-analysis-r967.json",
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
    "policy-label-addendum-r972.json",
    "resource-monitor-terminal.json",
    "resource-policy.json",
    "result.json",
    "runtime-result.json",
    "spec.json",
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

## perf966-p2-reference-users20k

```json
{
  "directory": "perf966-p2-reference-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf966-p2-reference-users20k",
  "result_sha256": "ec88b65b39be60fe61eac6b1f08b840fa3f05cba1799716dde126de9fd468de5",
  "status": "COMPLETE60000_CLIENT_RESOURCE_AND_SERVER_CLOSURE_PASS_NOT_CAPACITY",
  "issue_id": "PERF-966",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "barrier-contract.json",
    "checks.json",
    "client-command.json",
    "client-ports.json",
    "client-records.json",
    "clock-after.json",
    "clock-before.json",
    "closed-audit.json",
    "formal-window-analysis-r967.json",
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
    "policy-label-addendum-r972.json",
    "resource-monitor-terminal.json",
    "resource-policy.json",
    "result.json",
    "runtime-result.json",
    "spec.json",
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

## PERF968-current-followthrough-checkpoint-r1

```json
{
  "directory": "PERF968-current-followthrough-checkpoint-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF968-current-followthrough-checkpoint-r1",
  "result_sha256": "5da9df43a1387330fd87d00b71b701188103d30216b46d3c57766063ca21281e",
  "status": "INCREMENTAL_CURRENT_SOURCE_BUSINESS_RESULTS_AND_ACTIVE_FORMAL_CONTINUATION_ACTUALLY_RESTORED_ALL_SHA_EQUAL",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "continuation.json",
    "result.json"
  ]
}
```

## PERF969-production-reference-budget-mismatch-r1

```json
{
  "directory": "PERF969-production-reference-budget-mismatch-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF969-production-reference-budget-mismatch-r1",
  "result_sha256": "b3df63a16591ddb72fa0da3df33321a14d91d0fd583e2397117260fc2280042e",
  "status": "REAL20K_LOAD_PROVES_ENABLED_REGISTRATION_BATCHES_HAVE_SIZE1",
  "issue_id": "PERF-969",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF970-reference-production-budget-r1

```json
{
  "directory": "PERF970-reference-production-budget-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF970-reference-production-budget-r1",
  "result_sha256": "103bc9e24a52c0bad39f366e811191ec93c64a286ba978f8e037a3d3c19b0059",
  "status": "ACTUAL_CPP8_CALLERS32_POOL_OLD31_FAILS_NEW4_BATCHES5_REGISTRATIONS_ALL_CONTRACTS_AND_SANITIZERS_PASS",
  "issue_id": "PERF-970",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "functional-resource-result.json",
    "host-samples.json",
    "preflight.json",
    "result.json"
  ]
}
```

## PERF971-fixed-budget-complete-cohort-SQL-r1

```json
{
  "directory": "PERF971-fixed-budget-complete-cohort-SQL-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF971-fixed-budget-complete-cohort-SQL-r1",
  "result_sha256": "df83622447aec198716d5dfc03311cd315567bbade8f4ddaea9fa190ed88ef4f",
  "status": "FIXED_BUDGET_COMPLETE9_LOADERS9_REAL_SQL_CASES4_CRASH_RECOVERIES_AND_PRODUCTION8_CALLER32_POOL_PASS",
  "issue_id": "PERF-971",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "cases.json",
    "checks.json",
    "cohort-source-manifest.json",
    "database-profile.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "planned-source-manifest.json",
    "preflight-memory.json",
    "products.json",
    "resource-policy.json",
    "result.json"
  ]
}
```

## PERF971-fixed-budget-current-SQL-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF971-fixed-budget-current-SQL-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF971-fixed-budget-current-SQL-p1-perf921-q2-local-users20k",
  "result_sha256": "9c8a96806678fab621569140d6466a074546a79b489f876022717b9d4dc0133e",
  "status": "COLD_INFRA6_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-971",
  "error": null,
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF972-reference-budget-followthrough-r1

```json
{
  "directory": "PERF972-reference-budget-followthrough-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF972-reference-budget-followthrough-r1",
  "result_sha256": "a724a30a44b49d4c657de3ac8dc3071daea961de76e3f930818e04f8297c3712",
  "status": "NO_OP_OPTIMIZATION_REJECTED_PRODUCTION_CONCURRENCY_DEFECT_FIXED_REGRESSION_AND_SANITIZERS_PASS_REAL_SQL_EXECUTING",
  "issue_id": "PERF-972",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF973-budget-fix-checkpoint-r1

```json
{
  "directory": "PERF973-budget-fix-checkpoint-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF973-budget-fix-checkpoint-r1",
  "result_sha256": "143599fc7beed4982a62440a7125546727312622b9078b376df3aa82d2cfff81",
  "status": "CLOSED120000_FORMAL_RAW_WORK_COUNTERS_SOURCE_PATCH_UNIT_SANITIZERS_AND_ACTIVE_SQL_CONTINUATION_ACTUALLY_RESTORED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "continuation.json",
    "result.json"
  ]
}
```

## PERF974-current-full-business-a-r1

```json
{
  "directory": "PERF974-current-full-business-a-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF974-current-full-business-a-r1",
  "result_sha256": "7f61e971776a5f8f17684a7f86b4bd0d684933830a84f86f8d0e5d943ceef43c",
  "status": "COMPLETE_CURRENT9_PLUS_DESKTOP_REAL_TCP_SQL_MQ_CACHE_GROUP_FILE_AUTHORITY_BUSINESS_PASS",
  "issue_id": "PERF-974",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "checks.json",
    "client-records.json",
    "closed-audit.json",
    "functional-resource-result.json",
    "grant.json",
    "headroom-grant.json",
    "host-preflight.json",
    "host-samples.json",
    "http-status-summary.json",
    "identity.json",
    "namespace.json",
    "namespace.json.intent.json",
    "namespace.json.routing.json",
    "owned-part-corruption.json",
    "permission-available.json",
    "permission-block_a.json",
    "permission-block_b.json",
    "permission-remove_a.json",
    "permission-restore.json",
    "permission-snapshot.json",
    "permission-unavailable.json",
    "permission-unblock.json",
    "read-projection-converged.json",
    "resource-policy.json",
    "result.json",
    "route-census.json",
    "runtime-result.json",
    "suite.json"
  ]
}
```

## PERF974-current-full-business-b-r1

```json
{
  "directory": "PERF974-current-full-business-b-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF974-current-full-business-b-r1",
  "result_sha256": "7b61320c3c1c71cacc185c93601ad40aa2524572521cd28fe6280b7550f4dde7",
  "status": "FAILED",
  "issue_id": "PERF-974",
  "error": "RuntimeError('Current full graph did not become ready; no rate test')\nTraceback (most recent call last):\n  File \"<WORKSPACE>\\benchmark\\peer_compare\\run_fixed_budget_full_business_r974.py\", line 222, in <module>\n    if server.poll()is not None or time.monotonic()>deadline:raise RuntimeError('Current full graph did not become ready; no rate test')\n                                                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\nRuntimeError: Current full graph did not become ready; no rate test\n\nCLOSED_AUDIT:AssertionError('closed-audit: Traceback (most recent call last):\\n  File \"<stdin>\", line 7, in <module>\\n  File \"/usr/lib/python3.10/pathlib.py\", line 1126, in read_bytes\\n    with self.open(mode=\\'rb\\') as f:\\n  File \"/usr/lib/python3.10/pathlib.py\", line 1119, in open\\n    return self._accessor.open(self, mode, buffering, encoding, errors,\\nFileNotFoundError: [Errno 2] No such file or directory: \\'/mnt/tinyimx-benchmark/full-business-trials-perf974/perf974-b-current-business/private-runtime.json\\'\\n')\nDeclared resource gate failed",
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "checks.json",
    "client-records.json",
    "closed-audit.json",
    "functional-resource-result.json",
    "host-samples.json",
    "resource-policy.json",
    "result.json",
    "runtime-result.json"
  ]
}
```

## PERF974-fixed-budget-full-business-a-perf921-q2-local-users20k

```json
{
  "directory": "PERF974-fixed-budget-full-business-a-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF974-fixed-budget-full-business-a-perf921-q2-local-users20k",
  "result_sha256": "31246a86fddad1c0bf2f97c7cb0187c7fa9d9c72fdf42590ef233bbf75c55200",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-974",
  "error": "idle-greeter-cleanup: Traceback (most recent call last):\n  File \"<stdin>\", line 4, in <module>\n  File \"<string>\", line 31, in stop_idle_greeter\nAssertionError\n",
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF974-fixed-budget-full-business-a-perf921-q2-local-users20k-r2

```json
{
  "directory": "PERF974-fixed-budget-full-business-a-perf921-q2-local-users20k-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF974-fixed-budget-full-business-a-perf921-q2-local-users20k-r2",
  "result_sha256": "2134c2f7b2de3750920769c7cf8ae9f737ae931b782978d1a4b58bbb54c7d898",
  "status": "COLD_INFRA6_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-974",
  "error": null,
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF974-fixed-budget-full-business-b-perf921-q2-local-users20k

```json
{
  "directory": "PERF974-fixed-budget-full-business-b-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF974-fixed-budget-full-business-b-perf921-q2-local-users20k",
  "result_sha256": "246177cace7b3114cc211c13de28ac4aacebabf6bdbd0cf52bc628783768e18b",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-974",
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF975-fixed-budget-formal-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF975-fixed-budget-formal-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF975-fixed-budget-formal-p1-perf921-q2-local-users20k",
  "result_sha256": "df78b958904417afcf552bdedfd725dfdd019f0f9a6e61c8d960a45e1ea9cb05",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-975",
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## perf975-p1-reference-users20k

```json
{
  "directory": "perf975-p1-reference-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf975-p1-reference-users20k",
  "result_sha256": "e0335f9b2ea5bf4ae3f3b6749a4eb71c7ce7cb6b04aea81bb07cfa0aec022fc6",
  "status": "FAILED",
  "issue_id": "PERF-975",
  "error": "AssertionError('Host6GiB gate failed; stop owned load')\nTraceback (most recent call last):\n  File \"<WORKSPACE>\\benchmark\\peer_compare\\run_fixed_budget_formal_point_r975.py\", line 246, in <module>\n    suite=run_rate_case(O,ssh,remote,remote_folder,case,samples)\n          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n  File \"<WORKSPACE>\\benchmark\\peer_compare\\formal_current_client_r975.py\", line 65, in run_rate_case\n    assert not host_samples or host_samples[-1]['available_bytes']>=6*2**30,'Host6GiB gate failed; stop owned load'\n           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\nAssertionError: Host6GiB gate failed; stop owned load\n\nCLOSED_AUDIT:AssertionError('closed-audit: Traceback (most recent call last):\\n  File \"<stdin>\", line 12, in <module>\\nAssertionError: (\\'21024\\', \\'0\\', \\'0\\', \\'0\\', \\'21024\\')\\n')\nDeclared resource gate failed",
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "barrier-contract.json",
    "checks.json",
    "client-command.json",
    "client-ports.json",
    "client-records.json",
    "clock-before.json",
    "closed-audit.json",
    "functional-resource-result.json",
    "grant.json",
    "headroom-grant.json",
    "host-preflight.json",
    "host-samples.json",
    "identity.json",
    "namespace.json",
    "namespace.json.intent.json",
    "namespace.json.routing.json",
    "resource-monitor-terminal.json",
    "resource-policy.json",
    "result.json",
    "runtime-result.json",
    "spec.json",
    "sql-work-before-offer.json"
  ]
}
```

## PERF977-lifecycle-phase-label-r1

```json
{
  "directory": "PERF977-lifecycle-phase-label-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF977-lifecycle-phase-label-r1",
  "result_sha256": "8c9aae4e300126035e3c7fed9fe7e2851371981d84f44445c08055b9b6234ec3",
  "status": "EXACT_OVERLONG70_PHASE_LABEL_REPRODUCED_SHORT_LABELS120_COMBINATIONS_PASS",
  "issue_id": "PERF-977",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "before.json",
    "result.json"
  ]
}
```

## PERF978-read-only-SQL-work-snapshot-r1

```json
{
  "directory": "PERF978-read-only-SQL-work-snapshot-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF978-read-only-SQL-work-snapshot-r1",
  "result_sha256": "6cd33d47859764b8fa2f2dc9261237297a8a2620fdabc5be073b1bd4d625d64c",
  "status": "ACTUAL_MYSQL8_COUNTER_QUERY_AND_PARSE_PASS_NO_RESET_NO_ENABLE_NO_WORKLOAD",
  "issue_id": "PERF-978",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json",
    "snapshot.json"
  ]
}
```

## PERF978-read-only-SQL-work-snapshot-r2

```json
{
  "directory": "PERF978-read-only-SQL-work-snapshot-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF978-read-only-SQL-work-snapshot-r2",
  "result_sha256": "a952cca561368b10eed83d3a495599a554b5096ec4e012e3418cafd2c5566170",
  "status": "ACTUAL_MYSQL8_COUNTER_QUERY_AND_PARSE_PASS_NO_RESET_NO_ENABLE_NO_WORKLOAD",
  "issue_id": "PERF-978",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json",
    "snapshot.json"
  ]
}
```

## PERF978-read-only-SQL-work-snapshot-r3

```json
{
  "directory": "PERF978-read-only-SQL-work-snapshot-r3",
  "relative_path": "benchmark/results/peer-20261017/PERF978-read-only-SQL-work-snapshot-r3",
  "result_sha256": "8aad7669b380ca7916a04bc966a79622c52f8290af19ab350d0097c68de85b10",
  "status": "ACTUAL_MYSQL8_COUNTER_QUERY_AND_PARSE_PASS_NO_RESET_NO_ENABLE_NO_WORKLOAD",
  "issue_id": "PERF-978",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json",
    "snapshot.json"
  ]
}
```

## PERF979-closed-diagnostic-inventory-r1

```json
{
  "directory": "PERF979-closed-diagnostic-inventory-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF979-closed-diagnostic-inventory-r1",
  "result_sha256": "e007ecca84c8054474108ecd92f1f0b1eaf6541c446d81c7705ad5f3ed765d08",
  "status": "CLOSED_KNOWN_BROKER_DIAGNOSTIC_TEXT_ONLY_NO_ACTIVE_OR_MESSAGE_DATA",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF980-closed-resource-report-inventory-r1

```json
{
  "directory": "PERF980-closed-resource-report-inventory-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF980-closed-resource-report-inventory-r1",
  "result_sha256": "ee7b3574e29ffaea3c57b65c8ddff52d50d6dcb7028224b65b1fa0b46a7ad516",
  "status": "CLOSED_OWNED_PEER_RESOURCE_OBSERVATION_JSON_ONLY",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF981-stopped-diagnostic-text-cold-r1

```json
{
  "directory": "PERF981-stopped-diagnostic-text-cold-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF981-stopped-diagnostic-text-cold-r1",
  "result_sha256": "6949bdfbc2c2bc4ef0b9683ea3e20c930ebd1b1c18a515de7ed1d0b14e81001e",
  "status": "CLOSED_DIAGNOSTIC_TEXT_LOGS_E_RESTORED_AND_COLD_ARCHIVED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "closed-diagnostic-plan.json",
    "execution.json",
    "final-acceptance.json",
    "normal-stop14-before-diagnostic-archive.json",
    "restore-failure.json",
    "restore-proof.json",
    "restore-recovery-execution.json",
    "result.json",
    "source-proof.json"
  ]
}
```

## PERF982-current-full-business-b-r1

```json
{
  "directory": "PERF982-current-full-business-b-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF982-current-full-business-b-r1",
  "result_sha256": "cf85cad85690ae08262fb5c7efe4bc24ea24375242240d5b3e7350c050aeb8df",
  "status": "COMPLETE_CURRENT9_PLUS_DESKTOP_REAL_TCP_SQL_MQ_CACHE_GROUP_FILE_AUTHORITY_BUSINESS_PASS",
  "issue_id": "PERF-982",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "checks.json",
    "client-records.json",
    "closed-audit.json",
    "functional-resource-result.json",
    "grant.json",
    "headroom-grant.json",
    "host-preflight.json",
    "host-samples.json",
    "http-status-summary.json",
    "identity.json",
    "namespace.json",
    "namespace.json.intent.json",
    "namespace.json.routing.json",
    "owned-part-corruption.json",
    "permission-available.json",
    "permission-block_a.json",
    "permission-block_b.json",
    "permission-remove_a.json",
    "permission-restore.json",
    "permission-snapshot.json",
    "permission-unavailable.json",
    "permission-unblock.json",
    "read-projection-converged.json",
    "resource-policy.json",
    "result.json",
    "route-census.json",
    "runtime-result.json",
    "suite.json"
  ]
}
```

## PERF982-ref-budget-full-business-b-perf921-q2-local-users20k

```json
{
  "directory": "PERF982-ref-budget-full-business-b-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF982-ref-budget-full-business-b-perf921-q2-local-users20k",
  "result_sha256": "1581867668b99178fc06c75b63bf3e64f5cbe614fa55d95dd6394d63c7499c02",
  "status": "COLD_INFRA6_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-982",
  "error": null,
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## PERF984-first-user-traversal-windows-r1

```json
{
  "directory": "PERF984-first-user-traversal-windows-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF984-first-user-traversal-windows-r1",
  "result_sha256": "dfe724fed6cadd0ad2e414066f1f0169620f90d6161d8a53a71980135334528c",
  "status": "BOTH120000_ROWS_FIXED_FIRST_TRAVERSAL_AND_HOT30_RECOMPUTED_RAW_PRESERVED",
  "issue_id": "PERF-984",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "analysis-plan.json",
    "result.json"
  ]
}
```

## PERF985-fixed-budget-business-acceptance-r1

```json
{
  "directory": "PERF985-fixed-budget-business-acceptance-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF985-fixed-budget-business-acceptance-r1",
  "result_sha256": "eeceaaac5cb2c3062d694041e8ac8383f4495c2cb55c535b60cd15090a5771e9",
  "status": "FIXED_CAP4_COMPLETE9_LOADERS_REAL_SQL9_4_PROCESS_RECOVERIES_AND414_FULL_BUSINESS_ASSERTIONS_PASS",
  "issue_id": "PERF-985",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF986-bounded-preparation-hash-parity-r2

```json
{
  "directory": "PERF986-bounded-preparation-hash-parity-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF986-bounded-preparation-hash-parity-r2",
  "result_sha256": "0677748d7d2c8785ae4935fc6f017f8645376771bf7ec99a0f3d30490495fd77",
  "status": "BOUNDED_DIRECT_HASH_EQUALS_ALL_PRIOR_PROTECTED_FILE_DIGESTS_NO_SWAP_NO_GLOBAL_CACHE_DROP",
  "issue_id": "PERF-986",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "result.json"
  ]
}
```

## PERF987-bounded-hash-formal-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF987-bounded-hash-formal-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF987-bounded-hash-formal-p1-perf921-q2-local-users20k",
  "result_sha256": "61eb1a9906ddff948e493d3b4f554ab6cab9817d4b29e919417146ac2d0f75c5",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-987",
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
    "restore-non-MQ-infrastructure6-only.json",
    "result.json",
    "stop-remaining-infrastructure.json"
  ]
}
```

## perf987-p1-reference-users20k

```json
{
  "directory": "perf987-p1-reference-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf987-p1-reference-users20k",
  "result_sha256": "e6ff2f170da3a4f6737da77056b901619f283c799a0da7ddb3d3bd14ce4f26a5",
  "status": "FAILED",
  "issue_id": "PERF-987",
  "error": "AssertionError('Host6GiB gate failed; stop owned load')\nTraceback (most recent call last):\n  File \"<WORKSPACE>\\benchmark\\peer_compare\\run_bounded_hash_formal_point_r987.py\", line 246, in <module>\n    suite=run_rate_case(O,ssh,remote,remote_folder,case,samples)\n          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n  File \"<WORKSPACE>\\benchmark\\peer_compare\\formal_current_client_r987.py\", line 71, in run_rate_case\n    assert not host_samples or host_samples[-1]['available_bytes']>=6*2**30,'Host6GiB gate failed; stop owned load'\n           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\nAssertionError: Host6GiB gate failed; stop owned load\n\nCLOSED_AUDIT:AssertionError('closed-audit: Traceback (most recent call last):\\n  File \"<stdin>\", line 12, in <module>\\nAssertionError: (\\'40658\\', \\'0\\', \\'0\\', \\'0\\', \\'40658\\')\\n')\nDeclared resource gate failed",
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "barrier-contract.json",
    "checks.json",
    "client-command.json",
    "client-ports.json",
    "client-records.json",
    "client-vmware-memory.json",
    "clock-before.json",
    "closed-audit.json",
    "functional-resource-result.json",
    "grant.json",
    "headroom-grant.json",
    "host-preflight.json",
    "host-samples.json",
    "identity.json",
    "namespace.json",
    "namespace.json.intent.json",
    "namespace.json.routing.json",
    "resource-monitor-terminal.json",
    "resource-policy.json",
    "result.json",
    "runtime-result.json",
    "spec.json",
    "sql-work-before-offer.json"
  ]
}
```

## PERF989-bounded-preparation-checkpoint-r1

```json
{
  "directory": "PERF989-bounded-preparation-checkpoint-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF989-bounded-preparation-checkpoint-r1",
  "result_sha256": "6d3301ee4a880e41ca2b965eb4965992e80a9c35141787b6b07e008efc0a96eb",
  "status": "CLOSED_FULL9_SQL9_BUSINESS414_DIAGNOSTIC_CLEANUP_RAW_RESOURCE_ABORT_BOUNDED_HASH_FIX_AND_CURRENT_SOURCE_ACTUALLY_RESTORED",
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "continuation.json",
    "result.json"
  ]
}
```

## PERF990-unused-background-dependency-audit-r1

```json
{
  "directory": "PERF990-unused-background-dependency-audit-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF990-unused-background-dependency-audit-r1",
  "result_sha256": "aaf6a2e0756b3ac7f38156640cdb9a2694401695ed03e3b3e85d893d62462b28",
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "decision.json",
    "execution.json",
    "result.json"
  ]
}
```

## PERF991-lab4-formal-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF991-lab4-formal-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF991-lab4-formal-p1-perf921-q2-local-users20k",
  "result_sha256": "a4c6f4e7badddc726bc918f3fc4c70303337d7f7e10dd3cc656d860ad8c5c20a",
  "status": "COLD_INFRA4_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-991",
  "error": null,
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

## PERF991-lab4-formal-p2-perf921-q2-local-users20k

```json
{
  "directory": "PERF991-lab4-formal-p2-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF991-lab4-formal-p2-perf921-q2-local-users20k",
  "result_sha256": "95e3cabec00cff72b398c458a56ca84ceb62095517051e8212458989aefb237d",
  "status": "COLD_INFRA4_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-991",
  "error": null,
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

## perf991-p1-reference-users20k

```json
{
  "directory": "perf991-p1-reference-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf991-p1-reference-users20k",
  "result_sha256": "df5fffcabd5c75f881dfbecc55626f1eea7013fd6cf18062e4e07b44b6a4f780",
  "status": "COMPLETE60000_CLIENT_RESOURCE_AND_SERVER_CLOSURE_PASS_NOT_CAPACITY",
  "issue_id": "PERF-991",
  "error": null,
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
    "formal-window-analysis-r992.json",
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
    "sql-work-delta-r993.json",
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

## perf991-p2-reference-users20k

```json
{
  "directory": "perf991-p2-reference-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf991-p2-reference-users20k",
  "result_sha256": "a90fbfa4ed6ee30a7dac8edbdb9673e62be03b46fc2972353251cd85a2c7e0d0",
  "status": "COMPLETE60000_CLIENT_RESOURCE_AND_SERVER_CLOSURE_PASS_NOT_CAPACITY",
  "issue_id": "PERF-991",
  "error": null,
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
    "formal-window-analysis-r992.json",
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
    "sql-work-delta-r993.json",
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

## PERF994-current-identity-CMake-SQL-r1

```json
{
  "directory": "PERF994-current-identity-CMake-SQL-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF994-current-identity-CMake-SQL-r1",
  "result_sha256": "f3354cc72b43522d99ebee2fe93d3de30bfeabe26e57223efa1cd265e8b92fd9",
  "status": "FAILED",
  "issue_id": "PERF-994",
  "error": "AssertionError(('current-CMake-two-identity-contract-targets', '-- Found RE2 via CMake.\\n-- Configuring incomplete, errors occurred!\\nSee also \"/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/build/CMakeFiles/CMakeOutput.log\".\\nCMake Error at /home/jackson7/projects/TinyIMX_publish/toolchains/vcpkg-tinyimx/scripts/buildsystems/vcpkg.cmake:612 (_add_executable):\\n  _add_executable cannot create target \"immutable_ascii_identity_sql_tests\"\\n  because another target with the same name already exists.  The existing\\n  target is an executable created in source directory\\n  \"/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source\".  See\\n  documentation for policy CMP0002 for more details.\\nCall Stack (most recent call first):\\n  cmake/TinyIMXLocalStore.cmake:56 (add_executable)\\n  CMakeLists.txt:1581 (include)\\n\\n\\nCMake Error at /home/jackson7/projects/TinyIMX_publish/toolchains/vcpkg-tinyimx/scripts/buildsystems/vcpkg.cmake:612 (_add_executable):\\n  _add_executable cannot create target\\n  \"immutable_identity_native_fence_sql_tests\" because another target with the\\n  same name already exists.  The existing target is an executable created in\\n  source directory\\n  \"/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source\".  See\\n  documentation for policy CMP0002 for more details.\\nCall Stack (most recent call first):\\n  cmake/TinyIMXLocalStore.cmake:58 (add_executable)\\n  CMakeLists.txt:1581 (include)\\n\\n\\ngmake: *** [Makefile:15638：cmake_check_build_system] 错误 1\\n'))\nTraceback (most recent call last):\n  File \"<stdin>\", line 41, in <module>\n  File \"<stdin>\", line 21, in run\nAssertionError: ('current-CMake-two-identity-contract-targets', '-- Found RE2 via CMake.\\n-- Configuring incomplete, errors occurred!\\nSee also \"/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/build/CMakeFiles/CMakeOutput.log\".\\nCMake Error at /home/jackson7/projects/TinyIMX_publish/toolchains/vcpkg-tinyimx/scripts/buildsystems/vcpkg.cmake:612 (_add_executable):\\n  _add_executable cannot create target \"immutable_ascii_identity_sql_tests\"\\n  because another target with the same name already exists.  The existing\\n  target is an executable created in source directory\\n  \"/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source\".  See\\n  documentation for policy CMP0002 for more details.\\nCall Stack (most recent call first):\\n  cmake/TinyIMXLocalStore.cmake:56 (add_executable)\\n  CMakeLists.txt:1581 (include)\\n\\n\\nCMake Error at /home/jackson7/projects/TinyIMX_publish/toolchains/vcpkg-tinyimx/scripts/buildsystems/vcpkg.cmake:612 (_add_executable):\\n  _add_executable cannot create target\\n  \"immutable_identity_native_fence_sql_tests\" because another target with the\\n  same name already exists.  The existing target is an executable created in\\n  source directory\\n  \"/mnt/tinyimx-benchmark/complete-reference-cohort-perf943-r2/source\".  See\\n  documentation for policy CMP0002 for more details.\\nCall Stack (most recent call first):\\n  cmake/TinyIMXLocalStore.cmake:58 (add_executable)\\n  CMakeLists.txt:1581 (include)\\n\\n\\ngmake: *** [Makefile:15638：cmake_check_build_system] 错误 1\\n')\n",
  "performance_claim": null,
  "root_json_files": [
    "execution.json",
    "host-samples.json",
    "planned-source-manifest.json",
    "preflight-memory.json",
    "recovered-archive-proof.json",
    "resource-policy.json",
    "result.json"
  ]
}
```

## PERF994-current-identity-CMake-SQL-r2

```json
{
  "directory": "PERF994-current-identity-CMake-SQL-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF994-current-identity-CMake-SQL-r2",
  "result_sha256": "02e54f853ccac98490a55b9dd01a13713974725409d4971c0eda49264e1ffd57",
  "status": "CURRENT_CMAKE_REAL_SQL283086_EQUIVALENCE_NATIVE_OWNER_FENCE_CID_RECOVERY_AND_QUIESCED_PARENT_LIFECYCLE_PASS",
  "issue_id": "PERF-994",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "archive-proof.json",
    "artifact-manifest.json",
    "checks.json",
    "current-nine-products-unchanged.json",
    "execution.json",
    "functional-resource-result.json",
    "host-samples.json",
    "planned-source-manifest.json",
    "preflight-memory.json",
    "resource-policy.json",
    "result.json",
    "SQL-counter-reconciliation.json"
  ]
}
```

## PERF994-current-identity-functional-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF994-current-identity-functional-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF994-current-identity-functional-p1-perf921-q2-local-users20k",
  "result_sha256": "be1dfdd6fe73676646a7f473f468d1c2b8e118afb31a171cd3c8f3a950ced729",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-994",
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

## PERF994-current-identity-functional-p1-perf921-q2-local-users20k-r2

```json
{
  "directory": "PERF994-current-identity-functional-p1-perf921-q2-local-users20k-r2",
  "relative_path": "benchmark/results/peer-20261017/PERF994-current-identity-functional-p1-perf921-q2-local-users20k-r2",
  "result_sha256": "61d2290177b47dc4c1d9f6488fb1ad67a1303d8341cabd6c225342e96d7a4231",
  "status": "COLD_INFRA4_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-994",
  "error": null,
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

## PERF995-reference-actual-load-decision-r1

```json
{
  "directory": "PERF995-reference-actual-load-decision-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF995-reference-actual-load-decision-r1",
  "result_sha256": "c01b5d6f2f4f9f0ab6618011d7d5f90da43e5650b61182b224e6fcbfcabea0ea",
  "status": "REJECT_REFERENCE_BATCHING_FOR_CURRENT_FORMAL_LOAD_KEEP_DEFAULT_OFF_ADVANCE_IMMUTABLE_IDENTITY_SQL_FACTOR",
  "issue_id": "PERF-995",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF996-current-full-business-b-r1

```json
{
  "directory": "PERF996-current-full-business-b-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF996-current-full-business-b-r1",
  "result_sha256": "824a36ce4c3576ac979657d0dee137afbb367b3097385e3bf7dc21609e854aff",
  "status": "COMPLETE_CURRENT9_PLUS_DESKTOP_REAL_TCP_SQL_MQ_CACHE_GROUP_FILE_AUTHORITY_BUSINESS_PASS",
  "issue_id": "PERF-996",
  "error": null,
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "checks.json",
    "client-records.json",
    "closed-audit.json",
    "functional-resource-result.json",
    "grant.json",
    "headroom-grant.json",
    "host-preflight.json",
    "host-samples.json",
    "http-status-summary.json",
    "identity.json",
    "namespace.json",
    "namespace.json.intent.json",
    "namespace.json.routing.json",
    "owned-part-corruption.json",
    "permission-available.json",
    "permission-block_a.json",
    "permission-block_b.json",
    "permission-remove_a.json",
    "permission-restore.json",
    "permission-snapshot.json",
    "permission-unavailable.json",
    "permission-unblock.json",
    "read-projection-converged.json",
    "resource-policy.json",
    "result.json",
    "route-census.json",
    "runtime-result.json",
    "suite.json"
  ]
}
```

## PERF996-current-identity-business-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF996-current-identity-business-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF996-current-identity-business-p1-perf921-q2-local-users20k",
  "result_sha256": "bd1d5504a23dee521599c596365ab53c4b4abe90b93d82621dfe400be518f4d0",
  "status": "COLD_INFRA4_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-996",
  "error": null,
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

## PERF997-current-identity-admission-r1

```json
{
  "directory": "PERF997-current-identity-admission-r1",
  "relative_path": "benchmark/results/peer-20261017/PERF997-current-identity-admission-r1",
  "result_sha256": "a1577ed7e324660639861eb7ff7c3335c2c173a631431eeb0e5c2a6f7fd92872",
  "status": "CURRENT9_REAL_SQL283086_AUTHORITY_FENCE_RECOVERY_AND207_BUSINESS_PASS_ADMIT_IDENTITY_ONLY_FORMAL",
  "issue_id": "PERF-997",
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "result.json"
  ]
}
```

## PERF998-current-identity-formal-p1-perf921-q2-local-users20k

```json
{
  "directory": "PERF998-current-identity-formal-p1-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF998-current-identity-formal-p1-perf921-q2-local-users20k",
  "result_sha256": "c8161522d1f2d2ec14a927cda4c199c12f5093f84eb67dcabc517227e6bd0ac4",
  "status": "COLD_INFRA4_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-998",
  "error": null,
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

## PERF998-current-identity-formal-p2-perf921-q2-local-users20k

```json
{
  "directory": "PERF998-current-identity-formal-p2-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF998-current-identity-formal-p2-perf921-q2-local-users20k",
  "result_sha256": "77b61dacd6e6149002be397fa626f8fd24819d32c00c1dbf3a6d55647748fd69",
  "status": "COLD_INFRA4_SQL_QUALIFICATION_ORIGINAL20_ALL_RETAINED_PROTECTED_PASS",
  "issue_id": "PERF-998",
  "error": null,
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

## PERF998-current-identity-formal-p3-perf921-q2-local-users20k

```json
{
  "directory": "PERF998-current-identity-formal-p3-perf921-q2-local-users20k",
  "relative_path": "benchmark/results/peer-20261017/PERF998-current-identity-formal-p3-perf921-q2-local-users20k",
  "result_sha256": "21a2077f4bf89ef9d7faabf49f2848be398ef2670a1bbfa61db52643ef320908",
  "status": "INCOMPLETE_RECOVER_ORIGINAL20",
  "issue_id": "PERF-998",
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

## perf998-p1-identity-users20k

```json
{
  "directory": "perf998-p1-identity-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf998-p1-identity-users20k",
  "result_sha256": "9d2fd92b3a19ee532eb9b8a4f1943a64f2537500d4d8f010e482b93d8d193b7c",
  "status": "COMPLETE60000_CLIENT_RESOURCE_AND_SERVER_CLOSURE_PASS_NOT_CAPACITY",
  "issue_id": "PERF-998",
  "error": null,
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
    "formal-window-analysis-r999.json",
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
    "sql-work-delta-r999.json",
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

## perf998-p2-identity-users20k

```json
{
  "directory": "perf998-p2-identity-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf998-p2-identity-users20k",
  "result_sha256": "2ac2e6d8b479599896afd97ab740abae0fd0cbbe4e5df6cc98fbea788a349070",
  "status": "COMPLETE60000_CLIENT_RESOURCE_AND_SERVER_CLOSURE_PASS_NOT_CAPACITY",
  "issue_id": "PERF-998",
  "error": null,
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
    "formal-window-analysis-r999.json",
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
    "sql-work-delta-r999.json",
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

## perf998-p3-identity-users20k

```json
{
  "directory": "perf998-p3-identity-users20k",
  "relative_path": "benchmark/results/peer-20261017/perf998-p3-identity-users20k",
  "result_sha256": "286a385a7b3745f77906b7546576de77ef7fe0133138ca4553814a18abacb5d9",
  "status": "FAILED",
  "issue_id": "PERF-998",
  "error": "RuntimeError('Current full graph did not become ready; no rate test')\nTraceback (most recent call last):\n  File \"<WORKSPACE>\\benchmark\\peer_compare\\run_current_identity_formal_point_r998.py\", line 231, in <module>\n    if server.poll()is not None or time.monotonic()>deadline:raise RuntimeError('Current full graph did not become ready; no rate test')\n                                                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\nRuntimeError: Current full graph did not become ready; no rate test\n\nCLOSED_AUDIT:AssertionError('closed-audit: Traceback (most recent call last):\\n  File \"<stdin>\", line 7, in <module>\\n  File \"/usr/lib/python3.10/pathlib.py\", line 1126, in read_bytes\\n    with self.open(mode=\\'rb\\') as f:\\n  File \"/usr/lib/python3.10/pathlib.py\", line 1119, in open\\n    return self._accessor.open(self, mode, buffering, encoding, errors,\\nFileNotFoundError: [Errno 2] No such file or directory: \\'/mnt/tinyimx-benchmark/current-formal-trials-perf998/perf998-p3-identity-users20k/private-runtime.json\\'\\n')",
  "performance_claim": false,
  "root_json_files": [
    "archive-proof.json",
    "checks.json",
    "client-records.json",
    "closed-audit.json",
    "functional-resource-result.json",
    "host-samples.json",
    "resource-policy.json",
    "result.json",
    "runtime-result.json"
  ]
}
```

## wukong-10k100-r1

```json
{
  "directory": "wukong-10k100-r1",
  "relative_path": "benchmark/results/peer-20261017/wukong-10k100-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "audit.json",
    "client-private.json",
    "client-process.json",
    "history-reconciliation.json",
    "server-audit.json",
    "server-resources.json",
    "server-restoration.json",
    "tcp-after.json",
    "tcp-before.json",
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
    "duplicate_deliveries": 0
  }
}
```

## wukong-capacity-10k1000-r7

```json
{
  "directory": "wukong-capacity-10k1000-r7",
  "relative_path": "benchmark/results/peer-20261017/wukong-capacity-10k1000-r7",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "audit.json",
    "client-private.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "history-reconciliation.json",
    "restoration.json",
    "server-audit.json",
    "server-resources.json",
    "tcp-after.json",
    "tcp-authentication.json",
    "tcp-before.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 10000,
    "rate": 1000,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 60000,
    "sent": 60000,
    "completed_valid": 60000,
    "duplicate_deliveries": 0
  }
}
```

## wukong-capacity-10k700-r10

```json
{
  "directory": "wukong-capacity-10k700-r10",
  "relative_path": "benchmark/results/peer-20261017/wukong-capacity-10k700-r10",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "audit.json",
    "client-private.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "cpu-modes-r47.json",
    "evidence-collection.json",
    "history-reconciliation.json",
    "memory-window-r42.json",
    "restoration.json",
    "server-audit.json",
    "server-resources.json",
    "tcp-after.json",
    "tcp-authentication.json",
    "tcp-before.json",
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
    "duplicate_deliveries": 0
  }
}
```

## wukong-capacity-10k700-r12

```json
{
  "directory": "wukong-capacity-10k700-r12",
  "relative_path": "benchmark/results/peer-20261017/wukong-capacity-10k700-r12",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "audit.json",
    "client-private.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "history-reconciliation.json",
    "memory-window-r42.json",
    "restoration.json",
    "server-audit.json",
    "server-resources.json",
    "tcp-after.json",
    "tcp-authentication.json",
    "tcp-before.json",
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
    "duplicate_deliveries": 0
  }
}
```

## wukong-capacity-10k700-r13

```json
{
  "directory": "wukong-capacity-10k700-r13",
  "relative_path": "benchmark/results/peer-20261017/wukong-capacity-10k700-r13",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "audit.json",
    "client-private.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "history-reconciliation.json",
    "memory-window-r42.json",
    "restoration.json",
    "server-audit.json",
    "server-resources.json",
    "tcp-after.json",
    "tcp-authentication.json",
    "tcp-before.json",
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
    "duplicate_deliveries": 0
  }
}
```

## wukong-capacity-10k700-r14

```json
{
  "directory": "wukong-capacity-10k700-r14",
  "relative_path": "benchmark/results/peer-20261017/wukong-capacity-10k700-r14",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "audit.json",
    "client-private.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "history-reconciliation.json",
    "memory-window-r42.json",
    "restoration.json",
    "server-audit.json",
    "server-resources.json",
    "tcp-after.json",
    "tcp-authentication.json",
    "tcp-before.json",
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
    "duplicate_deliveries": 0
  }
}
```

## wukong-capacity-10k700-r15

```json
{
  "directory": "wukong-capacity-10k700-r15",
  "relative_path": "benchmark/results/peer-20261017/wukong-capacity-10k700-r15",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "audit.json",
    "client-private.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "history-reconciliation.json",
    "memory-window-r42.json",
    "restoration.json",
    "server-audit.json",
    "server-resources.json",
    "tcp-after.json",
    "tcp-authentication.json",
    "tcp-before.json",
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
    "duplicate_deliveries": 0
  }
}
```

## wukong-capacity-10k700-r8

```json
{
  "directory": "wukong-capacity-10k700-r8",
  "relative_path": "benchmark/results/peer-20261017/wukong-capacity-10k700-r8",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "audit.json",
    "client-private.json",
    "client-process.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "evidence-collection.json",
    "history-reconciliation.json",
    "memory-window-r42.json",
    "restoration.json",
    "server-audit.json",
    "server-resources.json",
    "tcp-after.json",
    "tcp-authentication.json",
    "tcp-before.json",
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
    "duplicate_deliveries": 0
  }
}
```

## wukong-capacity-20k3000-r77-r1

```json
{
  "directory": "wukong-capacity-20k3000-r77-r1",
  "relative_path": "benchmark/results/peer-20261017/wukong-capacity-20k3000-r77-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "client-exit.json",
    "client-ports.json",
    "client-private.json",
    "client-process.json",
    "clock-before-auth.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "history-execution.json",
    "history-reconciliation.json",
    "host-preflight.json",
    "predeclared-warm-steady-assessment-r738.json",
    "protected-after.json",
    "protected-before.json",
    "restoration-control.json",
    "spec.json",
    "tcp-after.json",
    "tcp-before.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 20000,
    "rate": 3000,
    "seconds": 80,
    "payload_bytes": 128,
    "planned": 240000,
    "sent": 240000,
    "completed_valid": 240000,
    "duplicate_deliveries": 0
  }
}
```

## wukong-capacity-20k4000-r76-r1

```json
{
  "directory": "wukong-capacity-20k4000-r76-r1",
  "relative_path": "benchmark/results/peer-20261017/wukong-capacity-20k4000-r76-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "client-exit.json",
    "client-ports.json",
    "client-private.json",
    "client-process.json",
    "clock-before-auth.json",
    "clock-bridge-after.json",
    "clock-bridge-before.json",
    "history-execution.json",
    "history-reconciliation.json",
    "host-preflight.json",
    "predeclared-warm-steady-assessment-r703.json",
    "protected-after.json",
    "protected-before.json",
    "restoration-control.json",
    "spec.json",
    "tcp-after.json",
    "tcp-before.json",
    "windows-host.json"
  ],
  "client": {
    "status": "PASS",
    "connections": 20000,
    "rate": 4000,
    "seconds": 60,
    "payload_bytes": 128,
    "planned": 240000,
    "sent": 240000,
    "completed_valid": 240000,
    "duplicate_deliveries": 0
  }
}
```

## wukong-clean-10k500-r3

```json
{
  "directory": "wukong-clean-10k500-r3",
  "relative_path": "benchmark/results/peer-20261017/wukong-clean-10k500-r3",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "audit.json",
    "client-private.json",
    "client-process.json",
    "history-reconciliation.json",
    "restoration.json",
    "server-audit.json",
    "server-resources.json",
    "tcp-after.json",
    "tcp-before.json",
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
    "duplicate_deliveries": 0
  }
}
```

## wukong-common2-20k1000-r1212-r1

```json
{
  "directory": "wukong-common2-20k1000-r1212-r1",
  "relative_path": "benchmark/results/peer-20261017/wukong-common2-20k1000-r1212-r1",
  "result_sha256": null,
  "status": null,
  "issue_id": null,
  "error": null,
  "performance_claim": null,
  "root_json_files": [
    "audit.json",
    "client-exit.json",
    "client-ports.json",
    "client-private.json",
    "client-process.json",
    "clock-before-auth.json",
    "clock-bridge-after.json",
    "closed-artifact-collection.json",
    "disk-stages.json",
    "formal-window-analysis-r1213.json",
    "history-execution.json",
    "history-reconciliation.json",
    "host-preflight.json",
    "resources.json",
    "restoration-control.json",
    "restoration.json",
    "seed-proof.json",
    "spec.json",
    "tcp-after.json",
    "tcp-before.json",
    "windows-host.json"
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
    "duplicate_deliveries": 0,
    "client_revision": "R819_KERNEL_TCP_BUFFERS_BOUNDED_JOURNAL"
  }
}
```
