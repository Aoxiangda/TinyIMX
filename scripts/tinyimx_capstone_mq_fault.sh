#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/tinyimx_capstone_common.sh"

STAMP="$(date +%Y%m%d-%H%M%S)"
ART="${TINYIMX_CAPSTONE_ARTIFACT_DIR:-$HOME/tinyimx-final-evidence/capstone-mq-fault-${STAMP}}"
BUILD="${TINYIMX_BUILD_DIR:-$ROOT/build/linux-release}"
BIN="${TINYIMX_LOADGEN_BIN:-$BUILD/tinyimx_im_loadgen}"
RATE="${TINYIMX_MQ_FAULT_RATE:-200}"
CONNS="${TINYIMX_MQ_FAULT_CONNECTIONS:-1000}"
OUTAGE="${TINYIMX_MQ_FAULT_OUTAGE_SECONDS:-30}"
DURATION="${TINYIMX_MQ_FAULT_DURATION_SECONDS:-100}"
TARGET="${TINYIMX_TARGET_HOST:-127.0.0.1}"
mkdir -p "$ART"

capstone_require_clean_benchmark_host
if [[ "${TINYIMX_CAPSTONE_SKIP_BUILD:-0}" != 1 ]]; then
  cmake --build "$BUILD" --target tinyimx_im_loadgen -- -j1 | tee "$ART/build.log"
else
  echo "CAPSTONE_BUILD_SKIPPED=1 bin=$BIN" | tee "$ART/build.log"
fi
[[ -x "$BIN" ]] || capstone_fail LOADGEN_MISSING "$BIN"
capstone_wait_healthy gateway-a 60 || capstone_fail GATEWAY_A_NOT_HEALTHY
capstone_wait_healthy gateway-b 60 || capstone_fail GATEWAY_B_NOT_HEALTHY

BROKER_CID="$(capstone_compose ps -q rocketmq-broker)"
BROKER_POLICY="$(docker inspect -f '{{.HostConfig.RestartPolicy.Name}}' "$BROKER_CID")"
STOP_FILE="$ART/.stop-outbox-sampler"

cleanup(){
  set +e
  touch "$STOP_FILE"
  [[ -n "${LG_PID:-}" ]] && kill "$LG_PID" 2>/dev/null || true
  [[ -n "${SAMPLE_PID:-}" ]] && kill "$SAMPLE_PID" 2>/dev/null || true
  docker update --restart="$BROKER_POLICY" "$BROKER_CID" >/dev/null 2>&1 || true
  [[ "$(docker inspect -f '{{.State.Running}}' "$BROKER_CID" 2>/dev/null)" == true ]] || docker start "$BROKER_CID" >/dev/null 2>&1 || true
}
trap cleanup EXIT

BASE_PENDING="$(capstone_mysql_scalar 'SELECT COUNT(*) FROM im_event_outbox WHERE status IN (0,2);')"
BASE_QUARANTINED="$(capstone_mysql_scalar 'SELECT COUNT(*) FROM im_event_outbox WHERE status=3;')"
echo "baseline_pending=$BASE_PENDING baseline_quarantined=$BASE_QUARANTINED" | tee "$ART/runner.log"

(
  while [[ ! -e "$STOP_FILE" ]]; do
    ts="$(capstone_epoch_ms)"
    p="$(capstone_mysql_scalar 'SELECT COUNT(*) FROM im_event_outbox WHERE status IN (0,2);' 2>/dev/null || echo -1)"
    q="$(capstone_mysql_scalar 'SELECT COUNT(*) FROM im_event_outbox WHERE status=3;' 2>/dev/null || echo -1)"
    echo "$ts $p $q" >> "$ART/outbox-samples.txt"
    sleep 2
  done
) &
SAMPLE_PID=$!

LOG="$ART/loadgen.log"
ulimit -Sn 524288 2>/dev/null || true
"$BIN" --host "$TARGET" --port 9000 \
  --connections "$CONNS" --user-id-base 500000 --username-prefix m21b500000_ --password 123456 \
  --mode private --peer-mode ring --duration "$DURATION" --rate "$RATE" --ramp-per-sec 100 \
  --payload-bytes 128 --heartbeat-seconds 30 --drain-seconds 10 --max-outstanding 1 \
  >"$LOG" 2>&1 &
LG_PID=$!

READY=0
for _ in $(seq 1 180); do
  grep -q '^TINYIMX_LOADGEN_READY ' "$LOG" 2>/dev/null && { READY=1; break; }
  kill -0 "$LG_PID" 2>/dev/null || break
  sleep 1
done
[[ "$READY" == 1 ]] || { cat "$LOG" >&2; capstone_fail MQ_FAULT_LOADGEN_NOT_READY; }

