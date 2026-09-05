#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
export VCPKG_ROOT="${VCPKG_ROOT:-${ROOT_DIR}/toolchains/vcpkg-tinyimx}"
export VCPKG_BINARY_SOURCES="${VCPKG_BINARY_SOURCES:-clear;default,readwrite}"
unset X_VCPKG_ASSET_SOURCES || true
unset VCPKG_DOWNLOADS || true
BUILD_DIR="${TINYIMX_BUILD_DIR:-${ROOT_DIR}/build/linux-debug}"
SOURCE_CONFIG="${1:-${ROOT_DIR}/config/gateway-a.local.json}"
[[ -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]] || { echo "[FAIL] vcpkg toolchain missing: ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" >&2; exit 1; }
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m14-c3-${TIMESTAMP}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tinyimx-m14-c3-XXXXXX")"

USER_PID=""; MESSAGE_PID=""; GATEWAY_PID=""
USER_PORT=""; MESSAGE_PORT=""; GATEWAY_PORT=""
USER_TARGET=""; MESSAGE_TARGET=""; GATEWAY_CONFIG=""
mkdir -p "$ARTIFACT_DIR"
chmod 700 "$TMP_DIR"

log(){ printf '[M14-C3] %s\n' "$*"; }
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

free_port(){ python3 - <<'PY'
import socket
s=socket.socket(); s.bind(('127.0.0.1',0)); print(s.getsockname()[1]); s.close()
PY
}

wait_port(){
  python3 - "$1" "$2" "$3" <<'PY'
import os,socket,sys,time
port=int(sys.argv[1]); pid=int(sys.argv[2]); label=sys.argv[3]
end=time.monotonic()+15
while time.monotonic()<end:
  try: os.kill(pid,0)
  except OSError: raise SystemExit(f'{label} exited before readiness')
  s=socket.socket(); s.settimeout(.2)
  try:
    s.connect(('127.0.0.1',port)); s.close(); raise SystemExit(0)
  except OSError:
    s.close(); time.sleep(.1)
raise SystemExit(f'{label} readiness timeout')
PY
}

stop_pid(){
  local pid="${1:-}" signal="${2:-TERM}"
  [[ -z "$pid" ]] && return 0
  if kill -0 "$pid" 2>/dev/null; then
    kill "-${signal}" "$pid" 2>/dev/null || true
    for _ in $(seq 1 80); do kill -0 "$pid" 2>/dev/null || break; sleep .1; done
    kill -0 "$pid" 2>/dev/null && kill -KILL "$pid" 2>/dev/null || true
  fi
  wait "$pid" 2>/dev/null || true
}

