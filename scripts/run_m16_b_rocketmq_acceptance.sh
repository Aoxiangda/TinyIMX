#!/usr/bin/env bash
set -Eeuo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
SOURCE_CONFIG="${1:-${ROOT_DIR}/config/gateway-a.local.json}"
BUILD_DIR="${TINYIMX_M16B_BUILD_DIR:-${ROOT_DIR}/build/linux-debug}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
PREFIX="${TINYIMX_ROCKETMQ_PREFIX:-${ROOT_DIR}/toolchains/rocketmq-cpp-5.1.1}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m16-b-${TIMESTAMP}"
mkdir -p "$ARTIFACT_DIR"
log(){ printf '[M16-B] %s\n' "$*"; }
fail(){ log "FAIL: $*"; exit 1; }
[[ -f "$SOURCE_CONFIG" ]] || fail "missing config: $SOURCE_CONFIG"
for c in cmake python3 grep mysql redis-cli; do command -v "$c" >/dev/null 2>&1 || fail "missing command: $c"; done

log "architecture boundary gate"
if grep -R -E -n '#include <rocketmq/|ROCKETMQ_NAMESPACE|rocketmq::' "$ROOT_DIR/gateway" "$ROOT_DIR/services/message" "$ROOT_DIR/proto"; then fail "RocketMQ SDK leaked into Gateway/MessageService/Proto"; fi
if grep -R -E -n 'RocketMQ|rocketmq' "$ROOT_DIR/proto"; then fail "business proto contains RocketMQ metadata"; fi
if grep -R -E -n 'RocketMQ|rocketmq' "$ROOT_DIR/net" 2>/dev/null; then fail "Reactor/network hot path contains RocketMQ"; fi
grep -q 'FOR UPDATE SKIP LOCKED' "$ROOT_DIR/services/outbox/OutboxRepository.cpp" || fail "SKIP LOCKED claim missing"
grep -q "namespace tinyimx::eventing::rocketmq_transport" "$ROOT_DIR/services/eventing/rocketmq/RocketMQProducer.cpp" || fail "transport namespace fence missing"
if grep -R -n 'namespace tinyimx::eventing::rocketmq {' "$ROOT_DIR/services/eventing/rocketmq"; then fail "namespace collision regressed"; fi
log "PASS architecture-boundary"

log "bootstrap official RocketMQ cpp-5.1.1"
TINYIMX_ROCKETMQ_PREFIX="$PREFIX" "$ROOT_DIR/scripts/bootstrap_rocketmq_cpp_client.sh" | tee "$ARTIFACT_DIR/bootstrap-client.log"
log "start RocketMQ 5.5.1 and provision NORMAL topic"
"$ROOT_DIR/scripts/local_rocketmq.sh" create-topic | tee "$ARTIFACT_DIR/local-rocketmq.log"

TMP_CONFIG="$ARTIFACT_DIR/m16-b-runtime.json"
python3 - "$SOURCE_CONFIG" "$TMP_CONFIG" <<'PY'
import json,sys
src,dst=sys.argv[1:]
with open(src,encoding='utf-8') as f: c=json.load(f)
c['rocketmq']={'enable':True,'endpoint':'127.0.0.1:8081','message_topic':'tinyimx-message-events','request_timeout_ms':3000,'tls':False,'access_key':'','access_secret':''}
c['outbox_relay']={'enable':True,'instance_id':'outbox-relay-m16b-acceptance','batch_size':32,'worker_threads':4,'max_inflight':128,'poll_interval_ms':100,'lease_ms':30000,'lease_renew_interval_ms':5000,'retry_base_ms':200,'retry_max_ms':30000,'published_retention_hours':168,'cleanup_interval_ms':60000,'cleanup_batch_size':1000,'shutdown_timeout_ms':10000}
c['unread_projection']={'enable':True,'owner':'gateway','shadow_mode':True,'consumer_group':'tinyimx-unread-projector-m16b-acceptance','consumer_request_timeout_ms':30000,'batch_size':16,'invisible_duration_ms':30000,'await_duration_ms':1000,'receive_error_backoff_ms':500}
with open(dst,'w',encoding='utf-8') as f: json.dump(c,f,ensure_ascii=False,indent=2)
PY

