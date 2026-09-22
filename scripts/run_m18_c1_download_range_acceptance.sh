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
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m18-c1-${TIMESTAMP}"
B1_STORAGE_ROOT="${ARTIFACT_DIR}/b1-storage-root"
B2_STORAGE_ROOT="${ARTIFACT_DIR}/b2-storage-root"
C1_STORAGE_ROOT="${ARTIFACT_DIR}/c1-storage-root"
mkdir -p "$ARTIFACT_DIR" "$B1_STORAGE_ROOT" "$B2_STORAGE_ROOT" "$C1_STORAGE_ROOT"

log(){ printf '[M18-C1] %s\n' "$*"; }
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
grep -q 'rpc GetDownloadInfo' "$ROOT_DIR/proto/tinyimx/file/v1/file_service.proto" || fail "GetDownloadInfo RPC missing"
grep -q 'rpc ReadFileRange' "$ROOT_DIR/proto/tinyimx/file/v1/file_service.proto" || fail "ReadFileRange RPC missing"
grep -q 'ReadObjectRange' "$ROOT_DIR/services/file/storage/FileStoragePort.h" || fail "range storage contract missing"
grep -q 'FindFileById' "$ROOT_DIR/services/repository/FileRepository.cpp" || fail "owner-scoped file lookup missing"
grep -q 'kMaxDownloadRangeSize = 1ULL \* 1024ULL \* 1024ULL' \
  "$ROOT_DIR/services/file/application/FileApplicationService.cpp" || fail "1MiB application range bound missing"
if grep -R -E -n 'GetDownloadInfo|ReadFileRange' "$ROOT_DIR/gateway"; then
  fail "download data-plane RPC leaked into Gateway business-message path"
fi
if grep -R -E -n '\b(downloaded_offset|download_offset)\b' \
    "$ROOT_DIR/db/migrations" "$ROOT_DIR/services/file"; then
  fail "download resume must remain stateless; persisted download offset found"
fi
if grep -n 'std::mutex' "$ROOT_DIR/services/file/storage/LocalFilesystemStorage.cpp"; then
  fail "range-read correctness must not depend on process-local mutex"
fi
log "PASS architecture/correctness"

log "verify retained A1/B1 schema; C1 adds no download-progress table"
for table in im_files im_file_upload_sessions im_file_upload_chunks; do
  count="$(mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='${table}';")"
  [[ "$count" == "1" ]] || fail "missing retained table: $table"
done
download_tables="$(mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name LIKE 'im_file_download%';")"
[[ "$download_tables" == "0" ]] || fail "unexpected persistent download-session/progress table found"
log "PASS retained-schema/stateless-download"

log "configure/build focused C1 targets (-j${BUILD_JOBS})"
[[ -f "${ROCKETMQ_PREFIX}/.tinyimx-rocketmq-isolated-toolchain" ]] || \
  fail "RocketMQ isolated SDK missing for retained regression build: ${ROCKETMQ_PREFIX}"
(cd "$ROOT_DIR" && cmake --preset linux-debug \
  -DTINYIMX_ENABLE_ROCKETMQ_CLIENT=ON \
  -DTINYIMX_ROCKETMQ_PREFIX="$ROCKETMQ_PREFIX")
cmake --build "$BUILD_DIR" --target \
  rpc_contract_tests \
  file_application_service_tests \
  file_storage_tests \
  file_service_integration_tests \
  file_repository_integration_tests \
  file_chunk_integration_tests \
  file_finalize_integration_tests \
  file_download_integration_tests \
  file_service_demo \
  -j"$BUILD_JOBS"
log "PASS focused-build"

log "run contract/application/storage/real-gRPC focused CTest"
ctest --test-dir "$BUILD_DIR" --output-on-failure -R \
  '^(rpc_contract_tests|file_application_service_tests|file_storage_tests|file_service_integration_tests)$' \
  | tee "$ARTIFACT_DIR/ctest-focused.log"
log "PASS focused-ctest"

log "retain M18-A1 real-MySQL regression gate"
"$BUILD_DIR/file_repository_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/file-repository-a1-regression.log"
grep -q '\[PASS\] M18-A1 file repository integration' \
  "$ARTIFACT_DIR/file-repository-a1-regression.log" || fail "A1 repository regression marker missing"
log "PASS a1-real-mysql-regression"

log "retain M18-B1 durable chunk/filesystem regression gate"
"$BUILD_DIR/file_chunk_integration_tests" "$SOURCE_CONFIG" "$B1_STORAGE_ROOT" \
  | tee "$ARTIFACT_DIR/file-chunk-b1-regression.log"
grep -q '\[PASS\] M18-B1 durable chunk/filesystem integration' \
  "$ARTIFACT_DIR/file-chunk-b1-regression.log" || fail "B1 chunk regression marker missing"
log "PASS b1-real-mysql-filesystem-regression"

log "retain M18-B2 resume/finalize/whole-checksum regression gate"
"$BUILD_DIR/file_finalize_integration_tests" "$SOURCE_CONFIG" "$B2_STORAGE_ROOT" \
  | tee "$ARTIFACT_DIR/file-finalize-b2-regression.log"
grep -q '\[PASS\] M18-B2 resume/finalize/filesystem integration' \
  "$ARTIFACT_DIR/file-finalize-b2-regression.log" || fail "B2 finalize regression marker missing"
log "PASS b2-real-mysql-finalize-regression"

log "run M18-C1 owner-auth/range-resume/final-object integrity gate"
"$BUILD_DIR/file_download_integration_tests" "$SOURCE_CONFIG" "$C1_STORAGE_ROOT" \
  | tee "$ARTIFACT_DIR/file-download-integration.log"
grep -q '\[PASS\] M18-C1 download authorization/range-resume integration' \
  "$ARTIFACT_DIR/file-download-integration.log" || fail "C1 download integration marker missing"
log "PASS real-mysql-filesystem-range-download"

log "ordinary regression build/test (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" | tee "$ARTIFACT_DIR/build-ordinary.log"
ctest --test-dir "$BUILD_DIR" --output-on-failure | tee "$ARTIFACT_DIR/ctest-ordinary.log"
log "PASS ordinary-regression"

printf '\n[M18-C1 PASS] Authorized stateless range download + reconnect resume accepted.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
