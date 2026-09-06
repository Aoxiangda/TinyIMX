#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="3.8.5"
VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/toolchains/vcpkg-tinyimx}"
BASE="${TINYIMX_ZK_ENSEMBLE_BASE:-/tmp/tinyimx-zookeeper-ensemble}"
HOME_DIR="$BASE/apache-zookeeper-${VERSION}-bin"
ARCHIVE="$VCPKG_ROOT/downloads/apache-zookeeper-${VERSION}-bin.tar.gz"
CLIENT_BASE="${TINYIMX_ZK_ENSEMBLE_CLIENT_BASE:-22181}"
PEER_BASE="${TINYIMX_ZK_ENSEMBLE_PEER_BASE:-22881}"
ELECTION_BASE="${TINYIMX_ZK_ENSEMBLE_ELECTION_BASE:-23881}"
TICK_TIME="${TINYIMX_ZK_ENSEMBLE_TICK_TIME:-1000}"
INIT_LIMIT="${TINYIMX_ZK_ENSEMBLE_INIT_LIMIT:-10}"
SYNC_LIMIT="${TINYIMX_ZK_ENSEMBLE_SYNC_LIMIT:-5}"
START_TIMEOUT="${TINYIMX_ZK_ENSEMBLE_START_TIMEOUT:-30}"
JVMFLAGS="${TINYIMX_ZK_ENSEMBLE_JVMFLAGS:--Xms64m -Xmx256m}"

log(){ printf '[local-zookeeper-ensemble] %s\n' "$*"; }
fail(){ printf '[local-zookeeper-ensemble][FAIL] %s\n' "$*" >&2; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }

validate_configuration(){
  local name value
  for name in CLIENT_BASE PEER_BASE ELECTION_BASE; do
    value="${!name}"
    [[ "$value" =~ ^[0-9]+$ ]] || fail "$name must be numeric: $value"
    (( value >= 1024 && value + 2 <= 65535 )) || fail "$name port block is out of range: $value"
  done
  local ports=() base offset
  for base in "$CLIENT_BASE" "$PEER_BASE" "$ELECTION_BASE"; do
    for offset in 0 1 2; do ports+=("$((base+offset))"); done
  done
  [[ "$(printf '%s\n' "${ports[@]}" | sort -n -u | wc -l)" -eq 9 ]] || fail "client/peer/election port blocks overlap"
  [[ -n "$BASE" && "$BASE" != "/" && "$BASE" != "/tmp" ]] || fail "unsafe ensemble base: $BASE"
}

port_for(){ local base="$1" id="$2"; printf '%d\n' "$((base + id - 1))"; }
client_port(){ port_for "$CLIENT_BASE" "$1"; }
peer_port(){ port_for "$PEER_BASE" "$1"; }
election_port(){ port_for "$ELECTION_BASE" "$1"; }
node_dir(){ printf '%s/node%d\n' "$BASE" "$1"; }
node_conf_dir(){ printf '%s/conf\n' "$(node_dir "$1")"; }
node_data_dir(){ printf '%s/data\n' "$(node_dir "$1")"; }
node_log_dir(){ printf '%s/logs\n' "$(node_dir "$1")"; }
node_txn_dir(){ printf '%s/txnlog\n' "$(node_dir "$1")"; }
node_pid_file(){ printf '%s/zookeeper_server.pid\n' "$(node_dir "$1")"; }

connect_string(){
  printf '127.0.0.1:%s,127.0.0.1:%s,127.0.0.1:%s\n' \
    "$(client_port 1)" "$(client_port 2)" "$(client_port 3)"
}

four_letter(){
  local port="$1" command="$2" timeout="${3:-2.0}"
  python3 - "$port" "$command" "$timeout" <<'PY'
import socket,sys
port=int(sys.argv[1]); command=sys.argv[2].encode(); timeout=float(sys.argv[3])
s=socket.socket(); s.settimeout(timeout)
try:
    s.connect(('127.0.0.1',port)); s.sendall(command)
    chunks=[]
    while True:
        try: data=s.recv(65536)
        except socket.timeout: break
        if not data: break
        chunks.append(data)
finally:
    s.close()
sys.stdout.write(b''.join(chunks).decode(errors='replace'))
PY
}