sleep 10
docker update --restart=no "$BROKER_CID" >/dev/null
OUTAGE_START="$(capstone_epoch_ms)"
docker kill "$BROKER_CID" >/dev/null
echo "broker_kill_epoch_ms=$OUTAGE_START" | tee -a "$ART/runner.log"
sleep "$OUTAGE"

MID_PENDING="$(capstone_mysql_scalar 'SELECT COUNT(*) FROM im_event_outbox WHERE status IN (0,2);')"
echo "outage_pending=$MID_PENDING" | tee -a "$ART/runner.log"
(( MID_PENDING > BASE_PENDING )) || capstone_fail OUTBOX_BACKLOG_DID_NOT_GROW "baseline=$BASE_PENDING outage=$MID_PENDING"

docker update --restart="$BROKER_POLICY" "$BROKER_CID" >/dev/null
docker start "$BROKER_CID" >/dev/null || true
sleep 5
# Recreate the topic if needed and refresh proxy connectivity.
capstone_compose restart rocketmq-proxy >/dev/null || true
sleep 5
capstone_compose run --rm rocketmq-init > "$ART/rocketmq-init.log" 2>&1 || capstone_fail ROCKETMQ_TOPIC_RECOVERY_FAILED

set +e
wait "$LG_PID"
LG_RC=$?
set -e
LG_PID=""
[[ "$LG_RC" -eq 0 || "$LG_RC" -eq 2 ]] || capstone_fail MQ_FAULT_LOADGEN_RUNTIME "rc=$LG_RC"

RESULT="$(grep '^TINYIMX_LOADGEN_RESULT ' "$LOG" | tail -1 | sed 's/^TINYIMX_LOADGEN_RESULT //')"
[[ -n "$RESULT" ]] || capstone_fail MQ_FAULT_RESULT_MISSING
printf '%s\n' "$RESULT" > "$ART/result.json"

DRAINED=0
for _ in $(seq 1 120); do
  pending="$(capstone_mysql_scalar 'SELECT COUNT(*) FROM im_event_outbox WHERE status IN (0,2);')"
  quarantined="$(capstone_mysql_scalar 'SELECT COUNT(*) FROM im_event_outbox WHERE status=3;')"
  if (( pending <= BASE_PENDING && quarantined <= BASE_QUARANTINED )); then DRAINED=1; break; fi
  sleep 1
done
FINAL_PENDING="$(capstone_mysql_scalar 'SELECT COUNT(*) FROM im_event_outbox WHERE status IN (0,2);')"
FINAL_QUARANTINED="$(capstone_mysql_scalar 'SELECT COUNT(*) FROM im_event_outbox WHERE status=3;')"

touch "$STOP_FILE"
kill "$SAMPLE_PID" 2>/dev/null || true
wait "$SAMPLE_PID" 2>/dev/null || true
SAMPLE_PID=""

RUN_ID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["run_id"])' "$ART/result.json")"
SENDS="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["send_attempts"])' "$ART/result.json")"
DURABLE="$(capstone_mysql_scalar "SELECT COUNT(*) FROM im_private_messages WHERE client_message_id LIKE 'b${RUN_ID}-%';")"

python3 - "$BASE_PENDING" "$MID_PENDING" "$FINAL_PENDING" "$BASE_QUARANTINED" "$FINAL_QUARANTINED" "$DRAINED" "$DURABLE" "$SENDS" "$RESULT" "$ART/outbox-samples.txt" <<'PY' | tee "$ART/gate.log"
import json,sys
base,mid,final,bq,fq,drained,durable,sends=map(int,sys.argv[1:9]); r=json.loads(sys.argv[9]); samples=sys.argv[10]
peak=mid
try:
    for line in open(samples):
        p=int(line.split()[1]); peak=max(peak,p)
except Exception: pass
checks={
 'message_success': r.get('chat_ack_fail',0)==0 and r.get('overload_rejections',0)==0 and float(r.get('success_rate',0))>=99.9,
 'durable': durable==sends==int(r.get('chat_ack_ok',0)),
 'backlog_grew': peak>base,
 'drained': drained==1 and final<=base,
 'no_new_quarantine': fq<=bq,
}
print(f"TINYIMX_CAPSTONE_MQ_FAULT_RESULT base_pending={base} peak_pending={peak} final_pending={final} base_quarantined={bq} final_quarantined={fq} durable_rows={durable} send_attempts={sends} throughput={r.get('throughput_msg_s')} p99_ms={r.get('p99_ms')} checks={','.join(k for k,v in checks.items() if not v) or 'all-pass'}")
if not all(checks.values()): raise SystemExit(42)
PY

sha256sum "$ART"/* 2>/dev/null > "$ART/SHA256SUMS" || true
echo "TINYIMX_CAPSTONE_MQ_FAULT_GATE=PASS" | tee "$ART/PASS.marker"
echo "ARTIFACT_DIR=$ART"
