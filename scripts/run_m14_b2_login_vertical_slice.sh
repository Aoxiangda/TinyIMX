#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${TINYIMX_BUILD_DIR:-${ROOT_DIR}/build/linux-debug}"
SOURCE_CONFIG="${1:-${ROOT_DIR}/config/gateway-a.local.json}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m14-b2-${TIMESTAMP}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tinyimx-m14-b2-XXXXXX")"

USER_PID=""
GATEWAY_PID=""

mkdir -p "${ARTIFACT_DIR}"
chmod 700 "${TMP_DIR}"

log() {
  printf '[M14-B2] %s\n' "$*"
}

stop_pid() {
  local pid="${1:-}"
  local signal="${2:-TERM}"
  [[ -z "${pid}" ]] && return 0

  if kill -0 "${pid}" 2>/dev/null; then
    kill "-${signal}" "${pid}" 2>/dev/null || true
    for _ in $(seq 1 80); do
      if ! kill -0 "${pid}" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
    if kill -0 "${pid}" 2>/dev/null; then
      kill -KILL "${pid}" 2>/dev/null || true
    fi
  fi
  wait "${pid}" 2>/dev/null || true
}

cleanup() {
  set +e
  stop_pid "${GATEWAY_PID}" INT
  stop_pid "${USER_PID}" TERM
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT INT TERM

free_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

wait_for_port() {
  local host="$1"
  local port="$2"
  local pid="$3"
  local label="$4"
  python3 - "${host}" "${port}" "${pid}" "${label}" <<'PY'
import os
import socket
import sys
import time

host, port_text, pid_text, label = sys.argv[1:]
port = int(port_text)
pid = int(pid_text)
deadline = time.monotonic() + 15.0
while time.monotonic() < deadline:
    try:
        os.kill(pid, 0)
    except OSError:
        raise SystemExit(f"{label} exited before readiness")
    sock = socket.socket()
    sock.settimeout(0.2)
    try:
        sock.connect((host, port))
        sock.close()
        raise SystemExit(0)
    except OSError:
        sock.close()
        time.sleep(0.1)
raise SystemExit(f"{label} readiness timeout")
PY
}

wait_for_port_closed() {
  local host="$1"
  local port="$2"
  python3 - "${host}" "${port}" <<'PY'
import socket
import sys
import time
host, port_text = sys.argv[1:]
port = int(port_text)
deadline = time.monotonic() + 10.0
while time.monotonic() < deadline:
    sock = socket.socket()
    sock.settimeout(0.2)
    try:
        sock.connect((host, port))
    except OSError:
        raise SystemExit(0)
    finally:
        sock.close()
    time.sleep(0.1)
raise SystemExit("port remained open")
PY
}

[[ -f "${SOURCE_CONFIG}" ]] || {
  echo "[FAIL] missing config: ${SOURCE_CONFIG}" >&2
  exit 1
}

log "static migration scan"
if grep -n 'user_repository_->VerifyLogin' "${ROOT_DIR}/gateway/GatewayServer.cpp"; then
  echo "[FAIL] local Gateway VerifyLogin path still exists" >&2
  exit 1
fi
grep -q 'user_rpc_client_->Authenticate' "${ROOT_DIR}/gateway/GatewayServer.cpp" || {
  echo "[FAIL] UserRpcClient Authenticate path missing" >&2
  exit 1
}
if grep -n 'UserRepository' "${ROOT_DIR}/gateway/GatewayServer.h"; then
  echo "[FAIL] GatewayServer still exposes UserRepository" >&2
  exit 1
fi

log "configuring/building B2 targets"
(
  cd "${ROOT_DIR}"
  cmake --preset linux-debug
  cmake --build "${BUILD_DIR}" --target \
    gateway_demo \
    user_service_demo \
    gateway_login_auth_client_demo \
    gateway_login_case_client_demo \
    -j1
)

for executable in \
  gateway_demo \
  user_service_demo \
  gateway_login_auth_client_demo \
  gateway_login_case_client_demo; do
  [[ -x "${BUILD_DIR}/${executable}" ]] || {
    echo "[FAIL] missing executable: ${BUILD_DIR}/${executable}" >&2
    exit 1
  }
done

USER_PORT="$(free_port)"
GATEWAY_PORT="$(free_port)"
USER_TARGET="127.0.0.1:${USER_PORT}"
GATEWAY_CONFIG="${TMP_DIR}/gateway.json"

python3 - "${SOURCE_CONFIG}" "${GATEWAY_CONFIG}" "${GATEWAY_PORT}" "${ARTIFACT_DIR}" <<'PY'
import json
import os
import sys
source, target, port_text, artifact_dir = sys.argv[1:]
with open(source, "r", encoding="utf-8") as handle:
    root = json.load(handle)
root.setdefault("server", {})["host"] = "127.0.0.1"
root["server"]["port"] = int(port_text)
root.setdefault("gateway_registry", {})["enable"] = False
root.setdefault("app", {})["instance_id"] = f"gateway-m14-b2-{os.getpid()}"
root.setdefault("logger", {})["file"] = os.path.join(artifact_dir, "gateway-internal.log")
root["logger"]["console"] = True
with open(target, "w", encoding="utf-8") as handle:
    json.dump(root, handle, ensure_ascii=False, indent=2)
    handle.write("\n")
os.chmod(target, 0o600)
PY

start_user_service() {
  local delay_ms="${1:-0}"
  local label="${2:-normal}"
  local log_file="${ARTIFACT_DIR}/user-service-${label}.log"

  if [[ "${delay_ms}" == "0" ]]; then
    TINYIMX_USER_LISTEN_TARGET="${USER_TARGET}" \
      "${BUILD_DIR}/user_service_demo" "${SOURCE_CONFIG}" >"${log_file}" 2>&1 &
  else
    TINYIMX_FAULT_USER_AUTH_DELAY_MS="${delay_ms}" \
    TINYIMX_USER_LISTEN_TARGET="${USER_TARGET}" \
      "${BUILD_DIR}/user_service_demo" "${SOURCE_CONFIG}" >"${log_file}" 2>&1 &
  fi
  USER_PID=$!
  wait_for_port 127.0.0.1 "${USER_PORT}" "${USER_PID}" "UserService-${label}"
}

stop_user_service() {
  stop_pid "${USER_PID}" TERM
  USER_PID=""
  wait_for_port_closed 127.0.0.1 "${USER_PORT}"
}

start_gateway() {
  local label="$1"
  local log_file="${ARTIFACT_DIR}/gateway-${label}.log"
  TINYIMX_USER_RPC_TARGET="${USER_TARGET}" \
    "${BUILD_DIR}/gateway_demo" "${GATEWAY_CONFIG}" >"${log_file}" 2>&1 &
  GATEWAY_PID=$!
  wait_for_port 127.0.0.1 "${GATEWAY_PORT}" "${GATEWAY_PID}" "Gateway-${label}"
}

stop_gateway() {
  stop_pid "${GATEWAY_PID}" INT
  GATEWAY_PID=""
  wait_for_port_closed 127.0.0.1 "${GATEWAY_PORT}"
}

log "case 1: real Client -> Gateway -> BusinessExecutor -> UserRpcClient -> UserService -> MySQL"
start_user_service 0 normal
start_gateway normal
"${BUILD_DIR}/gateway_login_auth_client_demo" \
  127.0.0.1 "${GATEWAY_PORT}" \
  | tee "${ARTIFACT_DIR}/login-auth.log"
grep -q 'gateway login auth client validation passed' "${ARTIFACT_DIR}/login-auth.log"
grep -q 'gateway user logged in via UserService' "${ARTIFACT_DIR}/gateway-normal.log"
stop_gateway
stop_user_service

log "case 2: UserService unavailable must fail closed without killing Gateway"
start_gateway unavailable
"${BUILD_DIR}/gateway_login_case_client_demo" \
  127.0.0.1 "${GATEWAY_PORT}" \
  user10001 123456 0 auth_unavailable \
  | tee "${ARTIFACT_DIR}/unavailable.log"
grep -q 'gateway login case validation passed' "${ARTIFACT_DIR}/unavailable.log"
kill -0 "${GATEWAY_PID}"
stop_gateway

log "case 3: one M13 E2E deadline budget propagates into Authenticate RPC"
start_user_service 3500 slow
start_gateway timeout
"${BUILD_DIR}/gateway_login_case_client_demo" \
  127.0.0.1 "${GATEWAY_PORT}" \
  user10001 123456 0 auth_timeout \
  | tee "${ARTIFACT_DIR}/timeout.log"
grep -q 'gateway login case validation passed' "${ARTIFACT_DIR}/timeout.log"
grep -q 'reason=auth_timeout' "${ARTIFACT_DIR}/gateway-timeout.log"
kill -0 "${GATEWAY_PID}"
stop_gateway
stop_user_service

log "checking no password material was logged by B2 artifacts"
if grep -R -n -E 'wrong-password|password_hash|password_salt' "${ARTIFACT_DIR}"; then
  echo "[FAIL] password material appeared in B2 logs" >&2
  exit 1
fi

printf '\n[PASS] M14-B2 Login vertical slice\n'
printf 'artifacts: %s\n' "${ARTIFACT_DIR}"