cleanup(){
  set +e
  stop_pid "$GATEWAY_PID" INT
  stop_pid "$MESSAGE_PID" TERM
  stop_pid "$USER_PID" TERM
  rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT INT TERM

[[ -f "$SOURCE_CONFIG" ]] || fail "missing config: $SOURCE_CONFIG"
for c in cmake python3 mysql grep; do require_cmd "$c"; done

MYSQL_HOST="${TINYIMX_MYSQL_HOST:-$(json_get "$SOURCE_CONFIG" mysql host)}"
MYSQL_PORT="${TINYIMX_MYSQL_PORT:-$(json_get "$SOURCE_CONFIG" mysql port)}"
MYSQL_DATABASE="${TINYIMX_MYSQL_DATABASE:-$(json_get "$SOURCE_CONFIG" mysql database)}"
MYSQL_USER="${TINYIMX_MYSQL_USER:-$(json_get "$SOURCE_CONFIG" mysql user)}"
MYSQL_PASSWORD="${TINYIMX_MYSQL_PASSWORD:-$(json_get "$SOURCE_CONFIG" mysql password)}"
mysql_scalar(){
  MYSQL_PWD="$MYSQL_PASSWORD" mysql --protocol=tcp -h "$MYSQL_HOST" -P "$MYSQL_PORT" \
    -u "$MYSQL_USER" -D "$MYSQL_DATABASE" --batch --skip-column-names -e "$1"
}

log "static ownership / contract gate"
for rpc in GetPrivateMessage CountPending ListPendingAfter ConfirmReceiver ConfirmReceiverBatch MarkDialogRead; do
  grep -q "rpc ${rpc}" "$ROOT_DIR/proto/tinyimx/message/v1/message_service.proto" || fail "proto missing ${rpc}"
  grep -q "MessageRpcClient::${rpc}" "$ROOT_DIR/services/rpc/MessageRpcClient.cpp" || fail "client missing ${rpc}"
  grep -q "MessageServiceImpl::${rpc}" "$ROOT_DIR/services/message/service/MessageServiceImpl.cpp" || fail "service missing ${rpc}"
done
if grep -E -n 'message_repository_|HasMessageRepository|SetMessageRepository|MessageRepository' \
  "$ROOT_DIR/gateway/GatewayServer.cpp" "$ROOT_DIR/gateway/GatewayServer.h" "$ROOT_DIR/examples/gateway_demo.cpp"; then
  fail "Gateway still owns durable MessageRepository"
fi
if grep -E -n 'packet_seq|delivery_seq|session_epoch|connection_id' \
  "$ROOT_DIR/proto/tinyimx/message/v1/message_service.proto"; then
  fail "MessageService contract leaked Gateway runtime identity"
fi
grep -q 'read_state_uncertain' "$ROOT_DIR/gateway/GatewayServer.cpp" || fail "Read uncertainty mapping missing"
grep -q 'TINYIMX_FAULT_MESSAGE_CONFIRM_POST_COMMIT_DELAY_MS' "$ROOT_DIR/services/message/service/MessageServiceImpl.cpp" || fail "Confirm uncertainty seam missing"
grep -q 'TINYIMX_FAULT_MESSAGE_MARK_READ_POST_COMMIT_DELAY_MS' "$ROOT_DIR/services/message/service/MessageServiceImpl.cpp" || fail "Read uncertainty seam missing"

log "configure/build C3 targets (-j${BUILD_JOBS})"
(cd "$ROOT_DIR" && cmake --preset linux-debug)
cmake --build "$BUILD_DIR" --target \
  rpc_contract_tests message_application_service_tests message_service_integration_tests \
  message_service_demo user_service_demo gateway_demo gateway_session_client_demo \
  gateway_pending_replay_pagination_demo gateway_login_auth_client_demo \
  -j"$BUILD_JOBS"

log "contract/application/integration gates"
"$BUILD_DIR/rpc_contract_tests" | tee "$ARTIFACT_DIR/rpc-contract.log"
"$BUILD_DIR/message_application_service_tests" | tee "$ARTIFACT_DIR/message-application.log"
"$BUILD_DIR/message_service_integration_tests" | tee "$ARTIFACT_DIR/message-integration.log"
grep -q 'total_failed=0' "$ARTIFACT_DIR/rpc-contract.log"
grep -q 'failed=0' "$ARTIFACT_DIR/message-application.log"
grep -q 'failed=0' "$ARTIFACT_DIR/message-integration.log"

USER_PORT="$(free_port)"; MESSAGE_PORT="$(free_port)"; GATEWAY_PORT="$(free_port)"
USER_TARGET="127.0.0.1:${USER_PORT}"; MESSAGE_TARGET="127.0.0.1:${MESSAGE_PORT}"
GATEWAY_CONFIG="$TMP_DIR/gateway.json"
python3 - "$SOURCE_CONFIG" "$GATEWAY_CONFIG" "$GATEWAY_PORT" "$ARTIFACT_DIR" <<'PY'
import json,os,sys
src,dst,port,art=sys.argv[1:]
with open(src,encoding='utf-8') as f:r=json.load(f)
r.setdefault('server',{})['host']='127.0.0.1'; r['server']['port']=int(port)
r.setdefault('gateway_registry',{})['enable']=False
r.setdefault('business_runtime',{})['default_deadline_ms']=3000
r.setdefault('app',{})['instance_id']=f'gateway-m14-c3-{os.getpid()}'
r.setdefault('logger',{})['file']=os.path.join(art,'gateway-internal.log'); r['logger']['console']=True
with open(dst,'w',encoding='utf-8') as f: json.dump(r,f,indent=2)
PY

start_user(){ local f="$ARTIFACT_DIR/user.log"; TINYIMX_USER_LISTEN_TARGET="$USER_TARGET" "$BUILD_DIR/user_service_demo" "$SOURCE_CONFIG" >"$f" 2>&1 & USER_PID=$!; wait_port "$USER_PORT" "$USER_PID" UserService; }
start_message(){ local f="$ARTIFACT_DIR/message.log"; TINYIMX_MESSAGE_LISTEN_TARGET="$MESSAGE_TARGET" "$BUILD_DIR/message_service_demo" "$SOURCE_CONFIG" >"$f" 2>&1 & MESSAGE_PID=$!; wait_port "$MESSAGE_PORT" "$MESSAGE_PID" MessageService; }
start_gateway(){ local with_message="${1:-1}" f="$ARTIFACT_DIR/gateway-${1:-1}.log"; if [[ "$with_message" == 1 ]]; then TINYIMX_USER_RPC_TARGET="$USER_TARGET" TINYIMX_MESSAGE_RPC_TARGET="$MESSAGE_TARGET" "$BUILD_DIR/gateway_demo" "$GATEWAY_CONFIG" >"$f" 2>&1 & else TINYIMX_USER_RPC_TARGET="$USER_TARGET" "$BUILD_DIR/gateway_demo" "$GATEWAY_CONFIG" >"$f" 2>&1 & fi; GATEWAY_PID=$!; wait_port "$GATEWAY_PORT" "$GATEWAY_PID" Gateway; }
stop_all(){ stop_pid "$GATEWAY_PID" INT; GATEWAY_PID=""; stop_pid "$MESSAGE_PID" TERM; MESSAGE_PID=""; stop_pid "$USER_PID" TERM; USER_PID=""; }

log "case 1: Chat -> ACK -> ReceiverConfirmed -> Read through MessageService"
start_user; start_message; start_gateway 1
"$BUILD_DIR/gateway_session_client_demo" 127.0.0.1 "$GATEWAY_PORT" | tee "$ARTIFACT_DIR/session-state.log"
grep -q 'Pending -> ReceiverConfirmed -> Read' "$ARTIFACT_DIR/session-state.log"
MID="$(sed -n 's/.*share stable message_id, message_id=\([0-9][0-9]*\).*/\1/p' "$ARTIFACT_DIR/session-state.log" | tail -n1)"
[[ -n "$MID" ]] || fail "could not parse state-chain message_id"
STATUS="$(mysql_scalar "SELECT delivery_status FROM im_private_messages WHERE message_id=${MID} LIMIT 1;")"
[[ "$STATUS" == 2 ]] || fail "expected M=${MID} delivery_status=2(Read), actual=${STATUS}"
log "PASS case 1: M=${MID} durable status=Read"
stop_all

log "case 2: pending replay crosses 100-row page boundary and ACK repairs durable state"
start_user; start_message; start_gateway 1
"$BUILD_DIR/gateway_pending_replay_pagination_demo" 127.0.0.1 "$GATEWAY_PORT" | tee "$ARTIFACT_DIR/pending-pagination.log"
grep -q 'pending replay crossed repository page boundary' "$ARTIFACT_DIR/pending-pagination.log"
FIRST="$(sed -n 's/.*first_message_id=\([0-9][0-9]*\).*/\1/p' "$ARTIFACT_DIR/pending-pagination.log" | tail -n1)"
LAST="$(sed -n 's/.*last_message_id=\([0-9][0-9]*\).*/\1/p' "$ARTIFACT_DIR/pending-pagination.log" | tail -n1)"
if [[ -n "$FIRST" && -n "$LAST" ]]; then
  PENDING="$(mysql_scalar "SELECT COUNT(*) FROM im_private_messages WHERE message_id BETWEEN ${FIRST} AND ${LAST} AND delivery_status=0;")"
  [[ "$PENDING" == 0 ]] || fail "pagination fixture still has ${PENDING} Pending rows"
fi
log "PASS case 2: pagination/replay completed"
stop_all

log "case 3: MessageService is enrichment-only for Login"
start_user; start_gateway 0
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" | tee "$ARTIFACT_DIR/login-degraded.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/login-degraded.log"
log "PASS case 3: auth remains available without MessageRpcClient"
stop_all

printf '\n[PASS] M14-C3 Message state vertical slice\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
