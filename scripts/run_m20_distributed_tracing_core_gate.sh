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

echo "========== M20 TRACING CORE PRECHECK =========="
test "$(git branch --show-current)" = "${EXPECTED_BRANCH}"
test "$(git rev-parse HEAD)" = "${EXPECTED_HEAD}"
test -z "$(git diff --cached --name-only)"
git diff --check

PROTECTED_BEFORE="$(git diff -- "${PROTECTED}" | sha256sum | awk '{print $1}')"

python3 - <<'PY'
from pathlib import Path
files = [
    Path('common/observability/Metrics.cpp'),
    Path('common/observability/Trace.cpp'),
    Path('common/observability/GrpcTracing.cpp'),
]
for path in files:
    text = path.read_text(encoding='utf-8')
    forbidden = [
        '"user_id"', '"group_id"', '"file_id"', '"message_id"',
        '"request_id"', '"trace_id"', '"client_message_id"',
        '"prompt"', '"message.content"', '"peer.ip"'
    ]
    for token in forbidden:
        if token in text and path.name == 'Metrics.cpp':
            raise SystemExit(f'high-cardinality metric attribute candidate: {path}: {token}')
print('M20_TRACING_CARDINALITY_SOURCE_GATE=PASS')
PY

echo "========== M20 TRACING CORE CONFIGURE =========="
cmake \
  -S "${ROOT}" \
  -B "${BUILD}" \
  -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux \
  -DVCPKG_INSTALLED_DIR="${ROOT}/vcpkg_installed" \
  -DVCPKG_MANIFEST_INSTALL=OFF

echo "========== M20 TRACING CORE BUILD (-j${JOBS}) =========="
TARGETS=(
  m20_trace_context_tests
  m20_observability_runtime_tests
  tinyimx_rpc_client
  tinyimx_user_grpc
  tinyimx_social_grpc
  tinyimx_message_grpc
  tinyimx_group_grpc
  tinyimx_file_grpc
  m19_mcp_core_tests
  m19_mcp_domain_tools_tests
  m19_mcp_client_tests
  m19_ai_provider_tests
  m19_agent_orchestrator_tests
  tinyimx_mcp_server
  tinyimx_ai_agent_demo
)
for target in "${TARGETS[@]}"; do
  echo "----- BUILD ${target} -----"
  cmake --build "${BUILD}" --target "${target}" -j"${JOBS}"
done

echo "========== M20 TRACING CORE CTEST =========="
ctest --test-dir "${BUILD}" --output-on-failure \
  -R '^(tinyimx\.m20\.(trace_context|observability_runtime)|tinyimx\.m19\.(mcp_core|mcp_domain_tools|mcp_client|ai_provider|agent_orchestrator)|social_rpc_client_tests|group_rpc_client_tests|file_rpc_client_tests)$'

echo "========== RETAINED M20 FOUNDATION =========="
bash scripts/run_m20_observability_foundation_gate.sh

PROTECTED_AFTER="$(git diff -- "${PROTECTED}" | sha256sum | awk '{print $1}')"
test "${PROTECTED_BEFORE}" = "${PROTECTED_AFTER}"

echo
 echo "PROTECTED_GATEWAY_DELTA=PASS"
echo "M20_DISTRIBUTED_TRACING_CORE_GATE=PASS"
echo "NOTE: real multi-process trace export E2E is the next gate, not claimed here."
