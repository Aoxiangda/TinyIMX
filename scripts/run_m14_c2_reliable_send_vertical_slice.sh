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
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m14-c2-${TIMESTAMP}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tinyimx-m14-c2-XXXXXX")"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"

SENDER_ID="${TINYIMX_C2_SENDER_ID:-10001}"
RECEIVER_ID="${TINYIMX_C2_RECEIVER_ID:-10002}"
SENDER_USERNAME="${TINYIMX_C2_SENDER_USERNAME:-user10001}"
SENDER_PASSWORD="${TINYIMX_C2_SENDER_PASSWORD:-123456}"

USER_PID=""
MESSAGE_PID=""
GATEWAY_PID=""
CLIENT_PID=""
USER_TARGET=""
MESSAGE_TARGET=""
GATEWAY_CONFIG=""

mkdir -p "${ARTIFACT_DIR}"
chmod 700 "${TMP_DIR}"

log() { printf '[M14-C2] %s\n' "$*"; }
fail() { log "FAIL: $*"; exit 1; }

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

json_get() {
  local file="$1" section="$2" key="$3"
  python3 - "$file" "$section" "$key" <<'PY'
import json, sys
with open(sys.argv[1], 'r', encoding='utf-8') as f:
    root = json.load(f)
value = root[sys.argv[2]][sys.argv[3]]
if isinstance(value, bool):
    print('1' if value else '0')
else:
    print(value)
PY
}

MYSQL_HOST="${TINYIMX_MYSQL_HOST:-$(json_get "$SOURCE_CONFIG" mysql host)}"
MYSQL_PORT="${TINYIMX_MYSQL_PORT:-$(json_get "$SOURCE_CONFIG" mysql port)}"
MYSQL_DATABASE="${TINYIMX_MYSQL_DATABASE:-$(json_get "$SOURCE_CONFIG" mysql database)}"
MYSQL_USER="${TINYIMX_MYSQL_USER:-$(json_get "$SOURCE_CONFIG" mysql user)}"
MYSQL_PASSWORD="${TINYIMX_MYSQL_PASSWORD:-$(json_get "$SOURCE_CONFIG" mysql password)}"

REDIS_HOST="${TINYIMX_REDIS_HOST:-$(json_get "$SOURCE_CONFIG" redis host)}"
REDIS_PORT="${TINYIMX_REDIS_PORT:-$(json_get "$SOURCE_CONFIG" redis port)}"
REDIS_DB="${TINYIMX_REDIS_DB:-$(json_get "$SOURCE_CONFIG" redis db)}"
REDIS_PASSWORD="${TINYIMX_REDIS_PASSWORD:-$(json_get "$SOURCE_CONFIG" redis password)}"

free_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

port_open() {
  python3 - "$1" "$2" <<'PY'
import socket, sys
s=socket.socket(); s.settimeout(0.2)
try:
    s.connect((sys.argv[1], int(sys.argv[2])))
except OSError:
    raise SystemExit(1)
else:
    raise SystemExit(0)
finally:
    s.close()
PY
}

wait_for_port() {
  local host="$1" port="$2" pid="$3" label="$4"
  python3 - "$host" "$port" "$pid" "$label" <<'PY'
import os, socket, sys, time
host, port_text, pid_text, label = sys.argv[1:]
port=int(port_text); pid=int(pid_text)
deadline=time.monotonic()+15.0
while time.monotonic()<deadline:
    try: os.kill(pid,0)
    except OSError: raise SystemExit(f"{label} exited before readiness")
    s=socket.socket(); s.settimeout(0.2)
    try:
        s.connect((host,port)); s.close(); raise SystemExit(0)
    except OSError:
        s.close(); time.sleep(0.1)
raise SystemExit(f"{label} readiness timeout")
PY
}

wait_for_port_closed() {
  local host="$1" port="$2"
  python3 - "$host" "$port" <<'PY'
import socket, sys, time
host, port_text=sys.argv[1:]; port=int(port_text)
deadline=time.monotonic()+10.0
while time.monotonic()<deadline:
    s=socket.socket(); s.settimeout(0.2)
    try: s.connect((host,port))
    except OSError: raise SystemExit(0)
    finally: s.close()
    time.sleep(0.1)
raise SystemExit("port remained open")
PY
}

