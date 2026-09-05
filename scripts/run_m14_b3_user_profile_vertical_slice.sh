#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${TINYIMX_BUILD_DIR:-${ROOT_DIR}/build/linux-debug}"
SOURCE_CONFIG="${1:-${ROOT_DIR}/config/gateway-a.local.json}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m14-b3-${TIMESTAMP}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tinyimx-m14-b3-XXXXXX")"

USER_PID=""
GATEWAY_PID=""
CLIENT_PID=""

mkdir -p "${ARTIFACT_DIR}"
chmod 700 "${TMP_DIR}"

log() {
  printf '[M14-B3] %s\n' "$*"
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
  stop_pid "${CLIENT_PID}" TERM
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

wait_for_log_line() {
  local file="$1"
  local pattern="$2"
  local pid="$3"
  python3 - "${file}" "${pattern}" "${pid}" <<'PY'
import os
import sys
import time
path, pattern, pid_text = sys.argv[1:]
pid = int(pid_text)
deadline = time.monotonic() + 12.0
while time.monotonic() < deadline:
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8", errors="replace") as handle:
            if pattern in handle.read():
                raise SystemExit(0)
    try:
        os.kill(pid, 0)
    except OSError:
        raise SystemExit("client exited before readiness marker")
    time.sleep(0.05)
raise SystemExit(f"timed out waiting for marker: {pattern}")
PY
}

[[ -f "${SOURCE_CONFIG}" ]] || {
  echo "[FAIL] missing config: ${SOURCE_CONFIG}" >&2
  exit 1
}

log "static ownership/protocol scan"
grep -q 'kUserProfileRequest = 2021' "${ROOT_DIR}/common/protocol/Packet.h" || {
  echo "[FAIL] kUserProfileRequest protocol type missing" >&2
  exit 1
}
grep -q 'kUserProfileResponse = 2022' "${ROOT_DIR}/common/protocol/Packet.h" || {
  echo "[FAIL] kUserProfileResponse protocol type missing" >&2
  exit 1
}
grep -q 'HandleUserProfileRequest' "${ROOT_DIR}/gateway/GatewayServer.cpp" || {
  echo "[FAIL] Gateway user profile handler missing" >&2
  exit 1
}
grep -q 'user_rpc_client_->GetUserProfile' "${ROOT_DIR}/gateway/GatewayServer.cpp" || {
  echo "[FAIL] Gateway UserRpcClient GetUserProfile path missing" >&2
  exit 1
}
grep -q 'gateway.user_profile.rpc' "${ROOT_DIR}/gateway/GatewayServer.cpp" || {
  echo "[FAIL] profile BusinessExecutor operation missing" >&2
  exit 1
}
if grep -n -E 'UserRepository|user_repository_|FindById' \
  "${ROOT_DIR}/gateway/GatewayServer.h" \
  "${ROOT_DIR}/gateway/GatewayServer.cpp"; then
  echo "[FAIL] Gateway regained direct UserRepository/profile storage dependency" >&2
  exit 1
fi

log "configuring/building B3 targets"
(
  cd "${ROOT_DIR}"
  cmake --preset linux-debug
  cmake --build "${BUILD_DIR}" --target \
    protocol_tests \
    gateway_demo \
    user_service_demo \
    gateway_user_profile_client_demo \
    -j1
)

for executable in \
  protocol_tests \
  gateway_demo \
  user_service_demo \
  gateway_user_profile_client_demo; do
  [[ -x "${BUILD_DIR}/${executable}" ]] || {
    echo "[FAIL] missing executable: ${BUILD_DIR}/${executable}" >&2
    exit 1
  }
done

log "protocol gate: 2021/2022 must survive codec round-trip"
"${BUILD_DIR}/protocol_tests" | tee "${ARTIFACT_DIR}/protocol-tests.log"
grep -q 'ProtocolCodec.UserProfileMessageTypesRoundTrip' \
  "${ARTIFACT_DIR}/protocol-tests.log"

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
root.setdefault("app", {})["instance_id"] = f"gateway-m14-b3-{os.getpid()}"
root.setdefault("logger", {})["file"] = os.path.join(artifact_dir, "gateway-internal.log")
root["logger"]["console"] = True
with open(target, "w", encoding="utf-8") as handle:
    json.dump(root, handle, ensure_ascii=False, indent=2)
    handle.write("\n")
os.chmod(target, 0o600)
PY

start_user_service() {
  local profile_delay_ms="${1:-0}"
  local label="${2:-normal}"
  local log_file="${ARTIFACT_DIR}/user-service-${label}.log"

  if [[ "${profile_delay_ms}" == "0" ]]; then
    TINYIMX_USER_LISTEN_TARGET="${USER_TARGET}" \
      "${BUILD_DIR}/user_service_demo" "${SOURCE_CONFIG}" >"${log_file}" 2>&1 &
  else
    TINYIMX_FAULT_USER_PROFILE_DELAY_MS="${profile_delay_ms}" \
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

log "case 1: not-logged-in + real self profile + actor spoof rejection"
start_user_service 0 normal
start_gateway normal
"${BUILD_DIR}/gateway_user_profile_client_demo" \
  127.0.0.1 "${GATEWAY_PORT}" happy \
  | tee "${ARTIFACT_DIR}/profile-happy.log"
grep -q 'gateway user profile happy-path validation passed' \
  "${ARTIFACT_DIR}/profile-happy.log"
grep -q 'gateway user profile RPC completion sent' \
  "${ARTIFACT_DIR}/gateway-normal.log"
stop_gateway
stop_user_service

log "case 2: authenticated Session survives while UserService becomes unavailable"
start_user_service 0 unavailable
start_gateway unavailable
UNAVAILABLE_CLIENT_LOG="${ARTIFACT_DIR}/profile-unavailable.log"
"${BUILD_DIR}/gateway_user_profile_client_demo" \
  127.0.0.1 "${GATEWAY_PORT}" unavailable 1800 \
  >"${UNAVAILABLE_CLIENT_LOG}" 2>&1 &
CLIENT_PID=$!
wait_for_log_line "${UNAVAILABLE_CLIENT_LOG}" 'PROFILE_READY' "${CLIENT_PID}"
stop_user_service
wait "${CLIENT_PID}"
CLIENT_PID=""
cat "${UNAVAILABLE_CLIENT_LOG}"
grep -q 'gateway user profile unavailable validation passed' \
  "${UNAVAILABLE_CLIENT_LOG}"
kill -0 "${GATEWAY_PID}"
stop_gateway

log "case 3: one M13 E2E deadline propagates to GetUserProfile; heartbeat stays Reactor-fast"
start_user_service 3500 timeout
start_gateway timeout
"${BUILD_DIR}/gateway_user_profile_client_demo" \
  127.0.0.1 "${GATEWAY_PORT}" timeout \
  | tee "${ARTIFACT_DIR}/profile-timeout.log"
grep -q 'gateway user profile deadline/isolation validation passed' \
  "${ARTIFACT_DIR}/profile-timeout.log"
grep -q 'profile_timeout' "${ARTIFACT_DIR}/profile-timeout.log"
kill -0 "${GATEWAY_PID}"
stop_gateway
stop_user_service

log "case 4: SessionEpoch completion fence drops stale profile after duplicate login"
start_user_service 1500 stale
start_gateway stale
"${BUILD_DIR}/gateway_user_profile_client_demo" \
  127.0.0.1 "${GATEWAY_PORT}" stale \
  | tee "${ARTIFACT_DIR}/profile-stale.log"
grep -q 'gateway user profile stale-session fence validation passed' \
  "${ARTIFACT_DIR}/profile-stale.log"
grep -q 'gateway replaced old login connection' \
  "${ARTIFACT_DIR}/gateway-stale.log"
if grep -q 'gateway user profile RPC completion sent' \
  "${ARTIFACT_DIR}/gateway-stale.log"; then
  echo "[FAIL] stale profile completion reached Gateway EventLoop" >&2
  exit 1
fi
kill -0 "${GATEWAY_PID}"
stop_gateway
stop_user_service

log "checking Profile response/log data minimization"
if grep -R -n -E 'password_hash|password_salt' "${ARTIFACT_DIR}"; then
  echo "[FAIL] password credential material appeared in B3 artifacts" >&2
  exit 1
fi
if grep -n -E 'remote_address|session_epoch|gateway_id|password' \
  "${ARTIFACT_DIR}/profile-happy.log"; then
  echo "[FAIL] forbidden ownership/security field appeared in Client Profile response" >&2
  exit 1
fi

printf '\n[PASS] M14-B3 User Profile vertical slice\n'
printf 'artifacts: %s\n' "${ARTIFACT_DIR}"
