#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
SOURCE_CONFIG="${1:-$ROOT_DIR/config/gateway-a.local.json}"
BUILD_DIR="${TINYIMX_BUILD_DIR:-$ROOT_DIR/build/linux-debug}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
SESSION_TIMEOUT_MS="${TINYIMX_M15_C_SESSION_TIMEOUT_MS:-10000}"
CONNECT_TIMEOUT_MS="${TINYIMX_M15_C_CONNECT_TIMEOUT_MS:-5000}"
STALE_AFTER_MS="${TINYIMX_M15_C_STALE_AFTER_MS:-3000}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${TINYIMX_M15_C_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m15-c-failover-$TIMESTAMP}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tinyimx-m15-c-XXXXXX")"
SERVICE_ROOT="/tinyimx-m15-c-${TIMESTAMP}-$$/services"
FOCUSED_ROOT="/tinyimx-m15-c-focused-${TIMESTAMP}-$$/services"
ENSEMBLE_SCRIPT="$ROOT_DIR/scripts/local_zookeeper_ensemble.sh"
ENSEMBLE_BASE="${TINYIMX_ZK_ENSEMBLE_BASE:-$TMP_DIR/ensemble}"
ENSEMBLE_CLIENT_BASE="${TINYIMX_ZK_ENSEMBLE_CLIENT_BASE:-22181}"
ENSEMBLE_PEER_BASE="${TINYIMX_ZK_ENSEMBLE_PEER_BASE:-22881}"
ENSEMBLE_ELECTION_BASE="${TINYIMX_ZK_ENSEMBLE_ELECTION_BASE:-23881}"
SUMMARY="$ARTIFACT_DIR/summary.tsv"

export VCPKG_ROOT="${VCPKG_ROOT:-$ROOT_DIR/toolchains/vcpkg-tinyimx}"
export VCPKG_BINARY_SOURCES="${VCPKG_BINARY_SOURCES:-clear;default,readwrite}"
unset X_VCPKG_ASSET_SOURCES || true
unset VCPKG_DOWNLOADS || true
export TINYIMX_ZK_ENSEMBLE_BASE="$ENSEMBLE_BASE"
export TINYIMX_ZK_ENSEMBLE_CLIENT_BASE="$ENSEMBLE_CLIENT_BASE"
export TINYIMX_ZK_ENSEMBLE_PEER_BASE="$ENSEMBLE_PEER_BASE"
export TINYIMX_ZK_ENSEMBLE_ELECTION_BASE="$ENSEMBLE_ELECTION_BASE"

mkdir -p "$ARTIFACT_DIR/ensemble" "$ARTIFACT_DIR/services" "$ARTIFACT_DIR/gateway" "$ARTIFACT_DIR/failure-matrix"
chmod 700 "$TMP_DIR"
printf 'case\tstatus\tlog\n' > "$SUMMARY"

PASS_COUNT=0
FAIL_COUNT=0
USER_A_PID=""; USER_B_PID=""; USER_C_PID=""; USER_D_PID=""
SOCIAL_A_PID=""; SOCIAL_B_PID=""
MESSAGE_A_PID=""; MESSAGE_B_PID=""
GATEWAY_PID=""; FAULT_GATEWAY_PID=""; STATIC_GATEWAY_PID=""; ZK_PROXY_PID=""

log(){ printf '[M15-C] %s\n' "$*"; }
fail(){ log "FAIL: $*"; if [[ -n "${SUMMARY:-}" && -f "${SUMMARY:-}" ]]; then printf '__fatal__\tFAIL\t%s\n' "$*" >> "$SUMMARY"; fi; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }
record(){
  local name="$1" status="$2" file="$3"
  printf '%s\t%s\t%s\n' "$name" "$status" "$file" >> "$SUMMARY"
  if [[ "$status" == PASS ]]; then PASS_COUNT=$((PASS_COUNT+1)); else FAIL_COUNT=$((FAIL_COUNT+1)); fi
}
pass_case(){ log "PASS $1"; record "$1" PASS "${2:-}"; }

free_port(){ python3 - <<'PY'
import socket
s=socket.socket(); s.bind(('127.0.0.1',0)); print(s.getsockname()[1]); s.close()
PY
}

port_open(){
  python3 - "$1" <<'PY'
import socket,sys
s=socket.socket(); s.settimeout(.2)
try: s.connect(('127.0.0.1',int(sys.argv[1])))
except OSError: raise SystemExit(1)
finally: s.close()
PY
}

wait_port(){
  local port="$1" pid="$2" label="$3" timeout="${4:-20}"
  python3 - "$port" "$pid" "$label" "$timeout" <<'PY'
import os,socket,sys,time
port=int(sys.argv[1]); pid=int(sys.argv[2]); label=sys.argv[3]; timeout=float(sys.argv[4])
end=time.monotonic()+timeout
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
regex=re.compile(pattern); end=time.monotonic()+timeout
while time.monotonic()<end:
    try: os.kill(pid,0)
    except OSError: raise SystemExit(f'{label} exited while waiting for log marker')
    try: text=open(path,encoding='utf-8',errors='replace').read()
    except FileNotFoundError: text=''
    if regex.search(text): raise SystemExit(0)
    time.sleep(.1)
raise SystemExit(f'{label} log marker timeout: {pattern}')
PY
}

