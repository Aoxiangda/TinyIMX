#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT_DIR"
CONNS="${1:-1000}"; DUR="${2:-1800}"; RATE="${3:-100}"; BASE="${4:-380000}"; STAMP="$(date +%Y%m%d-%H%M%S)"; ART="${M21_SOAK_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m21-soak-$STAMP}"; mkdir -p "$ART"
C=(tinyimx-m21-nginx-1 tinyimx-m21-gateway-a-1 tinyimx-m21-gateway-b-1 tinyimx-m21-message-service-1 tinyimx-m21-mysql-1 tinyimx-m21-redis-1 tinyimx-m21-rocketmq-broker-1 tinyimx-m21-rocketmq-proxy-1)
: > "$ART/restarts-before.tsv";for c in "${C[@]}";do printf '%s\t%s\n' "$c" "$(docker inspect -f '{{.RestartCount}}' "$c")" >> "$ART/restarts-before.tsv";done
M21_BENCH_ARTIFACT_DIR="$ART/benchmark" M21_BENCH_STRICT=1 M21_BENCH_MIN_SUCCESS_RATE="${M21_SOAK_MIN_SUCCESS_RATE:-99}" "$ROOT_DIR/scripts/m21_run_im_benchmark.sh" private "$CONNS" "$DUR" "$RATE" "$BASE"|tee "$ART/runner.log"
: > "$ART/restarts-after.tsv";for c in "${C[@]}";do printf '%s\t%s\n' "$c" "$(docker inspect -f '{{.RestartCount}}' "$c")" >> "$ART/restarts-after.tsv";done
diff -u "$ART/restarts-before.tsv" "$ART/restarts-after.tsv" > "$ART/restart-diff.txt"||{ cat "$ART/restart-diff.txt";exit 1; }
python3 - "$ART/benchmark/result.json" <<'PY'
import json,sys
x=json.load(open(sys.argv[1]));bad={k:x.get(k) for k in ['protocol_errors','disconnects','inflight_at_end'] if int(x.get(k,0))!=0}
if bad:raise SystemExit(str(bad))
print('M21_SOAK_RESULT_VALIDATED=PASS')
PY
echo "M21_SOAK_GATE=PASS";echo "M21_SOAK_ARTIFACT_DIR=$ART"
