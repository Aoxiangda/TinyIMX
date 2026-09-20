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
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m14-c1-${TIMESTAMP}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tinyimx-m14-c1-XXXXXX")"

USER_PID=""
MESSAGE_PID=""
GATEWAY_PID=""
CLIENT_PID=""
USER_TARGET=""
MESSAGE_TARGET=""

mkdir -p "${ARTIFACT_DIR}"
chmod 700 "${TMP_DIR}"

log() {
  printf '[M14-C1] %s\n' "$*"
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
  stop_pid "${MESSAGE_PID}" TERM
  stop_pid "${USER_PID}" TERM
  rm -rf -- "${TMP_DIR}"
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

log "static ownership/contract scan"
grep -q 'service MessageService' \
  "${ROOT_DIR}/proto/tinyimx/message/v1/message_service.proto" || {
  echo "[FAIL] MessageService proto missing" >&2
  exit 1
}
grep -q 'rpc ListHistory' \
  "${ROOT_DIR}/proto/tinyimx/message/v1/message_service.proto" || {
  echo "[FAIL] MessageService ListHistory contract missing" >&2
  exit 1
}
grep -q 'rpc ListConversations' \
  "${ROOT_DIR}/proto/tinyimx/message/v1/message_service.proto" || {
  echo "[FAIL] MessageService ListConversations contract missing" >&2
  exit 1
}
grep -q 'ServiceKind::kMessage' \
  "${ROOT_DIR}/services/rpc/MessageRpcClient.cpp" || {
  echo "[FAIL] MessageRpcClient endpoint resolution missing" >&2
  exit 1
}
grep -q 'message_rpc_client_->ListHistory' \
  "${ROOT_DIR}/gateway/GatewayServer.cpp" || {
  echo "[FAIL] Gateway History has not migrated to MessageRpcClient" >&2
  exit 1
}
grep -q 'message_rpc_client_->ListConversations' \
  "${ROOT_DIR}/gateway/GatewayServer.cpp" || {
  echo "[FAIL] Gateway ConversationList has not migrated to MessageRpcClient" >&2
  exit 1
}
if grep -n 'message_repository_->ListDialogMessages' \
  "${ROOT_DIR}/gateway/GatewayServer.cpp"; then
  echo "[FAIL] Gateway still directly executes durable History query" >&2
  exit 1
fi
if grep -n 'message_repository_->ListConversations' \
  "${ROOT_DIR}/gateway/GatewayServer.cpp"; then
  echo "[FAIL] Gateway still directly executes durable Conversation query" >&2
  exit 1
fi
# Forward-compatible regression rule:
# C1 only owns the History/Conversation read migration. Later C2/C3 stages are
# expected to remove the remaining Gateway MessageRepository ownership, so a
# C1 regression must never require historical technical debt to still exist.
# The two negative checks above are the invariant: History and Conversation
# must not return to direct local repository access.
if grep -q 'message_repository_' "${ROOT_DIR}/gateway/GatewayServer.cpp"; then
  log "compatibility: running C1 regression on transitional pre-C3 ownership"
else
  log "compatibility: later C3 ownership migration already removed Gateway MessageRepository"
fi

log "configuring/building C1 targets"
(
  cd "${ROOT_DIR}"
  cmake --preset linux-debug
  cmake --build "${BUILD_DIR}" --target \
    rpc_contract_tests \
    message_application_service_tests \
    message_service_integration_tests \
    message_service_demo \
    user_service_demo \
    gateway_demo \
    gateway_history_client_demo \
    gateway_conversation_client_demo \
    gateway_message_read_case_client_demo \
    -j1
)

for executable in \
  rpc_contract_tests \
  message_application_service_tests \
  message_service_integration_tests \
  message_service_demo \
  user_service_demo \
  gateway_demo \
  gateway_history_client_demo \
  gateway_conversation_client_demo \
  gateway_message_read_case_client_demo; do
  [[ -x "${BUILD_DIR}/${executable}" ]] || {
    echo "[FAIL] missing executable: ${BUILD_DIR}/${executable}" >&2
    exit 1
  }
done

log "contract/application/integration gates"
"${BUILD_DIR}/rpc_contract_tests" | tee "${ARTIFACT_DIR}/rpc-contract.log"
"${BUILD_DIR}/message_application_service_tests" \
  | tee "${ARTIFACT_DIR}/message-application.log"
"${BUILD_DIR}/message_service_integration_tests" \
  | tee "${ARTIFACT_DIR}/message-integration.log"

grep -Fq 'MessageService method frozen: ListHistory' "$ARTIFACT_DIR/rpc-contract.log"
grep -Fq 'MessageService method frozen: ListConversations' "$ARTIFACT_DIR/rpc-contract.log"
grep -q 'failed=0' "${ARTIFACT_DIR}/message-application.log"
grep -q 'failed=0' "${ARTIFACT_DIR}/message-integration.log"

USER_PORT="$(free_port)"
MESSAGE_PORT="$(free_port)"
GATEWAY_PORT="$(free_port)"
USER_TARGET="127.0.0.1:${USER_PORT}"
MESSAGE_TARGET="127.0.0.1:${MESSAGE_PORT}"
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
root.setdefault("app", {})["instance_id"] = f"gateway-m14-c1-{os.getpid()}"
root.setdefault("logger", {})["file"] = os.path.join(artifact_dir, "gateway-internal.log")
root["logger"]["console"] = True
with open(target, "w", encoding="utf-8") as handle:
    json.dump(root, handle, ensure_ascii=False, indent=2)
    handle.write("\n")
os.chmod(target, 0o600)
PY

start_user_service() {
  local label="${1:-normal}"
  local log_file="${ARTIFACT_DIR}/user-service-${label}.log"
  TINYIMX_USER_LISTEN_TARGET="${USER_TARGET}" \
    "${BUILD_DIR}/user_service_demo" "${SOURCE_CONFIG}" >"${log_file}" 2>&1 &
  USER_PID=$!
  wait_for_port 127.0.0.1 "${USER_PORT}" "${USER_PID}" "UserService-${label}"
}

stop_user_service() {
  stop_pid "${USER_PID}" TERM
  USER_PID=""
  wait_for_port_closed 127.0.0.1 "${USER_PORT}"
}

start_message_service() {
  local history_delay_ms="${1:-0}"
  local label="${2:-normal}"
  local log_file="${ARTIFACT_DIR}/message-service-${label}.log"

  if [[ "${history_delay_ms}" == "0" ]]; then
    TINYIMX_MESSAGE_LISTEN_TARGET="${MESSAGE_TARGET}" \
      "${BUILD_DIR}/message_service_demo" "${SOURCE_CONFIG}" >"${log_file}" 2>&1 &
  else
    TINYIMX_FAULT_MESSAGE_HISTORY_DELAY_MS="${history_delay_ms}" \
    TINYIMX_MESSAGE_LISTEN_TARGET="${MESSAGE_TARGET}" \
      "${BUILD_DIR}/message_service_demo" "${SOURCE_CONFIG}" >"${log_file}" 2>&1 &
  fi
  MESSAGE_PID=$!
  wait_for_port 127.0.0.1 "${MESSAGE_PORT}" "${MESSAGE_PID}" "MessageService-${label}"
}

stop_message_service() {
  stop_pid "${MESSAGE_PID}" TERM
  MESSAGE_PID=""
  wait_for_port_closed 127.0.0.1 "${MESSAGE_PORT}"
}

start_gateway() {
  local label="$1"
  local log_file="${ARTIFACT_DIR}/gateway-${label}.log"
  TINYIMX_USER_RPC_TARGET="${USER_TARGET}" \
  TINYIMX_MESSAGE_RPC_TARGET="${MESSAGE_TARGET}" \
    "${BUILD_DIR}/gateway_demo" "${GATEWAY_CONFIG}" >"${log_file}" 2>&1 &
  GATEWAY_PID=$!
  wait_for_port 127.0.0.1 "${GATEWAY_PORT}" "${GATEWAY_PID}" "Gateway-${label}"
}

stop_gateway() {
  stop_pid "${GATEWAY_PID}" INT
  GATEWAY_PID=""
  wait_for_port_closed 127.0.0.1 "${GATEWAY_PORT}"
}

log "case 1: real Client -> Gateway -> MessageService -> MySQL History/Conversation"
start_user_service normal
start_message_service 0 normal
start_gateway normal
"${BUILD_DIR}/gateway_history_client_demo" 127.0.0.1 "${GATEWAY_PORT}" \
  | tee "${ARTIFACT_DIR}/history-normal.log"
"${BUILD_DIR}/gateway_conversation_client_demo" 127.0.0.1 "${GATEWAY_PORT}" \
  | tee "${ARTIFACT_DIR}/conversation-normal.log"
grep -q 'gateway history validation passed' "${ARTIFACT_DIR}/history-normal.log"
grep -q 'gateway conversation validation passed' "${ARTIFACT_DIR}/conversation-normal.log"
grep -q 'gateway history MessageService completion sent' "${ARTIFACT_DIR}/gateway-normal.log"
grep -q 'gateway conversation MessageService completion sent' "${ARTIFACT_DIR}/gateway-normal.log"
stop_gateway
stop_message_service
stop_user_service

log "case 2: authenticated Session survives while MessageService becomes unavailable"
start_user_service unavailable
start_message_service 0 unavailable
start_gateway unavailable
UNAVAILABLE_CLIENT_LOG="${ARTIFACT_DIR}/message-read-unavailable.log"
UNAVAILABLE_GATE="${TMP_DIR}/message-unavailable-go"
rm -f -- "${UNAVAILABLE_GATE}"
"${BUILD_DIR}/gateway_message_read_case_client_demo" \
  127.0.0.1 "${GATEWAY_PORT}" unavailable "${UNAVAILABLE_GATE}" \
  >"${UNAVAILABLE_CLIENT_LOG}" 2>&1 &
CLIENT_PID=$!
wait_for_log_line "${UNAVAILABLE_CLIENT_LOG}" 'MESSAGE_READ_READY' "${CLIENT_PID}"
stop_message_service
touch "${UNAVAILABLE_GATE}"
wait "${CLIENT_PID}"
CLIENT_PID=""
cat "${UNAVAILABLE_CLIENT_LOG}"
grep -q 'message_service_unavailable' "${UNAVAILABLE_CLIENT_LOG}"
grep -q 'gateway message read unavailable validation passed' "${UNAVAILABLE_CLIENT_LOG}"
kill -0 "${GATEWAY_PID}"
stop_gateway
stop_user_service

log "case 3: one M13 E2E deadline propagates to MessageService; heartbeat stays Reactor-fast"
start_user_service timeout
start_message_service 3500 timeout
start_gateway timeout
"${BUILD_DIR}/gateway_message_read_case_client_demo" \
  127.0.0.1 "${GATEWAY_PORT}" timeout \
  | tee "${ARTIFACT_DIR}/message-read-timeout.log"
grep -q 'message_service_timeout' "${ARTIFACT_DIR}/message-read-timeout.log"
grep -q 'gateway message read deadline/isolation validation passed' \
  "${ARTIFACT_DIR}/message-read-timeout.log"
kill -0 "${GATEWAY_PID}"
stop_gateway
stop_message_service
stop_user_service

log "case 4: SessionEpoch completion fence drops stale History after duplicate login"
start_user_service stale
start_message_service 1500 stale
start_gateway stale
"${BUILD_DIR}/gateway_message_read_case_client_demo" \
  127.0.0.1 "${GATEWAY_PORT}" stale \
  | tee "${ARTIFACT_DIR}/message-read-stale.log"
grep -q 'gateway message read stale-session fence validation passed' \
  "${ARTIFACT_DIR}/message-read-stale.log"
grep -q 'gateway replaced old login connection' "${ARTIFACT_DIR}/gateway-stale.log"
# A stale History completion must never reach the old EventLoop/session.
if grep -q 'gateway history MessageService completion sent' \
  "${ARTIFACT_DIR}/gateway-stale.log"; then
  echo "[FAIL] stale History completion reached Gateway EventLoop" >&2
  exit 1
fi
kill -0 "${GATEWAY_PID}"
stop_gateway
stop_message_service
stop_user_service

log "checking client protocol compatibility / boundary minimization"
if grep -n -E 'receiver_confirmed_at|trace_id|caller_service|caller_instance|rpc_request_id' \
  "${ARTIFACT_DIR}/history-normal.log" \
  "${ARTIFACT_DIR}/conversation-normal.log"; then
  echo "[FAIL] internal MessageService/RPC field leaked into client protocol" >&2
  exit 1
fi

grep -q 'last_delivered_at' "${ARTIFACT_DIR}/conversation-normal.log"
grep -q 'delivered_at' "${ARTIFACT_DIR}/history-normal.log"

printf '\n[PASS] M14-C1 MessageService read vertical slice\n'
printf 'artifacts: %s\n' "${ARTIFACT_DIR}"