log "migrate M16-A pending/retry Outbox rows to broker-legal RocketMQ topic"
mapfile -d '' -t MYSQL_CFG < <(python3 - "$TMP_CONFIG" <<'PY'
import json,sys
with open(sys.argv[1], encoding='utf-8') as f:
    c=json.load(f)
m=c.get('mysql', {})
values=[
    m.get('host','127.0.0.1'),
    str(m.get('port',3306)),
    m.get('user','root'),
    m.get('database','tinyimx'),
    m.get('password',''),
]
for value in values:
    sys.stdout.write(str(value))
    sys.stdout.write('\0')
PY
)
[[ ${#MYSQL_CFG[@]} -eq 5 ]] || fail "failed to read MySQL config for topic migration"
MYSQL_PWD="${MYSQL_CFG[4]}" mysql --protocol=tcp \
  -h "${MYSQL_CFG[0]}" -P "${MYSQL_CFG[1]}" -u "${MYSQL_CFG[2]}" \
  "${MYSQL_CFG[3]}" < "$ROOT_DIR/db/migrations/005_migrate_rocketmq_topic_name.sql"
log "PASS outbox-topic-migration"

log "configure/build M16-B focused targets (-j${BUILD_JOBS})"
(cd "$ROOT_DIR" && cmake --preset linux-debug -DTINYIMX_ENABLE_ROCKETMQ_CLIENT=ON -DTINYIMX_ROCKETMQ_PREFIX="$PREFIX")
cmake --build "$BUILD_DIR" --target \
  config_m16_b_tests outbox_relay_unit_tests \
  unread_projection_reader_integration_tests unread_projection_cache_integration_tests \
  rocketmq_transport_integration_tests outbox_relay_demo unread_projector_demo \
  message_outbox_integration_tests message_application_service_tests \
  message_service_integration_tests message_repository_idempotent_save_demo \
  message_repository_idempotent_concurrent_demo -j"$BUILD_JOBS"
log "PASS focused-build"

"$BUILD_DIR/config_m16_b_tests" | tee "$ARTIFACT_DIR/config.log"
"$BUILD_DIR/outbox_relay_unit_tests" | tee "$ARTIFACT_DIR/relay-unit.log"
"$BUILD_DIR/unread_projection_reader_integration_tests" "$TMP_CONFIG" | tee "$ARTIFACT_DIR/unread-reader.log"
"$BUILD_DIR/unread_projection_cache_integration_tests" "$TMP_CONFIG" | tee "$ARTIFACT_DIR/unread-cache.log"
"$BUILD_DIR/rocketmq_transport_integration_tests" "$TMP_CONFIG" | tee "$ARTIFACT_DIR/rocketmq-transport.log"

log "provision acceptance projector consumer group"

PROJECTOR_CONSUMER_GROUP="$(
  python3 -c \
    'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["unread_projection"]["consumer_group"])' \
    "$TMP_CONFIG"
)"

[[ -n "$PROJECTOR_CONSUMER_GROUP" ]] \
  || fail "projector acceptance consumer group is empty"

MQADMIN="$ROOT_DIR/.local/rocketmq-5.5.1/dist/bin/mqadmin"
[[ -x "$MQADMIN" ]] \
  || fail "mqadmin missing: $MQADMIN"

if "$MQADMIN" updateSubGroup \
    -n 127.0.0.1:9876 \
    -c "${TINYIMX_ROCKETMQ_CLUSTER:-DefaultCluster}" \
    -g "$PROJECTOR_CONSUMER_GROUP" \
    -s true \
    -d false \
    -m false \
    -q 1 \
    -r 16 \
    -a true \
    >"$ARTIFACT_DIR/projector-group-provision.log" 2>&1
then
  :
else
  cat "$ARTIFACT_DIR/projector-group-provision.log" >&2 || true
  fail "projector acceptance consumer group provisioning failed"
fi

log "PASS projector-consumer-group-provision"

log "real M16-A event -> Relay -> RocketMQ -> Shadow Projector chain"
"$BUILD_DIR/unread_projector_demo" "$TMP_CONFIG" >"$ARTIFACT_DIR/projector.log" 2>&1 & PROJECTOR_PID=$!
"$BUILD_DIR/outbox_relay_demo" "$TMP_CONFIG" >"$ARTIFACT_DIR/relay.log" 2>&1 & RELAY_PID=$!

cleanup_process() {
  local pid="$1"
  local name="$2"
  local deadline=$((SECONDS + 5))

  [[ -n "$pid" ]] || return 0

  kill -TERM "$pid" 2>/dev/null || true

  while kill -0 "$pid" 2>/dev/null; do
    if (( SECONDS >= deadline )); then
      log "cleanup forcing SIGKILL for ${name}, pid=${pid}"
      kill -KILL "$pid" 2>/dev/null || true
      break
    fi
    sleep 0.1
  done

  if wait "$pid" 2>/dev/null; then
    :
  else
    :
  fi
}

cleanup(){
  cleanup_process "$RELAY_PID" "outbox-relay"
  cleanup_process "$PROJECTOR_PID" "unread-projector"
}
trap cleanup EXIT

wait_ready_marker() {
  local pid="$1"
  local log_file="$2"
  local marker="$3"
  local process_name="$4"
  local timeout_seconds="${5:-30}"
  local deadline=$((SECONDS + timeout_seconds))

  while (( SECONDS < deadline )); do
    if grep -Fq "$marker" "$log_file" 2>/dev/null; then
      log "PASS ${process_name}-ready"
      return 0
    fi

    if ! kill -0 "$pid" 2>/dev/null; then
      tail -80 "$log_file" >&2 || true
      fail "${process_name} exited before readiness"
    fi

    sleep 0.1
  done

  tail -80 "$log_file" >&2 || true
  fail "${process_name} readiness timeout waiting for: ${marker}"
}

wait_ready_marker   "$PROJECTOR_PID"   "$ARTIFACT_DIR/projector.log"   "unread projector started"   "unread-projector"   30

wait_ready_marker   "$RELAY_PID"   "$ARTIFACT_DIR/relay.log"   "outbox relay started"   "outbox-relay"   30

"$BUILD_DIR/message_outbox_integration_tests" "$TMP_CONFIG"   | tee "$ARTIFACT_DIR/m16-a-event-source.log"

# Runtime chain is now measured only after both workers are actually ready.
sleep 3

kill -TERM "$RELAY_PID" "$PROJECTOR_PID" 2>/dev/null || true

# Capture both child exit codes without changing global errexit state.
# Commands evaluated as an `if` condition are exempt from `set -e`.
if wait "$RELAY_PID"; then
    RELAY_RC=0
else
    RELAY_RC=$?
fi

if wait "$PROJECTOR_PID"; then
    PROJECTOR_RC=0
else
    PROJECTOR_RC=$?
fi

trap - EXIT

log "relay/projector closeout rc: relay=${RELAY_RC}, projector=${PROJECTOR_RC}"
[[ "$RELAY_RC" == 0 ]] || fail "relay process did not drain cleanly"
[[ "$PROJECTOR_RC" == 0 ]] || fail "projector process failed"
grep -q 'outbox relay stopped' "$ARTIFACT_DIR/relay.log" || fail "relay closeout marker missing"
grep -q 'unread projector stopped' "$ARTIFACT_DIR/projector.log" || fail "projector closeout marker missing"

PROJECTOR_STATS="$(
  grep 'unread projector stopped, received=' \
    "$ARTIFACT_DIR/projector.log" \
  | tail -1 \
  || true
)"

[[ -n "$PROJECTOR_STATS" ]] \
  || fail "projector final stats missing"

PROJECTOR_RECEIVED="$(
  sed -nE 's/.*received=([0-9]+).*/\1/p' \
    <<<"$PROJECTOR_STATS"
)"

