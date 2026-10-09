#!/usr/bin/env bash
set -Eeuo pipefail

MODE="${1:-connection}"
case "$MODE" in connection|message) ;; *) echo "usage: $0 {connection|message}" >&2; exit 64;; esac

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=/dev/null
source "$ROOT/scripts/tinyimx_capstone_common.sh"

CONNECTIONS="${TINYIMX_FAILOVER_CONNECTIONS:-10000}"
INITIAL_RAMP="${TINYIMX_FAILOVER_INITIAL_RAMP:-100}"
RECONNECT_RATE="${TINYIMX_FAILOVER_RECONNECT_RATE:-300}"
WARMUP="${TINYIMX_FAILOVER_WARMUP_SECONDS:-15}"
DURATION="${TINYIMX_FAILOVER_DURATION_SECONDS:-300}"
MESSAGE_RATE="${TINYIMX_FAILOVER_MESSAGE_RATE:-200}"
POST_RECOVERY_HOLD="${TINYIMX_FAILOVER_POST_RECOVERY_HOLD_SECONDS:-35}"
TARGET_HOST="${TINYIMX_FAILOVER_TARGET_HOST:-127.0.0.1}"
TARGET_PORT="${TINYIMX_FAILOVER_TARGET_PORT:-9000}"
BASE="${TINYIMX_BENCH_USER_BASE:-500000}"
PREFIX="${TINYIMX_BENCH_USERNAME_PREFIX:-m21b500000_}"
PASSWORD="${TINYIMX_BENCH_PASSWORD:-123456}"
BUILD="${TINYIMX_BUILD_DIR:-$ROOT/build/linux-release}"
BIN="${TINYIMX_FAILOVER_LOADGEN_BIN:-$BUILD/tinyimx_failover_loadgen}"
STAMP="$(date +%Y%m%d-%H%M%S)"
ART="${TINYIMX_CAPSTONE_ARTIFACT_DIR:-$HOME/tinyimx-final-evidence/capstone-failover-${MODE}-${STAMP}}"
mkdir -p "$ART"

[[ "$CONNECTIONS" =~ ^[0-9]+$ ]] || capstone_fail INVALID_CONNECTIONS "$CONNECTIONS"
(( CONNECTIONS >= 1000 && CONNECTIONS <= 20000 )) || capstone_fail INVALID_CONNECTIONS "$CONNECTIONS"

unset TINYIMX_FINAL_DRAIN || true
capstone_require_clean_benchmark_host
capstone_wait_healthy gateway-a 60 || capstone_fail GATEWAY_A_NOT_HEALTHY
capstone_wait_healthy gateway-b 60 || capstone_fail GATEWAY_B_NOT_HEALTHY
capstone_wait_healthy nginx 60 || capstone_fail NGINX_NOT_HEALTHY

if [[ "${TINYIMX_CAPSTONE_SKIP_BUILD:-0}" != 1 ]]; then
  cmake --build "$BUILD" --target tinyimx_failover_loadgen -- -j1 | tee "$ART/build.log"
else
  echo "CAPSTONE_BUILD_SKIPPED=1 bin=$BIN" | tee "$ART/build.log"
fi
[[ -x "$BIN" ]] || capstone_fail FAILOVER_LOADGEN_MISSING "$BIN"

REGISTRY_KEY="tinyimx:gateway:registry:gateway-a"
GA_CID="$(capstone_compose ps -q gateway-a)"
GA_NAME="$(capstone_gateway_container_name gateway-a)"
OLD_POLICY="$(capstone_gateway_restart_policy gateway-a)"
OLD_REGISTRY_JSON="$(capstone_redis GET "$REGISTRY_KEY" || true)"
OLD_LEASE_TOKEN="$(python3 -c 'import json,sys; s=sys.stdin.read().strip(); print(json.loads(s).get("lease_token", "") if s else "")' <<<"$OLD_REGISTRY_JSON" 2>/dev/null || true)"
[[ -n "$OLD_REGISTRY_JSON" && -n "$OLD_LEASE_TOKEN" ]] || capstone_fail GATEWAY_A_REGISTRY_BASELINE_MISSING

