#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
SOURCE_CONFIG="${1:-${ROOT_DIR}/config/gateway-a.local.json}"
BUILD_DIR="${TINYIMX_BUILD_DIR:-${ROOT_DIR}/build/linux-debug}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
ROCKETMQ_PREFIX="${TINYIMX_ROCKETMQ_PREFIX:-${ROOT_DIR}/toolchains/rocketmq-cpp-5.1.1}"
export VCPKG_ROOT="${VCPKG_ROOT:-${ROOT_DIR}/toolchains/vcpkg-tinyimx}"
export VCPKG_BINARY_SOURCES="${VCPKG_BINARY_SOURCES:-clear;default,readwrite}"
unset X_VCPKG_ASSET_SOURCES || true
unset VCPKG_DOWNLOADS || true

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m17-b1-${TIMESTAMP}"
mkdir -p "$ARTIFACT_DIR"

log(){ printf '[M17-B1] %s\n' "$*"; }
fail(){ log "FAIL: $*"; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }

json_get(){
  python3 - "$1" "$2" "$3" <<'PY'
import json,sys
with open(sys.argv[1],encoding='utf-8') as f: root=json.load(f)
v=root[sys.argv[2]][sys.argv[3]]
print('1' if v is True else '0' if v is False else v)
PY
}

[[ -f "$SOURCE_CONFIG" ]] || fail "missing config: $SOURCE_CONFIG"
[[ -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]] || \
  fail "vcpkg toolchain missing: ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
[[ -f "${ROCKETMQ_PREFIX}/.tinyimx-rocketmq-isolated-toolchain" ]] || \
  fail "RocketMQ isolated SDK missing: ${ROCKETMQ_PREFIX}"
for c in cmake python3 mysql grep; do require_cmd "$c"; done

MYSQL_HOST="${TINYIMX_MYSQL_HOST:-$(json_get "$SOURCE_CONFIG" mysql host)}"
MYSQL_PORT="${TINYIMX_MYSQL_PORT:-$(json_get "$SOURCE_CONFIG" mysql port)}"
MYSQL_DATABASE="${TINYIMX_MYSQL_DATABASE:-$(json_get "$SOURCE_CONFIG" mysql database)}"
MYSQL_USER="${TINYIMX_MYSQL_USER:-$(json_get "$SOURCE_CONFIG" mysql user)}"
MYSQL_PASSWORD="${TINYIMX_MYSQL_PASSWORD:-$(json_get "$SOURCE_CONFIG" mysql password)}"

mysql_exec(){
  MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp \
    -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" \
    -D "$MYSQL_DATABASE" --batch --skip-column-names -e "$1"
}

log "static architecture/security hard gate"
grep -q 'kGroupMessageSendRequest = 2049' "$ROOT_DIR/common/protocol/Packet.h" || fail "group message request packet id missing"
grep -q 'kGroupMessageSendResponse = 2050' "$ROOT_DIR/common/protocol/Packet.h" || fail "group message response packet id missing"
grep -q 'rpc PersistGroupMessage' "$ROOT_DIR/proto/tinyimx/message/v1/message_service.proto" || fail "PersistGroupMessage RPC missing"
grep -q 'CREATE TABLE IF NOT EXISTS im_group_messages' "$ROOT_DIR/db/migrations/007_create_group_message_domain.sql" || fail "im_group_messages migration missing"
grep -q 'UNIQUE KEY uk_im_group_messages_client_msg' "$ROOT_DIR/db/migrations/007_create_group_message_domain.sql" || fail "group message durable idempotency key missing"
grep -q 'group_message.created.v1' "$ROOT_DIR/services/message/application/MessageEventFactory.cpp" || fail "group message event contract missing"
grep -q 'FindGroupMessageByClientMessageId' "$ROOT_DIR/services/message/service/MessageServiceImpl.cpp" || fail "durable-truth-first lookup missing"
grep -q 'PrepareGroupMessageSendRpcRequest' \
  "$ROOT_DIR/services/message/service/MessageServiceImpl.cpp" \
  || fail "GroupService prepare request dependency missing"