PROJECTOR_PROCESS_FAILURE="$(
  sed -nE 's/.*process_failure=([0-9]+).*/\1/p' \
    <<<"$PROJECTOR_STATS"
)"

PROJECTOR_ACK_FAILURE="$(
  sed -nE 's/.*ack_failure=([0-9]+).*/\1/p' \
    <<<"$PROJECTOR_STATS"
)"

[[ "$PROJECTOR_RECEIVED" =~ ^[0-9]+$ ]] \
  || fail "invalid projector received stat: ${PROJECTOR_STATS}"

[[ "$PROJECTOR_PROCESS_FAILURE" =~ ^[0-9]+$ ]] \
  || fail "invalid projector process_failure stat: ${PROJECTOR_STATS}"

[[ "$PROJECTOR_ACK_FAILURE" =~ ^[0-9]+$ ]] \
  || fail "invalid projector ack_failure stat: ${PROJECTOR_STATS}"

(( PROJECTOR_RECEIVED >= 1 )) \
  || fail "projector consumed no M16-A event"

(( PROJECTOR_PROCESS_FAILURE == 0 )) \
  || fail "projector process failures detected: ${PROJECTOR_STATS}"

(( PROJECTOR_ACK_FAILURE == 0 )) \
  || fail "projector ACK failures detected: ${PROJECTOR_STATS}"

