#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="3.8.5"
VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/toolchains/vcpkg-tinyimx}"
ZK_BASE="${TINYIMX_LOCAL_ZK_BASE:-/tmp/tinyimx-zookeeper}"
ZK_HOME="$ZK_BASE/apache-zookeeper-${VERSION}-bin"
ZK_ARCHIVE="$VCPKG_ROOT/downloads/apache-zookeeper-${VERSION}-bin.tar.gz"
DATA_DIR="$ZK_BASE/data"
LOG_DIR="$ZK_BASE/logs"
CLIENT_PORT="${TINYIMX_LOCAL_ZK_PORT:-2181}"

fail() {
  echo "[local-zookeeper][FAIL] $*" >&2
  exit 1
}

require_java() {
  command -v java >/dev/null 2>&1 || fail "java runtime not found; install a headless JRE first"
}

write_config() {
  mkdir -p "$ZK_HOME/conf" "$DATA_DIR" "$LOG_DIR"
  cat > "$ZK_HOME/conf/zoo.cfg" <<CFG
# TinyIMX local development ZooKeeper. Runtime data stays outside the repository.
tickTime=2000
dataDir=$DATA_DIR
clientPort=$CLIENT_PORT
maxClientCnxns=60
admin.enableServer=false
4lw.commands.whitelist=ruok,srvr,stat,conf
autopurge.snapRetainCount=3
autopurge.purgeInterval=1
CFG
}

setup() {
  require_java
  if [[ ! -f "$ZK_ARCHIVE" ]]; then
    echo "[local-zookeeper] ZooKeeper binary archive missing; bootstrapping vcpkg distfiles"
    VCPKG_ROOT="$VCPKG_ROOT" "$ROOT/scripts/bootstrap_zookeeper_vcpkg_distfiles.sh"
  fi

  if [[ ! -x "$ZK_HOME/bin/zkServer.sh" ]]; then
    echo "[local-zookeeper] extracting ZooKeeper $VERSION into $ZK_BASE"
    mkdir -p "$ZK_BASE"
    tar -xzf "$ZK_ARCHIVE" -C "$ZK_BASE"
  fi

  write_config
  echo "[local-zookeeper][PASS] setup complete: $ZK_HOME"
}

health() {
  python3 - "$CLIENT_PORT" <<'PY'
import socket
import sys
port = int(sys.argv[1])
s = socket.socket()
s.settimeout(2.0)
try:
    s.connect(("127.0.0.1", port))
    s.sendall(b"ruok")
    data = s.recv(1024).decode(errors="replace")
finally:
    s.close()
if data != "imok":
    raise SystemExit(f"unexpected ZooKeeper response: {data!r}")
print("ZooKeeper response: imok")
PY
}

start() {
  setup
  export ZOO_LOG_DIR="$LOG_DIR"
  "$ZK_HOME/bin/zkServer.sh" start

  for _ in $(seq 1 30); do
    if health >/dev/null 2>&1; then
      echo "[local-zookeeper][PASS] started on 127.0.0.1:$CLIENT_PORT"
      return 0
    fi
    sleep 0.2
  done
  "$ZK_HOME/bin/zkServer.sh" status || true
  fail "ZooKeeper did not become healthy"
}

stop() {
  if [[ ! -x "$ZK_HOME/bin/zkServer.sh" ]]; then
    echo "[local-zookeeper] not installed under $ZK_HOME"
    return 0
  fi
  export ZOO_LOG_DIR="$LOG_DIR"
  "$ZK_HOME/bin/zkServer.sh" stop || true
  echo "[local-zookeeper] stopped"
}

status() {
  [[ -x "$ZK_HOME/bin/zkServer.sh" ]] || fail "ZooKeeper is not set up; run: $0 setup"
  export ZOO_LOG_DIR="$LOG_DIR"
  "$ZK_HOME/bin/zkServer.sh" status
}

clean() {
  stop
  rm -rf "$ZK_BASE"
  echo "[local-zookeeper] removed $ZK_BASE"
}

usage() {
  cat <<USAGE
usage: $0 <setup|start|stop|restart|status|health|clean|path>

environment:
  VCPKG_ROOT               default: <repo>/toolchains/vcpkg-tinyimx
  TINYIMX_LOCAL_ZK_BASE    default: /tmp/tinyimx-zookeeper
  TINYIMX_LOCAL_ZK_PORT    default: 2181
USAGE
}

case "${1:-}" in
  setup)
    setup
    ;;
  start)
    start
    ;;
  stop)
    stop
    ;;
  restart)
    stop
    start
    ;;
  status)
    status
    ;;
  health)
    health
    ;;
  clean)
    clean
    ;;
  path)
    echo "$ZK_HOME"
    ;;
  *)
    usage
    exit 2
    ;;
esac
