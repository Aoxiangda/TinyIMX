#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
DEBUG_DIR="${ROOT_DIR}/build/linux-debug"
RELEASE_DIR="${ROOT_DIR}/build/linux-release"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m13-final-${TIMESTAMP}"
SUMMARY="${ARTIFACT_DIR}/summary.tsv"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"

mkdir -p "${ARTIFACT_DIR}"
printf 'case\tstatus\tlog\n' > "${SUMMARY}"

GATEWAY_PID=""
USER_SERVICE_PID=""
USER_SERVICE_TARGET=""
MESSAGE_SERVICE_PID=""
MESSAGE_SERVICE_TARGET=""
TEMP_CONFIGS=()
TEMP_CONFIG_PATH=""
PASS_COUNT=0
FAIL_COUNT=0

cleanup_gateway() {
  if [[ -n "${GATEWAY_PID}" ]] && kill -0 "${GATEWAY_PID}" 2>/dev/null; then
    kill -INT "${GATEWAY_PID}" 2>/dev/null || true
    for _ in $(seq 1 50); do
      if ! kill -0 "${GATEWAY_PID}" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
    if kill -0 "${GATEWAY_PID}" 2>/dev/null; then
      kill -TERM "${GATEWAY_PID}" 2>/dev/null || true
      sleep 1
    fi
    if kill -0 "${GATEWAY_PID}" 2>/dev/null; then
      kill -KILL "${GATEWAY_PID}" 2>/dev/null || true
    fi
    wait "${GATEWAY_PID}" 2>/dev/null || true
  fi
  GATEWAY_PID=""
}

cleanup_user_service() {
  if [[ -n "${USER_SERVICE_PID}" ]] && kill -0 "${USER_SERVICE_PID}" 2>/dev/null; then
    kill -TERM "${USER_SERVICE_PID}" 2>/dev/null || true
    for _ in $(seq 1 50); do
      if ! kill -0 "${USER_SERVICE_PID}" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
    if kill -0 "${USER_SERVICE_PID}" 2>/dev/null; then
      kill -KILL "${USER_SERVICE_PID}" 2>/dev/null || true
    fi
    wait "${USER_SERVICE_PID}" 2>/dev/null || true
  fi
  USER_SERVICE_PID=""
  USER_SERVICE_TARGET=""
}

cleanup_message_service() {
  if [[ -n "${MESSAGE_SERVICE_PID}" ]] && kill -0 "${MESSAGE_SERVICE_PID}" 2>/dev/null; then
    kill -TERM "${MESSAGE_SERVICE_PID}" 2>/dev/null || true
    for _ in $(seq 1 50); do
      if ! kill -0 "${MESSAGE_SERVICE_PID}" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
    if kill -0 "${MESSAGE_SERVICE_PID}" 2>/dev/null; then
      kill -KILL "${MESSAGE_SERVICE_PID}" 2>/dev/null || true
    fi
    wait "${MESSAGE_SERVICE_PID}" 2>/dev/null || true
  fi
  MESSAGE_SERVICE_PID=""
  MESSAGE_SERVICE_TARGET=""
}

cleanup() {
  cleanup_gateway
  cleanup_message_service
  cleanup_user_service
  for file in "${TEMP_CONFIGS[@]:-}"; do
    [[ -n "${file}" ]] && rm -f -- "${file}"
  done
}

trap cleanup EXIT INT TERM

record() {
  local name="$1"
  local status="$2"
  local log="$3"
  printf '%s\t%s\t%s\n' "${name}" "${status}" "${log}" >> "${SUMMARY}"
  if [[ "${status}" == "PASS" ]]; then
    PASS_COUNT=$((PASS_COUNT + 1))
  else
    FAIL_COUNT=$((FAIL_COUNT + 1))
  fi
}

run_case() {
  local name="$1"
  local timeout_seconds="$2"
  shift 2
  local log="${ARTIFACT_DIR}/${name}.log"

  echo
  echo "[M13] RUN ${name}"

  set +e
  timeout "${timeout_seconds}s" "$@" 2>&1 | tee "${log}"
  local status=${PIPESTATUS[0]}
  set -e

  if [[ ${status} -ne 0 ]]; then
    echo "[M13] FAIL ${name}, status=${status}" >&2
    record "${name}" "FAIL" "${log}"
    return 1
  fi

  echo "[M13] PASS ${name}"
  record "${name}" "PASS" "${log}"
}

require_tool() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "[M13] required tool missing: $1" >&2
    exit 1
  }
}

