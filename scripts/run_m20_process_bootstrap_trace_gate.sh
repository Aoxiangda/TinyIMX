#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${HOME}/projects/TinyIMX_publish"
BUILD="${ROOT}/build/linux-debug"
VCPKG_ROOT="${ROOT}/toolchains/vcpkg-tinyimx"
EXPECTED_BRANCH="feature/m20-observability-v1"
EXPECTED_HEAD="3f42a43f91da6a83fd7a73b10630d48283a06005"
PROTECTED="gateway/GatewayPeerTransport.h"
JOBS="${TINYIMX_BUILD_JOBS:-1}"

cd "${ROOT}"

echo "========== M20 PROCESS TELEMETRY BOOTSTRAP PRECHECK =========="
test "$(git branch --show-current)" = "${EXPECTED_BRANCH}"
test "$(git rev-parse HEAD)" = "${EXPECTED_HEAD}"
test -z "$(git diff --cached --name-only)"

grep -Fq 'class ProcessTelemetry final' common/observability/ProcessTelemetry.h
grep -Fq 'tinyimx-ai-agent' services/intelligence/ai/AIRuntimeConfig.h
grep -Fq 'ProcessTelemetry process_telemetry' examples/ai_agent_demo.cpp
for f in \
  examples/user_service_demo.cpp \
  examples/social_service_demo.cpp \
  examples/message_service_demo.cpp \
  examples/group_service_demo.cpp \
  examples/file_service_demo.cpp \
  examples/mcp_server_demo.cpp
 do
  grep -Fq 'ProcessTelemetry process_telemetry' "$f"
done

PROTECTED_BEFORE="$(git diff -- "${PROTECTED}" | sha256sum | awk '{print $1}')"

python3 - <<'PY'
import json
x=json.load(open('config/ai_agent.example.json',encoding='utf-8'))
obs=x.get('observability')
assert isinstance(obs,dict)
assert obs['enable'] is False
assert obs['traces_enable'] is True
assert obs['service_instance_id']=='tinyimx-ai-agent-1'
print('M20_AI_OBSERVABILITY_EXAMPLE_CONTRACT=PASS')
PY

echo "========== M20 PROCESS TELEMETRY CONFIGURE =========="
cmake \
  -S "${ROOT}" \
  -B "${BUILD}" \
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_INSTALLED_DIR="${ROOT}/vcpkg_installed" \
  -DVCPKG_MANIFEST_INSTALL=OFF

echo "========== M20 PROCESS TELEMETRY BUILD (-j${JOBS}) =========="
for target in \
  user_service_demo \
  social_service_demo \
  message_service_demo \
  group_service_demo \
  file_service_demo \
  tinyimx_mcp_server \
  tinyimx_ai_agent_demo \
  m20_ai_observability_config_tests \
  m20_trace_agent_e2e
 do
  echo "----- BUILD ${target} -----"
  cmake --build "${BUILD}" --target "${target}" -j"${JOBS}"
done

echo "========== M20 PROCESS TELEMETRY CTEST =========="
ctest --test-dir "${BUILD}" --output-on-failure \
  -R '^(tinyimx\.m20\.ai_observability_config|tinyimx\.m20\.trace_context|tinyimx\.m20\.observability_runtime)$'

git diff --check

PROTECTED_AFTER="$(git diff -- "${PROTECTED}" | sha256sum | awk '{print $1}')"
test "${PROTECTED_BEFORE}" = "${PROTECTED_AFTER}"

echo "PROTECTED_GATEWAY_DELTA=PASS"
echo "M20_PROCESS_TELEMETRY_BOOTSTRAP_GATE=PASS"
echo "NOTE: real multi-process trace export is a separate step in the umbrella gate."
