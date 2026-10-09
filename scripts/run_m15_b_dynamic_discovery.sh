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
ZK_CONNECT="${TINYIMX_ZOOKEEPER_CONNECT:-127.0.0.1:2181}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
SESSION_TIMEOUT_MS="${TINYIMX_M15_B_SESSION_TIMEOUT_MS:-6000}"
CONNECT_TIMEOUT_MS="${TINYIMX_M15_B_CONNECT_TIMEOUT_MS:-5000}"
STALE_AFTER_MS="${TINYIMX_M15_B_STALE_AFTER_MS:-3000}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m15-b-${TIMESTAMP}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tinyimx-m15-b-XXXXXX")"
SERVICE_ROOT="/tinyimx-m15-b-${TIMESTAMP}-$$/services"
INTEGRATION_ROOT="/tinyimx-m15-b-integration-${TIMESTAMP}-$$/services"

USER1_PID=""
USER2_PID=""
USER3_PID=""
SOCIAL_PID=""
MESSAGE_PID=""
GATEWAY_PID=""
STATIC_GATEWAY_PID=""
ZK_PROXY_PID=""

mkdir -p "$ARTIFACT_DIR"
chmod 700 "$TMP_DIR"

log(){ printf '[M15-B] %s\n' "$*"; }
fail(){ log "FAIL: $*"; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }

free_port(){ python3 - <<'PY'
import socket
s=socket.socket(); s.bind(('127.0.0.1',0)); print(s.getsockname()[1]); s.close()
PY
}