require_tool cmake
require_tool python3
require_tool timeout
require_tool grep
require_tool awk

cd "${ROOT_DIR}"

LOCAL_A="${ROOT_DIR}/config/gateway-a.local.json"
LOCAL_B="${ROOT_DIR}/config/gateway-b.local.json"

if [[ ! -f "${LOCAL_A}" || ! -f "${LOCAL_B}" ]]; then
  echo "[M13] gateway local configs are required" >&2
  exit 1
fi

# Final release must exercise the explicit C1 production configuration rather
# than the legacy thread_pool compatibility fallback.
python3 - "${LOCAL_A}" "${LOCAL_B}" <<'PY'
import json
import sys

expected = {
    "worker_threads": 4,
    "max_pending_tasks": 128,
    "stripe_count": 64,
    "per_stripe_queue_capacity": 32,
    "default_deadline_ms": 3000,
    "shutdown_timeout_ms": 30000,
}

for path in sys.argv[1:]:
    with open(path, "r", encoding="utf-8") as handle:
        root = json.load(handle)
    actual = root.get("business_runtime")
    if not isinstance(actual, dict):
        raise SystemExit(
            f"{path}: explicit business_runtime section is required for the M13 final gate"
        )
    mismatches = {
        key: (actual.get(key), value)
        for key, value in expected.items()
        if actual.get(key) != value
    }
    if mismatches:
        raise SystemExit(
            f"{path}: frozen M13-C1 business_runtime mismatch {mismatches}"
        )
PY

echo "[M13] configuring debug/release presets"
cmake --preset linux-debug
cmake --preset linux-release

echo "[M13] building debug acceptance targets"
cmake --build --preset build-debug --target \
  config_business_runtime_tests \
  business_runtime_tests \
  business_runtime_acceptance_tests \
  concurrency_tests \
  gateway_tests \
  gateway_demo \
  user_service_demo \
  message_service_demo \
  gateway_business_executor_isolation_demo \
  gateway_business_runtime_overload_demo \
  business_runtime_benchmark \
  -j"${BUILD_JOBS}"

echo "[M13] building release benchmark"
cmake --build --preset build-release --target business_runtime_benchmark -j"${BUILD_JOBS}"

run_case "config-runtime" 30 \
  "${DEBUG_DIR}/config_business_runtime_tests"
run_case "runtime-unit" 60 \
  "${DEBUG_DIR}/business_runtime_tests"
run_case "runtime-acceptance" 60 \
  "${DEBUG_DIR}/business_runtime_acceptance_tests"
run_case "concurrency" 60 \
  "${DEBUG_DIR}/concurrency_tests"
run_case "gateway-unit" 60 \
  "${DEBUG_DIR}/gateway_tests"

run_case "production-benchmark" 60 \
  "${RELEASE_DIR}/business_runtime_benchmark" \
  --workers 4 --tasks 128 --work-ms 10 \
  --max-pending 128 --stripes 64 --per-stripe 32 \
  --ordering unordered --keys 1 --submitters 2 \
  --case m13-final-production

if grep -q 'within_shutdown_budget = 1' "${ARTIFACT_DIR}/production-benchmark.log" &&
   grep -q 'worker_exceptions  = 0' "${ARTIFACT_DIR}/production-benchmark.log" &&
   grep -q 'completion_errors  = 0' "${ARTIFACT_DIR}/production-benchmark.log" &&
   grep -q 'current_pending    = 0' "${ARTIFACT_DIR}/production-benchmark.log" &&
   grep -q 'pending_completion = 0' "${ARTIFACT_DIR}/production-benchmark.log"; then
  record "production-benchmark-invariants" "PASS" "${ARTIFACT_DIR}/production-benchmark.log"
else
  record "production-benchmark-invariants" "FAIL" "${ARTIFACT_DIR}/production-benchmark.log"
  exit 1
fi

run_case "hot-key" 60 \
  "${RELEASE_DIR}/business_runtime_benchmark" \
  --workers 4 --tasks 128 --work-ms 20 \
  --max-pending 128 --stripes 64 --per-stripe 32 \
  --ordering striped --keys 1 --submitters 2 \
  --case m13-final-hot-key

HOT_REJECTED="$(awk -F '=' '/rejected_hot_key/ {gsub(/[[:space:]]/,"",$2); v=$2} END{print v+0}' "${ARTIFACT_DIR}/hot-key.log")"
if [[ "${HOT_REJECTED}" -le 0 ]]; then
  echo "[M13] hot-key final gate did not observe local rejection" >&2
  record "hot-key-invariant" "FAIL" "${ARTIFACT_DIR}/hot-key.log"
  exit 1
