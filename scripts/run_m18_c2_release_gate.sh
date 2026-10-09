#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE_CONFIG="${1:-config/gateway-a.local.json}"
if [[ "$SOURCE_CONFIG" != /* ]]; then SOURCE_CONFIG="$ROOT_DIR/$SOURCE_CONFIG"; fi
ARTIFACT_DIR="${2:-$ROOT_DIR/artifacts/m18-c2-$(date +%Y%m%d-%H%M%S)}"
BUILD_DIR="${TINYIMX_BUILD_DIR:-$ROOT_DIR/build/linux-debug}"
ACTOR_USER_ID="${TINYIMX_M18_C2_ACTOR_USER_ID:-10001}"
mkdir -p "$ARTIFACT_DIR"

log(){ printf '[M18-C2] %s\n' "$*"; }
fail(){ printf '[M18-C2] FAIL: %s\n' "$*" >&2; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"; }
for c in python3 mysql ss sha256sum ps; do require_cmd "$c"; done
[[ -f "$SOURCE_CONFIG" ]] || fail "config missing: $SOURCE_CONFIG"
for bin in file_service_demo file_transfer_release_e2e_client file_download_stress_client; do
  [[ -x "$BUILD_DIR/$bin" ]] || fail "missing executable: $BUILD_DIR/$bin"
done

readarray -t MYSQL_FIELDS < <(python3 - "$SOURCE_CONFIG" <<'PY'
import json,sys
with open(sys.argv[1],encoding='utf-8') as f:o=json.load(f)
m=o.get('mysql',{})
for k,d in [('host','127.0.0.1'),('port',3306),('user','root'),('password',''),('database','tinyimx')]:print(m.get(k,d))
PY
)
MYSQL_HOST="${MYSQL_FIELDS[0]}"; MYSQL_PORT="${MYSQL_FIELDS[1]}"; MYSQL_USER="${MYSQL_FIELDS[2]}"; MYSQL_PASSWORD="${MYSQL_FIELDS[3]}"; MYSQL_DATABASE="${MYSQL_FIELDS[4]}"
mysql_exec(){ MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp -h "$MYSQL_HOST" -P "$MYSQL_PORT" -u "$MYSQL_USER" -D "$MYSQL_DATABASE" --batch --skip-column-names -e "$1"; }

[[ "$(mysql_exec "SELECT COUNT(*) FROM im_users WHERE user_id=${ACTOR_USER_ID};")" == "1" ]] || fail "actor user ${ACTOR_USER_ID} does not exist"

PORT="$(python3 - <<'PY'
import socket
s=socket.socket();s.bind(('127.0.0.1',0));print(s.getsockname()[1]);s.close()
PY
)"
TARGET="127.0.0.1:${PORT}"
STATE_DIR="$ARTIFACT_DIR/e2e-state"
STORAGE_ROOT="$ARTIFACT_DIR/file-storage"
mkdir -p "$STATE_DIR" "$STORAGE_ROOT"
TMP_CONFIG="$(mktemp /tmp/tinyimx-m18c2-config-XXXXXX.json)"
python3 - "$SOURCE_CONFIG" "$TMP_CONFIG" <<'PY'
import json,sys
with open(sys.argv[1],encoding='utf-8') as f:o=json.load(f)
o.setdefault('zookeeper',{})['enable']=False
with open(sys.argv[2],'w',encoding='utf-8') as f:json.dump(o,f)
PY
chmod 600 "$TMP_CONFIG"

SERVER_PID=""; FILE_ID=""; UPLOAD_ID=""; STORAGE_KEY=""
stop_server(){
  if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    for _ in $(seq 1 80); do kill -0 "$SERVER_PID" 2>/dev/null || break; sleep 0.1; done
    kill -KILL "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  SERVER_PID=""
}
cleanup_db(){
  if [[ "${UPLOAD_ID:-}" =~ ^[1-9][0-9]*$ ]]; then mysql_exec "DELETE FROM im_file_upload_chunks WHERE upload_id=${UPLOAD_ID}; DELETE FROM im_file_upload_sessions WHERE upload_id=${UPLOAD_ID};" >/dev/null 2>&1 || true; fi
  if [[ "${FILE_ID:-}" =~ ^[1-9][0-9]*$ ]]; then mysql_exec "DELETE FROM im_files WHERE file_id=${FILE_ID};" >/dev/null 2>&1 || true; fi
}
trap 'stop_server; cleanup_db; rm -f "$TMP_CONFIG"' EXIT

start_server(){
  local log_file="$1"
  "$BUILD_DIR/file_service_demo" "$TMP_CONFIG" "$TARGET" "$STORAGE_ROOT" >"$log_file" 2>&1 &
  SERVER_PID=$!
  for _ in $(seq 1 100); do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then cat "$log_file" >&2; fail "FileService exited during startup"; fi
    if ss -ltn | grep -q ":${PORT}[[:space:]]"; then return 0; fi
    sleep 0.1
  done
  cat "$log_file" >&2
  fail "FileService listener ${TARGET} did not become ready"
}

log "start release FileService process #1"
start_server "$ARTIFACT_DIR/file-service-boot1.log"
PID1="$SERVER_PID"
{
  echo "target=$TARGET"; echo "pid1=$PID1"; echo "binary_sha256=$(sha256sum "$BUILD_DIR/file_service_demo" | awk '{print $1}')";
  ps -o pid,ppid,lstart,etime,args -p "$PID1"; ss -ltnp | grep ":${PORT}[[:space:]]" || true
} > "$ARTIFACT_DIR/deployment-evidence.txt"

"$BUILD_DIR/file_transfer_release_e2e_client" prepare "$TARGET" "$STATE_DIR" "$ACTOR_USER_ID" | tee "$ARTIFACT_DIR/e2e-prepare.log"
grep -q '\[PASS\] M18-C2 upload->finalize->partial-download before restart' "$ARTIFACT_DIR/e2e-prepare.log" || fail "prepare E2E marker missing"
FILE_ID="$(sed -n 's/^file_id=//p' "$STATE_DIR/state.env")"; UPLOAD_ID="$(sed -n 's/^upload_id=//p' "$STATE_DIR/state.env")"
[[ "$FILE_ID" =~ ^[1-9][0-9]*$ && "$UPLOAD_ID" =~ ^[1-9][0-9]*$ ]] || fail "invalid durable E2E ids"
STORAGE_KEY="$(mysql_exec "SELECT storage_key FROM im_files WHERE file_id=${FILE_ID} AND owner_user_id=${ACTOR_USER_ID};")"
[[ "$STORAGE_KEY" == "files/${FILE_ID}" ]] || fail "unexpected server-generated storage_key: $STORAGE_KEY"
FINAL_OBJECT="$STORAGE_ROOT/$STORAGE_KEY"
[[ -f "$FINAL_OBJECT" ]] || fail "final object missing after finalize"

log "interrupt download and stop FileService process #1"
stop_server
for _ in $(seq 1 30); do ss -ltn | grep -q ":${PORT}[[:space:]]" || break; sleep 0.1; done
if ss -ltn | grep -q ":${PORT}[[:space:]]"; then fail "listener remained after service shutdown"; fi
if "$BUILD_DIR/file_transfer_release_e2e_client" verify-recovered "$TARGET" "$STATE_DIR" >"$ARTIFACT_DIR/server-down-probe.log" 2>&1; then
  fail "download unexpectedly succeeded while FileService was stopped"
fi
log "PASS service-down interruption observed"

log "restart same FileService against same MySQL + storage root"
start_server "$ARTIFACT_DIR/file-service-boot2.log"
PID2="$SERVER_PID"
[[ "$PID2" != "$PID1" ]] || fail "service restart reused same process id unexpectedly"
{
  echo "pid2=$PID2"; ps -o pid,ppid,lstart,etime,args -p "$PID2"; ss -ltnp | grep ":${PORT}[[:space:]]" || true
} >> "$ARTIFACT_DIR/deployment-evidence.txt"
"$BUILD_DIR/file_transfer_release_e2e_client" resume "$TARGET" "$STATE_DIR" | tee "$ARTIFACT_DIR/e2e-resume.log"
grep -q '\[PASS\] M18-C2 service-restart resume reconstructs exact immutable object' "$ARTIFACT_DIR/e2e-resume.log" || fail "restart/resume marker missing"
log "PASS process-restart-range-resume"

BACKUP="$ARTIFACT_DIR/final-object.backup"
cp --reflink=auto "$FINAL_OBJECT" "$BACKUP"
ORIGINAL_SHA="$(sha256sum "$FINAL_OBJECT" | awk '{print $1}')"
STATE_SHA="$(sed -n 's/^sha256=//p' "$STATE_DIR/state.env")"
[[ "$ORIGINAL_SHA" == "$STATE_SHA" ]] || fail "published object checksum disagrees with E2E state"

log "fault: AVAILABLE metadata + missing final object"
rm -f "$FINAL_OBJECT"
"$BUILD_DIR/file_transfer_release_e2e_client" expect-data-loss "$TARGET" "$STATE_DIR" | tee "$ARTIFACT_DIR/fault-missing-object.log"
cp --reflink=auto "$BACKUP" "$FINAL_OBJECT"
"$BUILD_DIR/file_transfer_release_e2e_client" verify-recovered "$TARGET" "$STATE_DIR" | tee "$ARTIFACT_DIR/recovery-missing-object.log"

log "fault: same-size final-object corruption"
python3 - "$FINAL_OBJECT" <<'PY'
import os,sys
p=sys.argv[1]
with open(p,'r+b',buffering=0) as f:
    f.seek(17); b=f.read(1)
    if not b: raise SystemExit('object too small')
    f.seek(17); f.write(bytes([b[0]^0x5A])); f.flush(); os.fsync(f.fileno())
PY
[[ "$(stat -c %s "$FINAL_OBJECT")" == "$(stat -c %s "$BACKUP")" ]] || fail "corruption fault changed object size"
"$BUILD_DIR/file_transfer_release_e2e_client" expect-data-loss "$TARGET" "$STATE_DIR" | tee "$ARTIFACT_DIR/fault-same-size-corruption.log"
cp --reflink=auto "$BACKUP" "$FINAL_OBJECT"
"$BUILD_DIR/file_transfer_release_e2e_client" verify-recovered "$TARGET" "$STATE_DIR" | tee "$ARTIFACT_DIR/recovery-corruption.log"
[[ "$(sha256sum "$FINAL_OBJECT" | awk '{print $1}')" == "$STATE_SHA" ]] || fail "object recovery checksum mismatch"
log "PASS object-loss-corruption-recovery"

log "bounded concurrent range stress: 4/8/16 readers"
for threads in 4 8 16; do
  "$BUILD_DIR/file_download_stress_client" "$TARGET" "$STATE_DIR" "$threads" 32 131072 \
    | tee "$ARTIFACT_DIR/stress-${threads}.log"
  grep -q 'failures=0' "$ARTIFACT_DIR/stress-${threads}.log" || fail "stress ${threads} reported failures"
  max_response="$(sed -n 's/.*max_response=\([0-9][0-9]*\).*/\1/p' "$ARTIFACT_DIR/stress-${threads}.log" | tail -1)"
  [[ "$max_response" =~ ^[1-9][0-9]*$ ]] || fail "stress ${threads} response bound evidence missing"
  (( max_response <= 1048576 )) || fail "stress ${threads} exceeded 1MiB response bound: $max_response"
done
log "PASS bounded-concurrency-stress"

{
  echo "file_id=$FILE_ID upload_id=$UPLOAD_ID actor_user_id=$ACTOR_USER_ID storage_key=$STORAGE_KEY"
  mysql_exec "SELECT file_id, owner_user_id, total_size, status, version, expected_checksum, verified_checksum, storage_backend, storage_key FROM im_files WHERE file_id=${FILE_ID};"
  mysql_exec "SELECT upload_id, file_id, owner_user_id, status, version, total_size, chunk_size FROM im_file_upload_sessions WHERE upload_id=${UPLOAD_ID};"
  mysql_exec "SELECT COUNT(*), MIN(status), MAX(status), SUM(chunk_size) FROM im_file_upload_chunks WHERE upload_id=${UPLOAD_ID};"
  echo "object_size=$(stat -c %s "$FINAL_OBJECT") object_sha256=$(sha256sum "$FINAL_OBJECT" | awk '{print $1}')"
} > "$ARTIFACT_DIR/durable-recovery-evidence.txt"

log "PASS deployment/restart/fault/stress release gate"
printf '\n[M18-C2 PASS] Process restart + fault recovery + bounded concurrent download release gate accepted.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