cleanup() {
  set +e
  if [[ -n "${LG_PID:-}" ]]; then kill "$LG_PID" 2>/dev/null || true; wait "$LG_PID" 2>/dev/null || true; fi
  if [[ -n "${SAMPLE_PID:-}" ]]; then kill "$SAMPLE_PID" 2>/dev/null || true; wait "$SAMPLE_PID" 2>/dev/null || true; fi
  if docker inspect "$GA_CID" >/dev/null 2>&1; then
    docker update --restart="$OLD_POLICY" "$GA_CID" >/dev/null 2>&1 || true
    if [[ "$(docker inspect -f '{{.State.Running}}' "$GA_CID" 2>/dev/null)" != true ]]; then docker start "$GA_CID" >/dev/null 2>&1 || true; fi
  fi
}
trap cleanup EXIT

sudo -v
"$ROOT/scripts/m21_resource_sampler.sh" "$ART/resources.log" 1 &
SAMPLE_PID=$!

echo "mode=$MODE" > "$ART/experiment.env"
echo "connections=$CONNECTIONS" >> "$ART/experiment.env"
echo "initial_ramp=$INITIAL_RAMP" >> "$ART/experiment.env"
echo "reconnect_rate=$RECONNECT_RATE" >> "$ART/experiment.env"
echo "warmup_seconds=$WARMUP" >> "$ART/experiment.env"
echo "duration_seconds=$DURATION" >> "$ART/experiment.env"
echo "message_rate=$MESSAGE_RATE" >> "$ART/experiment.env"
echo "post_recovery_hold_seconds=$POST_RECOVERY_HOLD" >> "$ART/experiment.env"
echo "old_restart_policy=$OLD_POLICY" >> "$ART/experiment.env"
echo "old_lease_token=$OLD_LEASE_TOKEN" >> "$ART/experiment.env"

APP_MODE=hold
RATE=1
if [[ "$MODE" == message ]]; then APP_MODE=private; RATE="$MESSAGE_RATE"; fi
EVENTS="$ART/recovery-events.csv"
LOG="$ART/loadgen.log"
RUN_START_ISO="$(date --iso-8601=seconds)"

ulimit -Sn 524288 2>/dev/null || true
"$BIN" \
  --host "$TARGET_HOST" --port "$TARGET_PORT" \
  --connections "$CONNECTIONS" \
  --user-id-base "$BASE" --username-prefix "$PREFIX" --password "$PASSWORD" \
  --mode "$APP_MODE" --peer-mode ring \
  --duration "$DURATION" --rate "$RATE" --ramp-per-sec "$INITIAL_RAMP" \
  --payload-bytes 128 --heartbeat-seconds 15 --drain-seconds 10 --max-outstanding 1 \
  --reconnect-on-disconnect 1 --reconnect-rate "$RECONNECT_RATE" \
  --reconnect-base-ms 100 --reconnect-max-ms 3000 --reconnect-max-attempts 30 \
  --recovery-events-file "$EVENTS" \
  >"$LOG" 2>&1 &
LG_PID=$!

READY=0
for _ in $(seq 1 240); do
  if grep -q '^TINYIMX_FAILOVER_LOADGEN_READY ' "$LOG" 2>/dev/null; then READY=1; break; fi
  if ! kill -0 "$LG_PID" 2>/dev/null; then break; fi
  sleep 1
done
if [[ "$READY" != 1 ]]; then
  cat "$LOG" >&2
  capstone_fail FAILOVER_LOADGEN_NOT_READY
fi

echo "TINYIMX_CAPSTONE_FAILOVER_READY=PASS" | tee "$ART/runner.log"
sleep "$WARMUP"

