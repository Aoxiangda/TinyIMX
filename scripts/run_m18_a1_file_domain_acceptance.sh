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
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m18-a1-${TIMESTAMP}"
mkdir -p "$ARTIFACT_DIR"

log(){ printf '[M18-A1] %s\n' "$*"; }
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

log "static architecture/correctness gate"
grep -q 'service FileService' "$ROOT_DIR/proto/tinyimx/file/v1/file_service.proto" || fail "FileService proto missing"
grep -q 'class FileRepositoryPort' "$ROOT_DIR/services/file/application/FileRepositoryPort.h" || fail "FileRepositoryPort missing"
grep -q 'BeginTransaction' "$ROOT_DIR/services/file/repository/FileRepositoryAdapter.cpp" || fail "File adapter transaction boundary missing"
grep -q 'ON DUPLICATE KEY UPDATE upload_id = LAST_INSERT_ID(upload_id)' "$ROOT_DIR/services/repository/FileRepository.cpp" || fail "concurrent durable idempotency primitive missing"
grep -q 'UNIQUE KEY uk_im_file_upload_client (owner_user_id, client_upload_id)' "$ROOT_DIR/db/migrations/009_create_file_domain.sql" || fail "upload business idempotency key missing"
grep -q 'UNIQUE KEY uk_im_files_storage_key (storage_backend, storage_key)' "$ROOT_DIR/db/migrations/009_create_file_domain.sql" || fail "storage-key uniqueness boundary missing"
grep -q 'storage_key VARCHAR(512) NULL' "$ROOT_DIR/db/migrations/009_create_file_domain.sql" || fail "speculative file row must not reserve a fake storage key"
grep -q 'file_name is invalid or unsafe' "$ROOT_DIR/services/file/application/FileApplicationService.cpp" || fail "filename metadata validation missing"
if grep -R -E -n 'RocketMQ|DefaultMQProducer|PushConsumer' "$ROOT_DIR/services/file"; then
  fail "File domain leaked MQ transport concerns"
fi
if grep -R -E -n 'im_files|im_file_upload_sessions|FileRepository' "$ROOT_DIR/gateway"; then
  fail "M18-A1 leaked durable File ownership into Gateway"
fi
log "PASS architecture/correctness"

log "apply idempotent file-domain migration 009"
MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp \
  -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" \
  -D "$MYSQL_DATABASE" < "$ROOT_DIR/db/migrations/009_create_file_domain.sql"
for table in im_files im_file_upload_sessions; do
  count="$(mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='${table}';")"
  [[ "$count" == "1" ]] || fail "missing table after migration: $table"
done
upload_unique_columns="$(mysql_exec "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='im_file_upload_sessions' AND index_name='uk_im_file_upload_client' AND non_unique=0;")"
[[ "$upload_unique_columns" == "2" ]] || fail "upload idempotency composite unique key is incomplete"
log "PASS schema-migration"

log "configure/build focused M18-A1 targets (-j${BUILD_JOBS})"
[[ -f "${ROCKETMQ_PREFIX}/.tinyimx-rocketmq-isolated-toolchain" ]] || \
  fail "RocketMQ isolated SDK missing for retained M16 compatibility build: ${ROCKETMQ_PREFIX}"
(cd "$ROOT_DIR" && cmake --preset linux-debug \
  -DTINYIMX_ENABLE_ROCKETMQ_CLIENT=ON \
  -DTINYIMX_ROCKETMQ_PREFIX="$ROCKETMQ_PREFIX")
cmake --build "$BUILD_DIR" --target \
  rpc_contract_tests \
  file_application_service_tests \
  file_service_integration_tests \
  file_repository_integration_tests \
  -j"$BUILD_JOBS"
log "PASS focused-build"

log "run proto/application/real-gRPC focused CTest"
ctest --test-dir "$BUILD_DIR" --output-on-failure -R \
  '^(rpc_contract_tests|file_application_service_tests|file_service_integration_tests)$' \
  | tee "$ARTIFACT_DIR/ctest-focused.log"
log "PASS focused-ctest"

log "run real MySQL idempotency/concurrency/lifecycle gate"
"$BUILD_DIR/file_repository_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/file-repository-integration.log"
grep -q '\[PASS\] M18-A1 file repository integration' \
  "$ARTIFACT_DIR/file-repository-integration.log" || fail "repository integration marker missing"
log "PASS real-mysql-file-domain"

log "retained ordinary regression build/test (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" \
  | tee "$ARTIFACT_DIR/build-ordinary.log"
ctest --test-dir "$BUILD_DIR" --output-on-failure \
  | tee "$ARTIFACT_DIR/ctest-ordinary.log"
log "PASS ordinary-regression"

printf '\n[M18-A1 PASS] File Domain / Upload Session targeted acceptance passed.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
