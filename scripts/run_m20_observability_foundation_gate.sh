#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${HOME}/projects/TinyIMX_publish"
BUILD="${ROOT}/build/linux-debug"
TOOLCHAIN="${ROOT}/toolchains/vcpkg-tinyimx/scripts/buildsystems/vcpkg.cmake"
INSTALLED="${ROOT}/vcpkg_installed"
JOBS="${TINYIMX_BUILD_JOBS:-1}"

cd "${ROOT}"

[[ "$(git branch --show-current)" == "feature/m20-observability-v1" ]]

if [[ ! -f "${INSTALLED}/x64-linux/share/opentelemetry-cpp/opentelemetry-cpp-config.cmake" ]]; then
  echo "ERROR: opentelemetry-cpp is not installed in the reused TinyIMX vcpkg tree."
  echo "Run scripts/m20_observability_dependency_dry_run.sh first, review the plan, then install."
  exit 20
fi

echo "========== M20 OBSERVABILITY CONFIGURE =========="
cmake -S "${ROOT}" -B "${BUILD}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
  -DVCPKG_INSTALLED_DIR="${INSTALLED}" \
  -DVCPKG_MANIFEST_INSTALL=OFF \
  -DVCPKG_TARGET_TRIPLET=x64-linux

echo "========== M20 OBSERVABILITY BUILD =========="
for target in \
  m20_observability_config_tests \
  m20_observability_runtime_tests \
  concurrency_tests \
  net_tests \
  tinyimx_observability_demo
do
  echo "----- BUILD ${target} -----"
  cmake --build "${BUILD}" --target "${target}" -j"${JOBS}"
done

echo "========== M20 OBSERVABILITY CTEST =========="
ctest --test-dir "${BUILD}" \
  -R '^(tinyimx\.m20\.observability_config|tinyimx\.m20\.observability_runtime|tinyimx\.concurrency|tinyimx\.net)$' \
  --output-on-failure \
  --no-tests=error

echo "========== M20 COLLECTOR-UNAVAILABLE SAFETY =========="
"${BUILD}/m20_observability_runtime_tests"

echo
echo "M20_OBSERVABILITY_FOUNDATION_GATE=PASS"
echo "NOTE: Collector/Prometheus network E2E is a separate gate."