fi
record "hot-key-invariant" "PASS" "${ARTIFACT_DIR}/hot-key.log"

run_case "global-overload" 60 \
  "${RELEASE_DIR}/business_runtime_benchmark" \
  --workers 4 --tasks 512 --work-ms 50 \
  --max-pending 128 --stripes 64 --per-stripe 32 \
  --ordering unordered --keys 1 --submitters 4 \
  --case m13-final-overload

OVERLOAD_REJECTED="$(awk -F '=' '/rejected_overload/ {gsub(/[[:space:]]/,"",$2); v=$2} END{print v+0}' "${ARTIFACT_DIR}/global-overload.log")"
if [[ "${OVERLOAD_REJECTED}" -le 0 ]]; then
  echo "[M13] global overload final gate did not observe rejection" >&2
  record "global-overload-invariant" "FAIL" "${ARTIFACT_DIR}/global-overload.log"
  exit 1
fi
record "global-overload-invariant" "PASS" "${ARTIFACT_DIR}/global-overload.log"

free_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}


start_user_service() {
  local port
  port="$(free_port)"
  USER_SERVICE_TARGET="127.0.0.1:${port}"
  local log="${ARTIFACT_DIR}/user-service.log"

  echo "[M13] starting UserService at ${USER_SERVICE_TARGET}"
  TINYIMX_USER_LISTEN_TARGET="${USER_SERVICE_TARGET}" \
    "${DEBUG_DIR}/user_service_demo" "${LOCAL_A}" >"${log}" 2>&1 &
  USER_SERVICE_PID=$!

  python3 - "127.0.0.1" "${port}" "${USER_SERVICE_PID}" "${log}" <<'PY'
import os
import socket
import sys
import time

host, port_text, pid_text, log = sys.argv[1:]
port = int(port_text)
pid = int(pid_text)
deadline = time.monotonic() + 12.0
while time.monotonic() < deadline:
    try:
        os.kill(pid, 0)
    except OSError:
        raise SystemExit(f"UserService exited before readiness; log={log}")
    sock = socket.socket()
    sock.settimeout(0.2)
    try:
        sock.connect((host, port))
        sock.close()
        raise SystemExit(0)
    except OSError:
        sock.close()
        time.sleep(0.1)
raise SystemExit(f"UserService readiness timeout; log={log}")
PY
}

start_message_service() {
  local port
  port="$(free_port)"
  MESSAGE_SERVICE_TARGET="127.0.0.1:${port}"
  local log="${ARTIFACT_DIR}/message-service.log"

  echo "[M13] starting MessageService at ${MESSAGE_SERVICE_TARGET}"
  TINYIMX_MESSAGE_LISTEN_TARGET="${MESSAGE_SERVICE_TARGET}" \
    "${DEBUG_DIR}/message_service_demo" "${LOCAL_A}" >"${log}" 2>&1 &
  MESSAGE_SERVICE_PID=$!

  python3 - "127.0.0.1" "${port}" "${MESSAGE_SERVICE_PID}" "${log}" <<'PY'
import os
import socket
import sys
import time

host, port_text, pid_text, log = sys.argv[1:]
port = int(port_text)
pid = int(pid_text)
deadline = time.monotonic() + 12.0
while time.monotonic() < deadline:
    try:
        os.kill(pid, 0)
    except OSError:
        raise SystemExit(f"MessageService exited before readiness; log={log}")
    sock = socket.socket()
    sock.settimeout(0.2)
    try:
        sock.connect((host, port))
        sock.close()
        raise SystemExit(0)
    except OSError:
        sock.close()
        time.sleep(0.1)
raise SystemExit(f"MessageService readiness timeout; log={log}")
PY
}

