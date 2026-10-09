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
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m17-a2-${TIMESTAMP}"
mkdir -p "$ARTIFACT_DIR"

log(){ printf '[M17-A2] %s\n' "$*"; }
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

log "static A2 architecture/domain hard gate"
grep -q 'JoinGroup' "$ROOT_DIR/services/group/application/GroupRepositoryPort.h" || fail "JoinGroup port missing"
grep -q 'TransferOwnership' "$ROOT_DIR/services/group/application/GroupRepositoryPort.h" || fail "TransferOwnership port missing"
grep -q 'CheckGroupSendPermission' "$ROOT_DIR/services/group/application/GroupRepositoryPort.h" || fail "send-permission port missing"
grep -q 'membership_epoch' "$ROOT_DIR/services/group/application/GroupApplicationTypes.h" || fail "membership epoch contract missing"
grep -q 'CanTransferOwnership' "$ROOT_DIR/services/group/application/GroupPermissionPolicy.h" || fail "ownership transfer policy missing"
grep -q 'LockActorAndTarget' "$ROOT_DIR/services/group/repository/GroupMembershipRepositoryAdapter.cpp" || fail "ordered two-member locking missing"
grep -q 'CountActiveMembersOnConnection' "$ROOT_DIR/services/group/repository/GroupMembershipRepositoryAdapter.cpp" || fail "capacity guard missing"
grep -q 'UpdateGroupOwnerAndVersionsOnConnection' "$ROOT_DIR/services/group/repository/GroupMembershipRepositoryAdapter.cpp" || fail "atomic owner/version transition missing"
grep -q 'UTC_TIMESTAMP(3)' "$ROOT_DIR/services/repository/GroupRepository.cpp" || fail "UTC mute evaluation missing"
grep -q 'group.member.kicked.v1' "$ROOT_DIR/services/group/application/GroupEventFactory.cpp" || fail "membership events missing"
grep -q 'group.owner_transferred.v1' "$ROOT_DIR/services/group/application/GroupEventFactory.cpp" || fail "ownership event missing"
grep -q 'method_count() == 14' "$ROOT_DIR/tests/rpc/rpc_contract_test.cpp" || fail "14-method GroupService contract gate missing"

if grep -R -E -n 'RocketMQ|DefaultMQProducer|PushConsumer' "$ROOT_DIR/services/group" | \
   grep -v 'GroupEventFactory' | grep -v 'GroupMembershipRepositoryAdapter' >/dev/null; then
  fail "Group domain leaked RocketMQ SDK transport concerns"
fi
if grep -R -E -n 'GroupRepository|im_group_members|im_groups' "$ROOT_DIR/gateway" >/dev/null; then
  fail "M17-A2 leaked Group durable ownership into Gateway before A3"
fi
log "PASS architecture-domain-boundary"

log "apply/verify idempotent group-domain migration 006"
MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp \
  -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" \
  -D "$MYSQL_DATABASE" < "$ROOT_DIR/db/migrations/006_create_group_domain.sql"
for table in im_groups im_group_members im_group_membership_history im_group_operation_dedup; do
  count="$(mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='${table}';")"
  [[ "$count" == "1" ]] || fail "missing table after migration: $table"
done
log "PASS schema-contract"

log "configure/build focused M17-A2 targets (-j${BUILD_JOBS})"
[[ -f "${ROCKETMQ_PREFIX}/.tinyimx-rocketmq-isolated-toolchain" ]] || \
  fail "RocketMQ isolated SDK missing: ${ROCKETMQ_PREFIX}"
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
  group_membership_repository_integration_tests \
  group_service_demo \
  unread_projector_demo \
  m16_fault_recovery_runtime_test \
  -j"$BUILD_JOBS" \
  | tee "$ARTIFACT_DIR/build-focused.log"
log "PASS focused-build"

log "run A1+A2 pure/domain/gRPC contract gates"
ctest --test-dir "$BUILD_DIR" --output-on-failure -R \
  '^(rpc_contract_tests|m17_a1_rocketmq_subscription_contract_tests|group_permission_policy_tests|group_application_service_tests|group_service_integration_tests)$' \
  | tee "$ARTIFACT_DIR/ctest-focused.log"
log "PASS focused-ctest"

log "run retained A1 real-MySQL lifecycle/idempotency/outbox gate"
"$BUILD_DIR/group_repository_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/group-a1-repository-regression.log"
grep -q '\[PASS\] M17-A1 Group Repository integration tests' \
  "$ARTIFACT_DIR/group-a1-repository-regression.log" || fail "A1 repository regression marker missing"
log "PASS retained-a1-repository"

log "run real-MySQL durable membership/idempotency/concurrency gate"
"$BUILD_DIR/group_membership_repository_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/group-a2-membership-integration.log"
grep -q '\[PASS\] M17-A2 Membership Repository integration tests' \
  "$ARTIFACT_DIR/group-a2-membership-integration.log" || fail "A2 membership integration marker missing"
log "PASS real-mysql-membership"

log "build complete ordinary regression target graph (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" \
  | tee "$ARTIFACT_DIR/build-ordinary.log"
log "PASS ordinary-build"

log "retained ordinary regression (non-destructive only)"
ctest --test-dir "$BUILD_DIR" --output-on-failure \
  | tee "$ARTIFACT_DIR/ctest-ordinary.log"
log "PASS ordinary-regression"

printf '\n[M17-A2 PASS] Durable Membership + Idempotent Mutation acceptance passed.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