wait_port(){
  python3 - "$1" "$2" "$3" <<'PY'
import os,socket,sys,time
port=int(sys.argv[1]); pid=int(sys.argv[2]); label=sys.argv[3]
end=time.monotonic()+20
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

wait_log(){
  local file="$1" pattern="$2" pid="$3" label="$4" timeout="${5:-20}"
  python3 - "$file" "$pattern" "$pid" "$label" "$timeout" <<'PY'
import os,re,sys,time
path,pattern,pid,label,timeout=sys.argv[1],sys.argv[2],int(sys.argv[3]),sys.argv[4],float(sys.argv[5])
end=time.monotonic()+timeout
regex=re.compile(pattern)
while time.monotonic()<end:
    try: os.kill(pid,0)
    except OSError: raise SystemExit(f'{label} exited while waiting for log marker')
    try:
        text=open(path,encoding='utf-8',errors='replace').read()
    except FileNotFoundError:
        text=''
    if regex.search(text): raise SystemExit(0)
    time.sleep(.1)
raise SystemExit(f'{label} log marker timeout: {pattern}')
PY
}


wait_log_sequence_after(){
  local file="$1" offset="$2" pid="$3" label="$4" timeout="$5"
  shift 5
  python3 - "$file" "$offset" "$pid" "$label" "$timeout" "$@" <<'PY'
import os,re,sys,time
path=sys.argv[1]
offset=int(sys.argv[2])
pid=int(sys.argv[3])
label=sys.argv[4]
timeout=float(sys.argv[5])
patterns=[re.compile(value) for value in sys.argv[6:]]
end=time.monotonic()+timeout
while time.monotonic()<end:
    try: os.kill(pid,0)
    except OSError: raise SystemExit(f'{label} exited while waiting for ordered log markers')
    try:
        with open(path,'rb') as f:
            f.seek(offset)
            text=f.read().decode('utf-8',errors='replace')
    except FileNotFoundError:
        text=''
    cursor=0
    complete=True
    for pattern in patterns:
        match=pattern.search(text,cursor)
        if match is None:
            complete=False
            break
        cursor=match.end()
    if complete:
        raise SystemExit(0)
    time.sleep(.1)
raise SystemExit(
    f'{label} ordered log sequence timeout after offset {offset}: ' +
    ' -> '.join(pattern.pattern for pattern in patterns)
)
PY
}

stop_pid(){
  local pid="${1:-}" signal="${2:-TERM}"
  [[ -z "$pid" ]] && return 0
  if kill -0 "$pid" 2>/dev/null; then
    kill "-${signal}" "$pid" 2>/dev/null || true
    for _ in $(seq 1 120); do
      kill -0 "$pid" 2>/dev/null || break
      sleep .1
    done
    kill -0 "$pid" 2>/dev/null && kill -KILL "$pid" 2>/dev/null || true
  fi
  wait "$pid" 2>/dev/null || true
}

cleanup(){
  set +e
  stop_pid "$STATIC_GATEWAY_PID" INT
  stop_pid "$GATEWAY_PID" INT
  stop_pid "$ZK_PROXY_PID" TERM
  stop_pid "$MESSAGE_PID" TERM
  stop_pid "$SOCIAL_PID" TERM
  stop_pid "$USER3_PID" TERM
  stop_pid "$USER2_PID" TERM
  stop_pid "$USER1_PID" TERM
  rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT INT TERM

[[ -f "$SOURCE_CONFIG" ]] || fail "missing source config: $SOURCE_CONFIG"
[[ -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]] || \
  fail "vcpkg toolchain missing: ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
for cmd in cmake python3 grep env; do require_cmd "$cmd"; done

python3 - "$ZK_CONNECT" <<'PY'
import socket,sys
value=sys.argv[1]
host,sep,port=value.rpartition(':')
if not sep or ',' in value: raise SystemExit('M15-B targeted fault gate requires one host:port ZooKeeper endpoint')
if host.startswith('[') and host.endswith(']'): host=host[1:-1]
s=socket.socket(); s.settimeout(2)
s.connect((host,int(port))); s.sendall(b'ruok'); data=s.recv(64); s.close()
if data != b'imok': raise SystemExit(f'ZooKeeper health check failed: {data!r}')
print('ZooKeeper response: imok')
PY

# Isolated TCP proxy: only Gateway loses ZooKeeper connectivity. Services keep
# direct sessions so data-plane gRPC remains healthy while discovery control
# plane is partitioned.
ZK_PROXY_SCRIPT="$TMP_DIR/zk_tcp_proxy.py"
cat >"$ZK_PROXY_SCRIPT" <<'PY'
import signal,socket,sys,threading
listen_host='127.0.0.1'; listen_port=int(sys.argv[1]); upstream_host=sys.argv[2]; upstream_port=int(sys.argv[3])
stop=threading.Event(); listeners=[]; connections=set(); lock=threading.Lock()
def close(s):
    try: s.shutdown(socket.SHUT_RDWR)
    except OSError: pass
    try: s.close()
    except OSError: pass
def shutdown(*_):
    stop.set()
    for s in list(listeners): close(s)
    with lock: current=list(connections)
    for s in current: close(s)
def pump(a,b):
    try:
        while not stop.is_set():
            data=a.recv(65536)
            if not data: break
            b.sendall(data)
    except OSError: pass
    finally: close(a); close(b)
def handle(client):
    upstream=None
    try:
        upstream=socket.create_connection((upstream_host,upstream_port),timeout=3)
        upstream.settimeout(None); client.settimeout(None)
        with lock: connections.update((client,upstream))
        t=threading.Thread(target=pump,args=(client,upstream),daemon=True); t.start(); pump(upstream,client); t.join(timeout=1)
    except OSError:
        close(client)
        if upstream is not None: close(upstream)
    finally:
        with lock:
            connections.discard(client)
            if upstream is not None: connections.discard(upstream)
signal.signal(signal.SIGTERM,shutdown); signal.signal(signal.SIGINT,shutdown)
server=socket.socket(); server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); server.bind((listen_host,listen_port)); server.listen(32); server.settimeout(.5); listeners.append(server)
print(f'proxy_ready={listen_host}:{listen_port}->{upstream_host}:{upstream_port}',flush=True)
while not stop.is_set():
    try: client,_=server.accept()
    except socket.timeout: continue
    except OSError: break
    threading.Thread(target=handle,args=(client,),daemon=True).start()
shutdown()
PY
chmod 600 "$ZK_PROXY_SCRIPT"

parse_connect(){
  python3 - "$1" <<'PY'
import sys
value=sys.argv[1]
if value.startswith('['):
    end=value.find(']')
    if end < 0 or end+2 > len(value) or value[end+1] != ':': raise SystemExit('invalid endpoint')
    print(value[1:end]); print(value[end+2:])
else:
    host,sep,port=value.rpartition(':')
    if not sep or not host or not port or ',' in value: raise SystemExit('one host:port endpoint required')
    print(host); print(port)
PY
}

start_zk_proxy(){
  local port="$1" upstream_host="$2" upstream_port="$3" log_file="$4"
  python3 "$ZK_PROXY_SCRIPT" "$port" "$upstream_host" "$upstream_port" >"$log_file" 2>&1 &
  ZK_PROXY_PID=$!
  wait_port "$port" "$ZK_PROXY_PID" ZooKeeperFaultProxy
  grep -q '^proxy_ready=' "$log_file" || fail "ZooKeeper fault proxy did not become ready"
}

stop_zk_proxy(){
  stop_pid "$ZK_PROXY_PID" TERM
  ZK_PROXY_PID=""
}

log "static architecture scan"
if grep -E -n 'zookeeper|ZooKeeper|zhandle_t|ephemeralOwner' \
  "$ROOT_DIR/gateway/GatewayServer.cpp" \
  "$ROOT_DIR/gateway/GatewayServer.h"; then
  fail "ZooKeeper leaked into GatewayServer runtime"
fi
for proto in "$ROOT_DIR"/proto/tinyimx/*/v1/*.proto; do
  if grep -E -n 'zookeeper|zk_node|ephemeral_owner|registry_path' "$proto"; then
    fail "ZooKeeper control-plane metadata leaked into business proto: $proto"
  fi
done
grep -q 'ZooKeeperServiceEndpointProvider' "$ROOT_DIR/examples/gateway_demo.cpp" || \
  fail "Gateway composition root is not wired to ZooKeeper endpoint provider"
grep -q 'WatchObserverId' "$ROOT_DIR/services/registry/zookeeper/ZooKeeperClient.h" || \
  fail "M15-B ZooKeeper watch observer interface missing from ZooKeeperClient.h"
grep -q 'WatchEventType' "$ROOT_DIR/services/registry/zookeeper/ZooKeeperTypes.h" || \
  fail "M15-B ZooKeeper watch event types missing from ZooKeeperTypes.h"
grep -q 'GetChildren' "$ROOT_DIR/services/registry/zookeeper/ZooKeeperClient.cpp" || \
  fail "ZooKeeper child-watch primitive missing"
grep -q 'ephemeral_owner == 0' "$ROOT_DIR/services/registry/zookeeper/ZooKeeperServiceDiscovery.cpp" || \
  fail "Discovery EPHEMERAL membership fence missing"
grep -q 'kMaxCachedTargets = 16' "$ROOT_DIR/services/rpc/MessageRpcClient.h" || \
  fail "bounded RPC multi-target cache missing"
log "PASS architecture boundary"

log "configure/build M15-B targeted targets (-j${BUILD_JOBS})"
(cd "$ROOT_DIR" && cmake --preset linux-debug)
cmake --build "$BUILD_DIR" --target \
  config_service_discovery_tests \
  rpc_multi_target_cache_tests \
  zookeeper_service_discovery_integration_tests \
  zookeeper_registry_probe \
  user_service_demo social_service_demo message_service_demo gateway_demo \
  gateway_login_auth_client_demo gateway_login_case_client_demo \
  gateway_friend_list_client_demo gateway_history_client_demo \
  gateway_session_client_demo \
  -j"$BUILD_JOBS"

log "config + bounded multi-target cache gates"
"$BUILD_DIR/config_service_discovery_tests" | tee "$ARTIFACT_DIR/config.log"
"$BUILD_DIR/rpc_multi_target_cache_tests" | tee "$ARTIFACT_DIR/rpc-cache.log"
grep -q '\[PASS\] M15-B service discovery config tests' "$ARTIFACT_DIR/config.log"
grep -q '\[PASS\] M15-B RPC multi-target cache tests' "$ARTIFACT_DIR/rpc-cache.log"

log "real ZooKeeper watch/re-watch + RR + LKG/stale integration"
"$BUILD_DIR/zookeeper_service_discovery_integration_tests" \
  "$ZK_CONNECT" "$INTEGRATION_ROOT" \
  | tee "$ARTIFACT_DIR/discovery-integration.log"
grep -q '\[PASS\] M15-B ZooKeeper discovery integration tests' \
  "$ARTIFACT_DIR/discovery-integration.log"

USER1_PORT="$(free_port)"; USER2_PORT="$(free_port)"; USER3_PORT="$(free_port)"
SOCIAL_PORT="$(free_port)"; MESSAGE_PORT="$(free_port)"; GATEWAY_PORT="$(free_port)"; STATIC_GATEWAY_PORT="$(free_port)"; PROXY_PORT="$(free_port)"
USER1_TARGET="127.0.0.1:${USER1_PORT}"; USER2_TARGET="127.0.0.1:${USER2_PORT}"; USER3_TARGET="127.0.0.1:${USER3_PORT}"
SOCIAL_TARGET="127.0.0.1:${SOCIAL_PORT}"; MESSAGE_TARGET="127.0.0.1:${MESSAGE_PORT}"
USER1_PATH="${SERVICE_ROOT}/user/user@${USER1_TARGET}"; USER2_PATH="${SERVICE_ROOT}/user/user@${USER2_TARGET}"; USER3_PATH="${SERVICE_ROOT}/user/user@${USER3_TARGET}"
SOCIAL_PATH="${SERVICE_ROOT}/social/social@${SOCIAL_TARGET}"; MESSAGE_PATH="${SERVICE_ROOT}/message/message@${MESSAGE_TARGET}"

mapfile -t ZK_UPSTREAM < <(parse_connect "$ZK_CONNECT")
ZK_UPSTREAM_HOST="${ZK_UPSTREAM[0]}"; ZK_UPSTREAM_PORT="${ZK_UPSTREAM[1]}"
PROXY_CONNECT="127.0.0.1:${PROXY_PORT}"

SERVICE_CONFIG="$TMP_DIR/services.json"
GATEWAY_CONFIG="$TMP_DIR/gateway-zookeeper.json"
STATIC_CONFIG="$TMP_DIR/gateway-static.json"
python3 - \
  "$SOURCE_CONFIG" "$SERVICE_CONFIG" "$GATEWAY_CONFIG" "$STATIC_CONFIG" \
  "$ZK_CONNECT" "$PROXY_CONNECT" "$SERVICE_ROOT" \
  "$SESSION_TIMEOUT_MS" "$CONNECT_TIMEOUT_MS" "$STALE_AFTER_MS" \
  "$GATEWAY_PORT" "$STATIC_GATEWAY_PORT" "$ARTIFACT_DIR" <<'PY'
import json,os,sys
(src,svc_dst,gw_dst,static_dst,direct,proxy,root,session_ms,connect_ms,stale_ms,gw_port,static_port,art)=sys.argv[1:]
with open(src,encoding='utf-8') as f: base=json.load(f)
if not base.setdefault('mysql',{}).get('enable',False): raise SystemExit('source config must have mysql.enable=true')

def common(cfg,connect):
    cfg.setdefault('gateway_registry',{})['enable']=False
    cfg['zookeeper']={'enable':True,'connect_string':connect,'session_timeout_ms':int(session_ms),'connect_timeout_ms':int(connect_ms),'service_root':root,'advertise_host':'127.0.0.1','service_version':'v1'}
    cfg.setdefault('logger',{})['console']=True
    return cfg
svc=common(json.loads(json.dumps(base)),direct)
svc['service_discovery']={'provider':'static','initial_sync_timeout_ms':int(connect_ms),'snapshot_stale_after_ms':int(stale_ms),'retain_last_known_good':True}
svc['logger']['file']=os.path.join(art,'services-internal.log')
gw=common(json.loads(json.dumps(base)),proxy)
gw.setdefault('server',{})['host']='127.0.0.1'; gw['server']['port']=int(gw_port); gw.setdefault('app',{})['instance_id']=f'gateway-m15-b-{os.getpid()}'
gw['service_discovery']={'provider':'zookeeper','initial_sync_timeout_ms':int(connect_ms),'snapshot_stale_after_ms':int(stale_ms),'retain_last_known_good':True}
gw['logger']['file']=os.path.join(art,'gateway-internal.log')
static=json.loads(json.dumps(base)); static.setdefault('gateway_registry',{})['enable']=False; static.setdefault('server',{})['host']='127.0.0.1'; static['server']['port']=int(static_port); static.setdefault('app',{})['instance_id']=f'gateway-m15-b-static-{os.getpid()}'
static['zookeeper']={'enable':False,'connect_string':direct,'session_timeout_ms':int(session_ms),'connect_timeout_ms':int(connect_ms),'service_root':root,'advertise_host':'127.0.0.1','service_version':'v1'}
static['service_discovery']={'provider':'static','initial_sync_timeout_ms':int(connect_ms),'snapshot_stale_after_ms':int(stale_ms),'retain_last_known_good':True}
static.setdefault('logger',{})['console']=True; static['logger']['file']=os.path.join(art,'gateway-static-internal.log')
for path,obj in ((svc_dst,svc),(gw_dst,gw),(static_dst,static)):
    with open(path,'w',encoding='utf-8') as f: json.dump(obj,f,indent=2); f.write('\n')
    os.chmod(path,0o600)
PY

log "start real multi-instance User + Social + Message services (direct ZooKeeper sessions)"
TINYIMX_USER_LISTEN_TARGET="$USER1_TARGET" "$BUILD_DIR/user_service_demo" "$SERVICE_CONFIG" >"$ARTIFACT_DIR/user1.log" 2>&1 & USER1_PID=$!
wait_port "$USER1_PORT" "$USER1_PID" UserService1
TINYIMX_USER_LISTEN_TARGET="$USER2_TARGET" "$BUILD_DIR/user_service_demo" "$SERVICE_CONFIG" >"$ARTIFACT_DIR/user2.log" 2>&1 & USER2_PID=$!
wait_port "$USER2_PORT" "$USER2_PID" UserService2
TINYIMX_SOCIAL_LISTEN_TARGET="$SOCIAL_TARGET" "$BUILD_DIR/social_service_demo" "$SERVICE_CONFIG" >"$ARTIFACT_DIR/social.log" 2>&1 & SOCIAL_PID=$!
wait_port "$SOCIAL_PORT" "$SOCIAL_PID" SocialService
TINYIMX_MESSAGE_LISTEN_TARGET="$MESSAGE_TARGET" "$BUILD_DIR/message_service_demo" "$SERVICE_CONFIG" >"$ARTIFACT_DIR/message.log" 2>&1 & MESSAGE_PID=$!
wait_port "$MESSAGE_PORT" "$MESSAGE_PID" MessageService

for entry in "user1:$USER1_PATH" "user2:$USER2_PATH" "social:$SOCIAL_PATH" "message:$MESSAGE_PATH"; do
  label="${entry%%:*}"; path="${entry#*:}"
  "$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$path" present 5000 >"$ARTIFACT_DIR/${label}-registration.log"
done
log "PASS service membership fixtures"

start_zk_proxy "$PROXY_PORT" "$ZK_UPSTREAM_HOST" "$ZK_UPSTREAM_PORT" "$ARTIFACT_DIR/zk-proxy.log"

log "start Gateway with provider=zookeeper and NO static RPC targets"
env -u TINYIMX_USER_RPC_TARGET -u TINYIMX_SOCIAL_RPC_TARGET -u TINYIMX_MESSAGE_RPC_TARGET \
  "$BUILD_DIR/gateway_demo" "$GATEWAY_CONFIG" >"$ARTIFACT_DIR/gateway-dynamic.log" 2>&1 &
GATEWAY_PID=$!
wait_port "$GATEWAY_PORT" "$GATEWAY_PID" DynamicGateway
wait_log "$ARTIFACT_DIR/gateway-dynamic.log" 'gateway dynamic RPC discovery enabled' "$GATEWAY_PID" DynamicGateway 15

log "real dynamic User/Social/Message vertical slices"
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" | tee "$ARTIFACT_DIR/login.log"
"$BUILD_DIR/gateway_friend_list_client_demo" 127.0.0.1 "$GATEWAY_PORT" | tee "$ARTIFACT_DIR/friends.log"
"$BUILD_DIR/gateway_history_client_demo" 127.0.0.1 "$GATEWAY_PORT" | tee "$ARTIFACT_DIR/history.log"
"$BUILD_DIR/gateway_session_client_demo" 127.0.0.1 "$GATEWAY_PORT" | tee "$ARTIFACT_DIR/session.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/login.log"
grep -q 'gateway friend list validation passed' "$ARTIFACT_DIR/friends.log"
grep -q 'gateway history validation passed' "$ARTIFACT_DIR/history.log"
grep -q 'gateway session client validation passed' "$ARTIFACT_DIR/session.log"
log "PASS dynamic User/Social/Message business routing"

log "SIGKILL one UserService; next calls must converge to surviving instance"
kill -KILL "$USER1_PID"; wait "$USER1_PID" 2>/dev/null || true; USER1_PID=""
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$USER1_PATH" absent "$((SESSION_TIMEOUT_MS + 8000))" | tee "$ARTIFACT_DIR/user1-expired.log"
sleep 1
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" | tee "$ARTIFACT_DIR/login-after-user1-crash.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/login-after-user1-crash.log"
log "PASS instance removal + next-RPC failover"

log "short discovery control-plane partition: fresh LKG must keep RPC data plane alive"
stop_zk_proxy
sleep 0.5
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" | tee "$ARTIFACT_DIR/login-lkg.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/login-lkg.log"
SHORT_RECOVERY_LOG_OFFSET=0
if [[ -f "$ARTIFACT_DIR/gateway-dynamic.log" ]]; then
  SHORT_RECOVERY_LOG_OFFSET="$(wc -c < "$ARTIFACT_DIR/gateway-dynamic.log")"
fi
start_zk_proxy "$PROXY_PORT" "$ZK_UPSTREAM_HOST" "$ZK_UPSTREAM_PORT" "$ARTIFACT_DIR/zk-proxy-short-restart.log"
wait_log_sequence_after \
  "$ARTIFACT_DIR/gateway-dynamic.log" \
  "$SHORT_RECOVERY_LOG_OFFSET" \
  "$GATEWAY_PID" \
  DynamicGateway \
  15 \
  'ZooKeeper session connected.*generation=1' \
  'ZooKeeper discovery snapshot refreshed.*service=user'
log "PASS last-known-good during short ZooKeeper outage + same-session refresh"

log "long discovery partition: stale snapshot must fence routing before session expiry"
stop_zk_proxy
python3 - "$STALE_AFTER_MS" <<'PY'
import sys,time
time.sleep((int(sys.argv[1])+1200)/1000.0)
PY
"$BUILD_DIR/gateway_login_case_client_demo" 127.0.0.1 "$GATEWAY_PORT" user10001 123456 0 auth_unavailable | tee "$ARTIFACT_DIR/login-stale-fenced.log"
grep -q 'gateway login case validation passed' "$ARTIFACT_DIR/login-stale-fenced.log"
kill -0 "$GATEWAY_PID" 2>/dev/null || fail "Gateway died while discovery snapshot was stale"
log "PASS stale snapshot fencing"

# Keep the proxy down long enough for the live ZooKeeper server to expire the
# Gateway discovery session, then restore connectivity and require generation=2.
REMAIN_MS=$((SESSION_TIMEOUT_MS + 1800 - STALE_AFTER_MS - 1200))
if (( REMAIN_MS > 0 )); then python3 - "$REMAIN_MS" <<'PY'
import sys,time
time.sleep(int(sys.argv[1])/1000.0)
PY
fi
RECOVERY_LOG_OFFSET=0
if [[ -f "$ARTIFACT_DIR/gateway-dynamic.log" ]]; then
  RECOVERY_LOG_OFFSET="$(wc -c < "$ARTIFACT_DIR/gateway-dynamic.log")"
fi
start_zk_proxy "$PROXY_PORT" "$ZK_UPSTREAM_HOST" "$ZK_UPSTREAM_PORT" "$ARTIFACT_DIR/zk-proxy-expiry-restart.log"
wait_log_sequence_after \
  "$ARTIFACT_DIR/gateway-dynamic.log" \
  "$RECOVERY_LOG_OFFSET" \
  "$GATEWAY_PID" \
  DynamicGateway \
  25 \
  'ZooKeeper session expired' \
  'ZooKeeper session connected.*generation=2' \
  'ZooKeeper discovery snapshot refreshed.*service=user'
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" | tee "$ARTIFACT_DIR/login-after-session-recovery.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/login-after-session-recovery.log"
log "PASS discovery SessionExpired -> new session -> full refresh"

log "prove watches were re-armed after new discovery session"
TINYIMX_USER_LISTEN_TARGET="$USER3_TARGET" "$BUILD_DIR/user_service_demo" "$SERVICE_CONFIG" >"$ARTIFACT_DIR/user3.log" 2>&1 & USER3_PID=$!
wait_port "$USER3_PORT" "$USER3_PID" UserService3
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$USER3_PATH" present 5000 >"$ARTIFACT_DIR/user3-registration.log"
sleep 1
kill -KILL "$USER2_PID"; wait "$USER2_PID" 2>/dev/null || true; USER2_PID=""
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$USER2_PATH" absent "$((SESSION_TIMEOUT_MS + 8000))" >"$ARTIFACT_DIR/user2-expired.log"
sleep 1
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" | tee "$ARTIFACT_DIR/login-after-rewatch.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/login-after-rewatch.log"
log "PASS post-SessionExpired watch re-arm"

log "M14 static provider compatibility after Gateway wiring changes"
stop_pid "$GATEWAY_PID" INT; GATEWAY_PID=""
env \
  TINYIMX_USER_RPC_TARGET="$USER3_TARGET" \
  TINYIMX_SOCIAL_RPC_TARGET="$SOCIAL_TARGET" \
  TINYIMX_MESSAGE_RPC_TARGET="$MESSAGE_TARGET" \
  "$BUILD_DIR/gateway_demo" "$STATIC_CONFIG" >"$ARTIFACT_DIR/gateway-static.log" 2>&1 &
STATIC_GATEWAY_PID=$!
wait_port "$STATIC_GATEWAY_PORT" "$STATIC_GATEWAY_PID" StaticGateway
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$STATIC_GATEWAY_PORT" | tee "$ARTIFACT_DIR/login-static.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/login-static.log"
log "PASS M14 static-provider compatibility"

printf '\n[M15-B PASS] Dynamic Discovery & RPC Routing targeted acceptance passed.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