make_temp_config() {
  local mode="$1"
  local port="$2"
  local internal_log="$3"
  local tmp
  tmp="$(mktemp "${TMPDIR:-/tmp}/tinyimx-m13-${mode}-XXXXXX.json")"
  chmod 600 "${tmp}"
  TEMP_CONFIGS+=("${tmp}")

  python3 - "${LOCAL_A}" "${tmp}" "${mode}" "${port}" "${internal_log}" <<'PY'
import json
import os
import sys

source, target, mode, port_text, internal_log = sys.argv[1:]
with open(source, "r", encoding="utf-8") as handle:
    root = json.load(handle)

root.setdefault("app", {})["instance_id"] = f"gateway-m13-{mode}-{os.getpid()}"
root.setdefault("server", {})["host"] = "127.0.0.1"
root["server"]["port"] = int(port_text)
root.setdefault("logger", {})["file"] = internal_log
root["logger"]["console"] = True
root.setdefault("gateway_registry", {})["enable"] = False

if mode == "isolation":
    root["business_runtime"] = {
        "worker_threads": 4,
        "max_pending_tasks": 128,
        "stripe_count": 64,
        "per_stripe_queue_capacity": 32,
        "default_deadline_ms": 3000,
        "shutdown_timeout_ms": 30000,
    }
elif mode == "overload":
    root["business_runtime"] = {
        "worker_threads": 1,
        "max_pending_tasks": 4,
        "stripe_count": 64,
        "per_stripe_queue_capacity": 2,
        "default_deadline_ms": 3000,
        "shutdown_timeout_ms": 30000,
    }
else:
    raise SystemExit("unknown mode")

with open(target, "w", encoding="utf-8") as handle:
    json.dump(root, handle, ensure_ascii=False, indent=2)
    handle.write("\n")
PY

  TEMP_CONFIG_PATH="${tmp}"
}

wait_for_gateway() {
  local host="$1"
  local port="$2"
  local pid="$3"
  local gateway_log="$4"

  python3 - "${host}" "${port}" "${pid}" "${gateway_log}" <<'PY'
import os
import socket
import sys
import time

host, port_text, pid_text, gateway_log = sys.argv[1:]
port = int(port_text)
pid = int(pid_text)
deadline = time.monotonic() + 10.0

while time.monotonic() < deadline:
    try:
        os.kill(pid, 0)
    except OSError:
        raise SystemExit("gateway process exited before readiness")

    bootstrap_ready = False
    try:
        with open(gateway_log, "r", encoding="utf-8", errors="replace") as handle:
            bootstrap_ready = "gateway demo started" in handle.read()
    except OSError:
        pass

    if not bootstrap_ready:
        time.sleep(0.05)
        continue

    sock = socket.socket()
    sock.settimeout(0.2)
    try:
        sock.connect((host, port))
        sock.close()
        raise SystemExit(0)
    except OSError:
        sock.close()
        time.sleep(0.1)

raise SystemExit("gateway bootstrap readiness timeout")
PY
}

stop_gateway_checked() {
  local log="$1"

  if [[ -z "${GATEWAY_PID}" ]]; then
    return 0
  fi

  local pid="${GATEWAY_PID}"
  kill -INT "${pid}" 2>/dev/null || true

  for _ in $(seq 1 150); do
    if ! kill -0 "${pid}" 2>/dev/null; then
      set +e
      wait "${pid}"
      local status=$?
      set -e
      GATEWAY_PID=""
      if [[ ${status} -ne 0 ]]; then
        echo "[M13] gateway exited abnormally, status=${status}, log=${log}" >&2
        return 1
      fi
      return 0
    fi
    sleep 0.1
  done

  echo "[M13] gateway graceful stop timeout, pid=${pid}" >&2
  kill -TERM "${pid}" 2>/dev/null || true
  sleep 1
  if kill -0 "${pid}" 2>/dev/null; then
    kill -KILL "${pid}" 2>/dev/null || true
  fi
  wait "${pid}" 2>/dev/null || true
  GATEWAY_PID=""
  return 1
}

run_gateway_fault_case() {
  local mode="$1"
  local client_bin="$2"
  local case_name="$3"
  local port
  port="$(free_port)"
  local gateway_log="${ARTIFACT_DIR}/${case_name}-gateway.log"
  local internal_log="${ARTIFACT_DIR}/${case_name}-internal.log"
  local client_log="${ARTIFACT_DIR}/${case_name}-client.log"
  local temp_config
  make_temp_config "${mode}" "${port}" "${internal_log}"
  temp_config="${TEMP_CONFIG_PATH}"

  echo
  echo "[M13] RUN ${case_name} on 127.0.0.1:${port}"

  TINYIMX_FAULT_HISTORY_BUSINESS_DELAY_MS=500 \
  TINYIMX_USER_RPC_TARGET="${USER_SERVICE_TARGET}" \
  TINYIMX_MESSAGE_RPC_TARGET="${MESSAGE_SERVICE_TARGET}" \
    "${DEBUG_DIR}/gateway_demo" "${temp_config}" >"${gateway_log}" 2>&1 &
  GATEWAY_PID=$!

  if ! wait_for_gateway "127.0.0.1" "${port}" "${GATEWAY_PID}" "${gateway_log}"; then
    record "${case_name}" "FAIL" "${gateway_log}"
    return 1
  fi

  set +e
  timeout 30s "${client_bin}" 127.0.0.1 "${port}" 2>&1 | tee "${client_log}"
  local client_status=${PIPESTATUS[0]}
  set -e

  if ! stop_gateway_checked "${gateway_log}"; then
    record "${case_name}" "FAIL" "${gateway_log}"
    return 1
  fi

  if [[ ${client_status} -ne 0 ]]; then
    record "${case_name}" "FAIL" "${client_log}"
    return 1
  fi

  grep -q 'within_budget=1' "${gateway_log}"
  grep -q 'worker_exceptions=0' "${gateway_log}"
  grep -q 'completion_exceptions=0' "${gateway_log}"
  grep -q 'current_pending=0' "${gateway_log}"
  grep -q 'current_active=0' "${gateway_log}"
  grep -q 'pending_completions=0' "${gateway_log}"

  echo "[M13] PASS ${case_name}"
  record "${case_name}" "PASS" "${client_log}"
}