grep -q 'group_rpc_client_->PrepareGroupMessageSend' \
  "$ROOT_DIR/services/message/service/MessageServiceImpl.cpp" \
  || fail "MessageService PrepareGroupMessageSend dependency missing"

grep -q 'GroupRpcClient::PrepareGroupMessageSend' \
  "$ROOT_DIR/services/rpc/GroupRpcClient.cpp" \
  || fail "GroupRpcClient PrepareGroupMessageSend dependency missing"

grep -q 'GroupServiceImpl::PrepareGroupMessageSend' \
  "$ROOT_DIR/services/group/service/GroupServiceImpl.cpp" \
  || fail "GroupService PrepareGroupMessageSend handler missing"
grep -q 'TINYIMX_FAULT_GROUP_MESSAGE_PERSIST_POST_COMMIT_DELAY_MS' "$ROOT_DIR/services/message/service/MessageServiceImpl.cpp" || fail "group post-commit uncertainty seam missing"
grep -q 'HandleGroupMessageSend' "$ROOT_DIR/gateway/GatewayServer.cpp" || fail "Gateway group-message handler missing"
grep -q 'const UserId actor_user_id = session_snapshot->user_id;' "$ROOT_DIR/gateway/GatewayServer.cpp" || fail "Session-authoritative actor gate missing"
grep -q 'group_message_persistence_uncertain' "$ROOT_DIR/gateway/GatewayServer.cpp" || fail "Gateway mutation uncertainty mapping missing"
if grep -R -E -n 'GroupRepository|im_group_members|im_groups' "$ROOT_DIR/gateway" >/dev/null; then
  fail "Gateway directly depends on Group durable storage"
fi
# Client actor/sender fields are deliberately not parsed by the B1 handler.
handler_slice="$(sed -n '/void GatewayServer::HandleGroupMessageSend(/,/void GatewayServer::HandleHeartbeat(/p' "$ROOT_DIR/gateway/GatewayServer.cpp")"
if printf '%s\n' "$handler_slice" | grep -E '\.at\("(actor_user_id|from_user_id)"\)|\.value\("(actor_user_id|from_user_id)"|\["(actor_user_id|from_user_id)"\]' >/dev/null; then
  fail "Gateway group-message handler parses client-supplied actor/sender identity"
fi
log "PASS architecture-security-boundary"

log "apply idempotent M17-B1 group-message migration 007"
MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp \
  -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" \
  -D "$MYSQL_DATABASE" < "$ROOT_DIR/db/migrations/007_create_group_message_domain.sql"

table_count="$(mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_group_messages';")"
[[ "$table_count" == "1" ]] || fail "im_group_messages missing after migration"
unique_cols="$(mysql_exec "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='im_group_messages' AND index_name='uk_im_group_messages_client_msg' AND non_unique=0;")"
[[ "$unique_cols" == "2" ]] || fail "group message sender/client id unique key is incomplete"
log "PASS schema-migration"

log "configure M17-B1 build"
(cd "$ROOT_DIR" && cmake --preset linux-debug \
  -DTINYIMX_ENABLE_ROCKETMQ_CLIENT=ON \
  -DTINYIMX_ROCKETMQ_PREFIX="$ROCKETMQ_PREFIX") \
  | tee "$ARTIFACT_DIR/configure.log"

log "build focused M17-B1 targets (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" --target \
  rpc_contract_tests \
  protocol_tests \
  message_application_service_tests \
  message_service_integration_tests \
  group_message_durable_write_integration_tests \
  message_outbox_integration_tests \
  group_service_integration_tests \
  group_rpc_client_tests \
  gateway_tests \
  message_service_demo \
  group_service_demo \
  gateway_demo \
  gateway_group_client_demo \
  gateway_group_message_client_demo \
  -j"$BUILD_JOBS" \
  | tee "$ARTIFACT_DIR/build-focused.log"
