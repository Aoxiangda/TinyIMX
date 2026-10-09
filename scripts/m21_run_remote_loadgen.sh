#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)";cd "$ROOT_DIR"
HOST="${1:?SUT host required}";PORT="${2:-9000}";MODE="${3:-hold}";CONNS="${4:-10000}";DUR="${5:-300}";RATE="${6:-1000}";BASE="${7:-500000}"
BUILD="${M21_BUILD_DIR:-$ROOT_DIR/build/linux-release}";PREFIX="${M21_BENCH_USERNAME_PREFIX:-m21b${BASE}_}";STAMP="$(date +%Y%m%d-%H%M%S)";ART="${M21_REMOTE_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m21-remote-${MODE}-${CONNS}-$STAMP}";mkdir -p "$ART"
(( $(ulimit -Hn) >= 65535 ))||{ echo "loadgen hard nofile below 65535" >&2;exit 1;};ulimit -Sn 65535
cmake --build "$BUILD" --target tinyimx_im_loadgen -- -j1|tee "$ART/build.log"
set +e
timeout "$((DUR+300))s" "$BUILD/tinyimx_im_loadgen" --host "$HOST" --port "$PORT" --connections "$CONNS" --user-id-base "$BASE" --username-prefix "$PREFIX" --password 123456 --mode "$MODE" --peer-mode "${M21_BENCH_PEER_MODE:-ring}" --duration "$DUR" --rate "$RATE" --ramp-per-sec "${M21_BENCH_RAMP_PER_SEC:-500}" --payload-bytes "${M21_BENCH_PAYLOAD_BYTES:-128}" --heartbeat-seconds 30 --drain-seconds 10 --max-outstanding "${M21_BENCH_MAX_OUTSTANDING:-1}" 2>&1|tee "$ART/loadgen.log"
RC=${PIPESTATUS[0]};set -e;[[ "$RC" == 0 ]]||exit "$RC"
LINE="$(grep '^TINYIMX_LOADGEN_RESULT ' "$ART/loadgen.log"|tail -1)";[[ -n "$LINE" ]];printf '%s\n' "${LINE#TINYIMX_LOADGEN_RESULT }" > "$ART/result.json"
python3 - "$ART/result.json" "$CONNS" <<'PY'
import json,sys
x=json.load(open(sys.argv[1]));n=int(sys.argv[2]);bad=[]
if int(x.get('connected',0))!=n or int(x.get('login_ok',0))!=n:bad.append('connection/login incomplete')
if int(x.get('protocol_errors',0)) or int(x.get('disconnects',0)):bad.append('protocol/disconnect errors')
if bad:raise SystemExit(';'.join(bad))
print('M21_REMOTE_LOADGEN_GATE=PASS')
print('M21_REMOTE_LOADGEN_METRICS throughput_msg_s=%s success_rate=%s p50_ms=%s p95_ms=%s p99_ms=%s'%(x.get('throughput_msg_s'),x.get('success_rate'),x.get('p50_ms'),x.get('p95_ms'),x.get('p99_ms')))
PY
echo "M21_REMOTE_ARTIFACT_DIR=$ART"