start_user_service
start_message_service

run_gateway_fault_case \
  "isolation" \
  "${DEBUG_DIR}/gateway_business_executor_isolation_demo" \
  "gateway-isolation"

grep -q 'heartbeat_not_blocked=1' "${ARTIFACT_DIR}/gateway-isolation-client.log"
grep -q 'history_was_slow=1' "${ARTIFACT_DIR}/gateway-isolation-client.log"

run_gateway_fault_case \
  "overload" \
  "${DEBUG_DIR}/gateway_business_runtime_overload_demo" \
  "gateway-overload"

grep -q 'runtime_overload_observed=1' "${ARTIFACT_DIR}/gateway-overload-client.log"
grep -q 'heartbeat_not_blocked=1' "${ARTIFACT_DIR}/gateway-overload-client.log"

cleanup_message_service
cleanup_user_service

# Full reliable-messaging regression is deliberately last, after all temporary
# Gateway processes are stopped.  It owns its normal 9001/9002 topology.
run_case "m12-final-regression" 420 \
  bash "${ROOT_DIR}/scripts/run_m12_reliability_regression.sh"
if grep -q '\[M12\] PASS=19 FAIL=0' "${ARTIFACT_DIR}/m12-final-regression.log"; then
  record "m12-result-invariant" "PASS" "${ARTIFACT_DIR}/m12-final-regression.log"
else
  record "m12-result-invariant" "FAIL" "${ARTIFACT_DIR}/m12-final-regression.log"
  exit 1
fi

SEMANTIC_LOG="${ARTIFACT_DIR}/semantic-scan.log"
{
  echo "[M13] semantic/config consistency scan"
  grep -q 'max_pending_tasks{128}' gateway/business/BusinessExecutor.h
  grep -q 'per_stripe_queue_capacity{32}' gateway/business/BusinessExecutor.h
  grep -q 'max_pending_tasks{128}' common/config/ConfigTypes.h
  grep -q 'per_stripe_queue_capacity{32}' common/config/ConfigTypes.h
  grep -q '"max_pending_tasks": 128' config/gateway.example.json
  grep -q '"per_stripe_queue_capacity": 32' config/gateway.example.json
  grep -q 'At-Least-Once delivery attempts' M13_BUSINESS_RUNTIME_ACCEPTANCE.md
  if grep -Eqi '(^|[^A-Za-z])Exactly-Once([^A-Za-z]|$).*guarantee|guarantee.*Exactly-Once' \
      M13_BUSINESS_RUNTIME_ACCEPTANCE.md; then
    echo "positive Exactly-Once guarantee found" >&2
    exit 1
  fi
  echo "[M13] semantic scan passed"
} >"${SEMANTIC_LOG}" 2>&1
record "semantic-scan" "PASS" "${SEMANTIC_LOG}"

if [[ ${FAIL_COUNT} -ne 0 ]]; then
  echo "[M13] PASS=${PASS_COUNT} FAIL=${FAIL_COUNT}" >&2
  echo "[M13] summary: ${SUMMARY}" >&2
  exit 1
fi

echo
echo "[M13] final acceptance gate completed"
echo "[M13] PASS=${PASS_COUNT} FAIL=0"
echo "[M13] summary: ${SUMMARY}"
echo "[M13] artifacts: ${ARTIFACT_DIR}"
echo
echo "[M13 PASS] Business Execution Runtime final regression and acceptance gate passed."