wait_log_sequence_after(){
  local file="$1" offset="$2" pid="$3" label="$4" timeout="$5"; shift 5
  python3 - "$file" "$offset" "$pid" "$label" "$timeout" "$@" <<'PY'
import os,re,sys,time
path=sys.argv[1]; offset=int(sys.argv[2]); pid=int(sys.argv[3]); label=sys.argv[4]; timeout=float(sys.argv[5])
patterns=[re.compile(x) for x in sys.argv[6:]]; end=time.monotonic()+timeout
while time.monotonic()<end:
    try: os.kill(pid,0)
    except OSError: raise SystemExit(f'{label} exited while waiting for ordered markers')
    try:
        with open(path,'rb') as f: f.seek(offset); text=f.read().decode('utf-8',errors='replace')
    except FileNotFoundError: text=''
    pos=0
    for pattern in patterns:
        m=pattern.search(text,pos)
        if not m: break
        pos=m.end()
    else: raise SystemExit(0)
    time.sleep(.1)
raise SystemExit('ordered log sequence timeout: ' + ' -> '.join(x.pattern for x in patterns))
PY
}

stop_pid_value(){
  local pid="${1:-}" signal="${2:-TERM}"
  [[ -z "$pid" ]] && return 0
  if kill -0 "$pid" 2>/dev/null; then
    kill "-$signal" "$pid" 2>/dev/null || true
    for _ in $(seq 1 120); do kill -0 "$pid" 2>/dev/null || break; sleep .1; done
    if kill -0 "$pid" 2>/dev/null; then kill -KILL "$pid" 2>/dev/null || true; fi
  fi
  wait "$pid" 2>/dev/null || true
}