wait_for_log_line() {
  local file="$1" pattern="$2" pid="$3"
  python3 - "$file" "$pattern" "$pid" <<'PY'
import os, sys, time
path, pattern, pid_text=sys.argv[1:]; pid=int(pid_text)
deadline=time.monotonic()+15.0
while time.monotonic()<deadline:
    if os.path.exists(path):
        with open(path,'r',encoding='utf-8',errors='replace') as h:
            if pattern in h.read(): raise SystemExit(0)
    try: os.kill(pid,0)
    except OSError: raise SystemExit("client exited before readiness marker")
    time.sleep(0.05)
raise SystemExit(f"timed out waiting for marker: {pattern}")
PY
}

stop_pid() {
  local pid="${1:-}" label="${2:-process}" signal="${3:-TERM}"
  [[ -z "$pid" ]] && return 0
  if kill -0 "$pid" 2>/dev/null; then
    kill "-${signal}" "$pid" 2>/dev/null || true
    for _ in $(seq 1 80); do
      if ! kill -0 "$pid" 2>/dev/null; then break; fi
      sleep 0.1
    done
    if kill -0 "$pid" 2>/dev/null; then
      log "$label did not stop on $signal; sending KILL"
      kill -KILL "$pid" 2>/dev/null || true
    fi
  fi
  wait "$pid" 2>/dev/null || true
}

