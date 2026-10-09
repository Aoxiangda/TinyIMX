#!/usr/bin/env bash
set -Eeuo pipefail

MODE="${1:-connections}"
TARGET="${TINYIMX_TARGET_HOST:-192.168.58.129}"
PORT="${TINYIMX_TARGET_PORT:-9000}"
BIN="${TINYIMX_LOADGEN_BIN:-./tinyimx_im_loadgen}"
OUT="${TINYIMX_LOADGEN_OUT:-$PWD/tinyimx-final-loadgen-$(date +%Y%m%d-%H%M%S)}"
BASE="${TINYIMX_BENCH_USER_BASE:-500000}"
PREFIX="${TINYIMX_BENCH_USERNAME_PREFIX:-m21b500000_}"
PASSWORD="${TINYIMX_BENCH_PASSWORD:-123456}"
mkdir -p "$OUT"

[[ -x "$BIN" ]] || { echo "FIRST_FAILURE=LOADGEN_BIN path=$BIN"; exit 10; }
HARD="$(ulimit -Hn)"
[[ "$HARD" == unlimited || "$HARD" -ge 65536 ]] || { echo "FIRST_FAILURE=LOADGEN_HARD_NOFILE value=$HARD"; exit 11; }
ulimit -Sn 524288 2>/dev/null || ulimit -Sn "$HARD"
echo "loadgen_soft_nofile=$(ulimit -Sn)"

run_case() {
  local name="$1" connections="$2" ramp="$3" duration="$4" app_mode="$5" rate="$6" gate="${7:-auto}"
  local log="$OUT/${name}.log"
  echo "========== $name =========="
  date --iso-8601=seconds
  set +e
  "$BIN" \
    --host "$TARGET" --port "$PORT" \
    --connections "$connections" \
    --user-id-base "$BASE" --username-prefix "$PREFIX" --password "$PASSWORD" \
    --mode "$app_mode" --peer-mode ring \
    --duration "$duration" --rate "$rate" --ramp-per-sec "$ramp" \
    --payload-bytes 128 --heartbeat-seconds 30 --drain-seconds 10 --max-outstanding 1 \
    2>&1 | tee "$log"
  local rc=${PIPESTATUS[0]}
  set -e
  if [[ "$rc" -ne 0 && "$rc" -ne 2 ]]; then
    echo "FIRST_FAILURE=LOADGEN_RUNTIME case=$name rc=$rc"
    exit "$rc"
  fi
  local result
  result="$(grep '^TINYIMX_LOADGEN_RESULT ' "$log" | tail -1 | sed 's/^TINYIMX_LOADGEN_RESULT //')"
  [[ -n "$result" ]] || { echo "FIRST_FAILURE=RESULT_MISSING case=$name"; exit 20; }
  printf '%s\n' "$result" >> "$OUT/results.jsonl"
  python3 - "$name" "$connections" "$app_mode" "$rate" "$gate" "$result" <<'PY'
import json,sys,os
name=sys.argv[1]; n=int(sys.argv[2]); mode=sys.argv[3]; rate=float(sys.argv[4]); gate=sys.argv[5]; r=json.loads(sys.argv[6])
base=(not r['setup_failed'] and r['connected']==n and r['login_ok']==n and r['login_fail']==0 and r.get('login_unresolved_fail',0)==0 and r['disconnects']==0 and r['protocol_errors']==0 and r['server_errors']==0)
hb_sent=int(r.get('heartbeat_sent',0)); hb_ack=int(r.get('heartbeat_ack',0))
hb_ratio=1.0 if hb_sent==0 else hb_ack/hb_sent
if gate in ('connection','soak'):
    passed=base and hb_ratio>=0.9999
elif gate=='auth':
    passed=(base and r.get('login_response_fail',0)==0 and r.get('deadline_rejections',0)==0)
elif gate=='message' or mode!='hold':
    tput=float(r['throughput_msg_s']); success=float(r['success_rate'])
    p99=float(r.get('p99_ms',0.0)); p99_budget=float(os.environ.get('TINYIMX_MESSAGE_P99_BUDGET_MS','100'))
    passed=(base and r['chat_ack_fail']==0 and r['overload_rejections']==0 and success>=99.9 and tput>=rate*0.95 and p99<=p99_budget)
else:
    passed=base
print(f"CASE={name} PASS={int(passed)} connected={r['connected']} login_ok={r['login_ok']} login_fail={r['login_fail']} unresolved={r.get('login_unresolved_fail',0)} deadline={r.get('deadline_rejections',0)} setup_ms={r['setup_ms']} login_p99_ms={r.get('login_p99_ms',0)} heartbeat={hb_ack}/{hb_sent} hb_ratio={hb_ratio:.6f} throughput={r['throughput_msg_s']} p99_ms={r['p99_ms']}")
sys.exit(0 if passed else 42)
PY
}

case "$MODE" in
  connections)
    cases=(1000 5000 10000)
    [[ "${TINYIMX_FINAL_ENABLE_20K:-0}" == 1 ]] && cases+=(20000)
    for n in "${cases[@]}"; do
      if ! run_case "conn-${n}" "$n" 100 60 hold 1 connection; then
        echo "FIRST_FAILURE=CONNECTION_CAPACITY connections=$n"
        break
      fi
      sleep 15
    done
    ;;
  auth)
    for rate in ${TINYIMX_AUTH_RATES:-100 125 150 175 200 250 350 500}; do
      if ! run_case "auth-${rate}" "${TINYIMX_AUTH_CONNECTIONS:-1000}" "$rate" "${TINYIMX_AUTH_DURATION_SECONDS:-20}" hold 1 auth; then
        echo "FIRST_FAILURE=AUTH_KNEE rate=$rate"
        break
      fi
      sleep 15
    done
    ;;
  message)
    for rate in ${TINYIMX_MESSAGE_RATES:-250 300 325 350 375 400 425 450 475 500 600}; do
      if ! run_case "private-${rate}" 1000 100 60 private "$rate" message; then
        echo "FIRST_FAILURE=MESSAGE_KNEE rate=$rate"
        break
      fi
      sleep 15
    done
    ;;
  soak10k)
    run_case "soak-10k-30m" 10000 100 1800 hold 1 soak
    ;;
  *)
    echo "usage: $0 {connections|auth|message|soak10k}" >&2
    exit 64
    ;;
esac

sha256sum "$OUT"/*.log "$OUT/results.jsonl" 2>/dev/null > "$OUT/SHA256SUMS" || true
echo "TINYIMX_FINAL_LOADGEN_SUITE_COMPLETE=PASS mode=$MODE"
echo "RESULT_DIR=$OUT"
