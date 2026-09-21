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
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m18-a2-${TIMESTAMP}"
mkdir -p "$ARTIFACT_DIR"
TMP_CONFIG="$ARTIFACT_DIR/gateway-static.json"
USER_PORT="${TINYIMX_M18_A2_USER_PORT:-56252}"
FILE_PORT="${TINYIMX_M18_A2_FILE_PORT:-56255}"
GATEWAY_PORT="${TINYIMX_M18_A2_GATEWAY_PORT:-19180}"
USER_TARGET="127.0.0.1:${USER_PORT}"
FILE_TARGET="127.0.0.1:${FILE_PORT}"
USERNAME="${TINYIMX_M18_A2_USERNAME:-user10001}"
PASSWORD="${TINYIMX_M18_A2_PASSWORD:-123456}"
USER_PID=""; FILE_PID=""; GATEWAY_PID=""

log(){ printf '[M18-A2] %s\n' "$*"; }
fail(){ log "FAIL: $*"; exit 1; }
stop_pid(){ local pid="${1:-}"; [[ -z "$pid" ]] && return 0; if kill -0 "$pid" 2>/dev/null; then kill -TERM "$pid" 2>/dev/null || true; for _ in $(seq 1 50); do kill -0 "$pid" 2>/dev/null || return 0; sleep 0.1; done; kill -KILL "$pid" 2>/dev/null || true; fi; }
cleanup(){ stop_pid "$GATEWAY_PID"; stop_pid "$FILE_PID"; stop_pid "$USER_PID"; }
trap cleanup EXIT
wait_port(){ local port="$1" pid="$2" name="$3"; for _ in $(seq 1 100); do kill -0 "$pid" 2>/dev/null || fail "$name exited before ready"; python3 - "$port" <<'PY' >/dev/null 2>&1 && return 0 || true
import socket,sys
s=socket.socket(); s.settimeout(.1)
try: s.connect(('127.0.0.1',int(sys.argv[1]))); ok=True
except OSError: ok=False
finally: s.close()
raise SystemExit(0 if ok else 1)
PY
sleep .1; done; fail "$name port $port not ready"; }

[[ -f "$SOURCE_CONFIG" ]] || fail "missing config: $SOURCE_CONFIG"

log "static architecture/security gate"
grep -q 'kFile' "$ROOT_DIR/services/rpc/ServiceEndpointProvider.h" || fail "ServiceKind::kFile missing"
grep -q 'std::array<std::atomic<std::uint64_t>, 5>' "$ROOT_DIR/services/rpc/ZooKeeperServiceEndpointProvider.h" || fail "five-service discovery counter missing"
grep -q '"file"' "$ROOT_DIR/services/registry/zookeeper/ZooKeeperServiceDiscovery.cpp" || fail "FileService discovery watch missing"
grep -q 'SetFileRpcClient' "$ROOT_DIR/gateway/GatewayServer.h" || fail "Gateway FileRpcClient dependency missing"
grep -q 'actor identity is authoritative only from SessionManager' "$ROOT_DIR/gateway/GatewayServer.cpp" || fail "authenticated actor boundary marker missing"
grep -q 'TINYIMX_FILE_RPC_TARGET' "$ROOT_DIR/examples/gateway_demo.cpp" || fail "static FileService bootstrap missing"
grep -q 'instance.service_name = "file"' "$ROOT_DIR/examples/file_service_demo.cpp" || fail "FileService ZooKeeper registration missing"
log "PASS architecture/security"

log "configure/build focused A2 targets (-j${BUILD_JOBS})"
(cd "$ROOT_DIR" && cmake --preset linux-debug)
cmake --build "$BUILD_DIR" --target \
  file_rpc_client_tests gateway_demo gateway_file_client_demo \
  file_service_demo user_service_demo user_password_seed_demo \
  zookeeper_service_discovery_integration_tests -j"$BUILD_JOBS"
log "PASS focused-build"

log "focused RPC/protocol CTest"
ctest --test-dir "$BUILD_DIR" --output-on-failure -R \
  '^(file_rpc_client_tests|rpc_contract_tests|tinyimx.protocol)$' \
  | tee "$ARTIFACT_DIR/ctest-focused.log"
log "PASS focused-ctest"

log "prepare static-discovery runtime config"
python3 - "$SOURCE_CONFIG" "$TMP_CONFIG" "$GATEWAY_PORT" <<'PY'
import json,sys
src,dst,port=sys.argv[1],sys.argv[2],int(sys.argv[3])
with open(src,encoding='utf-8') as f: x=json.load(f)
x.setdefault('server',{})['host']='127.0.0.1'; x['server']['port']=port
x.setdefault('app',{})['instance_id']='tinyimx-gateway-m18-a2'
x.setdefault('zookeeper',{})['enable']=False
x.setdefault('service_discovery',{})['provider']='static'
with open(dst,'w',encoding='utf-8') as f: json.dump(x,f,ensure_ascii=False,indent=2)
PY

# Migration 009 is idempotent and A1 already proved its semantics; retain schema availability.
python3 - "$TMP_CONFIG" "$ROOT_DIR/db/migrations/009_create_file_domain.sql" <<'PY'
import json,subprocess,os,sys
cfg=json.load(open(sys.argv[1],encoding='utf-8'))['mysql']; env=os.environ.copy(); env['MYSQL_PWD']=cfg.get('password','')
cmd=['mysql','--protocol=tcp','-h',cfg['host'],'-P',str(cfg['port']),'-u',cfg['user'],'-D',cfg['database']]
subprocess.run(cmd,stdin=open(sys.argv[2],'rb'),env=env,check=True)
PY