port_open(){
  python3 - "$1" <<'PY'
import socket,sys
s=socket.socket(); s.settimeout(.2)
try:
    s.connect(('127.0.0.1',int(sys.argv[1])))
except OSError:
    raise SystemExit(1)
finally:
    s.close()
PY
}

node_mode(){
  local id="$1" out
  out="$(four_letter "$(client_port "$id")" srvr 1.5 2>/dev/null || true)"
  printf '%s\n' "$out" | awk -F': ' '/^Mode:/{print $2; exit}'
}

node_health(){
  local id="$1" response
  response="$(four_letter "$(client_port "$id")" ruok 1.5 2>/dev/null || true)"
  [[ "$response" == "imok" ]]
}

wait_node_port_closed(){
  local id="$1" deadline=$((SECONDS + 15)) port
  port="$(client_port "$id")"
  while (( SECONDS < deadline )); do
    if ! port_open "$port"; then return 0; fi
    sleep .2
  done
  return 1
}

wait_quorum(){
  local deadline=$((SECONDS + START_TIMEOUT)) leader_count serving_count mode id
  while (( SECONDS < deadline )); do
    leader_count=0; serving_count=0
    for id in 1 2 3; do
      mode="$(node_mode "$id")"
      case "$mode" in
        leader) leader_count=$((leader_count + 1)); serving_count=$((serving_count + 1));;
        follower|observer) serving_count=$((serving_count + 1));;
      esac
    done
    if (( leader_count == 1 && serving_count >= 2 )); then return 0; fi
    sleep .25
  done
  status || true
  return 1
}

require_runtime(){
  validate_configuration
  require_cmd java
  require_cmd python3
  require_cmd tar
  require_cmd ps
  if [[ ! -f "$ARCHIVE" ]]; then
    log "ZooKeeper binary archive missing; bootstrapping frozen vcpkg distfiles"
    VCPKG_ROOT="$VCPKG_ROOT" "$ROOT/scripts/bootstrap_zookeeper_vcpkg_distfiles.sh"
  fi
  [[ -f "$ARCHIVE" ]] || fail "ZooKeeper archive not found after bootstrap: $ARCHIVE"
}

write_node_config(){
  local id="$1" conf_dir data_dir logs_dir txn_dir client
  conf_dir="$(node_conf_dir "$id")"; data_dir="$(node_data_dir "$id")"; logs_dir="$(node_log_dir "$id")"; txn_dir="$(node_txn_dir "$id")"; client="$(client_port "$id")"
  mkdir -p "$conf_dir" "$data_dir" "$logs_dir" "$txn_dir"
  printf '%s\n' "$id" > "$data_dir/myid"
  cat > "$conf_dir/zoo.cfg" <<CFG
# TinyIMX M15-C local 3-node ZooKeeper ensemble.
# Runtime state is isolated under /tmp by default and is never repository data.
tickTime=$TICK_TIME
initLimit=$INIT_LIMIT
syncLimit=$SYNC_LIMIT
dataDir=$data_dir
dataLogDir=$txn_dir
clientPort=$client
clientPortAddress=127.0.0.1
maxClientCnxns=100
standaloneEnabled=false
reconfigEnabled=false
admin.enableServer=false
4lw.commands.whitelist=ruok,srvr,stat,mntr,conf
server.1=127.0.0.1:$(peer_port 1):$(election_port 1)
server.2=127.0.0.1:$(peer_port 2):$(election_port 2)
server.3=127.0.0.1:$(peer_port 3):$(election_port 3)
autopurge.snapRetainCount=3
autopurge.purgeInterval=1
CFG
}

setup(){
  local id
  require_runtime
  if [[ ! -x "$HOME_DIR/bin/zkServer.sh" ]]; then
    log "extracting ZooKeeper $VERSION into $BASE"
    mkdir -p "$BASE"
    tar -xzf "$ARCHIVE" -C "$BASE"
  fi
  for id in 1 2 3; do write_node_config "$id"; done
  log "[PASS] setup complete"
  log "connect_string=$(connect_string)"
}

