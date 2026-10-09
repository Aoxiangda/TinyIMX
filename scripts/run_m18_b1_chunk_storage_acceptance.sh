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
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m18-b1-${TIMESTAMP}"
STORAGE_ROOT="${ARTIFACT_DIR}/storage-root"
mkdir -p "$ARTIFACT_DIR" "$STORAGE_ROOT"

log(){ printf '[M18-B1] %s\n' "$*"; }
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
grep -q 'rpc UploadChunk' "$ROOT_DIR/proto/tinyimx/file/v1/file_service.proto" || fail "UploadChunk RPC missing"
grep -q 'class FileStoragePort' "$ROOT_DIR/services/file/storage/FileStoragePort.h" || fail "FileStoragePort missing"
grep -q 'class LocalFilesystemStorage' "$ROOT_DIR/services/file/storage/LocalFilesystemStorage.h" || fail "LocalFilesystemStorage missing"
grep -q 'PRIMARY KEY (upload_id, chunk_index)' "$ROOT_DIR/db/migrations/010_create_file_upload_chunks.sql" || fail "durable chunk identity missing"
grep -q 'status TINYINT UNSIGNED NOT NULL DEFAULT 1' "$ROOT_DIR/db/migrations/010_create_file_upload_chunks.sql" || fail "chunk RESERVED state missing"
grep -q 'StoreChunkAtomically' "$ROOT_DIR/services/file/application/FileApplicationService.cpp" || fail "storage orchestration missing"
grep -q 'MarkChunkStored' "$ROOT_DIR/services/file/application/FileApplicationService.cpp" || fail "durable storage completion missing"
if ! python3 - "$ROOT_DIR" <<'PY_GATE'
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
targets = [root / "db/migrations/010_create_file_upload_chunks.sql"]
targets.extend((root / "services/file").rglob("*.h"))
targets.extend((root / "services/file").rglob("*.cpp"))

def strip_comments(path: pathlib.Path, text: str) -> str:
    # Remove block comments first, then language-specific line comments.
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    if path.suffix == ".sql":
        text = re.sub(r"--[^\n]*", "", text)
    else:
        text = re.sub(r"//[^\n]*", "", text)
    return text

offenders = []
for path in targets:
    cleaned = strip_comments(path, path.read_text(encoding="utf-8"))
    if re.search(r"\buploaded_offset\b", cleaned):
        offenders.append(str(path.relative_to(root)))

if offenders:
    print("uploaded_offset appears in executable/schema content:", file=sys.stderr)
    for offender in offenders:
        print(f"  {offender}", file=sys.stderr)
    raise SystemExit(1)
PY_GATE
then
  fail "B1 correctness must not use uploaded_offset as durable progress truth"
fi
if grep -R -n 'UploadChunk' "$ROOT_DIR/gateway"; then
  fail "file payload leaked into Gateway; B1 data plane must stay outside Gateway"
fi
if grep -n 'std::mutex' "$ROOT_DIR/services/file/storage/LocalFilesystemStorage.cpp"; then
  fail "LocalFilesystemStorage correctness must not depend on process-local mutex"
fi
log "PASS architecture/correctness"

log "apply idempotent chunk-manifest migration 010"
MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp \
  -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" \
  -D "$MYSQL_DATABASE" < "$ROOT_DIR/db/migrations/010_create_file_upload_chunks.sql"
chunk_table="$(mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='im_file_upload_chunks';")"
[[ "$chunk_table" == "1" ]] || fail "im_file_upload_chunks missing after migration"
pk_columns="$(mysql_exec "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='im_file_upload_chunks' AND index_name='PRIMARY';")"
[[ "$pk_columns" == "2" ]] || fail "chunk manifest composite primary key incomplete"
log "PASS schema-migration"

log "configure/build focused B1 targets (-j${BUILD_JOBS})"
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

log "run real MySQL + local filesystem durable chunk gate"
"$BUILD_DIR/file_chunk_integration_tests" "$SOURCE_CONFIG" "$STORAGE_ROOT" \
  | tee "$ARTIFACT_DIR/file-chunk-integration.log"
grep -q '\[PASS\] M18-B1 durable chunk/filesystem integration' \
  "$ARTIFACT_DIR/file-chunk-integration.log" || fail "B1 chunk integration marker missing"
log "PASS real-mysql-filesystem-chunk"

log "ordinary regression build/test (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" | tee "$ARTIFACT_DIR/build-ordinary.log"
ctest --test-dir "$BUILD_DIR" --output-on-failure | tee "$ARTIFACT_DIR/ctest-ordinary.log"
log "PASS ordinary-regression"

printf '\n[M18-B1 PASS] Durable chunk identity + local filesystem storage accepted.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