log "seed deterministic login password"
"$BUILD_DIR/user_password_seed_demo" "$TMP_CONFIG" >"$ARTIFACT_DIR/password-seed.log" 2>&1

log "start real UserService + FileService + Gateway (static discovery)"
TINYIMX_USER_LISTEN_TARGET="$USER_TARGET" "$BUILD_DIR/user_service_demo" "$TMP_CONFIG" >"$ARTIFACT_DIR/user-service.log" 2>&1 & USER_PID=$!
wait_port "$USER_PORT" "$USER_PID" "UserService"
TINYIMX_FILE_LISTEN_TARGET="$FILE_TARGET" "$BUILD_DIR/file_service_demo" "$TMP_CONFIG" >"$ARTIFACT_DIR/file-service.log" 2>&1 & FILE_PID=$!
wait_port "$FILE_PORT" "$FILE_PID" "FileService"
TINYIMX_USER_RPC_TARGET="$USER_TARGET" TINYIMX_FILE_RPC_TARGET="$FILE_TARGET" \
  "$BUILD_DIR/gateway_demo" "$TMP_CONFIG" >"$ARTIFACT_DIR/gateway.log" 2>&1 & GATEWAY_PID=$!
wait_port "$GATEWAY_PORT" "$GATEWAY_PID" "Gateway"

CLIENT_UPLOAD_ID="m18-a2-${TIMESTAMP}"
BEGIN_BODY=$(python3 - "$CLIENT_UPLOAD_ID" <<'PY'
import json,sys
print(json.dumps({'actor_user_id':999999,'owner_user_id':999999,'client_upload_id':sys.argv[1],
 'file_name':'m18-a2.txt','content_type':'text/plain','total_size':4096,
 'checksum_algorithm':'sha256','expected_checksum':'a'*64,'preferred_chunk_size':262144}))
PY
)
"$BUILD_DIR/gateway_file_client_demo" 127.0.0.1 "$GATEWAY_PORT" "$USERNAME" "$PASSWORD" begin "$BEGIN_BODY" \
  | tee "$ARTIFACT_DIR/begin.json"
UPLOAD_ID=$(python3 - "$ARTIFACT_DIR/begin.json" <<'PY'
import json,sys
x=json.loads(open(sys.argv[1],encoding='utf-8').read().strip().splitlines()[-1])
assert x['success'] is True
assert x['file']['owner_user_id']==10001 and x['session']['owner_user_id']==10001
assert x['result'] in ('created','reused')
print(x['session']['upload_id'])
PY
)
log "PASS authenticated actor overrides spoofed body identity, upload_id=${UPLOAD_ID}"

GET_BODY=$(printf '{"actor_user_id":999999,"upload_id":%s}' "$UPLOAD_ID")
"$BUILD_DIR/gateway_file_client_demo" 127.0.0.1 "$GATEWAY_PORT" "$USERNAME" "$PASSWORD" get "$GET_BODY" \
  | tee "$ARTIFACT_DIR/get.json"
python3 - "$ARTIFACT_DIR/get.json" "$UPLOAD_ID" <<'PY'
import json,sys
x=json.loads(open(sys.argv[1],encoding='utf-8').read().strip().splitlines()[-1]); uid=int(sys.argv[2])
assert x['success'] is True and x['session']['upload_id']==uid
assert x['file']['owner_user_id']==10001 and x['session']['owner_user_id']==10001
PY

CANCEL_BODY=$(printf '{"owner_user_id":999999,"upload_id":%s}' "$UPLOAD_ID")
"$BUILD_DIR/gateway_file_client_demo" 127.0.0.1 "$GATEWAY_PORT" "$USERNAME" "$PASSWORD" cancel "$CANCEL_BODY" \
  | tee "$ARTIFACT_DIR/cancel.json"
python3 - "$ARTIFACT_DIR/cancel.json" <<'PY'
import json,sys
x=json.loads(open(sys.argv[1],encoding='utf-8').read().strip().splitlines()[-1])
assert x['success'] is True and x['file']['owner_user_id']==10001
assert x['result'] in ('applied','reused') and x['session']['status']=='canceled'
PY
log "PASS real TCP Begin/Get/Cancel File control plane"

stop_pid "$GATEWAY_PID"; GATEWAY_PID=""
stop_pid "$FILE_PID"; FILE_PID=""
stop_pid "$USER_PID"; USER_PID=""

log "ZooKeeper FileService discovery integration"
ZK_CONNECT=$(python3 - "$SOURCE_CONFIG" <<'PY'
import json,sys
x=json.load(open(sys.argv[1],encoding='utf-8')); print(x.get('zookeeper',{}).get('connect_string','127.0.0.1:2181'))
PY
)
"$BUILD_DIR/zookeeper_service_discovery_integration_tests" "$ZK_CONNECT" "/tinyimx-m18-a2-${TIMESTAMP}/services" \
  | tee "$ARTIFACT_DIR/zookeeper-file-discovery.log"
grep -q 'ServiceKind::kFile resolves discovered FileService' "$ARTIFACT_DIR/zookeeper-file-discovery.log" || fail "ZooKeeper FileService route gate missing"
log "PASS zookeeper-discovery"

log "ordinary regression build/test (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" | tee "$ARTIFACT_DIR/build-ordinary.log"
ctest --test-dir "$BUILD_DIR" --output-on-failure | tee "$ARTIFACT_DIR/ctest-ordinary.log"
log "PASS ordinary-regression"

printf '\n[M18-A2 PASS] Gateway authenticated File control plane accepted.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