node_server(){
  local id="$1" action="$2" conf_dir log_dir pid_file
  [[ "$id" =~ ^[123]$ ]] || fail "node id must be 1, 2, or 3"
  [[ -x "$HOME_DIR/bin/zkServer.sh" ]] || fail "ensemble is not set up; run: $0 setup"
  conf_dir="$(node_conf_dir "$id")"; log_dir="$(node_log_dir "$id")"; pid_file="$(node_pid_file "$id")"
  mkdir -p "$log_dir"
  env \
    ZOOCFGDIR="$conf_dir" \
    ZOOCFG="zoo.cfg" \
    ZOO_LOG_DIR="$log_dir" \
    ZOOPIDFILE="$pid_file" \
    SERVER_JVMFLAGS="$JVMFLAGS" \
    JMXDISABLE=true \
    "$HOME_DIR/bin/zkServer.sh" "$action"
}

start_node(){
  local id="$1"
  [[ "$id" =~ ^[123]$ ]] || fail "node id must be 1, 2, or 3"
  require_runtime
  if [[ ! -x "$HOME_DIR/bin/zkServer.sh" ]]; then
    setup >/dev/null
  elif [[ ! -f "$(node_conf_dir "$id")/zoo.cfg" ]]; then
    write_node_config "$id"
  fi
  if node_health "$id"; then
    log "node$id already healthy on 127.0.0.1:$(client_port "$id")"
    return 0
  fi
  node_server "$id" start
  local deadline=$((SECONDS + START_TIMEOUT))
  while (( SECONDS < deadline )); do
    if port_open "$(client_port "$id")"; then
      log "node$id process listening on 127.0.0.1:$(client_port "$id")"
      return 0
    fi
    sleep .2
  done
  fail "node$id did not open client port"
}

stop_node(){
  local id="$1"
  if [[ ! -x "$HOME_DIR/bin/zkServer.sh" ]]; then return 0; fi
  node_server "$id" stop || true
  wait_node_port_closed "$id" || fail "node$id client port did not close"
  log "node$id stopped"
}

kill_node(){
  local id="$1" pid_file pid
  [[ "$id" =~ ^[123]$ ]] || fail "node id must be 1, 2, or 3"
  pid_file="$(node_pid_file "$id")"
  if [[ ! -f "$pid_file" ]]; then
    log "node$id pid file absent; treating node as already down"
    return 0
  fi
  pid="$(tr -d '[:space:]' < "$pid_file")"
  [[ "$pid" =~ ^[0-9]+$ ]] || fail "invalid node$id pid file: $pid_file"
  if kill -0 "$pid" 2>/dev/null; then
    local args expected_cfg
    args="$(ps -p "$pid" -o args= 2>/dev/null || true)"
    expected_cfg="$(node_conf_dir "$id")/zoo.cfg"
    [[ "$args" == *QuorumPeerMain* && "$args" == *"$expected_cfg"* ]] || \
      fail "refusing SIGKILL: pid $pid is not node$id QuorumPeerMain ($args)"
    kill -KILL "$pid"
    wait_node_port_closed "$id" || fail "node$id client port did not close after SIGKILL"
  fi
  rm -f -- "$pid_file"
  log "node$id SIGKILL injected"
}

start(){
  local id
  setup
  # Start all members before requiring quorum. A single member in replicated
  # mode is expected to wait for peers and may not answer client requests yet.
  for id in 1 2 3; do
    if ! port_open "$(client_port "$id")"; then node_server "$id" start; fi
  done
  wait_quorum || fail "3-node ensemble failed to elect a leader/reach quorum"
  log "[PASS] ensemble ready: $(connect_string)"
  status
}

stop(){
  local id
  for id in 3 2 1; do stop_node "$id" || true; done
  log "ensemble stopped"
}

restart(){ stop; start; }

status(){
  local id port mode health
  printf 'node\tclient\tmode\thealth\n'
  for id in 1 2 3; do
    port="$(client_port "$id")"; mode="$(node_mode "$id")"; health="down"
    if node_health "$id"; then health="imok"; fi
    [[ -n "$mode" ]] || mode="-"
    printf '%s\t127.0.0.1:%s\t%s\t%s\n' "$id" "$port" "$mode" "$health"
  done
}

