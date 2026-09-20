#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_CONFIG="${1:-config/gateway-a.local.json}"
if [[ "$SOURCE_CONFIG" != /* ]]; then SOURCE_CONFIG="$ROOT_DIR/$SOURCE_CONFIG"; fi
BUILD_DIR="${TINYIMX_BUILD_DIR:-$ROOT_DIR/build/linux-debug}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="$ROOT_DIR/artifacts/m17-final-$TIMESTAMP"
mkdir -p "$ARTIFACT_DIR"

log() { printf '[M17-FINAL] %s\n' "$*"; }
fail() { printf '[M17-FINAL] FAIL: %s\n' "$*" >&2; exit 1; }
require_cmd() { command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"; }

require_cmd cmake
require_cmd python3
require_cmd mysql
require_cmd ss
[[ -f "$SOURCE_CONFIG" ]] || fail "config missing: $SOURCE_CONFIG"

readarray -t MYSQL_FIELDS < <(python3 - "$SOURCE_CONFIG" <<'PY'
import json,sys
with open(sys.argv[1], encoding='utf-8') as f: o=json.load(f)
m=o.get('mysql',{})
for k,d in [('host','127.0.0.1'),('port',3306),('user','root'),('password',''),('database','tinyimx')]:
    print(m.get(k,d))
PY
)
MYSQL_HOST="${MYSQL_FIELDS[0]}"
MYSQL_PORT="${MYSQL_FIELDS[1]}"
MYSQL_USER="${MYSQL_FIELDS[2]}"
MYSQL_PASSWORD="${MYSQL_FIELDS[3]}"
MYSQL_DATABASE="${MYSQL_FIELDS[4]}"
mysql_exec() {
  MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp \
    -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" \
    -D "$MYSQL_DATABASE" --batch --skip-column-names -e "$1"
}

log "static M17-final architecture/contract gates"
grep -q 'rpc ClaimGroupMessageDeliveriesForRecipient' \
  "$ROOT_DIR/proto/tinyimx/message/v1/message_service.proto" \
  || fail "recipient replay RPC missing"
grep -q 'ClaimGroupMessageDeliveriesForRecipient' \
  "$ROOT_DIR/services/repository/MessageRepository.cpp" \
  || fail "recipient replay repository claim missing"
grep -q 'delivery_status=2' \
  "$ROOT_DIR/services/repository/MessageRepository.cpp" \
  || fail "recipient replay is not restricted to DEFERRED_OFFLINE"
grep -q 'FOR UPDATE SKIP LOCKED' \
  "$ROOT_DIR/services/repository/MessageRepository.cpp" \
  || fail "cross-Gateway replay claim fence missing"
grep -q 'PushGroupOfflineMessages' "$ROOT_DIR/gateway/GatewayServer.cpp" \
  || fail "post-login group replay trigger missing"
grep -q 'ScheduleGroupMessageDeliveryAckTimeout' "$ROOT_DIR/gateway/GatewayServer.cpp" \
  || fail "group replay ACK-loss recovery missing"
grep -q 'ConfirmGroupMessageDelivery' "$ROOT_DIR/gateway/GatewayServer.cpp" \
  || fail "receiver ACK durable confirmation missing"
grep -q 'idx_im_group_delivery_recipient' \
  "$ROOT_DIR/db/migrations/008_create_group_message_delivery.sql" \
  || fail "recipient replay index missing"
if grep -R -E -n 'GroupRepository|im_group_members|im_groups' "$ROOT_DIR/gateway" >/dev/null; then
  fail "Gateway directly depends on Group durable storage"
fi
log "PASS static-architecture-contract"

log "source integrity before build"
(cd "$ROOT_DIR" && git diff --check) | tee "$ARTIFACT_DIR/git-diff-check-pre.log"
find "$ROOT_DIR" -type f \( -name '*.rej' -o -name '*.orig' \) -print \
  | tee "$ARTIFACT_DIR/reject-orig-pre.log"
[[ ! -s "$ARTIFACT_DIR/reject-orig-pre.log" ]] || fail "reject/orig files present"

log "configure + focused M17 final build (-j${BUILD_JOBS})"
(cd "$ROOT_DIR" && cmake --preset linux-debug) | tee "$ARTIFACT_DIR/configure.log"
cmake --build "$BUILD_DIR" --target \
  rpc_contract_tests \
  protocol_tests \
  message_application_service_tests \
  message_service_integration_tests \
  group_message_durable_write_integration_tests \
  group_message_fanout_integration_tests \
  gateway_tests \
  user_service_demo \
  group_service_demo \
  message_service_demo \
  gateway_demo \
  gateway_group_fanout_e2e_client \
  gateway_group_offline_replay_e2e_client \
  -j"$BUILD_JOBS" | tee "$ARTIFACT_DIR/build-focused.log"
log "PASS focused-build"

log "focused contract/application/gateway regression"
ctest --test-dir "$BUILD_DIR" --output-on-failure -R \
  '^(rpc_contract_tests|tinyimx\.protocol|message_application_service_tests|message_service_integration_tests|tinyimx\.gateway)$' \
  | tee "$ARTIFACT_DIR/ctest-focused.log"
log "PASS focused-ctest"

log "real MySQL retained B1 + B2/B3 repository gates"
"$BUILD_DIR/group_message_durable_write_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/b1-durable-write.log"
grep -q '\[PASS\] M17-B1 group durable write integration tests' \
  "$ARTIFACT_DIR/b1-durable-write.log" || fail "B1 retained marker missing"

"$BUILD_DIR/group_message_fanout_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/b2-b3-repository.log"
grep -q '\[PASS\] M17-B2 durable fanout repository integration' \
  "$ARTIFACT_DIR/b2-b3-repository.log" || fail "B2 retained marker missing"
grep -q '\[PASS\] M17-B3 durable recipient replay repository integration' \
  "$ARTIFACT_DIR/b2-b3-repository.log" || fail "B3 repository marker missing"
log "PASS durable-repository-regression"

HOST_A="${TINYIMX_M17_FINAL_GATEWAY_A_HOST:-127.0.0.1}"
PORT_A="${TINYIMX_M17_FINAL_GATEWAY_A_PORT:-9001}"
HOST_B="${TINYIMX_M17_FINAL_GATEWAY_B_HOST:-127.0.0.1}"
PORT_B="${TINYIMX_M17_FINAL_GATEWAY_B_PORT:-9002}"
SENDER="${TINYIMX_M17_FINAL_SENDER_USERNAME:-user10001}"
LOCAL_RECIPIENT="${TINYIMX_M17_FINAL_LOCAL_RECIPIENT_USERNAME:-user10002}"
REMOTE_RECIPIENT="${TINYIMX_M17_FINAL_REMOTE_RECIPIENT_USERNAME:-user10003}"
PASSWORD="${TINYIMX_M17_FINAL_TCP_PASSWORD:-}"
[[ -n "$PASSWORD" ]] || fail "TINYIMX_M17_FINAL_TCP_PASSWORD required for final TCP E2E"

for port in 50052 50053 50054 "$PORT_A" "$PORT_B"; do
  ss -ltn | grep -q ":${port}[[:space:]]" || fail "required listener :${port} missing"
done

log "real TCP offline/replay/ACK-loss/Gateway-movement E2E"
"$BUILD_DIR/gateway_group_offline_replay_e2e_client" \
  "$HOST_A" "$PORT_A" "$HOST_B" "$PORT_B" \
  "$SENDER" "$LOCAL_RECIPIENT" "$REMOTE_RECIPIENT" "$PASSWORD" \
  | tee "$ARTIFACT_DIR/tcp-b3-final.log"
grep -q '\[PASS\] M17-B3 offline recipient reconnect receives durable replay' \
  "$ARTIFACT_DIR/tcp-b3-final.log" || fail "offline replay marker missing"
grep -q '\[PASS\] M17-B3 ACK loss produces fresh-seq retry; late/duplicate ACK is safe' \
  "$ARTIFACT_DIR/tcp-b3-final.log" || fail "ACK-loss recovery marker missing"
grep -q '\[PASS\] M17-B3 replay follows authenticated Gateway movement' \
  "$ARTIFACT_DIR/tcp-b3-final.log" || fail "Gateway movement marker missing"

RESULT_LINE="$(grep 'M17_B3_RESULT ' "$ARTIFACT_DIR/tcp-b3-final.log" | tail -1)"
field() { sed -n "s/.*$1=\\([^[:space:]]*\\).*/\\1/p" <<<"$RESULT_LINE"; }
GROUP_ID="$(field group_id)"
SENDER_USER_ID="$(field sender_user_id)"
LOCAL_USER_ID="$(field local_user_id)"
REMOTE_USER_ID="$(field remote_user_id)"
REPLAY_MESSAGE_ID="$(field replay_message_id)"
MOVEMENT_MESSAGE_ID="$(field movement_message_id)"
for value in "$GROUP_ID" "$SENDER_USER_ID" "$LOCAL_USER_ID" "$REMOTE_USER_ID" \
             "$REPLAY_MESSAGE_ID" "$MOVEMENT_MESSAGE_ID"; do
  [[ "$value" =~ ^[1-9][0-9]*$ ]] || fail "invalid E2E identity: $RESULT_LINE"
done

wait_scalar() {
  local expected="$1" sql="$2" timeout_s="${3:-12}" value=""
  for _ in $(seq 1 $((timeout_s * 10))); do
    value="$(mysql_exec "$sql" 2>/dev/null || true)"
    [[ "$value" == "$expected" ]] && { printf '%s' "$value"; return 0; }
    sleep 0.1
  done
  printf '%s' "$value"
  return 1
}

for mid in "$REPLAY_MESSAGE_ID" "$MOVEMENT_MESSAGE_ID"; do
  [[ "$(mysql_exec "SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id=${mid};")" == "2" ]] \
    || fail "message ${mid} expected exactly two recipient rows"
  [[ "$(mysql_exec "SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id=${mid} AND recipient_user_id=${SENDER_USER_ID};")" == "0" ]] \
    || fail "message ${mid} materialized sender as recipient"
done

wait_scalar 3 "SELECT delivery_status FROM im_group_message_deliveries WHERE message_id=${REPLAY_MESSAGE_ID} AND recipient_user_id=${LOCAL_USER_ID};" 12 >/dev/null \
  || fail "offline replay recipient did not converge to DELIVERED"
wait_scalar 3 "SELECT delivery_status FROM im_group_message_deliveries WHERE message_id=${MOVEMENT_MESSAGE_ID} AND recipient_user_id=${REMOTE_USER_ID};" 12 >/dev/null \
  || fail "moved recipient did not converge to DELIVERED"

REPLAY_ATTEMPTS="$(mysql_exec "SELECT attempt_count FROM im_group_message_deliveries WHERE message_id=${REPLAY_MESSAGE_ID} AND recipient_user_id=${LOCAL_USER_ID};")"
[[ "$REPLAY_ATTEMPTS" =~ ^[0-9]+$ ]] || fail "replay attempt_count missing"
(( REPLAY_ATTEMPTS >= 2 )) || fail "offline replay claim did not increment durable attempt count"
log "PASS real-tcp-b3-final"

log "final ordinary build (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" | tee "$ARTIFACT_DIR/build-full.log"
log "PASS full-build"

log "final full CTest"
ctest --test-dir "$BUILD_DIR" --output-on-failure | tee "$ARTIFACT_DIR/ctest-full.log"
log "PASS full-ctest"

(cd "$ROOT_DIR" && git diff --check) | tee "$ARTIFACT_DIR/git-diff-check-final.log"
find "$ROOT_DIR" -type f \( -name '*.rej' -o -name '*.orig' \) -print \
  | tee "$ARTIFACT_DIR/reject-orig-final.log"
[[ ! -s "$ARTIFACT_DIR/reject-orig-final.log" ]] || fail "reject/orig files present after final"
log "PASS final-source-integrity"

printf '\n[M17 FINAL PASS] Group domain + reliable write + fanout + offline replay/recovery accepted.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
printf 'NOTE: receiver client ACK is still the only authority that advances group delivery to DELIVERED.\n'
printf 'NOTE: group delivery ACK is not a read receipt; group unread/read-cursor semantics remain a separate product concern.\n'
