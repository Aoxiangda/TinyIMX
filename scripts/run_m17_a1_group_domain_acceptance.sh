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
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m17-a1-${TIMESTAMP}"
mkdir -p "$ARTIFACT_DIR"

log(){ printf '[M17-A1] %s\n' "$*"; }
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

log "static architecture hard gate"
grep -q 'class GroupRepositoryPort' "$ROOT_DIR/services/group/application/GroupRepositoryPort.h" || fail "Group repository port missing"
grep -q 'class GroupPermissionPolicy' "$ROOT_DIR/services/group/application/GroupPermissionPolicy.h" || fail "Group permission policy missing"
grep -q 'BeginTransaction' "$ROOT_DIR/services/group/repository/GroupRepositoryAdapter.cpp" || fail "Group adapter does not own transaction begin"
grep -q 'InsertOnConnection' "$ROOT_DIR/services/group/repository/GroupRepositoryAdapter.cpp" || fail "Group adapter does not compose Outbox in transaction"
grep -q 'InsertOperationOnConnection' "$ROOT_DIR/services/group/repository/GroupRepositoryAdapter.cpp" || fail "Group adapter durable idempotency missing"
grep -q 'expected_version' "$ROOT_DIR/proto/tinyimx/group/v1/group_service.proto" || fail "versioned mutation contract missing"
grep -q 'PRIMARY KEY (actor_user_id, client_operation_id)' "$ROOT_DIR/db/migrations/006_create_group_domain.sql" || fail "durable operation key missing"
grep -q 'PRIMARY KEY (group_id, user_id)' "$ROOT_DIR/db/migrations/006_create_group_domain.sql" || fail "group member identity missing"
grep -q 'tinyimx-message-events' "$ROOT_DIR/services/group/application/GroupEventFactory.cpp" || fail "Group events no longer match frozen M16 relay topic"
grep -q 'std::string filter_expression{"\*"}' "$ROOT_DIR/services/eventing/rocketmq/RocketMQSimpleConsumer.h" || fail "generic RocketMQ consumer lost backward-compatible wildcard filter"
grep -q 'message.created.v1||dialog.read_advanced.v1' "$ROOT_DIR/services/projection/unread/UnreadProjector.h" || fail "UnreadProjector private-event tag filter missing"
grep -q 'kUnreadProjectionTagFilter' "$ROOT_DIR/examples/unread_projector_demo.cpp" || fail "UnreadProjector runtime did not apply private-event tag filter"

if grep -R -E -n 'RocketMQ|DefaultMQProducer|PushConsumer' \
  "$ROOT_DIR/services/group"; then
  fail "Group domain leaked RocketMQ transport SDK concerns"
fi
if grep -R -E -n 'GroupRepository|im_groups|im_group_members' \
  "$ROOT_DIR/gateway"; then
  fail "M17-A1 leaked Group durable ownership into Gateway"
fi
log "PASS architecture-boundary"

log "apply idempotent group-domain migration 006"
MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp \
  -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" \
  -D "$MYSQL_DATABASE" < "$ROOT_DIR/db/migrations/006_create_group_domain.sql"

for table in im_groups im_group_members im_group_membership_history im_group_operation_dedup; do
  count="$(mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='${table}';")"
  [[ "$count" == "1" ]] || fail "missing table after migration: $table"
done
unique_count="$(mysql_exec "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='im_group_operation_dedup' AND index_name='PRIMARY' AND non_unique=0;")"
[[ "$unique_count" == "2" ]] || fail "operation dedup composite primary key is incomplete"
log "PASS schema-migration"

log "configure/build focused M17-A1 targets (-j${BUILD_JOBS})"
[[ -f "${ROCKETMQ_PREFIX}/.tinyimx-rocketmq-isolated-toolchain" ]] || \
  fail "RocketMQ isolated SDK missing for scoped M16 compatibility build: ${ROCKETMQ_PREFIX}"
(cd "$ROOT_DIR" && cmake --preset linux-debug \
  -DTINYIMX_ENABLE_ROCKETMQ_CLIENT=ON \
  -DTINYIMX_ROCKETMQ_PREFIX="$ROCKETMQ_PREFIX")
cmake --build "$BUILD_DIR" --target \
  rpc_contract_tests \
  m17_a1_rocketmq_subscription_contract_tests \
  group_permission_policy_tests \
  group_application_service_tests \
  group_service_integration_tests \
  group_repository_integration_tests \
  group_service_demo \
  unread_projector_demo \
  m16_fault_recovery_runtime_test \
  -j"$BUILD_JOBS"
log "PASS focused-build"

log "run pure/domain and real-gRPC gates"
ctest --test-dir "$BUILD_DIR" --output-on-failure -R \
  '^(rpc_contract_tests|m17_a1_rocketmq_subscription_contract_tests|group_permission_policy_tests|group_application_service_tests|group_service_integration_tests)$' \
  | tee "$ARTIFACT_DIR/ctest-focused.log"
log "PASS focused-ctest"

log "run real MySQL lifecycle/idempotency/outbox gate"
"$BUILD_DIR/group_repository_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/group-repository-integration.log"
grep -q '\[PASS\] M17-A1 Group Repository integration tests' \
  "$ARTIFACT_DIR/group-repository-integration.log" || fail "M17-A1 repository integration marker missing"
log "PASS real-mysql-group-domain"

log "build complete ordinary-regression target graph (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" \
  | tee "$ARTIFACT_DIR/build-ordinary.log"
log "PASS ordinary-build"

log "retained ordinary regression (non-destructive only)"
ctest --test-dir "$BUILD_DIR" --output-on-failure \
  | tee "$ARTIFACT_DIR/ctest-ordinary.log"
log "PASS ordinary-regression"

printf '\n[M17-A1 PASS] Group Domain Foundation targeted acceptance passed.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
