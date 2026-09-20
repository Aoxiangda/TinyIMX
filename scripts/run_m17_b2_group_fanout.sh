#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_CONFIG="${1:-config/gateway-a.local.json}"
if [[ "$SOURCE_CONFIG" != /* ]]; then SOURCE_CONFIG="$ROOT_DIR/$SOURCE_CONFIG"; fi
BUILD_DIR="${TINYIMX_BUILD_DIR:-$ROOT_DIR/build/linux-debug}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="$ROOT_DIR/artifacts/m17-b2-$TIMESTAMP"
mkdir -p "$ARTIFACT_DIR"

log() { printf '[M17-B2] %s\n' "$*"; }
fail() { printf '[M17-B2] FAIL %s\n' "$*" >&2; exit 1; }
require_cmd() { command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"; }

require_cmd cmake
require_cmd python3
require_cmd mysql
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

log "static architecture/security hard gate"
grep -q 'kGroupMessageDelivery = 2051' "$ROOT_DIR/common/protocol/Packet.h" || fail "packet 2051 missing"
grep -q 'kGroupMessageDeliveryAck = 2052' "$ROOT_DIR/common/protocol/Packet.h" || fail "packet 2052 missing"
grep -q 'kGatewayForwardGroupMessageRequest = 3003' "$ROOT_DIR/common/protocol/Packet.h" || fail "peer group request packet missing"
grep -q 'rpc PrepareGroupMessageSend' "$ROOT_DIR/proto/tinyimx/group/v1/group_service.proto" || fail "PrepareGroupMessageSend RPC missing"
grep -q 'BeginConsistentReadTransaction' "$ROOT_DIR/services/group/repository/GroupMembershipRepositoryAdapter.cpp" || fail "explicit recipient consistent-snapshot transaction missing"
grep -q 'rpc ClaimGroupMessageDeliveries' "$ROOT_DIR/proto/tinyimx/message/v1/message_service.proto" || fail "ClaimGroupMessageDeliveries RPC missing"
grep -q 'rpc ConfirmGroupMessageDelivery' "$ROOT_DIR/proto/tinyimx/message/v1/message_service.proto" || fail "ConfirmGroupMessageDelivery RPC missing"
grep -q 'CREATE TABLE IF NOT EXISTS im_group_message_deliveries' "$ROOT_DIR/db/migrations/008_create_group_message_delivery.sql" || fail "delivery migration missing"
grep -q 'PRIMARY KEY (message_id, recipient_user_id)' "$ROOT_DIR/db/migrations/008_create_group_message_delivery.sql" || fail "durable delivery identity missing"
grep -q 'DeliveryDomain::kGroupMessage' "$ROOT_DIR/gateway/DeliveryIdentity.h" || fail "group DeliveryIdentity domain definition missing"
grep -q 'GroupDeliveryIdentity(' "$ROOT_DIR/gateway/GatewayServer.cpp" || fail "Gateway group DeliveryIdentity helper usage missing"
grep -q 'GroupFanoutCoordinator' "$ROOT_DIR/examples/gateway_demo.cpp" || fail "fanout coordinator wiring missing"
grep -q 'GetGroupMessageDelivery' "$ROOT_DIR/gateway/GatewayServer.cpp" || fail "target peer durable validation missing"
if grep -R -E -n 'GroupRepository|im_group_members|im_groups' "$ROOT_DIR/gateway" >/dev/null; then
  fail "Gateway directly depends on Group durable storage"
fi
log "PASS architecture-security-boundary"

log "apply idempotent migrations 007/008"
MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp \
  -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" \
  -D "$MYSQL_DATABASE" < "$ROOT_DIR/db/migrations/007_create_group_message_domain.sql"
MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp \
  -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" \
  -D "$MYSQL_DATABASE" < "$ROOT_DIR/db/migrations/008_create_group_message_delivery.sql"
[[ "$(mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_message_deliveries';")" == "1" ]] || fail "delivery table missing"
[[ "$(mysql_exec "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='im_group_message_deliveries' AND index_name='PRIMARY' AND non_unique=0;")" == "2" ]] || fail "delivery primary key incomplete"
log "PASS schema-migration"

log "configure debug build"
(cd "$ROOT_DIR" && cmake --preset linux-debug) | tee "$ARTIFACT_DIR/configure.log"

log "build focused B2 targets (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" --target \
  rpc_contract_tests \
  protocol_tests \
  group_service_integration_tests \
  group_membership_repository_integration_tests \
  group_rpc_client_tests \
  message_application_service_tests \
  message_service_integration_tests \
  group_message_durable_write_integration_tests \
  group_message_fanout_integration_tests \
  gateway_tests \
  message_service_demo \
  group_service_demo \
  gateway_demo \
  gateway_group_fanout_e2e_client \
  -j"$BUILD_JOBS" | tee "$ARTIFACT_DIR/build-focused.log"
log "PASS focused-build"

log "run deterministic contract/application/gateway regressions"
ctest --test-dir "$BUILD_DIR" --output-on-failure -R \
  '^(rpc_contract_tests|tinyimx\.protocol|message_application_service_tests|message_service_integration_tests|group_service_integration_tests|group_rpc_client_tests|tinyimx\.gateway)$' \
  | tee "$ARTIFACT_DIR/ctest-focused.log"
log "PASS focused-ctest"

log "run real MySQL M17-B2 recipient consistent-snapshot gate"
"$BUILD_DIR/group_membership_repository_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/group-membership-snapshot.log"
grep -q 'M17-B2 frozen recipient snapshot excludes join-after-snapshot member' \
  "$ARTIFACT_DIR/group-membership-snapshot.log" \
  || fail "recipient consistent-snapshot marker missing"
grep -q '\[PASS\] M17-A2 Membership Repository integration tests' \
  "$ARTIFACT_DIR/group-membership-snapshot.log" \
  || fail "membership repository regression marker missing"
log "PASS recipient-consistent-snapshot"

log "run retained M17-B1 durable-write gate"
"$BUILD_DIR/group_message_durable_write_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/m17-b1-durable-write.log"
grep -q '\[PASS\] M17-B1 group durable write integration tests' "$ARTIFACT_DIR/m17-b1-durable-write.log" \
  || fail "M17-B1 retained marker missing"
log "PASS retained-m17-b1"

log "run real MySQL B2 fanout/lease/recovery gate"
"$BUILD_DIR/group_message_fanout_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/group-message-fanout.log"
grep -q '\[PASS\] M17-B2 durable fanout repository integration' "$ARTIFACT_DIR/group-message-fanout.log" \
  || fail "B2 durable fanout marker missing"
log "PASS durable-fanout-lease-recovery"

if [[ "${TINYIMX_M17_B2_RUN_TCP_E2E:-0}" == "1" ]]; then
  require_cmd ss
  HOST_A="${TINYIMX_M17_B2_GATEWAY_A_HOST:-127.0.0.1}"
  PORT_A="${TINYIMX_M17_B2_GATEWAY_A_PORT:-9001}"
  HOST_B="${TINYIMX_M17_B2_GATEWAY_B_HOST:-127.0.0.1}"
  PORT_B="${TINYIMX_M17_B2_GATEWAY_B_PORT:-9002}"
  SENDER="${TINYIMX_M17_B2_SENDER_USERNAME:-user10001}"
  LOCAL_RECIPIENT="${TINYIMX_M17_B2_LOCAL_RECIPIENT_USERNAME:-user10002}"
  REMOTE_RECIPIENT="${TINYIMX_M17_B2_REMOTE_RECIPIENT_USERNAME:-user10003}"
  PASSWORD="${TINYIMX_M17_B2_TCP_PASSWORD:-}"
  [[ -n "$PASSWORD" ]] || fail "TINYIMX_M17_B2_TCP_PASSWORD required"
  ss -ltn | grep -q ":${PORT_A}[[:space:]]" || fail "Gateway-A listener :${PORT_A} missing"
  ss -ltn | grep -q ":${PORT_B}[[:space:]]" || fail "Gateway-B listener :${PORT_B} missing"

  log "real local + cross-Gateway group delivery/ACK E2E"
  "$BUILD_DIR/gateway_group_fanout_e2e_client" \
    "$HOST_A" "$PORT_A" "$HOST_B" "$PORT_B" \
    "$SENDER" "$LOCAL_RECIPIENT" "$REMOTE_RECIPIENT" "$PASSWORD" \
    | tee "$ARTIFACT_DIR/tcp-two-gateway.log"
  grep -q '\[PASS\] M17-B2 real local+cross-gateway group delivery and ACK' \
    "$ARTIFACT_DIR/tcp-two-gateway.log" || fail "two-Gateway E2E marker missing"

  grep -q '\[PASS\] M17-B2 receiver spoof + retry + late/duplicate ACK semantics' \
    "$ARTIFACT_DIR/tcp-two-gateway.log" || fail "ACK semantics E2E marker missing"
  grep -q '\[PASS\] M17-B2 route refresh follows recipient Gateway movement' \
    "$ARTIFACT_DIR/tcp-two-gateway.log" || fail "route-refresh E2E marker missing"

  RESULT_LINE="$(grep 'M17_B2_RESULT ' "$ARTIFACT_DIR/tcp-two-gateway.log" | tail -1)"
  field() { sed -n "s/.*$1=\\([^[:space:]]*\\).*/\\1/p" <<<"$RESULT_LINE"; }
  GROUP_ID="$(field group_id)"
  SENDER_USER_ID="$(field sender_user_id)"
  LOCAL_USER_ID="$(field local_user_id)"
  REMOTE_USER_ID="$(field remote_user_id)"
  NORMAL_MESSAGE_ID="$(field normal_message_id)"
  ADVERSARIAL_MESSAGE_ID="$(field adversarial_message_id)"
  ROUTE_MESSAGE_ID="$(field route_message_id)"
  OFFLINE_MESSAGE_ID="$(field offline_message_id)"
  for value in "$GROUP_ID" "$SENDER_USER_ID" "$LOCAL_USER_ID" "$REMOTE_USER_ID" \
               "$NORMAL_MESSAGE_ID" "$ADVERSARIAL_MESSAGE_ID" \
               "$ROUTE_MESSAGE_ID" "$OFFLINE_MESSAGE_ID"; do
    [[ "$value" =~ ^[1-9][0-9]*$ ]] || fail "E2E result identity missing: $RESULT_LINE"
  done

  SOURCE_GATEWAY_ID="$(python3 - "$SOURCE_CONFIG" <<'PYCFG'
import json,sys
with open(sys.argv[1], encoding='utf-8') as f: o=json.load(f)
print(o.get('app',{}).get('instance_id',''))
PYCFG
)"
  [[ -n "$SOURCE_GATEWAY_ID" ]] || fail "source gateway instance_id missing from config"

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

  verify_two_delivered() {
    local mid="$1" label="$2"
    local count delivered sender_rows
    count="$(mysql_exec "SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id=${mid};")"
    delivered="$(wait_scalar 2 "SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id=${mid} AND delivery_status=3;" 12)" \
      || fail "$label did not converge to two DELIVERED rows"
    sender_rows="$(mysql_exec "SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id=${mid} AND recipient_user_id=${SENDER_USER_ID};")"
    [[ "$count" == "2" ]] || fail "$label expected exactly two recipient rows, got $count"
    [[ "$delivered" == "2" ]] || fail "$label expected both recipients DELIVERED, got $delivered"
    [[ "$sender_rows" == "0" ]] || fail "$label materialized sender as recipient"
  }

  verify_two_delivered "$NORMAL_MESSAGE_ID" "normal E2E"
  verify_two_delivered "$ADVERSARIAL_MESSAGE_ID" "ACK semantics E2E"
  verify_two_delivered "$ROUTE_MESSAGE_ID" "route-refresh E2E"

  ADVERSARIAL_REMOTE_ATTEMPTS="$(mysql_exec "SELECT attempt_count FROM im_group_message_deliveries WHERE message_id=${ADVERSARIAL_MESSAGE_ID} AND recipient_user_id=${REMOTE_USER_ID};")"
  [[ "$ADVERSARIAL_REMOTE_ATTEMPTS" =~ ^[0-9]+$ ]] || fail "adversarial remote attempt_count missing"
  (( ADVERSARIAL_REMOTE_ATTEMPTS >= 2 )) || fail "spoof/late-ACK scenario did not produce a durable retry"

  ROUTE_REMOTE_ATTEMPTS="$(mysql_exec "SELECT attempt_count FROM im_group_message_deliveries WHERE message_id=${ROUTE_MESSAGE_ID} AND recipient_user_id=${REMOTE_USER_ID};")"
  ROUTE_LAST_GATEWAY="$(mysql_exec "SELECT COALESCE(last_gateway_id,'') FROM im_group_message_deliveries WHERE message_id=${ROUTE_MESSAGE_ID} AND recipient_user_id=${REMOTE_USER_ID};")"
  [[ "$ROUTE_REMOTE_ATTEMPTS" =~ ^[0-9]+$ ]] || fail "route-refresh attempt_count missing"
  (( ROUTE_REMOTE_ATTEMPTS >= 2 )) || fail "route-refresh scenario did not retry durable delivery"
  [[ "$ROUTE_LAST_GATEWAY" == "$SOURCE_GATEWAY_ID" ]] || \
    fail "route-refresh final attempt did not move to source/local gateway: got '$ROUTE_LAST_GATEWAY' expected '$SOURCE_GATEWAY_ID'"

  OFFLINE_TOTAL="$(mysql_exec "SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id=${OFFLINE_MESSAGE_ID};")"
  [[ "$OFFLINE_TOTAL" == "2" ]] || fail "offline scenario expected two durable recipient rows, got $OFFLINE_TOTAL"
  wait_scalar 2 "SELECT delivery_status FROM im_group_message_deliveries WHERE message_id=${OFFLINE_MESSAGE_ID} AND recipient_user_id=${LOCAL_USER_ID};" 12 >/dev/null \
    || fail "offline recipient did not converge to DEFERRED_OFFLINE"
  wait_scalar 3 "SELECT delivery_status FROM im_group_message_deliveries WHERE message_id=${OFFLINE_MESSAGE_ID} AND recipient_user_id=${REMOTE_USER_ID};" 12 >/dev/null \
    || fail "online recipient in offline scenario did not converge to DELIVERED"
  [[ "$(mysql_exec "SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id=${OFFLINE_MESSAGE_ID} AND recipient_user_id=${SENDER_USER_ID};")" == "0" ]] \
    || fail "offline scenario materialized sender as recipient"

  log "PASS real-two-gateway-delivery-ack-sender-exclusion"
  log "PASS receiver-spoof-late-duplicate-ack"
  log "PASS route-refresh-after-cross-gateway-attempt"
  log "PASS offline-deferred-boundary"
else
  fail "final B2 acceptance requires real two-Gateway E2E; set TINYIMX_M17_B2_RUN_TCP_E2E=1"
fi

log "retained full build (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" | tee "$ARTIFACT_DIR/build-ordinary.log"
log "PASS ordinary-build"

log "retained full CTest"
ctest --test-dir "$BUILD_DIR" --output-on-failure | tee "$ARTIFACT_DIR/ctest-ordinary.log"
log "PASS ordinary-regression"

(cd "$ROOT_DIR" && git diff --check) | tee "$ARTIFACT_DIR/git-diff-check.log"
log "PASS source-integrity"

printf '\n[M17-B2 CORE PASS] Reliable Fanout & Multi-Gateway Delivery core correctness accepted.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
printf 'NOTE: This gate now covers explicit recipient snapshots, durable fanout, spoof/late/duplicate ACK, route refresh, offline deferred, and ordinary regression.\n'
printf 'NOTE: Do not declare M17-B2 CLOSED until the retained real Gateway crash/peer-response-loss fault gate is also accepted.\n'
printf 'NOTE: MySQL is the durable fanout truth; RocketMQ remains a domain-event transport, not the B2 correctness boundary.\n'
