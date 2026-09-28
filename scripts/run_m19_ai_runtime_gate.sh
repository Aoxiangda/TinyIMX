#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="${ROOT:-$HOME/projects/TinyIMX_publish}"
BUILD="${BUILD:-$ROOT/build/linux-debug}"
cd "$ROOT"

cmake -S "$ROOT" -B "$BUILD" -DVCPKG_MANIFEST_INSTALL:BOOL=OFF -DBUILD_TESTING=ON
for target in \
  m19_http_client_tests \
  m19_mcp_client_tests \
  m19_ai_provider_tests \
  m19_agent_orchestrator_tests \
  m19_mcp_core_tests \
  m19_mcp_domain_tools_tests \
  file_rpc_client_tests \
  tinyimx_mcp_server \
  tinyimx_ai_agent_demo
do
  echo "========== BUILD $target =========="
  cmake --build "$BUILD" --target "$target" -j1
done

ctest --test-dir "$BUILD" \
  -R '^(file_rpc_client_tests|tinyimx\.m19\.(mcp_core|mcp_domain_tools|http_client|mcp_client|ai_provider|agent_orchestrator))$' \
  --output-on-failure --no-tests=error

bash "$ROOT/scripts/run_m19_ai_agent_smoke.sh"
git diff --check

echo "M19_AI_RUNTIME_FOCUSED_GATE=PASS"
echo "NOTE: this is not M19 final acceptance or Git freeze."
