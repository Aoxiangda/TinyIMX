#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${1:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}"
OUT="${TINYIMX_FINAL_ARTIFACT_DIR:-$HOME/tinyimx-final-artifacts}"
STATE="${TINYIMX_FINAL_BUILD_STATE:-$HOME/.local/share/tinyimx/m21-final-build}"
IMAGE="${TINYIMX_FINAL_IMAGE:-tinyimx/runtime:m21-final}"
JOBS="${TINYIMX_BUILD_JOBS:-1}"

mkdir -p "$OUT" "$STATE"

"$ROOT/scripts/tinyimx_final_preflight.sh" build
"$ROOT/scripts/tinyimx_final_verify_vmware.sh" "$ROOT"

export TINYIMX_RUNTIME_IMAGE="$IMAGE"
export TINYIMX_M21_STATE_DIR="$STATE"
export TINYIMX_BUILD_JOBS="$JOBS"
export VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/toolchains/vcpkg-tinyimx}"

"$ROOT/scripts/m21_build_runtime_image.sh"
docker image inspect "$IMAGE" >/dev/null

# Release load generators for WSL / dedicated Linux LoadGen hosts.
cmake --build "$ROOT/build/linux-release" --target tinyimx_im_loadgen tinyimx_failover_loadgen -j"$JOBS"
for bin in tinyimx_im_loadgen tinyimx_failover_loadgen; do
  install -m 0755 "$ROOT/build/linux-release/$bin" "$OUT/$bin"
  ldd "$OUT/$bin" > "$OUT/$bin.ldd.txt"
  if grep -q 'not found' "$OUT/$bin.ldd.txt"; then
    cat "$OUT/$bin.ldd.txt"
    echo "FIRST_FAILURE=LOADGEN_RUNTIME_DEPENDENCY bin=$bin"
    exit 30
  fi
done

IMAGE_TAR="$OUT/tinyimx-m21-final-runtime-image.tar.gz"
docker save "$IMAGE" | gzip -1 > "$IMAGE_TAR"
[[ -s "$IMAGE_TAR" ]]

tar -C "$ROOT" -czf "$OUT/tinyimx-m21-final-deploy-config.tar.gz" \
  deploy/production \
  common/db/schema.sql \
  db/migrations/009_create_file_domain.sql \
  db/migrations/010_create_file_upload_chunks.sql \
  scripts/m21_production_preflight.sh \
  scripts/m21_render_production_configs.py \
  scripts/m21_generate_dev_tls.sh \
  scripts/m21_pull_runtime_images.sh \
  scripts/m21_prepare_rocketmq_volumes.sh \
  scripts/m21_verify_production_schema_complete.sh \
  scripts/m21_prepare_benchmark_users.sh \
  scripts/tinyimx_final_host_tune.sh \
  scripts/tinyimx_final_preflight.sh \
  scripts/tinyimx_final_schema_apply.sh \
  scripts/tinyimx_final_prepare_benchmark_fixture.sh \
  scripts/tinyimx_final_collect_sut.sh \
  scripts/tinyimx_final_loadgen_suite.sh \
  scripts/tinyimx_capstone_common.sh \
  scripts/tinyimx_capstone_failover.sh \
  scripts/tinyimx_capstone_user_scale.sh \
  scripts/tinyimx_capstone_mq_fault.sh \
  scripts/tinyimx_capstone_hotspot_group.sh \
  scripts/tinyimx_capstone_file.sh \
  scripts/tinyimx_capstone_soak.sh \
  scripts/tinyimx_capstone_backpressure.sh \
  scripts/tinyimx_capstone_local_acceptance.sh \
  scripts/tinyimx_capstone_verify.sh \
  scripts/tinyimx_capstone_presence_audit.py \
  scripts/tinyimx_capstone_auth_report.py \
  scripts/tinyimx_capstone_report.py \
  scripts/m21_resource_sampler.sh

{
  echo "image=$IMAGE"
  echo "image_id=$(docker image inspect -f '{{.Id}}' "$IMAGE")"
  echo "source_branch=$(git -C "$ROOT" branch --show-current)"
  echo "source_head=$(git -C "$ROOT" rev-parse HEAD)"
  echo "protected_gateway_sha=$(sha256sum "$ROOT/gateway/GatewayPeerTransport.h" | awk '{print $1}')"
  echo "created_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$OUT/FINAL_MANIFEST.txt"

(
  cd "$OUT"
  sha256sum \
    tinyimx-m21-final-runtime-image.tar.gz \
    tinyimx-m21-final-deploy-config.tar.gz \
    tinyimx_im_loadgen \
    tinyimx_im_loadgen.ldd.txt \
    tinyimx_failover_loadgen \
    tinyimx_failover_loadgen.ldd.txt \
    FINAL_MANIFEST.txt \
    > SHA256SUMS
)

cat "$OUT/FINAL_MANIFEST.txt"
cat "$OUT/SHA256SUMS"
echo "TINYIMX_FINAL_ARTIFACT_BUILD=PASS"
echo "ARTIFACT_DIR=$OUT"
