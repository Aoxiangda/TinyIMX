#!/usr/bin/env bash
set -euo pipefail

ROOT="${TINYIMX_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
BUILD_DIR="${TINYIMX_BUILD_DIR:-$ROOT/build/linux-debug}"

cd "$ROOT"

echo "========== M21 P0 Social Ownership Closure =========="

if rg -n \
  'FriendRepository|FriendRequestRepository|friend_repository_|friend_request_repository_|SetFriendRepository|SetFriendRequestRepository|HasFriendRepository|HasFriendRequestRepository|MySqlConnectionPool' \
  gateway/GatewayServer.h gateway/GatewayServer.cpp examples/gateway_demo.cpp; then
  echo "M21_P0_SOCIAL_EDGE_OWNERSHIP=FAIL"
  exit 1
fi
echo "M21_P0_SOCIAL_EDGE_OWNERSHIP=PASS"

python3 - <<'PY_CMAKE'
from pathlib import Path
import re
s = Path("CMakeLists.txt").read_text()
for target in ("tinyimx_gateway", "gateway_demo"):
    blocks = [m.group(1) for m in re.finditer(
        rf"target_link_libraries\(\s*{re.escape(target)}\s+(?:PUBLIC|PRIVATE)(.*?)\n\)",
        s,
        re.S,
    )]
    if not blocks:
        raise SystemExit(f"missing target_link_libraries block: {target}")
    merged = "\n".join(blocks)
    for forbidden in ("tinyimx_db", "tinyimx_repository"):
        if re.search(rf"\b{re.escape(forbidden)}\b", merged):
            raise SystemExit(f"{target} still links forbidden dependency: {forbidden}")
print("M21_P0_SOCIAL_CMAKE_OWNERSHIP=PASS")
PY_CMAKE

for symbol in \
  CheckPrivateChatPermission \
  CreateFriendRequest \
  ListPendingIncomingFriendRequests \
  AcceptFriendRequest \
  RejectFriendRequest; do
  if ! rg -q "social_rpc_client_->${symbol}" gateway/GatewayServer.cpp; then
    echo "missing Gateway -> SocialRpcClient path: ${symbol}" >&2
    exit 1
  fi
done

echo "M21_P0_SOCIAL_RPC_PATHS=PASS"

if [[ "$(rg -c 'friend_request_state_uncertain' gateway/GatewayServer.cpp)" -ne 3 ]]; then
  echo "M21_P0_SOCIAL_MUTATION_UNCERTAINTY=FAIL" >&2
  exit 1
fi
echo "M21_P0_SOCIAL_MUTATION_UNCERTAINTY=PASS"

export VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/toolchains/vcpkg-tinyimx}"
if [[ ! -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]]; then
  echo "vcpkg toolchain missing: $VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" >&2
  exit 1
fi
echo "VCPKG_ROOT=$VCPKG_ROOT"

cmake --preset linux-debug
cmake --build --preset build-debug -j1 --target \
  rpc_contract_tests \
  friend_application_service_tests \
  friend_request_application_service_tests \
  social_rpc_client_tests \
  social_ownership_rpc_tests \
  social_service_integration_tests \
  social_service_demo \
  user_service_demo \
  gateway_demo \
  gateway_friend_list_client_demo

ctest --test-dir "$BUILD_DIR" --output-on-failure -R \
  '^(rpc_contract_tests|friend_application_service_tests|friend_request_application_service_tests|social_rpc_client_tests|social_ownership_rpc_tests|social_service_integration_tests)$'

for executable in social_service_demo user_service_demo gateway_demo gateway_friend_list_client_demo; do
  if [[ ! -x "$BUILD_DIR/$executable" ]]; then
    echo "P0_SOCIAL_M14_A3_PREREQ_MISSING=$executable" >&2
    exit 1
  fi
done
echo "M21_P0_SOCIAL_M14_A3_PREREQUISITES=PASS"

# Preserve previously frozen vertical slices and runtime semantics.
bash scripts/run_m14_a3_vertical_slice.sh
bash scripts/run_m13_final_acceptance.sh
bash scripts/run_m12_reliability_regression.sh

if [[ -f gateway/GatewayPeerTransport.h ]]; then
  echo "protected_file_present=gateway/GatewayPeerTransport.h"
fi

echo "M21_P0_SOCIAL_OWNERSHIP_CLOSURE=PASS"