log "PASS focused-build"

log "run deterministic RPC/protocol/application/service gates"
ctest --test-dir "$BUILD_DIR" --output-on-failure -R \
  '^(rpc_contract_tests|tinyimx\.protocol|message_application_service_tests|message_service_integration_tests|group_service_integration_tests|group_rpc_client_tests|tinyimx\.gateway)$' \
  | tee "$ARTIFACT_DIR/ctest-focused.log"
log "PASS focused-ctest"

log "run real MySQL M17-B1 durable-write/idempotency/outbox gate"
"$BUILD_DIR/group_message_durable_write_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/group-message-durable-write.log"
grep -q '\[PASS\] M17-B1 group durable write integration tests' \
  "$ARTIFACT_DIR/group-message-durable-write.log" || fail "M17-B1 durable-write marker missing"
log "PASS real-mysql-group-message"

log "retained M16 private-message transactional-outbox regression"
"$BUILD_DIR/message_outbox_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/m16-message-outbox-regression.log"
grep -q '\[PASS\] M16-A Transactional Outbox integration tests' \
  "$ARTIFACT_DIR/m16-message-outbox-regression.log" || fail "M16 outbox regression marker missing"
log "PASS retained-m16-outbox"

if [[ "${TINYIMX_M17_B1_RUN_TCP_E2E:-0}" == "1" ]]; then
  require_cmd ss
  GATEWAY_HOST="${TINYIMX_M17_B1_GATEWAY_HOST:-127.0.0.1}"
  GATEWAY_PORT="${TINYIMX_M17_B1_GATEWAY_PORT:-9001}"
  TCP_USERNAME="${TINYIMX_M17_B1_TCP_USERNAME:-user10001}"
  TCP_PASSWORD="${TINYIMX_M17_B1_TCP_PASSWORD:-}"
  [[ -n "$TCP_PASSWORD" ]] || fail "TINYIMX_M17_B1_TCP_PASSWORD required for TCP E2E"

  log "real TCP vertical E2E against running User/Group/Message/Gateway services"
  ss -ltn | grep -q ":${GATEWAY_PORT}[[:space:]]" || fail "Gateway listener :${GATEWAY_PORT} missing"

  OP_ID="m17b1-create-${TIMESTAMP}-$$"
  GROUP_JSON="$($BUILD_DIR/gateway_group_client_demo \
    "$GATEWAY_HOST" "$GATEWAY_PORT" "$TCP_USERNAME" "$TCP_PASSWORD" create \
    "{\"client_operation_id\":\"${OP_ID}\",\"name\":\"M17-B1-${TIMESTAMP}\",\"description\":\"M17-B1 reliable write acceptance\",\"max_members\":20}")"
  printf '%s\n' "$GROUP_JSON" | tee "$ARTIFACT_DIR/tcp-create-group.json"
  GROUP_ID="$(python3 -c 'import json,sys; o=json.loads(sys.stdin.read()); assert o.get("success") is True; print(o["group"]["group_id"])' <<<"$GROUP_JSON")"
  [[ "$GROUP_ID" =~ ^[1-9][0-9]*$ ]] || fail "fresh group_id invalid"

  CLIENT_ID="m17b1-send-${TIMESTAMP}-$$"
  BODY="{\"group_id\":${GROUP_ID},\"client_message_id\":\"${CLIENT_ID}\",\"message_type\":1,\"content\":\"m17-b1-tcp-${TIMESTAMP}\",\"actor_user_id\":999999,\"from_user_id\":999999}"
  CREATED_JSON="$($BUILD_DIR/gateway_group_message_client_demo \
    "$GATEWAY_HOST" "$GATEWAY_PORT" "$TCP_USERNAME" "$TCP_PASSWORD" "$BODY")"
  printf '%s\n' "$CREATED_JSON" | tee "$ARTIFACT_DIR/tcp-created.json"
  CREATED_ID="$(python3 -c 'import json,sys; o=json.loads(sys.stdin.read()); assert o.get("success") is True and o.get("result")=="created"; print(o["message_id"])' <<<"$CREATED_JSON")"

  REUSED_JSON="$($BUILD_DIR/gateway_group_message_client_demo \
    "$GATEWAY_HOST" "$GATEWAY_PORT" "$TCP_USERNAME" "$TCP_PASSWORD" "$BODY")"
  printf '%s\n' "$REUSED_JSON" | tee "$ARTIFACT_DIR/tcp-reused.json"
  python3 -c '
