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

echo "========== FINAL CANDIDATE CONFIGURE =========="
MAKE_BIN="$(command -v make)"
cmake --preset linux-debug -S "$ROOT" -D"CMAKE_MAKE_PROGRAM:FILEPATH=$MAKE_BIN"
CACHE="$BUILD/CMakeCache.txt"
[[ -f "$CACHE" ]] || { echo "FIRST_FAILURE=CMAKE_CACHE_MISSING"; exit 20; }
ACTUAL_MAKE="$(awk -F= '/^CMAKE_MAKE_PROGRAM:/ {print $2; exit}' "$CACHE")"
[[ -x "$ACTUAL_MAKE" ]] || { echo "FIRST_FAILURE=CMAKE_MAKE_PROGRAM_INVALID path=$ACTUAL_MAKE"; exit 21; }
echo "CMAKE_MAKE_PROGRAM=$ACTUAL_MAKE"
echo "FINAL_CONFIGURE_GATE=PASS"

echo "========== FINAL CANDIDATE BUILD (-j1) =========="
TARGETS=(
  business_runtime_tests
  business_runtime_acceptance_tests
  gateway_tests
  net_tests
  user_application_service_tests
  user_service_integration_tests
  gateway_demo
  user_service_demo
  tinyimx_im_loadgen
)
for target in "${TARGETS[@]}"; do
  echo "----- BUILD $target -----"
  cmake --build "$BUILD" --target "$target" -- -j1
done
echo "FINAL_BUILD_GATE=PASS"

echo "========== FINAL CANDIDATE TESTS =========="
TESTS=(
  business_runtime_tests
  business_runtime_acceptance_tests
  gateway_tests
  net_tests
  user_application_service_tests
  user_service_integration_tests
)
for test_bin in "${TESTS[@]}"; do
  echo "----- RUN $test_bin -----"
  "$BUILD/$test_bin"
done
echo "FINAL_TEST_GATE=PASS"

echo "========== SCRIPT / CONFIG GATES =========="
bash -n "$ROOT/scripts/tinyimx_final_host_tune.sh"
bash -n "$ROOT/scripts/tinyimx_final_preflight.sh"
bash -n "$ROOT/scripts/tinyimx_final_prepare_benchmark_fixture.sh"
bash -n "$ROOT/scripts/tinyimx_final_schema_apply.sh"
bash -n "$ROOT/scripts/tinyimx_final_build_artifacts.sh"
bash -n "$ROOT/scripts/tinyimx_final_local_deploy.sh"
bash -n "$ROOT/scripts/tinyimx_final_collect_sut.sh"
bash -n "$ROOT/scripts/tinyimx_final_loadgen_suite.sh"
bash -n "$ROOT/scripts/tinyimx_final_rollback.sh"
python3 -m py_compile "$ROOT/scripts/m21_render_production_configs.py"

grep -q 'backlog=16384' "$ROOT/deploy/production/nginx/nginx.conf"
grep -q '"backlog": 16384' "$ROOT/scripts/m21_render_production_configs.py"
grep -q '009_create_file_domain.sql' "$ROOT/deploy/production/docker-compose.yml"
grep -q '010_create_file_upload_chunks.sql' "$ROOT/deploy/production/docker-compose.yml"
grep -q 'login_p99_ms' "$ROOT/benchmark/tinyimx_im_loadgen.cpp"
grep -q 'deadline_rejections' "$ROOT/benchmark/tinyimx_im_loadgen.cpp"

echo "FINAL_STATIC_GATE=PASS"
echo "TINYIMX_FINAL_VMWARE_VERIFY=PASS"