GA_CONN_BEFORE="$(sudo nsenter -t "$(capstone_gateway_pid gateway-a)" -n ss -Htn state established "sport = :9000" 2>/dev/null | wc -l || true)"
GB_CONN_BEFORE="$(sudo nsenter -t "$(capstone_gateway_pid gateway-b)" -n ss -Htn state established "sport = :9000" 2>/dev/null | wc -l || true)"
echo "gateway_a_established_before=${GA_CONN_BEFORE:-unknown}" | tee -a "$ART/runner.log"
echo "gateway_b_established_before=${GB_CONN_BEFORE:-unknown}" | tee -a "$ART/runner.log"

docker update --restart=no "$GA_CID" >/dev/null
KILL_MS="$(capstone_epoch_ms)"
echo "kill_epoch_ms=$KILL_MS" | tee -a "$ART/runner.log"
docker kill "$GA_CID" >/dev/null

REGISTRY_EXPIRE_MS=-1
for _ in $(seq 1 400); do
  if [[ "$(capstone_redis EXISTS "$REGISTRY_KEY" | tr -d '\r')" == 0 ]]; then
    REGISTRY_EXPIRE_MS=$(( $(capstone_epoch_ms) - KILL_MS ))
    break
  fi
  sleep 0.1
done
echo "registry_expire_ms=$REGISTRY_EXPIRE_MS" | tee -a "$ART/runner.log"
(( REGISTRY_EXPIRE_MS >= 0 )) || capstone_fail REGISTRY_LEASE_DID_NOT_EXPIRE

# Wait until the affected set is stable for a few samples, then until all affected clients recover.
AFFECTED=0
STABLE=0
PREV=-1
for _ in $(seq 1 60); do
  AFFECTED="$(awk -F, '$1=="affected"{print $2}' "$EVENTS" 2>/dev/null | sort -u | wc -l)"
  if [[ "$AFFECTED" == "$PREV" && "$AFFECTED" -gt 0 ]]; then STABLE=$((STABLE+1)); else STABLE=0; fi
  PREV="$AFFECTED"
  (( STABLE >= 3 )) && break
  sleep 0.5
done
(( AFFECTED > 0 )) || capstone_fail NO_CLIENTS_AFFECTED_BY_GATEWAY_A_KILL

echo "affected_clients_observed=$AFFECTED" | tee -a "$ART/runner.log"

RECOVERED=0
RECOVERY_CONVERGED_MS=-1
for _ in $(seq 1 300); do
  RECOVERED="$(awk -F, '$1=="recovered"{print $2}' "$EVENTS" 2>/dev/null | sort -u | wc -l)"
  if (( RECOVERED >= AFFECTED )); then
    RECOVERY_CONVERGED_MS=$(( $(capstone_epoch_ms) - KILL_MS ))
    break
  fi
  if ! kill -0 "$LG_PID" 2>/dev/null; then break; fi
  sleep 0.5
done

echo "recovered_clients_observed=$RECOVERED" | tee -a "$ART/runner.log"
echo "recovery_converged_ms=$RECOVERY_CONVERGED_MS" | tee -a "$ART/runner.log"
echo "all_clients_recovered_from_kill_ms=$RECOVERY_CONVERGED_MS" | tee -a "$ART/runner.log"

