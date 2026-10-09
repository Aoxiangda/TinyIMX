#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXPECTED_BRANCH="feature/m19-ai-mcpserver-v1"
EXPECTED_PRECOMMIT_HEAD="212de4eaa14bd05ef21016b487bd476628ad7bd4"
PROTECTED_FILE="gateway/GatewayPeerTransport.h"

cd "$ROOT_DIR"

[[ "$(git branch --show-current)" == "$EXPECTED_BRANCH" ]] || {
  echo "ERROR: wrong branch" >&2; exit 10;
}
[[ "$(git rev-parse HEAD)" == "$EXPECTED_PRECOMMIT_HEAD" ]] || {
  echo "ERROR: unexpected pre-commit HEAD" >&2; exit 11;
}
[[ -z "$(git diff --cached --name-only)" ]] || {
  echo "ERROR: index is not empty before exact M19 staging" >&2; exit 12;
}

if git ls-files --error-unmatch DartConfiguration.tcl >/dev/null 2>&1; then
  echo "ERROR: DartConfiguration.tcl is tracked" >&2
  exit 13
fi
rm -f DartConfiguration.tcl

[[ -z "$(git diff -- vcpkg.json)" ]] || {
  echo "ERROR: vcpkg.json has a diff" >&2; exit 14;
}
[[ -n "$(git diff -- "$PROTECTED_FILE")" ]] || {
  echo "ERROR: protected pre-existing GatewayPeerTransport.h delta is missing" >&2; exit 15;
}

git add \
  M19_CLOSEOUT_MANIFEST.md \
  CMakeLists.txt \
  common/config/Config.cpp \
  common/config/ConfigTypes.h \
  config/gateway.example.json \
  config/mcp_server.example.json \
  config/ai_agent.example.json \
  cmake/TinyIMXMcp.cmake \
  cmake/TinyIMXAI.cmake \
  examples/mcp_server_demo.cpp \
  examples/ai_agent_demo.cpp \
  services/rpc/FileRpcClient.cpp \
  services/rpc/FileRpcClient.h \
  services/rpc/FileRpcTypes.h \
  tests/rpc/file_rpc_client_test.cpp \
  services/intelligence/ai/AIProvider.h \
  services/intelligence/ai/AIRuntimeConfig.cpp \
  services/intelligence/ai/AIRuntimeConfig.h \
  services/intelligence/ai/AITypes.h \
  services/intelligence/ai/AgentOrchestrator.cpp \
  services/intelligence/ai/AgentOrchestrator.h \
  services/intelligence/ai/OpenAICompatibleProvider.cpp \
  services/intelligence/ai/OpenAICompatibleProvider.h \
  services/intelligence/http/HttpClient.cpp \
  services/intelligence/http/HttpClient.h \
  services/intelligence/mcp/HttpCodec.cpp \
  services/intelligence/mcp/HttpCodec.h \
  services/intelligence/mcp/McpAuth.cpp \
  services/intelligence/mcp/McpAuth.h \
  services/intelligence/mcp/McpClient.cpp \
  services/intelligence/mcp/McpClient.h \
  services/intelligence/mcp/McpDispatcher.cpp \
  services/intelligence/mcp/McpDispatcher.h \
  services/intelligence/mcp/McpDomainTools.cpp \
  services/intelligence/mcp/McpDomainTools.h \
  services/intelligence/mcp/McpRegistry.cpp \
  services/intelligence/mcp/McpRegistry.h \
  services/intelligence/mcp/McpRpcDomainBackend.cpp \
  services/intelligence/mcp/McpServer.cpp \
  services/intelligence/mcp/McpServer.h \
  services/intelligence/mcp/McpTypes.cpp \
  services/intelligence/mcp/McpTypes.h \
  tests/ai/agent_orchestrator_test.cpp \
  tests/ai/ai_provider_test.cpp \
  tests/ai/http_client_test.cpp \
  tests/ai/mcp_client_test.cpp \
  tests/mcp/mcp_core_test.cpp \
  tests/mcp/mcp_domain_tools_test.cpp \
  scripts/m19_fake_openai_provider.py \
  scripts/run_m19_ai_agent_smoke.sh \
  scripts/run_m19_ai_runtime_gate.sh \
  scripts/run_m19_real_ai_e2e.sh \
  scripts/run_m19_final_acceptance.sh \
  scripts/stage_m19_release.sh \
  scripts/m19_final_freeze_check.sh

if git diff --cached --name-only | grep -Fxq "$PROTECTED_FILE"; then
  echo "ERROR: protected GatewayPeerTransport.h staged" >&2
  exit 20
fi
if git diff --cached --name-only | grep -Fxq 'vcpkg.json'; then
  echo "ERROR: vcpkg.json staged" >&2
  exit 21
fi

git diff --cached --check
bash scripts/git_preflight.sh

echo "========== M19 EXACT STAGED FILES =========="
git diff --cached --name-only

echo
echo "M19_EXACT_STAGING=PASS"
echo "Protected local delta remains unstaged: $PROTECTED_FILE"