stop_var(){
  local var_name="${1:-}"
  local signal="${2:-TERM}"
  [[ "$var_name" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || fail "invalid PID variable name: $var_name"
  local -n pid_ref="$var_name"
  local pid="${pid_ref:-}"
  stop_pid_value "$pid" "$signal"
  pid_ref=""
}

copy_ensemble_logs(){
  local id
  set +e
  if [[ -d "$ENSEMBLE_BASE" ]]; then
    for id in 1 2 3; do
      mkdir -p "$ARTIFACT_DIR/ensemble/node$id"
      cp -a "$ENSEMBLE_BASE/node$id/conf" "$ARTIFACT_DIR/ensemble/node$id/" 2>/dev/null || true
      cp -a "$ENSEMBLE_BASE/node$id/logs" "$ARTIFACT_DIR/ensemble/node$id/" 2>/dev/null || true
      cp -a "$ENSEMBLE_BASE/node$id/data/myid" "$ARTIFACT_DIR/ensemble/node$id/" 2>/dev/null || true
    done
  fi
  set -e
}

cleanup(){
  set +e
  stop_var STATIC_GATEWAY_PID INT
  stop_var FAULT_GATEWAY_PID INT
  stop_var GATEWAY_PID INT
  stop_var ZK_PROXY_PID TERM
  stop_var MESSAGE_B_PID TERM; stop_var MESSAGE_A_PID TERM
  stop_var SOCIAL_B_PID TERM; stop_var SOCIAL_A_PID TERM
  stop_var USER_D_PID TERM; stop_var USER_C_PID TERM; stop_var USER_B_PID TERM; stop_var USER_A_PID TERM
  copy_ensemble_logs
  "$ENSEMBLE_SCRIPT" stop >/dev/null 2>&1 || true
  rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT INT TERM

for cmd in cmake python3 grep awk sed timeout; do require_cmd "$cmd"; done
[[ -f "$SOURCE_CONFIG" ]] || fail "source config missing: $SOURCE_CONFIG"
[[ -x "$ENSEMBLE_SCRIPT" ]] || fail "ensemble helper missing or not executable: $ENSEMBLE_SCRIPT"
[[ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]] || fail "vcpkg toolchain missing"

# Refuse to trample unrelated listeners. Users can override all three base-port
# ranges if their VM already uses these defaults.
for base in "$ENSEMBLE_CLIENT_BASE" "$ENSEMBLE_PEER_BASE" "$ENSEMBLE_ELECTION_BASE"; do
  for offset in 0 1 2; do
    p=$((base+offset))
    if port_open "$p"; then fail "ensemble port already in use: $p (override TINYIMX_ZK_ENSEMBLE_*_BASE)"; fi
  done
done

log "static architecture hard gate"
if grep -E -n 'zookeeper|ZooKeeper|zhandle_t|zoo_|GetChildren|AddWatchObserver' \
    "$ROOT_DIR/gateway/GatewayServer.cpp" "$ROOT_DIR/gateway/GatewayServer.h"; then
  fail "ZooKeeper leaked into GatewayServer"
fi
for proto in "$ROOT_DIR"/proto/tinyimx/*/v1/*.proto; do
  if grep -E -n 'zookeeper|ZooKeeper|ephemeral_owner|registry_path|service_discovery' "$proto"; then
    fail "registry metadata leaked into business proto: $proto"
  fi
done
if grep -E -n 'ZooKeeper|zookeeper|zhandle_t|zoo_' \
    "$ROOT_DIR/services/rpc/UserRpcClient."* "$ROOT_DIR/services/rpc/SocialRpcClient."* "$ROOT_DIR/services/rpc/MessageRpcClient."*; then
  fail "ZooKeeper leaked into RpcClient implementation"
fi
pass_case architecture-boundary "$ARTIFACT_DIR/failure-matrix/architecture.log"

log "configure/build M15-C focused targets (-j$BUILD_JOBS)"
(cd "$ROOT_DIR" && cmake --preset linux-debug) >"$ARTIFACT_DIR/build-configure.log" 2>&1
cmake --build "$BUILD_DIR" --target \
  zookeeper_failover_recovery_integration_tests \
  zookeeper_discovery_probe zookeeper_registry_probe \
  user_service_demo social_service_demo message_service_demo gateway_demo \
  gateway_login_auth_client_demo gateway_login_case_client_demo \
  gateway_friend_list_client_demo gateway_history_client_demo gateway_session_client_demo \
  -j"$BUILD_JOBS" >"$ARTIFACT_DIR/build.log" 2>&1
pass_case debug-focused-build "$ARTIFACT_DIR/build.log"

log "start isolated 3-node ZooKeeper ensemble"
"$ENSEMBLE_SCRIPT" start | tee "$ARTIFACT_DIR/ensemble/start.log"
ZK_CONNECT="$("$ENSEMBLE_SCRIPT" connect-string)"
"$ENSEMBLE_SCRIPT" wait-all | tee "$ARTIFACT_DIR/ensemble/wait-all.log"
"$ENSEMBLE_SCRIPT" status | tee "$ARTIFACT_DIR/ensemble/status-initial.log"
pass_case ensemble-bootstrap "$ARTIFACT_DIR/ensemble/status-initial.log"

log "focused rapid-churn + discovery restart integration"
"$BUILD_DIR/zookeeper_failover_recovery_integration_tests" "$ZK_CONNECT" "$FOCUSED_ROOT" \
  | tee "$ARTIFACT_DIR/failure-matrix/churn-integration.log"
grep -q '\[PASS\] M15-C failover/recovery integration tests' "$ARTIFACT_DIR/failure-matrix/churn-integration.log"
pass_case rapid-churn "$ARTIFACT_DIR/failure-matrix/churn-integration.log"

# Allocate the production-like service topology.
USER_A_PORT="$(free_port)"; USER_B_PORT="$(free_port)"; USER_C_PORT="$(free_port)"; USER_D_PORT="$(free_port)"
SOCIAL_A_PORT="$(free_port)"; SOCIAL_B_PORT="$(free_port)"
MESSAGE_A_PORT="$(free_port)"; MESSAGE_B_PORT="$(free_port)"
GATEWAY_PORT="$(free_port)"; FAULT_GATEWAY_PORT="$(free_port)"; STATIC_GATEWAY_PORT="$(free_port)"; FAILCLOSE_GATEWAY_PORT="$(free_port)"; PROXY_PORT="$(free_port)"; DEAD_ZK_PORT="$(free_port)"
USER_A_TARGET="127.0.0.1:$USER_A_PORT"; USER_B_TARGET="127.0.0.1:$USER_B_PORT"; USER_C_TARGET="127.0.0.1:$USER_C_PORT"; USER_D_TARGET="127.0.0.1:$USER_D_PORT"
SOCIAL_A_TARGET="127.0.0.1:$SOCIAL_A_PORT"; SOCIAL_B_TARGET="127.0.0.1:$SOCIAL_B_PORT"
MESSAGE_A_TARGET="127.0.0.1:$MESSAGE_A_PORT"; MESSAGE_B_TARGET="127.0.0.1:$MESSAGE_B_PORT"
USER_A_PATH="$SERVICE_ROOT/user/user@$USER_A_TARGET"; USER_B_PATH="$SERVICE_ROOT/user/user@$USER_B_TARGET"; USER_C_PATH="$SERVICE_ROOT/user/user@$USER_C_TARGET"; USER_D_PATH="$SERVICE_ROOT/user/user@$USER_D_TARGET"
SOCIAL_A_PATH="$SERVICE_ROOT/social/social@$SOCIAL_A_TARGET"; SOCIAL_B_PATH="$SERVICE_ROOT/social/social@$SOCIAL_B_TARGET"
MESSAGE_A_PATH="$SERVICE_ROOT/message/message@$MESSAGE_A_TARGET"; MESSAGE_B_PATH="$SERVICE_ROOT/message/message@$MESSAGE_B_TARGET"

SERVICE_CONFIG="$TMP_DIR/services.json"
GATEWAY_CONFIG="$TMP_DIR/gateway.json"
FAULT_GATEWAY_CONFIG="$TMP_DIR/gateway-fault.json"
STATIC_CONFIG="$TMP_DIR/gateway-static.json"
FAILCLOSE_CONFIG="$TMP_DIR/gateway-failclose.json"

# Proxy starts later; use a placeholder here and rewrite its connect string after
# selecting a healthy ensemble member.
python3 - "$SOURCE_CONFIG" "$SERVICE_CONFIG" "$GATEWAY_CONFIG" "$FAULT_GATEWAY_CONFIG" "$STATIC_CONFIG" "$FAILCLOSE_CONFIG" \
  "$ZK_CONNECT" "$SERVICE_ROOT" "$SESSION_TIMEOUT_MS" "$CONNECT_TIMEOUT_MS" "$STALE_AFTER_MS" \
  "$GATEWAY_PORT" "$FAULT_GATEWAY_PORT" "$STATIC_GATEWAY_PORT" "$FAILCLOSE_GATEWAY_PORT" "$DEAD_ZK_PORT" "$ARTIFACT_DIR" <<'PY'
import json,os,sys
(src,svc_path,gw_path,fault_path,static_path,failclose_path,zk,root,session_ms,connect_ms,stale_ms,gw_port,fault_port,static_port,failclose_port,dead_port,art)=sys.argv[1:]
with open(src,encoding='utf-8') as f: base=json.load(f)
if not base.setdefault('mysql',{}).get('enable',False): raise SystemExit('source config must have mysql.enable=true')

def common(cfg,connect):
    cfg.setdefault('gateway_registry',{})['enable']=False
    cfg['zookeeper']={'enable':True,'connect_string':connect,'session_timeout_ms':int(session_ms),'connect_timeout_ms':int(connect_ms),'service_root':root,'advertise_host':'127.0.0.1','service_version':'v1'}
    cfg.setdefault('logger',{})['console']=True
    return cfg
svc=common(json.loads(json.dumps(base)),zk)
svc['service_discovery']={'provider':'static','initial_sync_timeout_ms':int(connect_ms),'snapshot_stale_after_ms':int(stale_ms),'retain_last_known_good':True}
svc['logger']['file']=os.path.join(art,'services','service-internal.log')

def gateway(cfg,connect,port,instance):
    common(cfg,connect)
    cfg.setdefault('server',{})['host']='127.0.0.1'; cfg['server']['port']=int(port)
    cfg.setdefault('app',{})['instance_id']=instance
    cfg['service_discovery']={'provider':'zookeeper','initial_sync_timeout_ms':int(connect_ms),'snapshot_stale_after_ms':int(stale_ms),'retain_last_known_good':True}
    return cfg
gw=gateway(json.loads(json.dumps(base)),zk,gw_port,f'gateway-m15-c-{os.getpid()}'); gw['logger']['file']=os.path.join(art,'gateway','dynamic-internal.log')
fault=gateway(json.loads(json.dumps(base)),'127.0.0.1:1',fault_port,f'gateway-m15-c-fault-{os.getpid()}'); fault['logger']['file']=os.path.join(art,'gateway','fault-internal.log')
failclose=gateway(json.loads(json.dumps(base)),f'127.0.0.1:{dead_port}',failclose_port,f'gateway-m15-c-failclose-{os.getpid()}'); failclose['zookeeper']['connect_timeout_ms']=1200; failclose['service_discovery']['initial_sync_timeout_ms']=1200; failclose['logger']['file']=os.path.join(art,'gateway','failclose-internal.log')
static=json.loads(json.dumps(base)); static.setdefault('gateway_registry',{})['enable']=False
static.setdefault('server',{})['host']='127.0.0.1'; static['server']['port']=int(static_port); static.setdefault('app',{})['instance_id']=f'gateway-m15-c-static-{os.getpid()}'
static['zookeeper']={'enable':False,'connect_string':f'127.0.0.1:{dead_port}','session_timeout_ms':int(session_ms),'connect_timeout_ms':1200,'service_root':root,'advertise_host':'127.0.0.1','service_version':'v1'}
static['service_discovery']={'provider':'static','initial_sync_timeout_ms':1200,'snapshot_stale_after_ms':int(stale_ms),'retain_last_known_good':True}
static.setdefault('logger',{})['console']=True; static['logger']['file']=os.path.join(art,'gateway','static-internal.log')
for path,obj in ((svc_path,svc),(gw_path,gw),(fault_path,fault),(static_path,static),(failclose_path,failclose)):
    with open(path,'w',encoding='utf-8') as f: json.dump(obj,f,indent=2); f.write('\n')
    os.chmod(path,0o600)
PY

start_user(){ local label="$1" target="$2" var="$3"; local file="$ARTIFACT_DIR/services/${label}.log"; TINYIMX_USER_LISTEN_TARGET="$target" "$BUILD_DIR/user_service_demo" "$SERVICE_CONFIG" >"$file" 2>&1 & printf -v "$var" '%s' "$!"; local port="${target##*:}"; wait_port "$port" "${!var}" "$label"; wait_log "$file" 'UserService ready.*zookeeper_registered=1' "${!var}" "$label" 15; }
start_social(){ local label="$1" target="$2" var="$3"; local file="$ARTIFACT_DIR/services/${label}.log"; TINYIMX_SOCIAL_LISTEN_TARGET="$target" "$BUILD_DIR/social_service_demo" "$SERVICE_CONFIG" >"$file" 2>&1 & printf -v "$var" '%s' "$!"; local port="${target##*:}"; wait_port "$port" "${!var}" "$label"; wait_log "$file" 'SocialService ready.*zookeeper_registered=1' "${!var}" "$label" 15; }
start_message(){ local label="$1" target="$2" var="$3"; local file="$ARTIFACT_DIR/services/${label}.log"; TINYIMX_MESSAGE_LISTEN_TARGET="$target" "$BUILD_DIR/message_service_demo" "$SERVICE_CONFIG" >"$file" 2>&1 & printf -v "$var" '%s' "$!"; local port="${target##*:}"; wait_port "$port" "${!var}" "$label"; wait_log "$file" 'MessageService ready.*zookeeper_registered=1' "${!var}" "$label" 15; }

log "start two instances for each RPC service domain"
start_user user-a "$USER_A_TARGET" USER_A_PID
start_user user-b "$USER_B_TARGET" USER_B_PID
start_social social-a "$SOCIAL_A_TARGET" SOCIAL_A_PID
start_social social-b "$SOCIAL_B_TARGET" SOCIAL_B_PID
start_message message-a "$MESSAGE_A_TARGET" MESSAGE_A_PID
start_message message-b "$MESSAGE_B_TARGET" MESSAGE_B_PID
for spec in \
  "user-a:$USER_A_PATH" "user-b:$USER_B_PATH" \
  "social-a:$SOCIAL_A_PATH" "social-b:$SOCIAL_B_PATH" \
  "message-a:$MESSAGE_A_PATH" "message-b:$MESSAGE_B_PATH"; do
  label="${spec%%:*}"; path="${spec#*:}"
  "$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$path" present 7000 >"$ARTIFACT_DIR/services/${label}-registration.log"
done
pass_case six-service-memberships "$ARTIFACT_DIR/services"

log "start Gateway using full ensemble connect string and no static targets"
env -u TINYIMX_USER_RPC_TARGET -u TINYIMX_SOCIAL_RPC_TARGET -u TINYIMX_MESSAGE_RPC_TARGET \
  "$BUILD_DIR/gateway_demo" "$GATEWAY_CONFIG" >"$ARTIFACT_DIR/gateway/dynamic.log" 2>&1 & GATEWAY_PID=$!
wait_port "$GATEWAY_PORT" "$GATEWAY_PID" DynamicGateway
wait_log "$ARTIFACT_DIR/gateway/dynamic.log" 'gateway dynamic RPC discovery enabled' "$GATEWAY_PID" DynamicGateway 15
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/gateway/baseline-login.log"
"$BUILD_DIR/gateway_friend_list_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/gateway/baseline-friends.log"
"$BUILD_DIR/gateway_history_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/gateway/baseline-history.log"
"$BUILD_DIR/gateway_session_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/gateway/baseline-session.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/gateway/baseline-login.log"
grep -q 'gateway friend list validation passed' "$ARTIFACT_DIR/gateway/baseline-friends.log"
grep -q 'gateway history validation passed' "$ARTIFACT_DIR/gateway/baseline-history.log"
grep -q 'gateway session client validation passed' "$ARTIFACT_DIR/gateway/baseline-session.log"
pass_case baseline-dynamic-business "$ARTIFACT_DIR/gateway"

probe_owner(){
  local path="$1" log_file="$2"
  "$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$path" present 7000 | tee "$log_file" >/dev/null
  awk -F= '/^ephemeral_owner=/{print $2; exit}' "$log_file"
}

log "C1 follower failure: quorum and service sessions must survive"
LEADER_ID="$("$ENSEMBLE_SCRIPT" leader)"; [[ "$LEADER_ID" =~ ^[123]$ ]] || fail "cannot determine ensemble leader"
FOLLOWER_ID=""
for candidate_id in 1 2 3; do
  if [[ "$candidate_id" != "$LEADER_ID" ]]; then
    FOLLOWER_ID="$candidate_id"
    break
  fi
done
OWNER_BEFORE="$(probe_owner "$USER_A_PATH" "$ARTIFACT_DIR/failure-matrix/follower-owner-before.log")"
"$ENSEMBLE_SCRIPT" kill-node "$FOLLOWER_ID" | tee "$ARTIFACT_DIR/failure-matrix/follower-stop.log"
"$ENSEMBLE_SCRIPT" wait-quorum | tee "$ARTIFACT_DIR/failure-matrix/follower-quorum.log"
OWNER_AFTER="$(probe_owner "$USER_A_PATH" "$ARTIFACT_DIR/failure-matrix/follower-owner-after.log")"
[[ "$OWNER_BEFORE" == "$OWNER_AFTER" ]] || fail "service ZooKeeper session changed across follower failure"
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/login-after-follower.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/failure-matrix/login-after-follower.log"
"$ENSEMBLE_SCRIPT" start-node "$FOLLOWER_ID" | tee "$ARTIFACT_DIR/failure-matrix/follower-restart.log"
"$ENSEMBLE_SCRIPT" wait-all | tee -a "$ARTIFACT_DIR/failure-matrix/follower-restart.log"
pass_case ensemble-follower-failure "$ARTIFACT_DIR/failure-matrix/follower-stop.log"

log "C1 leader failure: re-election must preserve service ownership and data plane"
OLD_LEADER="$("$ENSEMBLE_SCRIPT" leader)"; [[ "$OLD_LEADER" =~ ^[123]$ ]] || fail "cannot determine pre-failure leader"
OWNER_BEFORE="$(probe_owner "$USER_A_PATH" "$ARTIFACT_DIR/failure-matrix/leader-owner-before.log")"
LEADER_FAIL_START_MS="$(date +%s%3N)"
"$ENSEMBLE_SCRIPT" kill-node "$OLD_LEADER" | tee "$ARTIFACT_DIR/failure-matrix/leader-stop.log"
"$ENSEMBLE_SCRIPT" wait-quorum | tee "$ARTIFACT_DIR/failure-matrix/leader-quorum.log"
LEADER_FAIL_END_MS="$(date +%s%3N)"
LEADER_FAILOVER_MS=$((LEADER_FAIL_END_MS-LEADER_FAIL_START_MS))
printf 'leader_failover_convergence_ms=%s\n' "$LEADER_FAILOVER_MS" >> "$ARTIFACT_DIR/failure-matrix/leader-quorum.log"
NEW_LEADER="$("$ENSEMBLE_SCRIPT" leader)"; [[ "$NEW_LEADER" =~ ^[123]$ && "$NEW_LEADER" != "$OLD_LEADER" ]] || fail "leader election did not converge to a different node"
OWNER_AFTER="$(probe_owner "$USER_A_PATH" "$ARTIFACT_DIR/failure-matrix/leader-owner-after.log")"
[[ "$OWNER_BEFORE" == "$OWNER_AFTER" ]] || fail "service ZooKeeper session changed across leader failover"
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/login-after-leader.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/failure-matrix/login-after-leader.log"
"$ENSEMBLE_SCRIPT" start-node "$OLD_LEADER" | tee "$ARTIFACT_DIR/failure-matrix/leader-restart.log"
"$ENSEMBLE_SCRIPT" wait-all | tee -a "$ARTIFACT_DIR/failure-matrix/leader-restart.log"
pass_case ensemble-leader-failover "$ARTIFACT_DIR/failure-matrix/leader-stop.log"

log "C2 User graceful unregister: routing must converge without session-timeout delay"
GRACEFUL_START_MS="$(date +%s%3N)"
stop_var USER_A_PID TERM
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$USER_A_PATH" absent 3000 >"$ARTIFACT_DIR/failure-matrix/user-a-graceful-absent.log"
GRACEFUL_END_MS="$(date +%s%3N)"; GRACEFUL_MS=$((GRACEFUL_END_MS-GRACEFUL_START_MS))
(( GRACEFUL_MS <= 3000 )) || fail "graceful UserService membership removal exceeded 3s: ${GRACEFUL_MS}ms"
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/login-after-user-graceful.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/failure-matrix/login-after-user-graceful.log"
printf 'convergence_ms=%s\n' "$GRACEFUL_MS" >> "$ARTIFACT_DIR/failure-matrix/user-a-graceful-absent.log"
pass_case user-graceful-failover "$ARTIFACT_DIR/failure-matrix/user-a-graceful-absent.log"

log "restart User-A then SIGKILL User-B; cleanup must follow Session expiry"
start_user user-a-restarted "$USER_A_TARGET" USER_A_PID
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$USER_A_PATH" present 5000 >"$ARTIFACT_DIR/failure-matrix/user-a-reregister.log"
kill -KILL "$USER_B_PID"; wait "$USER_B_PID" 2>/dev/null || true; USER_B_PID=""
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$USER_B_PATH" absent "$((SESSION_TIMEOUT_MS+5000))" >"$ARTIFACT_DIR/failure-matrix/user-b-crash-absent.log"
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/login-after-user-crash.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/failure-matrix/login-after-user-crash.log"
pass_case user-crash-failover "$ARTIFACT_DIR/failure-matrix/user-b-crash-absent.log"

log "C2 Social failover"
stop_var SOCIAL_A_PID TERM
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$SOCIAL_A_PATH" absent 3000 >"$ARTIFACT_DIR/failure-matrix/social-a-absent.log"
"$BUILD_DIR/gateway_friend_list_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/friends-after-social-failover.log"
grep -q 'gateway friend list validation passed' "$ARTIFACT_DIR/failure-matrix/friends-after-social-failover.log"
pass_case social-failover "$ARTIFACT_DIR/failure-matrix/social-a-absent.log"

log "C2 Message crash failover: both read and mutation paths must survive after convergence"
kill -KILL "$MESSAGE_A_PID"; wait "$MESSAGE_A_PID" 2>/dev/null || true; MESSAGE_A_PID=""
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$MESSAGE_A_PATH" absent "$((SESSION_TIMEOUT_MS+5000))" >"$ARTIFACT_DIR/failure-matrix/message-a-crash-absent.log"
"$BUILD_DIR/gateway_history_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/history-after-message-failover.log"
"$BUILD_DIR/gateway_session_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/session-after-message-failover.log"
grep -q 'gateway history validation passed' "$ARTIFACT_DIR/failure-matrix/history-after-message-failover.log"
grep -q 'gateway session client validation passed' "$ARTIFACT_DIR/failure-matrix/session-after-message-failover.log"
pass_case message-failover "$ARTIFACT_DIR/failure-matrix/message-a-crash-absent.log"

log "C2 persistent fake registration must never become routable"
ZK_HOME="$("$ENSEMBLE_SCRIPT" path)"; ZK_NODE1="$("$ENSEMBLE_SCRIPT" client-endpoint 1)"
FAKE_TARGET="127.0.0.1:59999"; FAKE_PATH="$SERVICE_ROOT/user/user@$FAKE_TARGET"
FAKE_JSON='{"schema_version":1,"service_name":"user","instance_id":"user@127.0.0.1:59999","target":"127.0.0.1:59999","protocol":"grpc","version":"v1"}'
"$ZK_HOME/bin/zkCli.sh" -server "$ZK_NODE1" create "$FAKE_PATH" "$FAKE_JSON" >"$ARTIFACT_DIR/failure-matrix/persistent-fake-create.log" 2>&1
grep -q 'Created ' "$ARTIFACT_DIR/failure-matrix/persistent-fake-create.log" || fail "failed to create persistent fake registration"
"$BUILD_DIR/zookeeper_discovery_probe" "$ZK_CONNECT" "$SERVICE_ROOT" user absent "$FAKE_TARGET" 5000 >"$ARTIFACT_DIR/failure-matrix/persistent-fake-discovery.log"
"$ZK_HOME/bin/zkCli.sh" -server "$ZK_NODE1" delete "$FAKE_PATH" >"$ARTIFACT_DIR/failure-matrix/persistent-fake-delete.log" 2>&1 || true
pass_case persistent-node-rejected "$ARTIFACT_DIR/failure-matrix/persistent-fake-discovery.log"

log "C2 late binding: Gateway survives zero UserService instances and learns a later instance"
stop_var USER_A_PID TERM
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$USER_A_PATH" absent 3000 >"$ARTIFACT_DIR/failure-matrix/user-a-late-stop.log"
"$BUILD_DIR/zookeeper_discovery_probe" "$ZK_CONNECT" "$SERVICE_ROOT" user count 0 5000 >"$ARTIFACT_DIR/failure-matrix/user-empty-snapshot.log"
"$BUILD_DIR/gateway_login_case_client_demo" 127.0.0.1 "$GATEWAY_PORT" user10001 123456 0 auth_unavailable >"$ARTIFACT_DIR/failure-matrix/login-no-user.log"
grep -q 'gateway login case validation passed' "$ARTIFACT_DIR/failure-matrix/login-no-user.log"
kill -0 "$GATEWAY_PID" 2>/dev/null || fail "Gateway died with authoritative empty UserService membership"
start_user user-c-late "$USER_C_TARGET" USER_C_PID
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$USER_C_PATH" present 5000 >"$ARTIFACT_DIR/failure-matrix/user-c-late-register.log"
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/login-after-late-user.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/failure-matrix/login-after-late-user.log"
pass_case late-service-discovery "$ARTIFACT_DIR/failure-matrix/login-after-late-user.log"

log "C2 fresh dynamic Gateway must fail closed when ZooKeeper is unreachable"
set +e
timeout 8 env -u TINYIMX_USER_RPC_TARGET -u TINYIMX_SOCIAL_RPC_TARGET -u TINYIMX_MESSAGE_RPC_TARGET \
  "$BUILD_DIR/gateway_demo" "$FAILCLOSE_CONFIG" >"$ARTIFACT_DIR/failure-matrix/gateway-startup-failclose.log" 2>&1
FAILCLOSE_STATUS=$?
set -e
if [[ $FAILCLOSE_STATUS -eq 0 || $FAILCLOSE_STATUS -eq 124 ]]; then fail "fresh dynamic Gateway did not fail closed on unreachable ZooKeeper, status=$FAILCLOSE_STATUS"; fi
grep -Eq 'ZooKeeper.*(start failed|initial sync failed)|discovery.*failed' "$ARTIFACT_DIR/failure-matrix/gateway-startup-failclose.log" || fail "fail-close log lacks ZooKeeper startup failure marker"
pass_case startup-fail-close "$ARTIFACT_DIR/failure-matrix/gateway-startup-failclose.log"

log "C2 static provider is independent from ZooKeeper availability"
env TINYIMX_USER_RPC_TARGET="$USER_C_TARGET" TINYIMX_SOCIAL_RPC_TARGET="$SOCIAL_B_TARGET" TINYIMX_MESSAGE_RPC_TARGET="$MESSAGE_B_TARGET" \
  "$BUILD_DIR/gateway_demo" "$STATIC_CONFIG" >"$ARTIFACT_DIR/gateway/static.log" 2>&1 & STATIC_GATEWAY_PID=$!
wait_port "$STATIC_GATEWAY_PORT" "$STATIC_GATEWAY_PID" StaticGateway
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$STATIC_GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/static-login.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/failure-matrix/static-login.log"
stop_var STATIC_GATEWAY_PID INT
pass_case static-provider-with-dead-zk "$ARTIFACT_DIR/failure-matrix/static-login.log"

# Replace the normal dynamic Gateway with an isolated fault Gateway whose only
# ZooKeeper path traverses a controllable TCP proxy. Services stay connected to
# the live 3-node ensemble, so this is a true client-network-partition test.
stop_var GATEWAY_PID INT
PROXY_SCRIPT="$TMP_DIR/zk_tcp_proxy.py"
cat > "$PROXY_SCRIPT" <<'PY'
import signal,socket,sys,threading
lh='127.0.0.1'; lp=int(sys.argv[1]); uh=sys.argv[2]; up=int(sys.argv[3]); stop=threading.Event(); conns=set(); lock=threading.Lock()
def close(s):
    try:s.shutdown(socket.SHUT_RDWR)
    except OSError:pass
    try:s.close()
    except OSError:pass
def shutdown(*_):
    stop.set(); close(server)
    with lock: current=list(conns)
    for s in current: close(s)
def pump(a,b):
    try:
        while not stop.is_set():
            data=a.recv(65536)
            if not data: break
            b.sendall(data)
    except OSError: pass
    finally: close(a); close(b)
def handle(c):
    u=None
    try:
        u=socket.create_connection((uh,up),timeout=3); u.settimeout(None); c.settimeout(None)
        with lock: conns.update((c,u))
        t=threading.Thread(target=pump,args=(c,u),daemon=True); t.start(); pump(u,c); t.join(timeout=1)
    except OSError: close(c); u is not None and close(u)
    finally:
        with lock:
            conns.discard(c)
            if u is not None: conns.discard(u)
signal.signal(signal.SIGTERM,shutdown); signal.signal(signal.SIGINT,shutdown)
server=socket.socket(); server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); server.bind((lh,lp)); server.listen(32); server.settimeout(.5)
print(f'proxy_ready={lh}:{lp}->{uh}:{up}',flush=True)
while not stop.is_set():
    try:c,_=server.accept()
    except socket.timeout:continue
    except OSError:break
    threading.Thread(target=handle,args=(c,),daemon=True).start()
shutdown()
PY
chmod 600 "$PROXY_SCRIPT"
UPSTREAM_ENDPOINT="$("$ENSEMBLE_SCRIPT" client-endpoint "$("$ENSEMBLE_SCRIPT" leader)")"
UPSTREAM_HOST="${UPSTREAM_ENDPOINT%:*}"; UPSTREAM_PORT="${UPSTREAM_ENDPOINT##*:}"
start_proxy(){ python3 "$PROXY_SCRIPT" "$PROXY_PORT" "$UPSTREAM_HOST" "$UPSTREAM_PORT" >"$1" 2>&1 & ZK_PROXY_PID=$!; wait_port "$PROXY_PORT" "$ZK_PROXY_PID" ZooKeeperProxy; grep -q '^proxy_ready=' "$1"; }
stop_proxy(){ stop_var ZK_PROXY_PID TERM; }
start_proxy "$ARTIFACT_DIR/failure-matrix/zk-proxy-start.log"
python3 - "$FAULT_GATEWAY_CONFIG" "$PROXY_PORT" <<'PY'
import json,sys
p=sys.argv[1]; port=int(sys.argv[2])
with open(p,encoding='utf-8') as f: x=json.load(f)
x['zookeeper']['connect_string']=f'127.0.0.1:{port}'
with open(p,'w',encoding='utf-8') as f: json.dump(x,f,indent=2); f.write('\n')
PY
env -u TINYIMX_USER_RPC_TARGET -u TINYIMX_SOCIAL_RPC_TARGET -u TINYIMX_MESSAGE_RPC_TARGET \
  "$BUILD_DIR/gateway_demo" "$FAULT_GATEWAY_CONFIG" >"$ARTIFACT_DIR/gateway/fault.log" 2>&1 & FAULT_GATEWAY_PID=$!
wait_port "$FAULT_GATEWAY_PORT" "$FAULT_GATEWAY_PID" FaultGateway
wait_log "$ARTIFACT_DIR/gateway/fault.log" 'gateway dynamic RPC discovery enabled' "$FAULT_GATEWAY_PID" FaultGateway 15

log "C2 short client partition: fresh LKG keeps data plane alive"
stop_proxy; sleep .5
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$FAULT_GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/login-short-lkg.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/failure-matrix/login-short-lkg.log"
SHORT_OFFSET="$(wc -c < "$ARTIFACT_DIR/gateway/fault.log")"
start_proxy "$ARTIFACT_DIR/failure-matrix/zk-proxy-short-restart.log"
wait_log_sequence_after "$ARTIFACT_DIR/gateway/fault.log" "$SHORT_OFFSET" "$FAULT_GATEWAY_PID" FaultGateway 15 \
  'ZooKeeper session connected.*generation=1' \
  'ZooKeeper discovery snapshot refreshed.*service=user'
pass_case short-partition-lkg "$ARTIFACT_DIR/failure-matrix/login-short-lkg.log"

log "C2 long client partition: stale fence then SessionExpired full recovery"
stop_proxy
python3 - "$STALE_AFTER_MS" <<'PY'
import sys,time; time.sleep((int(sys.argv[1])+1200)/1000.0)
PY
"$BUILD_DIR/gateway_login_case_client_demo" 127.0.0.1 "$FAULT_GATEWAY_PORT" user10001 123456 0 auth_unavailable >"$ARTIFACT_DIR/failure-matrix/login-stale.log"
grep -q 'gateway login case validation passed' "$ARTIFACT_DIR/failure-matrix/login-stale.log"
kill -0 "$FAULT_GATEWAY_PID" 2>/dev/null || fail "FaultGateway died while snapshot stale"
pass_case stale-route-fencing "$ARTIFACT_DIR/failure-matrix/login-stale.log"
REMAIN_MS=$((SESSION_TIMEOUT_MS + 1800 - STALE_AFTER_MS - 1200)); if (( REMAIN_MS > 0 )); then python3 - "$REMAIN_MS" <<'PY'
import sys,time; time.sleep(int(sys.argv[1])/1000.0)
PY
fi
RECOVERY_OFFSET="$(wc -c < "$ARTIFACT_DIR/gateway/fault.log")"
start_proxy "$ARTIFACT_DIR/failure-matrix/zk-proxy-expiry-restart.log"
wait_log_sequence_after "$ARTIFACT_DIR/gateway/fault.log" "$RECOVERY_OFFSET" "$FAULT_GATEWAY_PID" FaultGateway 25 \
  'ZooKeeper session expired' \
  'ZooKeeper session connected.*generation=2' \
  'ZooKeeper discovery snapshot refreshed.*service=user'
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$FAULT_GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/login-after-session-recovery.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/failure-matrix/login-after-session-recovery.log"
pass_case session-expired-recovery "$ARTIFACT_DIR/failure-matrix/login-after-session-recovery.log"

log "C2 post-recovery watch re-arm"
start_user user-d-post-recovery "$USER_D_TARGET" USER_D_PID
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$USER_D_PATH" present 5000 >"$ARTIFACT_DIR/failure-matrix/user-d-register.log"
stop_var USER_C_PID TERM
"$BUILD_DIR/zookeeper_registry_probe" "$ZK_CONNECT" "$USER_C_PATH" absent 3000 >"$ARTIFACT_DIR/failure-matrix/user-c-remove.log"
"$BUILD_DIR/gateway_login_auth_client_demo" 127.0.0.1 "$FAULT_GATEWAY_PORT" >"$ARTIFACT_DIR/failure-matrix/login-post-rewatch.log"
grep -q 'gateway login auth client validation passed' "$ARTIFACT_DIR/failure-matrix/login-post-rewatch.log"
pass_case post-session-rewatch "$ARTIFACT_DIR/failure-matrix/login-post-rewatch.log"

# Final ensemble state must return to all three members serving.
"$ENSEMBLE_SCRIPT" wait-all | tee "$ARTIFACT_DIR/ensemble/status-final.log"
"$ENSEMBLE_SCRIPT" status | tee -a "$ARTIFACT_DIR/ensemble/status-final.log"
pass_case ensemble-final-health "$ARTIFACT_DIR/ensemble/status-final.log"

copy_ensemble_logs
cat > "$ARTIFACT_DIR/summary.txt" <<TXT
TinyIMX M15-C Failover / Recovery Matrix
=========================================
ZooKeeper connect string: $ZK_CONNECT
Service root: $SERVICE_ROOT
PASS: $PASS_COUNT
FAIL: $FAIL_COUNT

Covered gates:
- 3-node ensemble bootstrap and quorum
- follower loss with service-session ownership continuity
- leader loss / re-election with service-session ownership continuity
- User graceful and crash failover
- Social failover
- Message read + mutation failover
- persistent fake registration rejection
- Gateway late service binding
- dynamic startup fail-close when ZooKeeper is unreachable
- explicit static-provider independence from ZooKeeper
- fresh LKG during short client partition
- stale route fencing
- SessionExpired -> new session -> full refresh
- watch re-arm after recovery
- rapid membership churn + Discovery restart authoritative sync
TXT

if (( FAIL_COUNT != 0 )); then fail "failure matrix has FAIL=$FAIL_COUNT"; fi
printf '\n[M15-C PASS] ZooKeeper ensemble + failover/recovery matrix passed.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
