#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="${1:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}"
BUILD="$ROOT/build/linux-debug"
EXPECTED_BRANCH="feature/m21-production-release-v1"
PROTECTED="gateway/GatewayPeerTransport.h"
PROTECTED_SHA="66eb26b8182e6b421c79e175c05404bd3e8a7542c974f1f234c06b55d6a13afa"
cd "$ROOT"
export VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/toolchains/vcpkg-tinyimx}"
for cmd in cmake make g++ python3 bash sha256sum; do
  command -v "$cmd" >/dev/null 2>&1 || { echo "FIRST_FAILURE=MISSING_TOOL tool=$cmd"; exit 10; }
done
[[ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]] || { echo "FIRST_FAILURE=VCPKG_ROOT path=$VCPKG_ROOT"; exit 11; }
[[ "$(git branch --show-current)" == "$EXPECTED_BRANCH" ]] || { echo "FIRST_FAILURE=BRANCH expected=$EXPECTED_BRANCH actual=$(git branch --show-current)"; exit 12; }
[[ -z "$(git diff --cached --name-only)" ]] || { echo "FIRST_FAILURE=STAGED_CHANGES_PRESENT"; exit 13; }
[[ "$(sha256sum "$PROTECTED" | awk '{print $1}')" == "$PROTECTED_SHA" ]] || { echo "FIRST_FAILURE=PROTECTED_GATEWAY_SHA"; exit 14; }
git diff --check

echo "========== STAGE1 MESSAGE CONFIGURE =========="
MAKE_BIN="$(command -v make)"
cmake --preset linux-debug -S "$ROOT" -D"CMAKE_MAKE_PROGRAM:FILEPATH=$MAKE_BIN"
[[ -f "$BUILD/CMakeCache.txt" ]] || { echo "FIRST_FAILURE=CMAKE_CACHE_MISSING"; exit 20; }
echo "STAGE1_CONFIGURE_GATE=PASS"

echo "========== STAGE1 MESSAGE BUILD (-j1) =========="
for target in business_runtime_tests business_runtime_acceptance_tests gateway_tests gateway_demo tinyimx_im_loadgen; do
  echo "----- BUILD $target -----"
  cmake --build "$BUILD" --target "$target" -- -j1
done
echo "STAGE1_BUILD_GATE=PASS"

echo "========== STAGE1 MESSAGE TESTS =========="
for test_bin in business_runtime_tests business_runtime_acceptance_tests gateway_tests; do
  echo "----- RUN $test_bin -----"
  "$BUILD/$test_bin"
done
echo "STAGE1_TEST_GATE=PASS"

echo "========== STAGE1 STATIC GATES =========="
bash -n "$ROOT/scripts/tinyimx_final_loadgen_suite.sh"
bash -n "$ROOT/scripts/tinyimx_final_collect_sut.sh"
grep -q 'gateway-message-runtime' "$ROOT/examples/gateway_demo.cpp"
grep -q 'SetMessageExecutor' "$ROOT/gateway/GatewayServer.cpp"
grep -q 'MessageExecutor()' "$ROOT/gateway/GatewayServer.cpp"
grep -q 'gateway message runtime drained' "$ROOT/examples/gateway_demo.cpp"
grep -q 'TINYIMX_MESSAGE_P99_BUDGET_MS' "$ROOT/scripts/tinyimx_final_loadgen_suite.sh"
grep -a -q 'gateway-message-runtime' "$BUILD/gateway_demo"
echo "STAGE1_STATIC_GATE=PASS"
echo "TINYIMX_STAGE1_MESSAGE_VERIFY=PASS"
