#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${HOME}/projects/TinyIMX_publish"
VCPKG_ROOT="${ROOT}/toolchains/vcpkg-tinyimx"
REPORT_DIR="${HOME}/tmp/m20-observability-deps-$(date +%Y%m%d-%H%M%S)"

cd "${ROOT}"
mkdir -p "${REPORT_DIR}"

[[ "$(git branch --show-current)" == "feature/m20-observability-v1" ]]
[[ -x "${VCPKG_ROOT}/vcpkg" ]]

echo "========== M20 OBSERVABILITY DEPENDENCY DRY RUN =========="
echo "baseline=$(python3 - <<'PY'
import json
print(json.load(open('vcpkg.json'))['builtin-baseline'])
PY
)"

"${VCPKG_ROOT}/vcpkg" install \
  --triplet x64-linux \
  --dry-run \
  2>&1 | tee "${REPORT_DIR}/vcpkg-dry-run.log"

echo
echo "M20_OBSERVABILITY_DEPENDENCY_DRY_RUN=PASS"
echo "REPORT_DIR=${REPORT_DIR}"
echo "NOTE: no dependency was installed by this script."