if (( RECOVERED >= AFFECTED )); then
  # Keep recovered sessions alive long enough to observe at least two failover
  # heartbeat cycles before auditing Redis presence. This validates steady-state
  # convergence, not merely the immediate Login SETEX.
  for _ in $(seq 1 "$POST_RECOVERY_HOLD"); do
    kill -0 "$LG_PID" 2>/dev/null || capstone_fail FAILOVER_LOADGEN_EXITED_BEFORE_PRESENCE_AUDIT
    sleep 1
  done

  #
  # Presence must be audited while recovered clients are still live.
  #
  # Do NOT collect the potentially large Gateway log before this audit:
  # log collection can consume the remaining LoadGen lifetime, causing
  # CloseAll() -> SetOfflineIfMatch() to legitimately remove every
  # tinyimx:online:* record before the audit executes.
  #
  kill -0 "$LG_PID" 2>/dev/null ||
    capstone_fail FAILOVER_LOADGEN_EXITED_BEFORE_PRESENCE_AUDIT

  PRESENCE_AUDIT_MS=$(( $(capstone_epoch_ms) - KILL_MS ))
  echo "presence_audit_from_kill_ms=$PRESENCE_AUDIT_MS" |
    tee -a "$ART/runner.log"

  set +e
  python3 "$ROOT/scripts/tinyimx_capstone_presence_audit.py" \
    "$EVENTS" gateway-b --output "$ART/presence-audit.json" \
    | tee "$ART/presence-audit.log"
  PRESENCE_RC=${PIPESTATUS[0]}
  set -e

  #
  # Diagnostic log collection happens only after state convergence has
  # already been audited.
  #
  docker logs "$(capstone_gateway_container_name gateway-b)" \
    --since "$RUN_START_ISO" > "$ART/gateway-b-window.log" 2>&1 || true

  PRESENCE_NOT_ADMITTED="$(grep -c 'gateway heartbeat presence refresh not admitted' "$ART/gateway-b-window.log" 2>/dev/null || true)"
  OFFLINE_NOT_ADMITTED="$(grep -c 'gateway async offline cleanup not admitted' "$ART/gateway-b-window.log" 2>/dev/null || true)"

  echo "presence_refresh_not_admitted=${PRESENCE_NOT_ADMITTED:-0}" |
    tee -a "$ART/runner.log"

  echo "offline_cleanup_not_admitted=${OFFLINE_NOT_ADMITTED:-0}" |
    tee -a "$ART/runner.log"
else
  PRESENCE_RC=4
  echo 'FIRST_FAILURE=PRESENCE_AUDIT_SKIPPED_RECOVERY_INCOMPLETE' > "$ART/presence-audit.log"
fi

# The surviving gateway must never cascade-fail.
capstone_wait_healthy gateway-b 10 || capstone_fail GATEWAY_B_CASCADE_FAILURE

set +e
wait "$LG_PID"
LG_RC=$?
set -e
LG_PID=""
[[ "$LG_RC" -eq 0 || "$LG_RC" -eq 2 ]] || capstone_fail FAILOVER_LOADGEN_RUNTIME "rc=$LG_RC"

RESULT="$(grep '^TINYIMX_FAILOVER_LOADGEN_RESULT ' "$LOG" | tail -1 | sed 's/^TINYIMX_FAILOVER_LOADGEN_RESULT //')"
[[ -n "$RESULT" ]] || capstone_fail FAILOVER_RESULT_MISSING
printf '%s\n' "$RESULT" > "$ART/result.json"

# This gate is diagnostic at this point. Do not let set -e abort
# before authoritative MySQL durable-state auditing is collected.
set +e
python3 - "$MODE" "$CONNECTIONS" "$AFFECTED" "$RECOVERY_CONVERGED_MS" "$REGISTRY_EXPIRE_MS" "$PRESENCE_RC" "$RESULT" <<'PY' | tee "$ART/gate.log"
import json,sys
mode=sys.argv[1]; n=int(sys.argv[2]); affected_observed=int(sys.argv[3]); recovery_ms=int(sys.argv[4]); registry_ms=int(sys.argv[5]); presence_rc=int(sys.argv[6]); r=json.loads(sys.argv[7])
recovery_ratio=1.0 if r.get('affected_clients',0)==0 else r.get('recovered_clients',0)/r['affected_clients']
hb_sent=int(r.get('heartbeat_sent',0)); hb_ratio=1.0 if hb_sent==0 else int(r.get('heartbeat_ack',0))/hb_sent
checks={
 'initial_auth': r.get('connected')==n and r.get('login_ok')==n and r.get('login_fail')==0,
 'affected': r.get('affected_clients',0)>0 and r.get('affected_clients',0)>=affected_observed,
 'recovery': recovery_ratio>=0.999 and r.get('recovery_pending_at_end',0)==0 and r.get('reconnect_exhausted',0)==0,
 'online_before_close': r.get('online_before_close')==n,
 'errors': r.get('protocol_errors',0)==0 and r.get('server_errors',0)==0,
 'registry': registry_ms>=0,
 'presence': presence_rc==0,
}
if mode=='message':
    checks.update({
      'message_success': r.get('chat_ack_fail',0)==0 and r.get('overload_rejections',0)==0 and r.get('inflight_at_end',0)==0 and float(r.get('success_rate',0))>=99.9,
      'visible_duplicate': r.get('receiver_delivery_duplicates',0)==0,
    })
