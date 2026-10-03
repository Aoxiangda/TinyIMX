#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build/linux-release"
STATE_DIR="${TINYIMX_M21_STATE_DIR:-${HOME}/.local/share/tinyimx/m21}"
BUNDLE="${STATE_DIR}/bundle"
JOBS="${TINYIMX_BUILD_JOBS:-1}"

export VCPKG_ROOT="${VCPKG_ROOT:-${ROOT}/toolchains/vcpkg-tinyimx}"

ROCKETMQ_PREFIX="${TINYIMX_ROCKETMQ_PREFIX:-${ROOT}/toolchains/rocketmq-cpp-5.1.1}"
ROCKETMQ_META="${ROCKETMQ_PREFIX}/.tinyimx-rocketmq-isolated-toolchain"

TARGETS=(
  gateway_demo
  user_service_demo
  social_service_demo
  message_service_demo
  group_service_demo
  file_service_demo
  tinyimx_mcp_server
  tinyimx_ai_agent_demo
  outbox_relay_demo
  unread_projector_demo
)

for cmd in cmake ldd cp find grep; do
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "ERROR: required command missing: $cmd"
    exit 10
  }
done

test "$(git -C "$ROOT" branch --show-current)" = "feature/m21-production-release-v1"

rocketmq_sdk_ready() {
  [[ -f "$ROCKETMQ_META" ]] || return 1
  [[ -f "$ROCKETMQ_PREFIX/include/rocketmq/Producer.h" ]] || return 1
  [[ -f "$ROCKETMQ_PREFIX/include/rocketmq/SimpleConsumer.h" ]] || return 1

  find "$ROCKETMQ_PREFIX/lib" "$ROCKETMQ_PREFIX/lib64" \
    -maxdepth 1 \( -type f -o -type l \) \
    -name 'librocketmq.so*' -print -quit 2>/dev/null \
    | grep -q .
}

echo "========== M21 ROCKETMQ C++ SDK GATE =========="

if rocketmq_sdk_ready; then
  echo "M21_ROCKETMQ_CPP_SDK_CACHE=PASS"
else
  echo "RocketMQ C++ SDK cache missing/incomplete; bootstrapping isolated M16 toolchain"
  TINYIMX_BUILD_JOBS="$JOBS" \
  TINYIMX_ROCKETMQ_PREFIX="$ROCKETMQ_PREFIX" \
    bash "$ROOT/scripts/bootstrap_rocketmq_cpp_client.sh"

  rocketmq_sdk_ready || {
    echo "ERROR: RocketMQ C++ SDK still incomplete after bootstrap"
    exit 20
  }
fi

echo "M21_ROCKETMQ_CPP_SDK_GATE=PASS"

echo "========== M21 RELEASE CONFIGURE =========="

cmake \
  --preset linux-release \
  -S "$ROOT" \
  -DTINYIMX_ENABLE_ROCKETMQ_CLIENT=ON \
  -DTINYIMX_ROCKETMQ_PREFIX="$ROCKETMQ_PREFIX"

CACHE_FILE="${BUILD_DIR}/CMakeCache.txt"
test -f "$CACHE_FILE"

grep -Eq '^TINYIMX_ENABLE_ROCKETMQ_CLIENT:BOOL=ON$' "$CACHE_FILE" || {
  echo "ERROR: Release CMake cache did not retain TINYIMX_ENABLE_ROCKETMQ_CLIENT=ON"
  grep -E '^TINYIMX_ENABLE_ROCKETMQ_CLIENT' "$CACHE_FILE" || true
  exit 21
}

grep -Fq "TINYIMX_ROCKETMQ_PREFIX:PATH=${ROCKETMQ_PREFIX}" "$CACHE_FILE" || {
  echo "ERROR: unexpected RocketMQ prefix in Release CMake cache"
  grep -E '^TINYIMX_ROCKETMQ_PREFIX' "$CACHE_FILE" || true
  exit 22
}

TARGET_HELP="$(cmake --build "$BUILD_DIR" --target help 2>/dev/null)"

grep -Eq '(^|[[:space:]])outbox_relay_demo([[:space:]]|$)' <<<"$TARGET_HELP" || {
  echo "ERROR: outbox_relay_demo target missing after RocketMQ-enabled configure"
  exit 23
}

grep -Eq '(^|[[:space:]])unread_projector_demo([[:space:]]|$)' <<<"$TARGET_HELP" || {
  echo "ERROR: unread_projector_demo target missing after RocketMQ-enabled configure"
  exit 24
}

echo "M21_ROCKETMQ_CMAKE_TARGETS=PASS"

echo "========== M21 RELEASE BUILD (-j${JOBS}) =========="

for target in "${TARGETS[@]}"; do
  echo "----- BUILD ${target} (-j${JOBS}) -----"
  cmake --build "$BUILD_DIR" --target "$target" -j"$JOBS"
done

echo "========== M21 RUNTIME BUNDLE =========="

rm -rf "$BUNDLE"
mkdir -p "$BUNDLE/bin" "$BUNDLE/lib"

for target in "${TARGETS[@]}"; do
  test -x "$BUILD_DIR/$target" || {
    echo "ERROR: expected Release binary missing: $BUILD_DIR/$target"
    exit 30
  }
  cp -a "$BUILD_DIR/$target" "$BUNDLE/bin/"
done

# Collect resolved shared objects. The RocketMQ executable RPATH resolves
# librocketmq.so from the isolated prefix, so ldd captures that library too.
for binary in "$BUNDLE"/bin/*; do
  while IFS= read -r lib; do
    [[ -n "$lib" && -f "$lib" ]] || continue

    case "$(basename "$lib")" in
      libc.so.6|libm.so.6|libpthread.so.0|librt.so.1|libdl.so.2|ld-linux-*.so.*)
        continue
        ;;
    esac

    cp -aL "$lib" "$BUNDLE/lib/"
  done < <(
    ldd "$binary" 2>/dev/null \
      | awk '
          $2 == "=>" && $3 ~ /^\// {print $3}
          $1 ~ /^\// {print $1}
        ' \
      | sort -u
  )
done

# Hard runtime closure check using the bundle library directory first.
for binary in "$BUNDLE"/bin/*; do
  if ! LD_LIBRARY_PATH="$BUNDLE/lib" ldd "$binary" >"$STATE_DIR/ldd-$(basename "$binary").txt" 2>&1; then
    cat "$STATE_DIR/ldd-$(basename "$binary").txt"
    exit 31
  fi

  if grep -q 'not found' "$STATE_DIR/ldd-$(basename "$binary").txt"; then
    cat "$STATE_DIR/ldd-$(basename "$binary").txt"
    echo "ERROR: unresolved runtime library: $(basename "$binary")"
    exit 32
  fi
done

{
  echo "m21_base=bb3e3a24180f16d7ac68702d2d185f75c06b5319"
  echo "rocketmq_prefix=$ROCKETMQ_PREFIX"
  echo "created_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "binaries:"
  find "$BUNDLE/bin" -maxdepth 1 -type f -printf '  %f\n' | sort
  echo "libraries:"
  find "$BUNDLE/lib" -maxdepth 1 -type f -printf '  %f\n' | sort
} > "$BUNDLE/MANIFEST.txt"

echo "M21_RUNTIME_BUNDLE=PASS"
echo "BUNDLE_DIR=$BUNDLE"
