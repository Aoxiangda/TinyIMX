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
SESSION_TIMEOUT_MS="${TINYIMX_M15_A_SESSION_TIMEOUT_MS:-10000}"
CONNECT_TIMEOUT_MS="${TINYIMX_M15_A_CONNECT_TIMEOUT_MS:-5000}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m15-a-${TIMESTAMP}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tinyimx-m15-a-XXXXXX")"
SERVICE_ROOT="/tinyimx-m15-a-${TIMESTAMP}-$$/services"

USER_PID=""
SOCIAL_PID=""
MESSAGE_PID=""
STATIC_USER_PID=""
EXPIRY_USER_PID=""
ZK_PROXY_PID=""

mkdir -p "$ARTIFACT_DIR"
chmod 700 "$TMP_DIR"

log(){ printf '[M15-A] %s\n' "$*"; }
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
    for _ in $(seq 1 100); do
      kill -0 "$pid" 2>/dev/null || break
      sleep .1
    done
    kill -0 "$pid" 2>/dev/null && kill -KILL "$pid" 2>/dev/null || true
  fi
  wait "$pid" 2>/dev/null || true
}

cleanup(){
  set +e
  stop_pid "$ZK_PROXY_PID" TERM
  ZK_PROXY_PID=""
  stop_pid "$EXPIRY_USER_PID" TERM
  stop_pid "$STATIC_USER_PID" TERM
  stop_pid "$MESSAGE_PID" TERM
  stop_pid "$SOCIAL_PID" TERM
  stop_pid "$USER_PID" TERM
  rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT INT TERM

[[ -f "$SOURCE_CONFIG" ]] || fail "missing source config: $SOURCE_CONFIG"
[[ -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]] || \
  fail "vcpkg toolchain missing: ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
for cmd in cmake python3 grep; do require_cmd "$cmd"; done

# A controllable TCP proxy lets us partition exactly one TinyIMX service from
# ZooKeeper while the ZooKeeper server stays alive. This is essential for a
# real SessionExpired test: session expiration is decided by the live cluster,
# not by a client-side timer. Killing the only standalone ZooKeeper process
# does not guarantee that the old session will be expired on restart.
ZK_PROXY_SCRIPT="$TMP_DIR/zk_tcp_proxy.py"
cat >"$ZK_PROXY_SCRIPT" <<'PY'
import signal
import socket
import sys
import threading

listen_host = "127.0.0.1"
listen_port = int(sys.argv[1])
upstream_host = sys.argv[2]
upstream_port = int(sys.argv[3])
stop = threading.Event()
listeners = []
connections = set()
connections_lock = threading.Lock()


def close_socket(sock):
    try:
        sock.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass
    try:
        sock.close()
    except OSError:
        pass


def on_stop(_signum, _frame):
    stop.set()
    for sock in list(listeners):
        close_socket(sock)
    with connections_lock:
        current = list(connections)
    for sock in current:
        close_socket(sock)


def pump(src, dst):
    try:
        while not stop.is_set():
            data = src.recv(65536)
            if not data:
                break
            dst.sendall(data)
    except OSError:
        pass
    finally:
        close_socket(src)
        close_socket(dst)


def handle(client):
    upstream = None
    try:
        upstream = socket.create_connection((upstream_host, upstream_port), timeout=3)
        upstream.settimeout(None)
        client.settimeout(None)
        with connections_lock:
            connections.add(client)
            connections.add(upstream)
        t = threading.Thread(target=pump, args=(client, upstream), daemon=True)
        t.start()
        pump(upstream, client)
        t.join(timeout=1)
    except OSError:
        close_socket(client)
        if upstream is not None:
            close_socket(upstream)
    finally:
        with connections_lock:
            connections.discard(client)
            if upstream is not None:
                connections.discard(upstream)


signal.signal(signal.SIGTERM, on_stop)
signal.signal(signal.SIGINT, on_stop)
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind((listen_host, listen_port))
server.listen(32)
server.settimeout(0.5)
listeners.append(server)
print(f"proxy_ready={listen_host}:{listen_port}->{upstream_host}:{upstream_port}", flush=True)

while not stop.is_set():
    try:
        client, _ = server.accept()
    except socket.timeout:
        continue
    except OSError:
        break
    threading.Thread(target=handle, args=(client,), daemon=True).start()

on_stop(signal.SIGTERM, None)
PY
chmod 600 "$ZK_PROXY_SCRIPT"

parse_connect(){
  python3 - "$1" <<'PY'
import sys
value=sys.argv[1]
if value.startswith('['):
    end=value.find(']')
    if end < 0 or end + 2 > len(value) or value[end+1] != ':':
        raise SystemExit('invalid bracketed ZooKeeper connect endpoint')
    print(value[1:end])
    print(value[end+2:])
else:
    host, sep, port=value.rpartition(':')
    if not sep or not host or not port:
        raise SystemExit('M15-A proxy fault test requires one host:port ZooKeeper endpoint')
    print(host)
    print(port)
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

log "static boundary scan"
grep -q '"name": "zookeeper"' "$ROOT_DIR/vcpkg.json" || fail "vcpkg zookeeper dependency missing"
grep -q 'unofficial-zookeeper' "$ROOT_DIR/cmake/TinyIMXDependencies.cmake" || fail "ZooKeeper CMake dependency missing"
grep -q 'tinyimx_service_registry' "$ROOT_DIR/cmake/TinyIMXZooKeeperRegistry.cmake" || fail "service registry target missing"
for proto in "$ROOT_DIR"/proto/tinyimx/*/v1/*.proto; do
  if grep -E -n 'zookeeper|zk_node|ephemeral_owner|registry_path' "$proto"; then
    fail "ZooKeeper control-plane metadata leaked into business proto: $proto"
  fi
done
if grep -E -n 'ZooKeeper|zookeeper' \
  "$ROOT_DIR/gateway/GatewayServer.cpp" \
  "$ROOT_DIR/gateway/GatewayServer.h" \
  "$ROOT_DIR/services/rpc/ServiceEndpointProvider.h" \
  "$ROOT_DIR/services/rpc/StaticServiceEndpointProvider.cpp"; then
  fail "M15-A leaked ZooKeeper into Gateway/RPC discovery boundary reserved for M15-B"
fi

log "configure/build M15-A targets (-j${BUILD_JOBS})"
(cd "$ROOT_DIR" && cmake --preset linux-debug)
cmake --build "$BUILD_DIR" --target \
  config_zookeeper_tests \
  service_instance_tests \
  zookeeper_service_registry_integration_tests \
  zookeeper_registry_probe \
  user_service_demo social_service_demo message_service_demo \
  -j"$BUILD_JOBS"

log "unit/config gates"
"$BUILD_DIR/config_zookeeper_tests" | tee "$ARTIFACT_DIR/config.log"
"$BUILD_DIR/service_instance_tests" | tee "$ARTIFACT_DIR/service-instance.log"
grep -q '\[PASS\] M15-A ZooKeeper config tests' "$ARTIFACT_DIR/config.log"
grep -q '\[PASS\] M15-A ServiceInstance tests' "$ARTIFACT_DIR/service-instance.log"

log "real ZooKeeper client/ownership integration gate"
INTEGRATION_ROOT="${SERVICE_ROOT}/integration"
"$BUILD_DIR/zookeeper_service_registry_integration_tests" \
  "$ZK_CONNECT" "$INTEGRATION_ROOT" \
  | tee "$ARTIFACT_DIR/registry-integration.log"
grep -q '\[PASS\] M15-A ZooKeeper registry integration tests' \
  "$ARTIFACT_DIR/registry-integration.log"

USER_PORT="$(free_port)"
SOCIAL_PORT="$(free_port)"
MESSAGE_PORT="$(free_port)"
USER_TARGET="127.0.0.1:${USER_PORT}"
SOCIAL_TARGET="127.0.0.1:${SOCIAL_PORT}"
MESSAGE_TARGET="127.0.0.1:${MESSAGE_PORT}"

USER_PATH="${SERVICE_ROOT}/user/user@${USER_TARGET}"
SOCIAL_PATH="${SERVICE_ROOT}/social/social@${SOCIAL_TARGET}"
MESSAGE_PATH="${SERVICE_ROOT}/message/message@${MESSAGE_TARGET}"

ZK_CONFIG="$TMP_DIR/zookeeper-services.json"
STATIC_CONFIG="$TMP_DIR/static-services.json"
python3 - \
  "$SOURCE_CONFIG" "$ZK_CONFIG" "$STATIC_CONFIG" \
  "$ZK_CONNECT" "$SERVICE_ROOT" \
  "$SESSION_TIMEOUT_MS" "$CONNECT_TIMEOUT_MS" \
  "$ARTIFACT_DIR" <<'PY'
import json,os,sys
src,zk_dst,static_dst,connect,root,session_ms,connect_ms,art=sys.argv[1:]
with open(src,encoding='utf-8') as f: base=json.load(f)
if not base.setdefault('mysql',{}).get('enable',False):
    raise SystemExit('source config must have mysql.enable=true')
zk=json.loads(json.dumps(base))
zk.setdefault('gateway_registry',{})['enable']=False
zk['zookeeper']={
    'enable':True,
    'connect_string':connect,
    'session_timeout_ms':int(session_ms),
    'connect_timeout_ms':int(connect_ms),
    'service_root':root,
    'advertise_host':'127.0.0.1',
    'service_version':'v1',
}
zk.setdefault('logger',{})['console']=True
zk['logger']['file']=os.path.join(art,'service-internal.log')
with open(zk_dst,'w',encoding='utf-8') as f: json.dump(zk,f,indent=2); f.write('\n')
os.chmod(zk_dst,0o600)
static=json.loads(json.dumps(base))
static.setdefault('gateway_registry',{})['enable']=False
static['zookeeper']={
    'enable':False,
    'connect_string':connect,
    'session_timeout_ms':int(session_ms),
    'connect_timeout_ms':int(connect_ms),
    'service_root':root,
    'advertise_host':'127.0.0.1',
    'service_version':'v1',
}
static.setdefault('logger',{})['console']=True
static['logger']['file']=os.path.join(art,'static-service-internal.log')
with open(static_dst,'w',encoding='utf-8') as f: json.dump(static,f,indent=2); f.write('\n')
os.chmod(static_dst,0o600)
PY

log "start three real gRPC services with ZooKeeper registration"
TINYIMX_USER_LISTEN_TARGET="$USER_TARGET" \
  "$BUILD_DIR/user_service_demo" "$ZK_CONFIG" \
  >"$ARTIFACT_DIR/user-service.log" 2>&1 &
USER_PID=$!
wait_port "$USER_PORT" "$USER_PID" UserService

TINYIMX_SOCIAL_LISTEN_TARGET="$SOCIAL_TARGET" \
  "$BUILD_DIR/social_service_demo" "$ZK_CONFIG" \
  >"$ARTIFACT_DIR/social-service.log" 2>&1 &
SOCIAL_PID=$!
wait_port "$SOCIAL_PORT" "$SOCIAL_PID" SocialService

TINYIMX_MESSAGE_LISTEN_TARGET="$MESSAGE_TARGET" \
  "$BUILD_DIR/message_service_demo" "$ZK_CONFIG" \
  >"$ARTIFACT_DIR/message-service.log" 2>&1 &
MESSAGE_PID=$!
wait_port "$MESSAGE_PORT" "$MESSAGE_PID" MessageService

for entry in \
  "user:$USER_PATH" \
  "social:$SOCIAL_PATH" \
  "message:$MESSAGE_PATH"; do
  label="${entry%%:*}"; path="${entry#*:}"
  "$BUILD_DIR/zookeeper_registry_probe" \
    "$ZK_CONNECT" "$path" present 5000 \
    | tee "$ARTIFACT_DIR/${label}-registration.log"
  grep -q 'ephemeral_owner=' "$ARTIFACT_DIR/${label}-registration.log" || \
    fail "${label} registration lacks ephemeral owner"
done
log "PASS three-service registration"

log "graceful UserService shutdown must remove owned registration"
stop_pid "$USER_PID" TERM
USER_PID=""
"$BUILD_DIR/zookeeper_registry_probe" \
  "$ZK_CONNECT" "$USER_PATH" absent 5000 \
  | tee "$ARTIFACT_DIR/user-graceful-unregister.log"
log "PASS graceful unregister"

log "SIGKILL MessageService; ephemeral registration must expire without explicit unregister"
kill -KILL "$MESSAGE_PID"
wait "$MESSAGE_PID" 2>/dev/null || true
MESSAGE_PID=""
CRASH_WAIT_MS=$((SESSION_TIMEOUT_MS + 7000))
"$BUILD_DIR/zookeeper_registry_probe" \
  "$ZK_CONNECT" "$MESSAGE_PATH" absent "$CRASH_WAIT_MS" \
  | tee "$ARTIFACT_DIR/message-crash-expiry.log"
log "PASS crash ephemeral expiry"

log "M14 static-mode compatibility: ZooKeeper disabled"
STATIC_PORT="$(free_port)"
TINYIMX_USER_LISTEN_TARGET="127.0.0.1:${STATIC_PORT}" \
  "$BUILD_DIR/user_service_demo" "$STATIC_CONFIG" \
  >"$ARTIFACT_DIR/user-static-mode.log" 2>&1 &
STATIC_USER_PID=$!
wait_port "$STATIC_PORT" "$STATIC_USER_PID" StaticUserService
stop_pid "$STATIC_USER_PID" TERM
STATIC_USER_PID=""
grep -q 'zookeeper_registered=0' "$ARTIFACT_DIR/user-static-mode.log" || \
  fail "static compatibility ready log missing zookeeper_registered=0"
log "PASS M14 static-mode compatibility"

# Session recovery is tested through a client-side network partition rather
# than by stopping the only standalone ZooKeeper server. The cluster must stay
# alive so that it can actually expire a silent client session and remove that
# session's ephemerals.
log "session-expiration recovery fault injection through isolated TCP proxy"
EXPIRY_PORT="$(free_port)"
EXPIRY_TARGET="127.0.0.1:${EXPIRY_PORT}"
EXPIRY_PATH="${SERVICE_ROOT}/user/user@${EXPIRY_TARGET}"
PROXY_PORT="$(free_port)"
mapfile -t ZK_UPSTREAM < <(parse_connect "$ZK_CONNECT")
ZK_UPSTREAM_HOST="${ZK_UPSTREAM[0]}"
ZK_UPSTREAM_PORT="${ZK_UPSTREAM[1]}"
[[ "$ZK_UPSTREAM_PORT" =~ ^[0-9]+$ ]] || fail "ZooKeeper upstream port is not numeric"
PROXY_CONNECT="127.0.0.1:${PROXY_PORT}"
EXPIRY_ZK_CONFIG="$TMP_DIR/zookeeper-expiry-service.json"
python3 - "$ZK_CONFIG" "$EXPIRY_ZK_CONFIG" "$PROXY_CONNECT" <<'PY'
import json,os,sys
src,dst,connect=sys.argv[1:]
with open(src,encoding='utf-8') as f:
    cfg=json.load(f)
cfg['zookeeper']['connect_string']=connect
with open(dst,'w',encoding='utf-8') as f:
    json.dump(cfg,f,indent=2)
    f.write('\n')
os.chmod(dst,0o600)
PY

start_zk_proxy "$PROXY_PORT" "$ZK_UPSTREAM_HOST" "$ZK_UPSTREAM_PORT" \
  "$ARTIFACT_DIR/zk-fault-proxy.log"

TINYIMX_USER_LISTEN_TARGET="$EXPIRY_TARGET" \
  "$BUILD_DIR/user_service_demo" "$EXPIRY_ZK_CONFIG" \
  >"$ARTIFACT_DIR/user-session-expiry.log" 2>&1 &
EXPIRY_USER_PID=$!
wait_port "$EXPIRY_PORT" "$EXPIRY_USER_PID" ExpiryUserService
"$BUILD_DIR/zookeeper_registry_probe" \
  "$ZK_CONNECT" "$EXPIRY_PATH" present 5000 \
  >"$ARTIFACT_DIR/expiry-before.log"
BEFORE_OWNER="$(sed -n 's/^ephemeral_owner=//p' "$ARTIFACT_DIR/expiry-before.log" | tail -n1)"
[[ -n "$BEFORE_OWNER" ]] || fail "cannot read pre-expiry owner"

# Short partition: the cluster stays alive, the proxy disappears briefly, and
# the service must reconnect using the same ZooKeeper session.
stop_zk_proxy
sleep "${TINYIMX_ZK_SHORT_OUTAGE_SECONDS:-1}"
start_zk_proxy "$PROXY_PORT" "$ZK_UPSTREAM_HOST" "$ZK_UPSTREAM_PORT" \
  "$ARTIFACT_DIR/zk-fault-proxy-short-restart.log"
kill -0 "$EXPIRY_USER_PID" 2>/dev/null || fail "service died during short ZooKeeper partition"
"$BUILD_DIR/zookeeper_registry_probe" \
  "$ZK_CONNECT" "$EXPIRY_PATH" present "$((CONNECT_TIMEOUT_MS + 10000))" \
  | tee "$ARTIFACT_DIR/reconnect-same-session.log"
SAME_OWNER="$(sed -n 's/^ephemeral_owner=//p' "$ARTIFACT_DIR/reconnect-same-session.log" | tail -n1)"
[[ -n "$SAME_OWNER" ]] || fail "cannot read same-session reconnect owner"
[[ "$SAME_OWNER" == "$BEFORE_OWNER" ]] || \
  fail "short ZooKeeper partition unexpectedly changed session owner"
log "PASS short disconnect -> same session -> same ephemeral owner"

# Long partition: ZooKeeper itself remains healthy and therefore has enough
# time to expire the silent service session. Prove the old ephemeral disappears
# before restoring connectivity, then prove the still-running service gets an
# Expired notification, creates a new session, and re-registers the same path.
stop_zk_proxy
LONG_PARTITION_WAIT_MS=$((SESSION_TIMEOUT_MS + 7000))
"$BUILD_DIR/zookeeper_registry_probe" \
  "$ZK_CONNECT" "$EXPIRY_PATH" absent "$LONG_PARTITION_WAIT_MS" \
  | tee "$ARTIFACT_DIR/expiry-old-owner-removed.log"
kill -0 "$EXPIRY_USER_PID" 2>/dev/null || fail "service died while ZooKeeper session was expiring"

start_zk_proxy "$PROXY_PORT" "$ZK_UPSTREAM_HOST" "$ZK_UPSTREAM_PORT" \
  "$ARTIFACT_DIR/zk-fault-proxy-expiry-restart.log"
"$BUILD_DIR/zookeeper_registry_probe" \
  "$ZK_CONNECT" "$EXPIRY_PATH" present "$((CONNECT_TIMEOUT_MS + 15000))" \
  | tee "$ARTIFACT_DIR/expiry-after.log"
AFTER_OWNER="$(sed -n 's/^ephemeral_owner=//p' "$ARTIFACT_DIR/expiry-after.log" | tail -n1)"
[[ -n "$AFTER_OWNER" ]] || fail "cannot read post-expiry owner"
[[ "$AFTER_OWNER" != "$BEFORE_OWNER" ]] || \
  fail "SessionExpired recovery did not establish a new ephemeral owner"
grep -q 'ZooKeeper service registration recovered' \
  "$ARTIFACT_DIR/user-session-expiry.log" || \
  fail "service log missing registration recovery marker"
kill -0 "$EXPIRY_USER_PID" 2>/dev/null || fail "service died after ZooKeeper session recovery"
log "PASS SessionExpired -> old ephemeral removed -> new session -> re-register"

stop_pid "$EXPIRY_USER_PID" TERM
EXPIRY_USER_PID=""
stop_zk_proxy

stop_pid "$SOCIAL_PID" TERM
SOCIAL_PID=""

printf '\n[M15-A PASS] ZooKeeper Registry Foundation targeted acceptance passed.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
