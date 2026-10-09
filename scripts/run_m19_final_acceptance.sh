#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${TINYIMX_BUILD_DIR:-$ROOT_DIR/build/linux-debug}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
SOURCE_CONFIG="${1:-config/gateway-a.local.json}"
if [[ "$SOURCE_CONFIG" != /* ]]; then
  SOURCE_CONFIG="$ROOT_DIR/$SOURCE_CONFIG"
fi

EXPECTED_BRANCH="feature/m19-ai-mcpserver-v1"
EXPECTED_PRECOMMIT_HEAD="212de4eaa14bd05ef21016b487bd476628ad7bd4"
PROTECTED_FILE="gateway/GatewayPeerTransport.h"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="$ROOT_DIR/artifacts/m19-final-$TIMESTAMP"
mkdir -p "$ARTIFACT_DIR"

log(){ printf '[M19-FINAL] %s\n' "$*"; }
fail(){ printf '[M19-FINAL] FAIL: %s\n' "$*" >&2; exit 1; }

for c in cmake ctest git python3 curl mysql sha256sum; do
  command -v "$c" >/dev/null 2>&1 || fail "missing command: $c"
done
[[ -f "$SOURCE_CONFIG" ]] || fail "config missing: $SOURCE_CONFIG"

cd "$ROOT_DIR"

log "release-candidate identity"
BRANCH="$(git branch --show-current)"
HEAD_SHA="$(git rev-parse HEAD)"
[[ "$BRANCH" == "$EXPECTED_BRANCH" ]] || fail "branch=$BRANCH expected=$EXPECTED_BRANCH"
[[ "$HEAD_SHA" == "$EXPECTED_PRECOMMIT_HEAD" ]] || fail "pre-commit HEAD=$HEAD_SHA expected=$EXPECTED_PRECOMMIT_HEAD"
[[ -z "$(git diff --cached --name-only)" ]] || fail "index must be empty before final acceptance"

if git ls-files --error-unmatch DartConfiguration.tcl >/dev/null 2>&1; then
  fail "DartConfiguration.tcl unexpectedly tracked"
fi
if [[ -e DartConfiguration.tcl ]]; then
  log "remove untracked CTest-generated DartConfiguration.tcl"
  rm -f DartConfiguration.tcl
fi

[[ -z "$(git diff -- vcpkg.json)" ]] || fail "vcpkg.json changed; M19 must not add dependencies"
[[ -n "$(git diff -- "$PROTECTED_FILE")" ]] || fail "protected pre-existing GatewayPeerTransport.h delta is missing"

log "source integrity"
git diff --check | tee "$ARTIFACT_DIR/git-diff-check-pre.log"
find "$ROOT_DIR" -type f \( -name '*.rej' -o -name '*.orig' \) -print > "$ARTIFACT_DIR/reject-orig-pre.log"
[[ ! -s "$ARTIFACT_DIR/reject-orig-pre.log" ]] || fail "reject/orig files present"
{
  git branch --show-current
  git rev-parse HEAD
  git status --short
} > "$ARTIFACT_DIR/git-state-pre.txt"

df -h / > "$ARTIFACT_DIR/disk-pre.txt"
FREE_BYTES="$(df -B1 --output=avail / | tail -n1 | tr -d ' ')"
[[ "$FREE_BYTES" -ge 2147483648 ]] || fail "less than 2 GiB free before final regression"

log "configure with existing dependency tree; manifest install disabled"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DVCPKG_MANIFEST_INSTALL:BOOL=OFF \
  -DBUILD_TESTING=ON \
  | tee "$ARTIFACT_DIR/configure.log"

log "build M19 + touched-surface regression targets (-j${BUILD_JOBS})"
TARGETS=(
  config_business_runtime_tests
  rpc_contract_tests
  social_rpc_client_tests
  group_rpc_client_tests
  file_rpc_client_tests
  business_runtime_tests
  concurrency_tests
  net_tests
  protocol_tests
  group_permission_policy_tests
  group_application_service_tests
  file_application_service_tests
  file_storage_tests
  m19_http_client_tests
  m19_mcp_client_tests
  m19_ai_provider_tests
  m19_agent_orchestrator_tests
  m19_mcp_core_tests
  m19_mcp_domain_tools_tests
  tinyimx_mcp_server
  tinyimx_ai_agent_demo
)

for target in "${TARGETS[@]}"; do
  echo "========== BUILD $target ==========" | tee -a "$ARTIFACT_DIR/build-focused.log"
  cmake --build "$BUILD_DIR" --target "$target" -j"$BUILD_JOBS" \
    2>&1 | tee -a "$ARTIFACT_DIR/build-focused.log"
done

log "run deterministic/touched-surface CTest regression"
CTEST_REGEX='^(tinyimx\.config\.business_runtime|rpc_contract_tests|social_rpc_client_tests|group_rpc_client_tests|file_rpc_client_tests|tinyimx\.business_runtime|tinyimx\.concurrency|tinyimx\.net|tinyimx\.protocol|group_permission_policy_tests|group_application_service_tests|file_application_service_tests|file_storage_tests|tinyimx\.m19\.(mcp_core|mcp_domain_tools|http_client|mcp_client|ai_provider|agent_orchestrator))$'
ctest --test-dir "$BUILD_DIR" \
  -R "$CTEST_REGEX" \
  --output-on-failure \
  --no-tests=error \
  | tee "$ARTIFACT_DIR/ctest-regression.log"
grep -q '100% tests passed, 0 tests failed' "$ARTIFACT_DIR/ctest-regression.log" \
  || fail "touched-surface CTest regression failed"

log "fake-provider native HTTP/MCP/Agent gate"
bash "$ROOT_DIR/scripts/run_m19_ai_runtime_gate.sh" \
  2>&1 | tee "$ARTIFACT_DIR/ai-runtime-focused-gate.log"
grep -q 'M19_AI_RUNTIME_FOCUSED_GATE=PASS' "$ARTIFACT_DIR/ai-runtime-focused-gate.log" \
  || fail "focused AI runtime marker missing"

log "real LLM -> Agent -> MCP -> 5 gRPC services -> MySQL gate"
bash "$ROOT_DIR/scripts/run_m19_real_ai_e2e.sh" "$SOURCE_CONFIG" \
  2>&1 | tee "$ARTIFACT_DIR/real-ai-e2e.log"
grep -q 'M19_REAL_AI_MCP_GRPC_MYSQL_E2E=PASS' "$ARTIFACT_DIR/real-ai-e2e.log" \
  || fail "real AI E2E marker missing"
REAL_TOOL_CALLS="$(grep -Eo 'REAL_AI_TOOL_CALL_COUNT=[0-9]+' "$ARTIFACT_DIR/real-ai-e2e.log" | tail -1 | cut -d= -f2 || true)"
[[ -n "$REAL_TOOL_CALLS" && "$REAL_TOOL_CALLS" -ge 5 ]] \
  || fail "expected at least five real domain tool calls; actual=${REAL_TOOL_CALLS:-missing}"
grep -q 'REAL_AI_FIVE_DOMAIN_SENTINELS=PASS' "$ARTIFACT_DIR/real-ai-e2e.log" \
  || fail "real AI database sentinel evidence missing"
grep -q 'E2E_LISTEN_RELEASE=PASS' "$ARTIFACT_DIR/real-ai-e2e.log" \
  || fail "listener cleanup evidence missing"

log "post-regression source integrity"
git diff --check | tee "$ARTIFACT_DIR/git-diff-check-final.log"
find "$ROOT_DIR" -type f \( -name '*.rej' -o -name '*.orig' \) -print > "$ARTIFACT_DIR/reject-orig-final.log"
[[ ! -s "$ARTIFACT_DIR/reject-orig-final.log" ]] || fail "reject/orig files present after regression"
[[ -z "$(git diff -- vcpkg.json)" ]] || fail "vcpkg.json changed during acceptance"
[[ -z "$(git diff --cached --name-only)" ]] || fail "acceptance unexpectedly changed Git index"
if git diff --cached --name-only | grep -Fxq "$PROTECTED_FILE"; then
  fail "protected GatewayPeerTransport.h staged"
fi

{
  echo "branch=$(git branch --show-current)"
  echo "precommit_head=$(git rev-parse HEAD)"
  echo "artifact_dir=$ARTIFACT_DIR"
  echo "protected_delta_present=yes"
  echo "vcpkg_diff=none"
  echo "ai_runtime_focused=PASS"
  echo "real_ai_mcp_grpc_mysql=PASS"
  echo "touched_surface_regression=PASS"
  echo "git_diff_check=PASS"
  echo
  sha256sum "$BUILD_DIR/tinyimx_mcp_server" "$BUILD_DIR/tinyimx_ai_agent_demo"
} > "$ARTIFACT_DIR/M19_ACCEPTANCE_EVIDENCE.txt"

df -h / > "$ARTIFACT_DIR/disk-post.txt"

echo
echo "=================================================="
echo "M19_FINAL_ACCEPTANCE=PASS"
echo "M19_RELEASE_CANDIDATE_READY_FOR_STAGING=PASS"
echo "M19_ACCEPTANCE_ARTIFACT_DIR=$ARTIFACT_DIR"
echo "=================================================="
echo "NOTE: branch is not committed/tagged/frozen yet."
