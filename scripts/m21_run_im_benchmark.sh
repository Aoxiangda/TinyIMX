#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT_DIR"
MODE="${1:-hold}"; CONNECTIONS="${2:-10}"; DURATION="${3:-20}"; RATE="${4:-10}"; BASE="${5:-${M21_BENCH_USER_BASE:-200000}}"
BUILD_DIR="${M21_BUILD_DIR:-$ROOT_DIR/build/linux-release}"; PREFIX="${M21_BENCH_USERNAME_PREFIX:-m21b${BASE}_}"; HOST="${M21_BENCH_HOST:-127.0.0.1}"; PORT="${M21_BENCH_PORT:-9000}"; PEER_MODE="${M21_BENCH_PEER_MODE:-ring}"; HOT_INDEX="${M21_BENCH_HOTSPOT_USER_INDEX:-0}"; RAMP="${M21_BENCH_RAMP_PER_SEC:-100}"; PAYLOAD="${M21_BENCH_PAYLOAD_BYTES:-128}"; HEARTBEAT="${M21_BENCH_HEARTBEAT_SECONDS:-30}"; DRAIN="${M21_BENCH_DRAIN_SECONDS:-5}"; MAX_OUT="${M21_BENCH_MAX_OUTSTANDING:-1}"; STRICT="${M21_BENCH_STRICT:-1}"; MIN_SUCCESS="${M21_BENCH_MIN_SUCCESS_RATE:-99.0}"; MAX_P99="${M21_BENCH_MAX_P99_MS:-5000}"; EXPECT_GROUP="${M21_EXPECT_MIN_GROUP_DELIVERIES:-0}"
STAMP="$(date +%Y%m%d-%H%M%S)"; ART="${M21_BENCH_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m21-benchmark-${MODE}-${CONNECTIONS}-${STAMP}}"; mkdir -p "$ART"
(( $(ulimit -Hn) >= 65535 )) || { echo "hard nofile below 65535" >&2; exit 1; }; ulimit -Sn 65535
for C in tinyimx-m21-nginx-1 tinyimx-m21-gateway-a-1 tinyimx-m21-gateway-b-1; do H="$(docker inspect -f '{{.State.Health.Status}}' "$C" 2>/dev/null||true)"; [[ "$H" == healthy ]]||{ echo "$C health=$H" >&2;exit 1;}; done
if [[ "${M21_BENCH_SKIP_FIXTURE:-0}" != 1 ]]; then M21_BENCH_USER_BASE="$BASE" M21_BENCH_USERNAME_PREFIX="$PREFIX" "$ROOT_DIR/scripts/m21_prepare_benchmark_users.sh" "$CONNECTIONS"|tee "$ART/fixture.log"; fi
cmake --build "$BUILD_DIR" --target tinyimx_im_loadgen -- -j1|tee "$ART/build.log"
SAMPLER_PID=""
cleanup() {
  if [[ -n "${SAMPLER_PID:-}" ]]; then
    kill "$SAMPLER_PID" 2>/dev/null || true
    wait "$SAMPLER_PID" 2>/dev/null || true
    SAMPLER_PID=""
  fi

  return 0
}
trap cleanup EXIT INT TERM
"$ROOT_DIR/scripts/m21_resource_sampler.sh" "$ART/resources.log" 1 & SAMPLER_PID=$!
set +e
timeout "$((DURATION+DRAIN+240))s" "$BUILD_DIR/tinyimx_im_loadgen" --host "$HOST" --port "$PORT" --connections "$CONNECTIONS" --user-id-base "$BASE" --username-prefix "$PREFIX" --password 123456 --mode "$MODE" --peer-mode "$PEER_MODE" --hotspot-user-index "$HOT_INDEX" --duration "$DURATION" --rate "$RATE" --ramp-per-sec "$RAMP" --payload-bytes "$PAYLOAD" --heartbeat-seconds "$HEARTBEAT" --drain-seconds "$DRAIN" --max-outstanding "$MAX_OUT" 2>&1|tee "$ART/loadgen.log"
RC=${PIPESTATUS[0]}; set -e; cleanup; SAMPLER_PID=""; [[ "$RC" == 0 ]]||exit "$RC"
LINE="$(grep '^TINYIMX_LOADGEN_RESULT ' "$ART/loadgen.log"|tail -n1)"; [[ -n "$LINE" ]]||{ echo "result missing" >&2;exit 1;}; printf '%s\n' "${LINE#TINYIMX_LOADGEN_RESULT }" > "$ART/result.json"
python3 - "$ART/result.json" "$MODE" "$CONNECTIONS" "$STRICT" "$MIN_SUCCESS" "$MAX_P99" "$EXPECT_GROUP" <<'PY'
import json,sys
p,mode,n,strict,mins,maxp99,expect=sys.argv[1:];n=int(n);strict=int(strict);mins=float(mins);maxp99=float(maxp99);expect=int(expect);x=json.load(open(p));issues=[]
if x.get('setup_failed'):issues.append('setup_failed')
for k,want in [('connected',n),('login_ok',n)]:
    if int(x.get(k,0))!=want:issues.append(f'{k}={x.get(k)} expected={want}')
for k in ('login_fail','protocol_errors','disconnects'):
    if int(x.get(k,0))!=0:issues.append(f'{k}={x.get(k)}')
if mode=='private':
    if int(x.get('send_attempts',0))<=0 or int(x.get('chat_ack_ok',0))<=0:issues.append('no successful private traffic')
    if float(x.get('success_rate',0))<mins:issues.append(f"success_rate={x.get('success_rate')}<{mins}")
    if float(x.get('p99_ms',0))>maxp99:issues.append(f"p99_ms={x.get('p99_ms')}>{maxp99}")
if int(x.get('group_delivery',0))<expect:issues.append(f"group_delivery={x.get('group_delivery')}<{expect}")
if int(x.get('group_ack_sent',0))<expect:issues.append(f"group_ack_sent={x.get('group_ack_sent')}<{expect}")
if issues:
    print('M21_IM_BENCHMARK_ISSUES='+';'.join(issues))
    if strict:raise SystemExit(1)
    print('M21_IM_BENCHMARK_GATE=MEASURED_WITH_ISSUES')
else: print('M21_IM_BENCHMARK_GATE=PASS')
PY
echo "M21_BENCH_ARTIFACT_DIR=$ART"