import json,sys
o=json.loads(sys.argv[1])
expected=int(sys.argv[2])
assert o.get("success") is True and o.get("result")=="reused" and int(o["message_id"])==expected
' "$REUSED_JSON" "$CREATED_ID"

  CONFLICT_BODY="{\"group_id\":${GROUP_ID},\"client_message_id\":\"${CLIENT_ID}\",\"message_type\":1,\"content\":\"different-content\"}"
  CONFLICT_JSON="$($BUILD_DIR/gateway_group_message_client_demo \
    "$GATEWAY_HOST" "$GATEWAY_PORT" "$TCP_USERNAME" "$TCP_PASSWORD" "$CONFLICT_BODY")"
  printf '%s\n' "$CONFLICT_JSON" | tee "$ARTIFACT_DIR/tcp-conflict.json"
  python3 -c '
import json,sys
o=json.loads(sys.argv[1])
assert o.get("success") is False and o.get("reason")=="client_message_id_conflict"
' "$CONFLICT_JSON"

  DB_ROW="$(mysql_exec "SELECT message_id,from_user_id,group_id FROM im_group_messages WHERE client_message_id='${CLIENT_ID}' LIMIT 1;")"
  [[ -n "$DB_ROW" ]] || fail "TCP group message not durable"
  DB_COUNT="$(mysql_exec "SELECT COUNT(*) FROM im_group_messages WHERE client_message_id='${CLIENT_ID}';")"
  [[ "$DB_COUNT" == "1" ]] || fail "TCP retry created duplicate group-message row"
  OUTBOX_COUNT="$(mysql_exec "SELECT COUNT(*) FROM im_event_outbox WHERE event_id='group_message.created.v1:${CREATED_ID}';")"
  [[ "$OUTBOX_COUNT" == "1" ]] || fail "TCP retry created duplicate group-message event"
  PAYLOAD="$(mysql_exec "SELECT payload FROM im_event_outbox WHERE event_id='group_message.created.v1:${CREATED_ID}' LIMIT 1;")"
  [[ "$PAYLOAD" != *"${CLIENT_ID}"* ]] || fail "client_message_id leaked into group event payload"
  [[ "$PAYLOAD" != *"m17-b1-tcp-${TIMESTAMP}"* ]] || fail "message content leaked into group event payload"
  log "PASS real-tcp-created-reused-conflict-and-actor-spoof"
else
  fail "final M17-B1 acceptance requires real TCP E2E; set TINYIMX_M17_B1_RUN_TCP_E2E=1 and provide TINYIMX_M17_B1_TCP_PASSWORD"
fi

log "build complete ordinary regression target graph (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" \
  | tee "$ARTIFACT_DIR/build-ordinary.log"
log "PASS ordinary-build"

log "retained ordinary regression"
ctest --test-dir "$BUILD_DIR" --output-on-failure \
  | tee "$ARTIFACT_DIR/ctest-ordinary.log"
log "PASS ordinary-regression"

(cd "$ROOT_DIR" && git diff --check) \
  | tee "$ARTIFACT_DIR/git-diff-check.log"
log "PASS source-integrity"

printf '\n[M17-B1 PASS] Reliable Group Message Write vertical slice accepted.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
printf 'NOTE: real TCP E2E completed in this acceptance run.\n'
