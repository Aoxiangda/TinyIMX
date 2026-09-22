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
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m18-b2-${TIMESTAMP}"
B1_STORAGE_ROOT="${ARTIFACT_DIR}/b1-storage-root"
B2_STORAGE_ROOT="${ARTIFACT_DIR}/b2-storage-root"
mkdir -p "$ARTIFACT_DIR" "$B1_STORAGE_ROOT" "$B2_STORAGE_ROOT"

log(){ printf '[M18-B2] %s\n' "$*"; }
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
grep -q 'rpc GetUploadProgress' "$ROOT_DIR/proto/tinyimx/file/v1/file_service.proto" || fail "GetUploadProgress RPC missing"
grep -q 'rpc FinalizeUpload' "$ROOT_DIR/proto/tinyimx/file/v1/file_service.proto" || fail "FinalizeUpload RPC missing"
grep -q 'ComposeObjectAtomically' "$ROOT_DIR/services/file/storage/FileStoragePort.h" || fail "final-object storage contract missing"
grep -q 'BeginFinalizeOnConnection' "$ROOT_DIR/services/repository/FileRepository.cpp" || fail "durable finalize-start transition missing"
grep -q 'CompleteFinalizeOnConnection' "$ROOT_DIR/services/repository/FileRepository.cpp" || fail "durable finalize-complete transition missing"
grep -q 'FailFinalizeChecksumOnConnection' "$ROOT_DIR/services/repository/FileRepository.cpp" || fail "checksum-failure durable transition missing"
grep -q 'chk_im_file_upload_chunks_size' "$ROOT_DIR/db/migrations/010_create_file_upload_chunks.sql" || fail "accepted B1 constraint naming fix missing"
if ! python3 - "$ROOT_DIR" <<'PY_GATE'
import pathlib,re,sys
root=pathlib.Path(sys.argv[1])
targets=[root/'db/migrations/010_create_file_upload_chunks.sql']
targets.extend((root/'services/file').rglob('*.h'))
targets.extend((root/'services/file').rglob('*.cpp'))
def strip_comments(path,text):
    text=re.sub(r'/\*.*?\*/','',text,flags=re.S)
    text=re.sub(r'--[^\n]*' if path.suffix=='.sql' else r'//[^\n]*','',text)
    return text
off=[]
for path in targets:
    if re.search(r'\buploaded_offset\b',strip_comments(path,path.read_text(encoding='utf-8'))):
        off.append(str(path.relative_to(root)))
if off:
    print('uploaded_offset appears in executable/schema content:',*off,sep='\n  ',file=sys.stderr)
    raise SystemExit(1)
PY_GATE
then
  fail "resume/finalize correctness must derive from durable chunk manifest, not uploaded_offset"
fi
if grep -R -E -n 'UploadChunk|FinalizeUpload' "$ROOT_DIR/gateway"; then
  fail "file data-plane payload/finalize leaked into Gateway"
fi
if grep -n 'std::mutex' "$ROOT_DIR/services/file/storage/LocalFilesystemStorage.cpp"; then
  fail "filesystem data-plane correctness must not depend on process-local mutex"
fi
log "PASS architecture/correctness"

log "verify retained A1/B1 schema"
for table in im_files im_file_upload_sessions im_file_upload_chunks; do
  count="$(mysql_exec "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='${table}';")"
  [[ "$count" == "1" ]] || fail "missing retained table: $table"
done
pk_columns="$(mysql_exec "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='im_file_upload_chunks' AND index_name='PRIMARY';")"
[[ "$pk_columns" == "2" ]] || fail "B1 durable chunk composite key missing"
log "PASS retained-schema"

log "configure/build focused B2 targets (-j${BUILD_JOBS})"
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

log "run M18-B2 resume/finalize/whole-checksum/crash-window gate"
"$BUILD_DIR/file_finalize_integration_tests" "$SOURCE_CONFIG" "$B2_STORAGE_ROOT" \
  | tee "$ARTIFACT_DIR/file-finalize-integration.log"
grep -q '\[PASS\] M18-B2 resume/finalize/filesystem integration' \
  "$ARTIFACT_DIR/file-finalize-integration.log" || fail "B2 finalize integration marker missing"
log "PASS real-mysql-resume-finalize"

log "ordinary regression build/test (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" | tee "$ARTIFACT_DIR/build-ordinary.log"
ctest --test-dir "$BUILD_DIR" --output-on-failure | tee "$ARTIFACT_DIR/ctest-ordinary.log"
log "PASS ordinary-regression"

printf '\n[M18-B2 PASS] Durable resume + finalize + whole-file verification accepted.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
