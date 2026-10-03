#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT_DIR"
STAMP="$(date +%Y%m%d-%H%M%S)"; ART="${M21_MATRIX_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m21-local-perf-matrix-$STAMP}"; mkdir -p "$ART"
run_case(){ local name="$1" mode="$2" conns="$3" dur="$4" rate="$5" base="$6" peer="$7" strict="$8"; echo "========== $name =========="; M21_BENCH_ARTIFACT_DIR="$ART/$name" M21_BENCH_PEER_MODE="$peer" M21_BENCH_STRICT="$strict" M21_BENCH_MIN_SUCCESS_RATE="${M21_BENCH_MIN_SUCCESS_RATE:-99.0}" "$ROOT_DIR/scripts/m21_run_im_benchmark.sh" "$mode" "$conns" "$dur" "$rate" "$base"|tee "$ART/$name.runner.log"; }
M21_BENCH_MIN_SUCCESS_RATE=100 run_case smoke-hold hold 10 15 1 210000 ring 1
M21_BENCH_MIN_SUCCESS_RATE=100 run_case smoke-private private 10 20 10 220000 ring 1
run_case C100 hold 100 60 1 230000 ring 0
run_case C500 hold 500 60 1 240000 ring 0
run_case C1000 hold 1000 120 1 250000 ring 0
run_case M100 private 100 60 "${M21_RATE_100:-100}" 260000 ring 0
run_case M500 private 500 60 "${M21_RATE_500:-500}" 270000 ring 0
run_case M1000 private 1000 120 "${M21_RATE_1000:-1000}" 280000 ring 0
run_case HOT100 private 100 60 "${M21_HOT_RATE_100:-100}" 290000 hotspot 0
run_case HOT500 private 500 60 "${M21_HOT_RATE_500:-500}" 300000 hotspot 0
python3 - "$ART" <<'PY'
import csv,json,sys
from pathlib import Path
root=Path(sys.argv[1]);rows=[]
for d in sorted(root.iterdir()):
 p=d/'result.json'
 if not p.exists():continue
 x=json.loads(p.read_text())
 rows.append({k:x.get(k) for k in ['mode','peer_mode','connections','connected','login_ok','send_attempts','chat_ack_ok','receiver_delivery','receiver_ack_sent','success_rate','throughput_msg_s','p50_ms','p95_ms','p99_ms','protocol_errors','disconnects','overload_rejections']}|{'case':d.name})
fields=['case','mode','peer_mode','connections','connected','login_ok','send_attempts','chat_ack_ok','receiver_delivery','receiver_ack_sent','success_rate','throughput_msg_s','p50_ms','p95_ms','p99_ms','protocol_errors','disconnects','overload_rejections']
with (root/'summary.csv').open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
print(f'M21_LOCAL_PERF_MATRIX_CASES={len(rows)}')
PY
echo "M21_LOCAL_PERF_MATRIX=COMPLETE"
echo "M21_MATRIX_ARTIFACT_DIR=$ART"
