#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT_DIR"
CONNS="${1:-500}"; DUR="${2:-60}"; RATE="${3:-500}"; BASE="${4:-350000}"; STAMP="$(date +%Y%m%d-%H%M%S)"; ART="${M21_HOT_USER_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m21-hot-user-$STAMP}"
M21_BENCH_ARTIFACT_DIR="$ART" M21_BENCH_PEER_MODE=hotspot M21_BENCH_HOTSPOT_USER_INDEX=0 M21_BENCH_STRICT=0 "$ROOT_DIR/scripts/m21_run_im_benchmark.sh" private "$CONNS" "$DUR" "$RATE" "$BASE"|tee "$ART.runner.log"
python3 - "$ART/result.json" <<'PY'
import json,sys
x=json.load(open(sys.argv[1]));print('M21_HOT_USER_RESULT connections=%s throughput_msg_s=%s success_rate=%s p95_ms=%s p99_ms=%s overload_rejections=%s'%(x['connections'],x['throughput_msg_s'],x['success_rate'],x['p95_ms'],x['p99_ms'],x['overload_rejections']))
PY
echo "M21_HOT_USER_BENCHMARK=MEASURED"