log "PASS projector-semantic-consumption received=${PROJECTOR_RECEIVED}"

log "PASS real-relay-projector-chain"

log "authoritative projection rebuild"
python3 - "$TMP_CONFIG" "$ARTIFACT_DIR/projector-writer.json" <<'PY'
import json,sys
with open(sys.argv[1],encoding='utf-8') as f:c=json.load(f)
c['unread_projection']['owner']='projector'; c['unread_projection']['shadow_mode']=False
with open(sys.argv[2],'w',encoding='utf-8') as f:json.dump(c,f,indent=2)
PY
"$BUILD_DIR/unread_projector_demo" "$ARTIFACT_DIR/projector-writer.json" --rebuild-user 10002 | tee "$ARTIFACT_DIR/rebuild.log"
log "PASS projection-rebuild"

log "retained M16-A/M14/M12 regressions"
"$BUILD_DIR/message_application_service_tests" | tee "$ARTIFACT_DIR/message-application.log"
"$BUILD_DIR/message_service_integration_tests" | tee "$ARTIFACT_DIR/message-service.log"
"$BUILD_DIR/message_repository_idempotent_save_demo" "$TMP_CONFIG" | tee "$ARTIFACT_DIR/m12-save.log"
"$BUILD_DIR/message_repository_idempotent_concurrent_demo" "$TMP_CONFIG" | tee "$ARTIFACT_DIR/m12-concurrent.log"
grep -q 'failed=0' "$ARTIFACT_DIR/message-application.log" || fail "M14 app regression failed"
grep -q 'failed=0' "$ARTIFACT_DIR/message-service.log" || fail "M14 grpc regression failed"
grep -q 'idempotent save validation passed' "$ARTIFACT_DIR/m12-save.log" || fail "M12 save failed"
grep -q 'concurrent idempotent save validation passed' "$ARTIFACT_DIR/m12-concurrent.log" || fail "M12 concurrent failed"
printf '\n[M16-B PASS] Reliable RocketMQ publication + unread projection targeted acceptance passed.\nartifacts: %s\n' "$ARTIFACT_DIR"