cleanup() {
  set +e
  stop_pid "$CLIENT_PID" client TERM
  stop_pid "$GATEWAY_PID" gateway INT
  stop_pid "$MESSAGE_PID" message-service TERM
  stop_pid "$USER_PID" user-service TERM
  rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT INT TERM

mysql_scalar() {
  local sql="$1"
  MYSQL_PWD="$MYSQL_PASSWORD" mysql \
    --protocol=tcp \
    -h "$MYSQL_HOST" -P "$MYSQL_PORT" \
    -u "$MYSQL_USER" -D "$MYSQL_DATABASE" \
    --batch --skip-column-names -e "$sql"
}

redis_args() {
  :
}

redis_get() {
  local key="$1"
  local args=(-h "$REDIS_HOST" -p "$REDIS_PORT" -n "$REDIS_DB" --raw)
  if [[ -n "$REDIS_PASSWORD" ]]; then
    args+=(-a "$REDIS_PASSWORD" --no-auth-warning)
  fi
  redis-cli "${args[@]}" GET "$key"
}

redis_exists() {
  local key="$1"
  local args=(-h "$REDIS_HOST" -p "$REDIS_PORT" -n "$REDIS_DB" --raw)
  if [[ -n "$REDIS_PASSWORD" ]]; then
    args+=(-a "$REDIS_PASSWORD" --no-auth-warning)
  fi
  redis-cli "${args[@]}" EXISTS "$key"
}

redis_count() {
  local value
  value="$(redis_get "$1")"
  if [[ -z "$value" ]]; then printf '0\n'; else printf '%s\n' "$value"; fi
}

assert_db_identity() {
  local cid="$1" expected_count="$2" expected_mid="${3:-}"
  local row count min_mid max_mid
  row="$(mysql_scalar "SELECT COUNT(*), COALESCE(MIN(message_id),0), COALESCE(MAX(message_id),0) FROM im_private_messages WHERE from_user_id=${SENDER_ID} AND client_message_id='${cid}';")"
  IFS=$'\t' read -r count min_mid max_mid <<< "$row"
  [[ "$count" == "$expected_count" ]] || fail "C=$cid row count expected=$expected_count actual=$count"
  if [[ -n "$expected_mid" ]]; then
    [[ "$min_mid" == "$expected_mid" && "$max_mid" == "$expected_mid" ]] || \
      fail "C=$cid expected stable M=$expected_mid actual min=$min_mid max=$max_mid"
  fi
  printf '%s\n' "$min_mid"
}

[[ -f "$SOURCE_CONFIG" ]] || fail "missing config: $SOURCE_CONFIG"
for cmd in cmake python3 mysql redis-cli grep; do require_cmd "$cmd"; done

log "static C2 ownership / semantic scan"
grep -q 'rpc PersistPrivateMessage' "$ROOT_DIR/proto/tinyimx/message/v1/message_service.proto" || \
  fail "PersistPrivateMessage contract missing"
grep -q 'message_rpc_client_->PersistPrivateMessage' "$ROOT_DIR/gateway/GatewayServer.cpp" || \
  fail "Gateway production Persist RPC call missing"
if grep -n 'SavePrivateMessageIdempotent' "$ROOT_DIR/gateway/GatewayServer.cpp"; then
  fail "Gateway still directly owns C->M persistence"
fi
grep -q 'message_persistence_uncertain' "$ROOT_DIR/gateway/GatewayServer.cpp" || \
  fail "Gateway uncertain persistence vocabulary missing"
grep -q 'EnsurePrivateUnreadProjection' "$ROOT_DIR/services/cache/UnreadCountCache.cpp" || \
  fail "stable-M unread projection gate missing"
grep -q 'TINYIMX_FAULT_MESSAGE_PERSIST_POST_COMMIT_DELAY_MS' \
  "$ROOT_DIR/services/message/service/MessageServiceImpl.cpp" || \
  fail "post-commit uncertainty fault seam missing"
# Forward-compatible regression rule:
# C2 owns reliable persistence semantics, but later C3 is allowed to migrate
# ReceiverConfirm / Read / PendingReplay from the local MessageRepository to
# MessageService. A historical C2 tree may still use the local path; a C3+
# tree must use the RPC path and must not regain the old local durable call.
if grep -q 'message_rpc_client_->ConfirmReceiver' "$ROOT_DIR/gateway/GatewayServer.cpp"; then
  if grep -q 'MarkReceiverConfirmed(' "$ROOT_DIR/gateway/GatewayServer.cpp"; then
    fail "C3+ Gateway regained local ReceiverConfirm durable path"
  fi
else
  grep -q 'MarkReceiverConfirmed(' "$ROOT_DIR/gateway/GatewayServer.cpp" || \
    fail "ReceiverConfirm path missing: neither historical C2 local path nor C3+ RPC path found"
fi

if grep -q 'message_rpc_client_->MarkDialogRead' "$ROOT_DIR/gateway/GatewayServer.cpp"; then
  if grep -q 'MarkReadByDialog(' "$ROOT_DIR/gateway/GatewayServer.cpp"; then
    fail "C3+ Gateway regained local Read durable path"
  fi
else
  grep -q 'MarkReadByDialog(' "$ROOT_DIR/gateway/GatewayServer.cpp" || \
    fail "Read path missing: neither historical C2 local path nor C3+ RPC path found"
fi

if grep -q 'message_rpc_client_->ListPendingAfter' "$ROOT_DIR/gateway/GatewayServer.cpp"; then
  if grep -q 'ListPendingMessagesAfter(' "$ROOT_DIR/gateway/GatewayServer.cpp"; then
    fail "C3+ Gateway regained local Pending Replay durable path"
  fi
else
  grep -q 'ListPendingMessagesAfter(' "$ROOT_DIR/gateway/GatewayServer.cpp" || \
    fail "Pending Replay path missing: neither historical C2 local path nor C3+ RPC path found"
fi
python3 - "$ROOT_DIR/proto/tinyimx/message/v1/message_service.proto" <<'PY_C2_CONTRACT'
import re
import sys
from pathlib import Path

proto_path = Path(sys.argv[1])
text = proto_path.read_text(encoding="utf-8")

# M14-C2 owns the private durable-send contract.
# Later stages may legitimately add Gateway execution metadata to
# independent group-delivery RPCs; that must not invalidate this gate.
targets = (
    "PersistPrivateMessageRequest",
    "PersistPrivateMessageResponse",
)

for name in targets:
    match = re.search(
        rf"message\s+{re.escape(name)}\s*\{{(.*?)^\}}",
        text,
        flags=re.S | re.M,
    )

    if match is None:
        raise SystemExit(
            f"[FAIL] M14-C2 retained contract missing protobuf message: {name}"
        )

    body = match.group(1)

    forbidden = re.search(
        r"\b("
        r"packet_seq|delivery_seq|session_epoch|connection_id|"
        r"gateway_id|last_gateway_id"
        r")\b",
        body,
    )

    if forbidden:
        raise SystemExit(
            "[FAIL] M14-C2 private persistence leaked Gateway runtime "
            f"identity: {name}.{forbidden.group(1)}"
        )

print(
    "[PASS] M14-C2 private persistence remains "
    "Gateway-runtime-identity independent"
)
PY_C2_CONTRACT

log "configuring/building C2 targets"
(
  cd "$ROOT_DIR"
  cmake --preset linux-debug
  cmake --build "$BUILD_DIR" --target \
    rpc_contract_tests \
    message_application_service_tests \
    message_service_integration_tests \
    message_service_demo \
    user_service_demo \
    gateway_demo \
    -j"$BUILD_JOBS"
)

log "contract/application/integration gates"
"$BUILD_DIR/rpc_contract_tests" | tee "$ARTIFACT_DIR/rpc-contract.log"
"$BUILD_DIR/message_application_service_tests" | tee "$ARTIFACT_DIR/message-application.log"
"$BUILD_DIR/message_service_integration_tests" | tee "$ARTIFACT_DIR/message-integration.log"
grep -q 'total_failed=0' "$ARTIFACT_DIR/rpc-contract.log"
grep -q 'failed=0' "$ARTIFACT_DIR/message-application.log"
grep -q 'failed=0' "$ARTIFACT_DIR/message-integration.log"
grep -q 'PersistPostCommitTimeoutIsAttempted' "$ARTIFACT_DIR/message-integration.log"

USER_PORT="$(free_port)"
MESSAGE_PORT="$(free_port)"
GATEWAY_PORT="$(free_port)"
USER_TARGET="127.0.0.1:${USER_PORT}"
MESSAGE_TARGET="127.0.0.1:${MESSAGE_PORT}"
GATEWAY_CONFIG="$TMP_DIR/gateway.json"

python3 - "$SOURCE_CONFIG" "$GATEWAY_CONFIG" "$GATEWAY_PORT" "$ARTIFACT_DIR" <<'PY'
import json, os, sys
source,target,port_text,artifact_dir=sys.argv[1:]
with open(source,'r',encoding='utf-8') as h: root=json.load(h)
root.setdefault('server',{})['host']='127.0.0.1'
root['server']['port']=int(port_text)
root.setdefault('gateway_registry',{})['enable']=False
root.setdefault('business_runtime',{})['default_deadline_ms']=3000
root.setdefault('app',{})['instance_id']=f"gateway-m14-c2-{os.getpid()}"
root.setdefault('logger',{})['file']=os.path.join(artifact_dir,'gateway-internal.log')
root['logger']['console']=True
with open(target,'w',encoding='utf-8') as h:
    json.dump(root,h,ensure_ascii=False,indent=2); h.write('\n')
os.chmod(target,0o600)
PY

cat > "$TMP_DIR/c2_client.py" <<'PY'
import json, os, socket, struct, sys, time
MAGIC=0x54494D58; VERSION=1; HDR='!IHHHHII'; HS=20
LOGIN=1001; LOGIN_RESP=1002; CHAT=2001; CHAT_ACK=2002; HEARTBEAT=9001

def recvn(sock,n):
    out=b''
    while len(out)<n:
        part=sock.recv(n-len(out))
        if not part: raise RuntimeError('connection closed')
        out+=part
    return out

def sendp(sock,t,seq,obj):
    body=json.dumps(obj,separators=(',',':')).encode()
    sock.sendall(struct.pack(HDR,MAGIC,VERSION,t,0,0,seq,len(body))+body)

def recvp(sock, timeout=8.0):
    sock.settimeout(timeout)
    h=recvn(sock,HS)
    magic,version,t,flags,reserved,seq,size=struct.unpack(HDR,h)
    if magic!=MAGIC or version!=VERSION: raise RuntimeError('invalid protocol header')
    body=recvn(sock,size) if size else b'{}'
    try: obj=json.loads(body.decode())
    except Exception: obj={'_raw':body.decode(errors='replace')}
    print(f'[recv] type={t} seq={seq} body={json.dumps(obj,ensure_ascii=False,separators=(",",":"))}', flush=True)
    return t,seq,obj

def wait_packet(sock,want_type,want_seq,timeout=10.0):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        t,seq,obj=recvp(sock,max(0.2,deadline-time.monotonic()))
        if t==want_type and seq==want_seq: return obj
    raise RuntimeError(f'timeout waiting type={want_type} seq={want_seq}')

def connect(host,port):
    s=socket.create_connection((host,port),timeout=5.0)
    s.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
    return s

def login(sock,user,password,seq=1):
    sendp(sock,LOGIN,seq,{'username':user,'password':password})
    obj=wait_packet(sock,LOGIN_RESP,seq)
    if not obj.get('success'): raise RuntimeError(f'login failed: {obj}')
    return obj

def chat(sock,seq,cid,to,text):
    sendp(sock,CHAT,seq,{'client_message_id':cid,'to':to,'text':text})
    return wait_packet(sock,CHAT_ACK,seq,12.0)

def heartbeat(sock,seq):
    sendp(sock,HEARTBEAT,seq,{})

def require(cond,msg):
    if not cond: raise RuntimeError(msg)

host=sys.argv[1]; port=int(sys.argv[2]); mode=sys.argv[3]; cid=sys.argv[4]
sender_user=sys.argv[5]; password=sys.argv[6]; receiver=int(sys.argv[7])
gate=sys.argv[8] if len(sys.argv)>8 else ''
s=connect(host,port)
login(s,sender_user,password,1)
text=f'm14-c2-{mode}-{cid}'

if mode=='normal':
    first=chat(s,10,cid,receiver,text)
    require(first.get('success') is True,'first send not accepted')
    require(first.get('stored_persistent') is True,'first send not durable')
    require(first.get('reused') is False,'first unique C unexpectedly reused')
    mid=int(first.get('message_id',0)); require(mid>0,'first send missing M')
    second=chat(s,11,cid,receiver,text)
    require(second.get('success') is True,'same-C retry not accepted')
    require(second.get('stored_persistent') is True,'same-C retry lost durable flag')
    require(second.get('reused') is True,'same-C retry not reused')
    require(int(second.get('message_id',0))==mid,'same-C retry changed M')
    conflict=chat(s,12,cid,receiver,text+'-different')
    require(conflict.get('success') is False,'conflicting same C unexpectedly accepted')
    require(conflict.get('reason')=='client_message_id_conflict',f'wrong conflict reason: {conflict}')
    require(int(conflict.get('message_id',0))==mid,'conflict did not report existing M')
    print(f'NORMAL_MESSAGE_ID={mid}', flush=True)
    print('NORMAL_CREATED_REUSED_CONFLICT_PASS', flush=True)

elif mode=='unavailable':
    heartbeat(s,21)
    sendp(s,CHAT,20,{'client_message_id':cid,'to':receiver,'text':text})
    got_hb=False; ack=None; deadline=time.monotonic()+5.0
    while time.monotonic()<deadline and (not got_hb or ack is None):
        t,seq,obj=recvp(s,deadline-time.monotonic())
        if t==HEARTBEAT and seq==21:
            require(obj.get('pong') is True,'heartbeat failed')
            got_hb=True
        elif t==CHAT_ACK and seq==20:
            ack=obj
    require(got_hb,'heartbeat missing while persistence unavailable')
    require(ack is not None,'chat ack missing while persistence unavailable')
    require(ack.get('success') is False,'unavailable persistence unexpectedly accepted')
    require(ack.get('reason')=='message_persistence_unavailable',f'wrong unavailable reason: {ack}')
    require(ack.get('stored_persistent') is False,'unavailable path claimed persistence')
    print('UNAVAILABLE_NOT_ATTEMPTED_PASS', flush=True)

elif mode=='uncertain':
    start=time.monotonic()
    sendp(s,CHAT,30,{'client_message_id':cid,'to':receiver,'text':text})
    heartbeat(s,31)
    ack=None; hb_ms=None; ack_ms=None; hb_before_ack=False
    deadline=time.monotonic()+7.0
    while time.monotonic()<deadline and (ack is None or hb_ms is None):
        t,seq,obj=recvp(s,deadline-time.monotonic())
        now=time.monotonic()
        if t==HEARTBEAT and seq==31:
            require(obj.get('pong') is True,'heartbeat failed')
            hb_ms=(now-start)*1000.0
            if ack is None: hb_before_ack=True
        elif t==CHAT_ACK and seq==30:
            ack=obj; ack_ms=(now-start)*1000.0
    require(ack is not None and hb_ms is not None,'uncertain case missing responses')
    require(ack.get('success') is False,'post-commit timeout unexpectedly returned success')
    require(ack.get('reason')=='message_persistence_uncertain',f'wrong uncertain reason: {ack}')
    require(ack.get('stored_persistent') is False,'uncertain caller claimed durable proof')
    require(hb_before_ack,'heartbeat did not beat slow mutation response')
    require(hb_ms < 1000.0,f'heartbeat blocked by persist RPC: {hb_ms:.1f}ms')
    require(ack_ms >= 2500.0,f'uncertain response returned too early: {ack_ms:.1f}ms')
    print(f'heartbeat_latency_ms={int(hb_ms)}', flush=True)
    print(f'persist_uncertain_latency_ms={int(ack_ms)}', flush=True)
    print('UNCERTAIN_READY', flush=True)
    deadline=time.monotonic()+20.0
    while time.monotonic()<deadline and not os.path.exists(gate): time.sleep(0.05)
    require(os.path.exists(gate),'timed out waiting for recovery gate')
    recovered=None
    for attempt in range(5):
        seq=40+attempt
        candidate=chat(s,seq,cid,receiver,text)
        if candidate.get('success') is True:
            recovered=candidate; break
        require(candidate.get('reason') in ('message_persistence_uncertain','message_persistence_unavailable'),
                f'unexpected recovery failure: {candidate}')
        time.sleep(0.5)
    require(recovered is not None,'same-C recovery never succeeded')
    require(recovered.get('reused') is True,'uncertain recovery did not reuse C')
    mid=int(recovered.get('message_id',0)); require(mid>0,'recovery missing M')
    print(f'RECOVERED_MESSAGE_ID={mid}', flush=True)
    print('UNCERTAIN_SAME_C_RECOVERY_PASS', flush=True)
else:
    raise RuntimeError(f'unknown mode: {mode}')

s.close()
PY
chmod 700 "$TMP_DIR/c2_client.py"

start_user_service() {
  local label="$1"
  local log_file="$ARTIFACT_DIR/user-service-${label}.log"
  TINYIMX_USER_LISTEN_TARGET="$USER_TARGET" \
    "$BUILD_DIR/user_service_demo" "$SOURCE_CONFIG" >"$log_file" 2>&1 &
  USER_PID=$!
  wait_for_port 127.0.0.1 "$USER_PORT" "$USER_PID" "UserService-$label"
}

start_message_service() {
  local label="$1" post_delay_ms="${2:-0}" pre_delay_ms="${3:-0}"
  local log_file="$ARTIFACT_DIR/message-service-${label}.log"
  TINYIMX_FAULT_MESSAGE_PERSIST_POST_COMMIT_DELAY_MS="$post_delay_ms" \
  TINYIMX_FAULT_MESSAGE_PERSIST_PRE_REPOSITORY_DELAY_MS="$pre_delay_ms" \
  TINYIMX_MESSAGE_LISTEN_TARGET="$MESSAGE_TARGET" \
    "$BUILD_DIR/message_service_demo" "$SOURCE_CONFIG" >"$log_file" 2>&1 &
  MESSAGE_PID=$!
  wait_for_port 127.0.0.1 "$MESSAGE_PORT" "$MESSAGE_PID" "MessageService-$label"
}

stop_message_service() {
  stop_pid "$MESSAGE_PID" message-service TERM
  MESSAGE_PID=""
  wait_for_port_closed 127.0.0.1 "$MESSAGE_PORT"
}

start_gateway() {
  local label="$1" with_message="${2:-1}"
  local log_file="$ARTIFACT_DIR/gateway-${label}.log"
  if [[ "$with_message" == "1" ]]; then
    TINYIMX_USER_RPC_TARGET="$USER_TARGET" \
    TINYIMX_MESSAGE_RPC_TARGET="$MESSAGE_TARGET" \
      "$BUILD_DIR/gateway_demo" "$GATEWAY_CONFIG" >"$log_file" 2>&1 &
  else
    TINYIMX_USER_RPC_TARGET="$USER_TARGET" \
      "$BUILD_DIR/gateway_demo" "$GATEWAY_CONFIG" >"$log_file" 2>&1 &
  fi
  GATEWAY_PID=$!
  wait_for_port 127.0.0.1 "$GATEWAY_PORT" "$GATEWAY_PID" "Gateway-$label"
}

stop_gateway() {
  stop_pid "$GATEWAY_PID" gateway INT
  GATEWAY_PID=""
  wait_for_port_closed 127.0.0.1 "$GATEWAY_PORT"
}

stop_user_service() {
  stop_pid "$USER_PID" user-service TERM
  USER_PID=""
  wait_for_port_closed 127.0.0.1 "$USER_PORT"
}

private_key="tinyimx:unread:private:${RECEIVER_ID}:${SENDER_ID}"
total_key="tinyimx:unread:total:${RECEIVER_ID}"

log "case 1: Created -> same-C Reused -> conflict; stable-M unread projection suppresses duplicate increments"
start_user_service normal
start_message_service normal 0 0
start_gateway normal 1
normal_c="m14c2-n-${TIMESTAMP}-$$"
normal_private_before="$(redis_count "$private_key")"
normal_total_before="$(redis_count "$total_key")"
normal_log="$ARTIFACT_DIR/normal-idempotency.log"
python3 "$TMP_DIR/c2_client.py" 127.0.0.1 "$GATEWAY_PORT" normal "$normal_c" \
  "$SENDER_USERNAME" "$SENDER_PASSWORD" "$RECEIVER_ID" | tee "$normal_log"
grep -q 'NORMAL_CREATED_REUSED_CONFLICT_PASS' "$normal_log"
normal_mid="$(sed -n 's/^NORMAL_MESSAGE_ID=//p' "$normal_log" | tail -n1)"
[[ -n "$normal_mid" ]] || fail "normal case did not report M"
assert_db_identity "$normal_c" 1 "$normal_mid" >/dev/null
normal_private_after="$(redis_count "$private_key")"
normal_total_after="$(redis_count "$total_key")"
[[ "$normal_private_after" -eq $((normal_private_before + 1)) ]] || \
  fail "normal same-C private unread delta expected +1 before=$normal_private_before after=$normal_private_after"
[[ "$normal_total_after" -eq $((normal_total_before + 1)) ]] || \
  fail "normal same-C total unread delta expected +1 before=$normal_total_before after=$normal_total_after"
normal_marker="tinyimx:unread:projection:message:${normal_mid}"
[[ "$(redis_get "$normal_marker")" == "${RECEIVER_ID}:${SENDER_ID}" ]] || \
  fail "normal C2 unread projection marker missing/wrong for M=$normal_mid"
log "PASS case 1: C=$normal_c M=$normal_mid unread delta=+1 with duplicate suppression"
stop_gateway; stop_message_service; stop_user_service

log "case 2: MessageService not configured => deterministic not-attempted failure; DB untouched; heartbeat alive"
start_user_service unavailable
start_gateway unavailable 0
unavailable_c="m14c2-u-${TIMESTAMP}-$$"
unavailable_log="$ARTIFACT_DIR/unavailable.log"
python3 "$TMP_DIR/c2_client.py" 127.0.0.1 "$GATEWAY_PORT" unavailable "$unavailable_c" \
  "$SENDER_USERNAME" "$SENDER_PASSWORD" "$RECEIVER_ID" | tee "$unavailable_log"
grep -q 'UNAVAILABLE_NOT_ATTEMPTED_PASS' "$unavailable_log"
assert_db_identity "$unavailable_c" 0 >/dev/null
kill -0 "$GATEWAY_PID" 2>/dev/null || fail "Gateway died during not-attempted persistence failure"
log "PASS case 2: no MessageRpcClient => known failure and no durable row"
stop_gateway; stop_user_service

log "case 3: DB COMMIT + lost/late RPC response => uncertain; same-C retry recovers same M; unread reconciles once"
start_user_service uncertain
start_message_service postcommit-delay 3500 0
start_gateway uncertain 1
uncertain_c="m14c2-x-${TIMESTAMP}-$$"
uncertain_private_before="$(redis_count "$private_key")"
uncertain_total_before="$(redis_count "$total_key")"
uncertain_gate="$TMP_DIR/uncertain-retry-go"
uncertain_log="$ARTIFACT_DIR/post-commit-uncertain.log"
rm -f "$uncertain_gate"
python3 "$TMP_DIR/c2_client.py" 127.0.0.1 "$GATEWAY_PORT" uncertain "$uncertain_c" \
  "$SENDER_USERNAME" "$SENDER_PASSWORD" "$RECEIVER_ID" "$uncertain_gate" \
  >"$uncertain_log" 2>&1 &
CLIENT_PID=$!
wait_for_log_line "$uncertain_log" 'UNCERTAIN_READY' "$CLIENT_PID"
cat "$uncertain_log"

grep -q 'message_persistence_uncertain' "$uncertain_log"
grep -q 'heartbeat_latency_ms=' "$uncertain_log"
uncertain_mid="$(assert_db_identity "$uncertain_c" 1)"
[[ "$uncertain_mid" != "0" ]] || fail "post-commit uncertainty did not create durable M"
# The Gateway never received an accepted response, so the projection must not
# have run yet. This is the exact window C2 hardening is designed to recover.
[[ "$(redis_count "$private_key")" -eq "$uncertain_private_before" ]] || \
  fail "unread changed before uncertain same-C recovery"
[[ "$(redis_count "$total_key")" -eq "$uncertain_total_before" ]] || \
  fail "total unread changed before uncertain same-C recovery"
uncertain_marker="tinyimx:unread:projection:message:${uncertain_mid}"
[[ "$(redis_exists "$uncertain_marker")" == "0" ]] || \
  fail "projection marker unexpectedly exists before uncertain recovery"

stop_message_service
start_message_service recovery 0 0
# Give the existing gRPC Channel a short reconnection window. Any remaining
# transient retry still uses the same C in the test client.
sleep 1
touch "$uncertain_gate"
wait "$CLIENT_PID"
CLIENT_PID=""
cat "$uncertain_log"
grep -q 'UNCERTAIN_SAME_C_RECOVERY_PASS' "$uncertain_log"
recovered_mid="$(sed -n 's/^RECOVERED_MESSAGE_ID=//p' "$uncertain_log" | tail -n1)"
[[ "$recovered_mid" == "$uncertain_mid" ]] || \
  fail "uncertain recovery changed M expected=$uncertain_mid actual=$recovered_mid"
assert_db_identity "$uncertain_c" 1 "$uncertain_mid" >/dev/null
uncertain_private_after="$(redis_count "$private_key")"
uncertain_total_after="$(redis_count "$total_key")"
[[ "$uncertain_private_after" -eq $((uncertain_private_before + 1)) ]] || \
  fail "uncertain recovery private unread delta expected +1 before=$uncertain_private_before after=$uncertain_private_after"
[[ "$uncertain_total_after" -eq $((uncertain_total_before + 1)) ]] || \
  fail "uncertain recovery total unread delta expected +1 before=$uncertain_total_before after=$uncertain_total_after"
[[ "$(redis_get "$uncertain_marker")" == "${RECEIVER_ID}:${SENDER_ID}" ]] || \
  fail "uncertain recovery projection marker missing/wrong"
kill -0 "$GATEWAY_PID" 2>/dev/null || fail "Gateway died during uncertain recovery"
log "PASS case 3: uncertain durable M=$uncertain_mid recovered by same C; unread delta=+1; heartbeat remained fast"
stop_gateway; stop_message_service; stop_user_service

log "checking no local C->M fallback exists"
if grep -n 'SavePrivateMessageIdempotent' "$ROOT_DIR/gateway/GatewayServer.cpp"; then
  fail "Gateway regained local C->M fallback"
fi

printf '\n[PASS] M14-C2 Reliable Send Acceptance vertical slice\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
