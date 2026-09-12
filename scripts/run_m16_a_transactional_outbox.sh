#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
SOURCE_CONFIG="${1:-${ROOT_DIR}/config/gateway-a.local.json}"
BUILD_DIR="${TINYIMX_BUILD_DIR:-${ROOT_DIR}/build/linux-debug}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
export VCPKG_ROOT="${VCPKG_ROOT:-${ROOT_DIR}/toolchains/vcpkg-tinyimx}"
export VCPKG_BINARY_SOURCES="${VCPKG_BINARY_SOURCES:-clear;default,readwrite}"
unset X_VCPKG_ASSET_SOURCES || true
unset VCPKG_DOWNLOADS || true

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m16-a-${TIMESTAMP}"
mkdir -p "$ARTIFACT_DIR"

log(){ printf '[M16-A] %s\n' "$*"; }
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
for c in cmake python3 mysql grep awk; do require_cmd "$c"; done

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

log "static architecture / ownership hard gate"
grep -q 'SavePrivateMessageOnConnection' "$ROOT_DIR/services/repository/MessageRepository.h" || fail "connection-aware message insert primitive missing"
grep -q 'FindPrivateMessageByIdOnConnection' "$ROOT_DIR/services/repository/MessageRepository.h" || fail "connection-aware verification primitive missing"
grep -q 'MarkReadByDialogOnConnection' "$ROOT_DIR/services/repository/MessageRepository.h" || fail "connection-aware read primitive missing"
grep -q 'BeginTransaction' "$ROOT_DIR/services/message/repository/MessageRepositoryAdapter.cpp" || fail "Adapter does not own transaction begin"
grep -q 'InsertOnConnection' "$ROOT_DIR/services/message/repository/MessageRepositoryAdapter.cpp" || fail "Adapter does not insert Outbox on caller transaction"
grep -q 'connection->Commit' "$ROOT_DIR/services/message/repository/MessageRepositoryAdapter.cpp" || fail "Adapter does not own transaction commit"
grep -q 'UNIQUE KEY uk_im_event_outbox_event_id' "$ROOT_DIR/db/migrations/004_create_event_outbox.sql" || fail "Outbox event_id uniqueness missing"
grep -q 'idx_im_event_outbox_publish_scan' "$ROOT_DIR/db/migrations/004_create_event_outbox.sql" || fail "Outbox relay scan index missing"

if grep -R -E -n 'rocketmq|RocketMQ|DefaultMQProducer|PushConsumer' \
  "$ROOT_DIR/services/eventing" "$ROOT_DIR/services/outbox" \
  "$ROOT_DIR/services/message/application/MessageEventFactory.cpp" \
  "$ROOT_DIR/services/message/repository/MessageRepositoryAdapter.cpp"; then
  fail "M16-A leaked RocketMQ SDK/runtime concerns"
fi

if grep -R -E -n 'OutboxRepository|im_event_outbox' \
  "$ROOT_DIR/gateway" "$ROOT_DIR/proto"; then
  fail "Outbox leaked into Gateway or Proto contract"
fi

# M14 ownership must remain closed.
if grep -E -n 'message_repository_|HasMessageRepository|SetMessageRepository|MessageRepository' \
  "$ROOT_DIR/gateway/GatewayServer.cpp" "$ROOT_DIR/gateway/GatewayServer.h" \
  "$ROOT_DIR/examples/gateway_demo.cpp"; then
  fail "Gateway regained durable MessageRepository ownership"
fi
log "PASS architecture-boundary"

log "apply idempotent Outbox migration"
MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp \
  -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" \
  -D "$MYSQL_DATABASE" < "$ROOT_DIR/db/migrations/004_create_event_outbox.sql"

TABLE_COUNT="$(mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_event_outbox';")"
[[ "$TABLE_COUNT" == "1" ]] || fail "im_event_outbox table missing after migration"
UNIQUE_COUNT="$(mysql_exec "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='im_event_outbox' AND index_name='uk_im_event_outbox_event_id' AND non_unique=0;")"
[[ "$UNIQUE_COUNT" == "1" ]] || fail "Outbox event_id unique index missing"
log "PASS schema-migration"

log "configure/build focused targets (-j${BUILD_JOBS})"
(cd "$ROOT_DIR" && cmake --preset linux-debug)
cmake --build "$BUILD_DIR" --target \
  message_outbox_integration_tests \
  message_application_service_tests \
  message_service_integration_tests \
  message_repository_idempotent_save_demo \
  message_repository_idempotent_concurrent_demo \
  message_service_demo \
  -j"$BUILD_JOBS"
log "PASS debug-focused-build"

log "real MySQL Transactional Outbox atomicity/idempotency gate"
"$BUILD_DIR/message_outbox_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/message-outbox-integration.log"
grep -q '\[PASS\] M16-A Transactional Outbox integration tests' \
  "$ARTIFACT_DIR/message-outbox-integration.log" || fail "M16-A integration marker missing"
log "PASS transactional-outbox-integration"

log "retained M14 Message application/gRPC gates"
"$BUILD_DIR/message_application_service_tests" \
  | tee "$ARTIFACT_DIR/message-application.log"
"$BUILD_DIR/message_service_integration_tests" \
  | tee "$ARTIFACT_DIR/message-service-integration.log"
grep -q 'failed=0' "$ARTIFACT_DIR/message-application.log" || fail "M14 application regression failed"
grep -q 'failed=0' "$ARTIFACT_DIR/message-service-integration.log" || fail "M14 gRPC regression failed"
log "PASS retained-m14-message-gates"

log "retained M12 sequential + concurrent idempotency gates"
"$BUILD_DIR/message_repository_idempotent_save_demo" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/m12-idempotent-save.log"
"$BUILD_DIR/message_repository_idempotent_concurrent_demo" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/m12-idempotent-concurrent.log"
grep -q 'idempotent save validation passed' "$ARTIFACT_DIR/m12-idempotent-save.log" || \
  fail "M12 sequential idempotency marker missing"
grep -q 'concurrent idempotent save validation passed' "$ARTIFACT_DIR/m12-idempotent-concurrent.log" || \
  fail "M12 concurrent idempotency marker missing"
log "PASS retained-m12-idempotency"

printf '\n[M16-A PASS] Transactional Outbox Foundation targeted acceptance passed.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
