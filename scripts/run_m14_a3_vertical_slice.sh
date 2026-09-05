#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/linux-debug"
SOURCE_CONFIG="${1:-${ROOT_DIR}/config/gateway-a.local.json}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m14-a3-${TIMESTAMP}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tinyimx-m14-a3-XXXXXX")"

SOCIAL_PID=""
USER_PID=""
GATEWAY_PID=""

mkdir -p "${ARTIFACT_DIR}"
chmod 700 "${TMP_DIR}"

stop_exact_pid() {
  local pid="$1"
  local first_signal="$2"

  if [[ -z "${pid}" ]] || ! kill -0 "${pid}" 2>/dev/null; then
    return
  fi

  kill "-${first_signal}" "${pid}" 2>/dev/null || true

  for _ in $(seq 1 50); do
    if ! kill -0 "${pid}" 2>/dev/null; then
      wait "${pid}" 2>/dev/null || true
      return
    fi
    sleep 0.1
  done

  kill -TERM "${pid}" 2>/dev/null || true
  sleep 1

  if kill -0 "${pid}" 2>/dev/null; then
    kill -KILL "${pid}" 2>/dev/null || true
  fi

  wait "${pid}" 2>/dev/null || true
}

cleanup() {
  set +e
  stop_exact_pid "${GATEWAY_PID}" INT
  stop_exact_pid "${USER_PID}" TERM
  stop_exact_pid "${SOCIAL_PID}" TERM
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT INT TERM

for executable in \
  social_service_demo \
  user_service_demo \
  gateway_demo \
  gateway_friend_list_client_demo; do
  if [[ ! -x "${BUILD_DIR}/${executable}" ]]; then
    echo "[FAIL] missing executable: ${BUILD_DIR}/${executable}" >&2
    echo "Build M14-A3 targets before running this script." >&2
    exit 1
  fi
done

if [[ ! -f "${SOURCE_CONFIG}" ]]; then
  echo "[FAIL] source config not found: ${SOURCE_CONFIG}" >&2
  exit 1
fi

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
  local name="$4"

  python3 - "${host}" "${port}" "${pid}" "${name}" <<'PY'
import os
import socket
import sys
import time

host, port_text, pid_text, name = sys.argv[1:]
port = int(port_text)
pid = int(pid_text)
deadline = time.monotonic() + 12.0

while time.monotonic() < deadline:
    try:
        os.kill(pid, 0)
    except OSError:
        raise SystemExit(f"{name} exited before readiness")

    sock = socket.socket()
    sock.settimeout(0.2)
    try:
        sock.connect((host, port))
        sock.close()
        raise SystemExit(0)
    except OSError:
        sock.close()
        time.sleep(0.1)

raise SystemExit(f"{name} readiness timeout")
PY
}

SOCIAL_PORT="$(free_port)"
USER_PORT="$(free_port)"
GATEWAY_PORT="$(free_port)"

SOCIAL_CONFIG="${TMP_DIR}/social.json"
USER_CONFIG="${TMP_DIR}/user.json"
GATEWAY_CONFIG="${TMP_DIR}/gateway.json"

python3 - \
  "${SOURCE_CONFIG}" \
  "${SOCIAL_CONFIG}" \
  "${USER_CONFIG}" \
  "${GATEWAY_CONFIG}" \
  "${GATEWAY_PORT}" \
  "${ARTIFACT_DIR}" <<'PY'
import json
import os
import sys

source, social_path, user_path, gateway_path, gateway_port_text, artifact_dir = sys.argv[1:]
with open(source, "r", encoding="utf-8") as handle:
    root = json.load(handle)

social = json.loads(json.dumps(root))
social.setdefault("gateway_registry", {})["enable"] = False
social.setdefault("logger", {})["file"] = os.path.join(artifact_dir, "social-internal.log")
social["logger"]["console"] = True

# SocialService owns the FriendRepository/MySQL path in this vertical slice.
if not social.setdefault("mysql", {}).get("enable", False):
    raise SystemExit("source config must have mysql.enable=true")

with open(social_path, "w", encoding="utf-8") as handle:
    json.dump(social, handle, ensure_ascii=False, indent=2)
    handle.write("\n")
os.chmod(social_path, 0o600)

user = json.loads(json.dumps(root))
user.setdefault("gateway_registry", {})["enable"] = False
user.setdefault("logger", {})["file"] = os.path.join(artifact_dir, "user-internal.log")
user["logger"]["console"] = True
if not user.setdefault("mysql", {}).get("enable", False):
    raise SystemExit("source config must have mysql.enable=true")
with open(user_path, "w", encoding="utf-8") as handle:
    json.dump(user, handle, ensure_ascii=False, indent=2)
    handle.write("\n")
os.chmod(user_path, 0o600)

gateway = json.loads(json.dumps(root))
gateway.setdefault("server", {})["host"] = "127.0.0.1"
gateway["server"]["port"] = int(gateway_port_text)
gateway.setdefault("gateway_registry", {})["enable"] = False
gateway.setdefault("app", {})["instance_id"] = f"gateway-m14-a3-{os.getpid()}"
gateway.setdefault("logger", {})["file"] = os.path.join(artifact_dir, "gateway-internal.log")
gateway["logger"]["console"] = True

with open(gateway_path, "w", encoding="utf-8") as handle:
    json.dump(gateway, handle, ensure_ascii=False, indent=2)
    handle.write("\n")
os.chmod(gateway_path, 0o600)
PY

SOCIAL_TARGET="127.0.0.1:${SOCIAL_PORT}"
USER_TARGET="127.0.0.1:${USER_PORT}"

echo "[M14-A3] artifacts: ${ARTIFACT_DIR}"
echo "[M14-A3] starting SocialService at ${SOCIAL_TARGET}"
TINYIMX_SOCIAL_LISTEN_TARGET="${SOCIAL_TARGET}" \
  "${BUILD_DIR}/social_service_demo" \
  "${SOCIAL_CONFIG}" \
  >"${ARTIFACT_DIR}/social-service.log" 2>&1 &
SOCIAL_PID=$!
wait_for_port 127.0.0.1 "${SOCIAL_PORT}" "${SOCIAL_PID}" "SocialService"

echo "[M14-A3] starting UserService at ${USER_TARGET}"
TINYIMX_USER_LISTEN_TARGET="${USER_TARGET}" \
  "${BUILD_DIR}/user_service_demo" \
  "${USER_CONFIG}" \
  >"${ARTIFACT_DIR}/user-service.log" 2>&1 &
USER_PID=$!
wait_for_port 127.0.0.1 "${USER_PORT}" "${USER_PID}" "UserService"

echo "[M14-A3] starting Gateway at 127.0.0.1:${GATEWAY_PORT}"
TINYIMX_SOCIAL_RPC_TARGET="${SOCIAL_TARGET}" \
TINYIMX_USER_RPC_TARGET="${USER_TARGET}" \
  "${BUILD_DIR}/gateway_demo" \
  "${GATEWAY_CONFIG}" \
  >"${ARTIFACT_DIR}/gateway.log" 2>&1 &
GATEWAY_PID=$!
wait_for_port 127.0.0.1 "${GATEWAY_PORT}" "${GATEWAY_PID}" "Gateway"

echo "[M14-A3] running real Client -> Gateway -> BusinessExecutor -> gRPC -> SocialService -> MySQL FriendList"
"${BUILD_DIR}/gateway_friend_list_client_demo" \
  127.0.0.1 "${GATEWAY_PORT}" \
  | tee "${ARTIFACT_DIR}/client.log"

echo "[PASS] M14-A3 FriendList vertical slice"