health(){
  local serving=0 leaders=0 mode id
  for id in 1 2 3; do
    mode="$(node_mode "$id")"
    case "$mode" in
      leader) serving=$((serving+1)); leaders=$((leaders+1));;
      follower|observer) serving=$((serving+1));;
    esac
  done
  if (( serving >= 2 && leaders == 1 )); then
    log "[PASS] quorum healthy serving=$serving leader_count=$leaders"
    status
    return 0
  fi
  status
  fail "quorum unhealthy serving=$serving leader_count=$leaders"
}

wait_quorum_ready(){
  # `health` is intentionally a point-in-time assertion. After a leader crash,
  # surviving members can answer `ruok` while no node reports Mode yet because
  # leader election is still in progress. Use the existing deadline-based
  # wait_quorum() for fault-recovery convergence instead of treating that legal
  # election window as a permanent quorum failure.
  wait_quorum || fail "ensemble quorum did not converge within ${START_TIMEOUT}s"
  log "[PASS] quorum converged"
  status
}

wait_all(){
  local deadline=$((SECONDS + START_TIMEOUT)) id mode serving
  while (( SECONDS < deadline )); do
    serving=0
    for id in 1 2 3; do
      mode="$(node_mode "$id")"
      case "$mode" in leader|follower|observer) serving=$((serving+1));; esac
    done
    if (( serving == 3 )); then
      health >/dev/null
      log "[PASS] all three ensemble members serving"
      return 0
    fi
    sleep .25
  done
  status || true
  fail "not all three ensemble members became serving"
}

leader(){
  local id mode
  for id in 1 2 3; do
    mode="$(node_mode "$id")"
    if [[ "$mode" == "leader" ]]; then
      printf '%s\n' "$id"
      return 0
    fi
  done
  return 1
}

clean(){
  validate_configuration
  stop || true
  [[ -n "$BASE" && "$BASE" != "/" && "$BASE" != "/tmp" ]] || fail "unsafe clean path: $BASE"
  rm -rf -- "$BASE"
  log "removed $BASE"
}

usage(){
  cat <<USAGE
usage: $0 <setup|start|stop|restart|status|health|wait-quorum|wait-all|leader|connect-string|path|client-endpoint ID|start-node ID|stop-node ID|kill-node ID|clean>

environment:
  VCPKG_ROOT                         default: <repo>/toolchains/vcpkg-tinyimx
  TINYIMX_ZK_ENSEMBLE_BASE          default: /tmp/tinyimx-zookeeper-ensemble
  TINYIMX_ZK_ENSEMBLE_CLIENT_BASE   default: 22181
  TINYIMX_ZK_ENSEMBLE_PEER_BASE     default: 22881
  TINYIMX_ZK_ENSEMBLE_ELECTION_BASE default: 23881
  TINYIMX_ZK_ENSEMBLE_JVMFLAGS      default: -Xms64m -Xmx256m
USAGE
}

case "${1:-}" in
  setup) setup;;
  start) start;;
  stop) stop;;
  restart) restart;;
  status) status;;
  health) health;;
  wait-quorum) wait_quorum_ready;;
  wait-all) wait_all;;
  leader) leader;;
  connect-string) connect_string;;
  path) printf '%s\n' "$HOME_DIR";;
  client-endpoint) [[ $# -eq 2 ]] || fail "client-endpoint requires ID"; [[ "$2" =~ ^[123]$ ]] || fail "node id must be 1, 2, or 3"; printf '127.0.0.1:%s\n' "$(client_port "$2")";;
  start-node) [[ $# -eq 2 ]] || fail "start-node requires ID"; start_node "$2";;
  stop-node) [[ $# -eq 2 ]] || fail "stop-node requires ID"; stop_node "$2";;
  kill-node) [[ $# -eq 2 ]] || fail "kill-node requires ID"; kill_node "$2";;
  clean) clean;;
  *) usage; exit 2;;
esac