print('TINYIMX_CAPSTONE_FAILOVER_RESULT mode=%s affected=%s recovered=%s recovery_ratio=%.6f recovery_p50_ms=%s recovery_p95_ms=%s recovery_p99_ms=%s recovery_max_ms=%s recovery_converged_ms=%s registry_expire_ms=%s heartbeat_ratio=%.6f checks=%s' % (
 mode,r.get('affected_clients'),r.get('recovered_clients'),recovery_ratio,r.get('recovery_p50_ms'),r.get('recovery_p95_ms'),r.get('recovery_p99_ms'),r.get('recovery_max_ms'),recovery_ms,registry_ms,hb_ratio,','.join(k for k,v in checks.items() if not v) or 'all-pass'))
if not all(checks.values()):
    raise SystemExit(42)
PY
GATE_RC=${PIPESTATUS[0]}
set -e

if [[ "$MODE" == message ]]; then
  RUN_ID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["run_id"])' "$ART/result.json")"
  SENDS="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["send_attempts"])' "$ART/result.json")"
  ACKS="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["chat_ack_ok"])' "$ART/result.json")"
  DURABLE="$(capstone_mysql_scalar "SELECT COUNT(*) FROM im_private_messages WHERE client_message_id LIKE 'b${RUN_ID}-%';")"
  CONFIRMED="$(capstone_mysql_scalar "SELECT COUNT(*) FROM im_private_messages WHERE client_message_id LIKE 'b${RUN_ID}-%' AND delivery_status>=1;")"
  echo "message_run_id=$RUN_ID send_attempts=$SENDS chat_ack_ok=$ACKS durable_rows=$DURABLE receiver_confirmed_rows=$CONFIRMED" | tee "$ART/message-durable-audit.log"
  [[ "$DURABLE" == "$SENDS" && "$ACKS" == "$SENDS" && "$CONFIRMED" == "$SENDS" ]] || GATE_RC=43
fi

# Restore Gateway-A only after all recovery assertions observed while it was down.
docker update --restart="$OLD_POLICY" "$GA_CID" >/dev/null
docker start "$GA_CID" >/dev/null || true
capstone_wait_healthy gateway-a 60 || capstone_fail GATEWAY_A_RESTORE_FAILED
NEW_REGISTRY_JSON=""
for _ in $(seq 1 100); do
  NEW_REGISTRY_JSON="$(capstone_redis GET "$REGISTRY_KEY" || true)"
  [[ -n "$NEW_REGISTRY_JSON" ]] && break
  sleep 0.2
done
NEW_LEASE_TOKEN="$(python3 -c 'import json,sys; s=sys.stdin.read().strip(); print(json.loads(s).get("lease_token", "") if s else "")' <<<"$NEW_REGISTRY_JSON" 2>/dev/null || true)"
echo "new_lease_token=$NEW_LEASE_TOKEN" | tee -a "$ART/runner.log"
[[ -n "$NEW_LEASE_TOKEN" && "$NEW_LEASE_TOKEN" != "$OLD_LEASE_TOKEN" ]] || GATE_RC=44

kill "$SAMPLE_PID" 2>/dev/null || true
wait "$SAMPLE_PID" 2>/dev/null || true
SAMPLE_PID=""

sha256sum "$ART"/* 2>/dev/null > "$ART/SHA256SUMS" || true
if (( GATE_RC != 0 )); then
  echo "FIRST_FAILURE=CAPSTONE_FAILOVER_${MODE^^}_GATE rc=$GATE_RC" >&2
  exit "$GATE_RC"
fi

echo "TINYIMX_CAPSTONE_FAILOVER_GATE=PASS mode=$MODE" | tee "$ART/PASS.marker"
echo "ARTIFACT_DIR=$ART"
