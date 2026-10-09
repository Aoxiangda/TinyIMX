#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

ROCKETMQ_TAG="${TINYIMX_ROCKETMQ_CPP_TAG:-cpp-5.1.1}"
ROCKETMQ_PREFIX="${TINYIMX_ROCKETMQ_PREFIX:-${ROOT_DIR}/toolchains/rocketmq-cpp-5.1.1}"
ROCKETMQ_SRC_DIR="${TINYIMX_ROCKETMQ_CPP_SRC:-${ROOT_DIR}/toolchains/src/rocketmq-clients-${ROCKETMQ_TAG}}"
ROCKETMQ_BUILD_DIR="${TINYIMX_ROCKETMQ_CPP_BUILD:-${ROOT_DIR}/toolchains/build/rocketmq-clients-${ROCKETMQ_TAG}}"

# Apache RocketMQ C++ cpp-5.1.1 is built against the gRPC 1.46 / protobuf
# 3.19 dependency family. TinyIMX owns a newer gRPC/protobuf stack, therefore
# the RocketMQ transport toolchain is intentionally isolated from vcpkg.
GRPC_TAG="${TINYIMX_ROCKETMQ_GRPC_TAG:-v1.46.3}"
GRPC_SRC_DIR="${TINYIMX_ROCKETMQ_GRPC_SRC:-${ROOT_DIR}/toolchains/src/grpc-${GRPC_TAG}}"
GRPC_BUILD_DIR="${TINYIMX_ROCKETMQ_GRPC_BUILD:-${ROOT_DIR}/toolchains/build/grpc-${GRPC_TAG}-rocketmq}"
GRPC_PREFIX="${TINYIMX_ROCKETMQ_GRPC_PREFIX:-${ROCKETMQ_PREFIX}/deps/grpc-${GRPC_TAG}}"

# gRPC v1.46.3 pins protobuf 3.19.4 as a git submodule. Build/install it
# explicitly first so protoc is guaranteed to be present; do not rely on the
# gRPC super-build to install the protoc executable as a side effect.
PROTOBUF_SRC_DIR="${GRPC_SRC_DIR}/third_party/protobuf"
PROTOBUF_BUILD_DIR="${TINYIMX_ROCKETMQ_PROTOBUF_BUILD:-${ROOT_DIR}/toolchains/build/protobuf-3.19.4-rocketmq}"

JOBS="${TINYIMX_BUILD_JOBS:-1}"
FORCE_REBUILD="${TINYIMX_ROCKETMQ_FORCE_REBUILD:-0}"
TOOLCHAIN_META="${ROCKETMQ_PREFIX}/.tinyimx-rocketmq-isolated-toolchain"
GRPC_META="${GRPC_PREFIX}/.tinyimx-grpc-isolated-toolchain"

RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="${TINYIMX_ROCKETMQ_BOOTSTRAP_LOG_DIR:-${ROOT_DIR}/artifacts/m16-b-bootstrap-${RUN_ID}}"
mkdir -p "$LOG_DIR"

log(){ printf '[rocketmq-cpp-bootstrap] %s\n' "$*"; }
fail(){ log "FAIL: $*"; exit 1; }

for c in git cmake c++ make ldd find tee; do
  command -v "$c" >/dev/null 2>&1 || fail "missing command: $c"
done

[[ -f /usr/include/openssl/ssl.h ]] || \
  fail "OpenSSL development headers missing (Ubuntu: sudo apt install libssl-dev)"
[[ -f /usr/include/zlib.h ]] || \
  fail "zlib development headers missing (Ubuntu: sudo apt install zlib1g-dev)"

git_retry(){
  local attempt=1
  local max_attempts=4
  while true; do
    if git -c http.version=HTTP/1.1 "$@"; then
      return 0
    fi
    if (( attempt >= max_attempts )); then
      return 1
    fi
    log "git command failed (attempt ${attempt}/${max_attempts}); retrying in $((attempt * 2))s: git $*"
    sleep $((attempt * 2))
    attempt=$((attempt + 1))
  done
}

run_logged(){
  local name="$1"
  shift
  local logfile="${LOG_DIR}/${name}.log"
  log "${name}: full log -> ${logfile}"
  if "$@" >"$logfile" 2>&1; then
    log "${name}: PASS"
    return 0
  fi
  log "${name}: FAIL; last 120 log lines follow"
  tail -n 120 "$logfile" >&2 || true
  fail "${name} failed; full log: ${logfile}"
}

clone_or_checkout_tag(){
  local repo="$1"
  local tag="$2"
  local dst="$3"
  if [[ ! -d "$dst/.git" ]]; then
    mkdir -p "$(dirname "$dst")"
    git_retry clone --branch "$tag" --depth 1 --no-recurse-submodules "$repo" "$dst" || \
      fail "failed to clone $repo at tag $tag"
  else
    git_retry -C "$dst" fetch --depth 1 origin "refs/tags/${tag}:refs/tags/${tag}" || \
      git_retry -C "$dst" fetch --depth 1 origin "$tag" || \
      fail "failed to fetch tag $tag for $repo"
    git -C "$dst" checkout --detach "$tag"
  fi
}

init_submodule_with_retry(){
  local repo_dir="$1"
  local path="$2"
  git -C "$repo_dir" submodule sync -- "$path"

  if git_retry -C "$repo_dir" -c submodule.fetchJobs=1 \
      submodule update --init --depth 1 --jobs 1 -- "$path"; then
    return 0
  fi

  log "shallow fetch failed for submodule $path; retrying exact submodule without --depth"
  git_retry -C "$repo_dir" -c submodule.fetchJobs=1 \
    submodule update --init --jobs 1 -- "$path" || \
    fail "failed to initialize required submodule: $path"
}

prepare_grpc_required_submodules(){
  local repo_dir="$1"
  # Only these module-provider dependency families are needed. SSL/zlib use
  # Ubuntu packages and gRPC tests are disabled. Do not recursively initialize
  # bloaty, BoringSSL, googletest, benchmarks, envoy/xDS, etc.
  local required=(
    third_party/abseil-cpp
    third_party/cares/cares
    third_party/protobuf
    third_party/re2
  )
  local path
  for path in "${required[@]}"; do
    init_submodule_with_retry "$repo_dir" "$path"
  done
}

prepare_rocketmq_required_submodules(){
  local repo_dir="$1"
  init_submodule_with_retry "$repo_dir" protos
}

find_config_dir(){
  local root="$1"
  local pattern="$2"
  local hit
  hit="$(find "$root" -type f -iname "$pattern" -print -quit 2>/dev/null || true)"
  [[ -n "$hit" ]] || return 1
  dirname "$hit"
}

rocketmq_library(){
  find "$ROCKETMQ_PREFIX/lib" "$ROCKETMQ_PREFIX/lib64" \
    -maxdepth 1 \( -type f -o -type l \) -name 'librocketmq.so*' -print 2>/dev/null | head -n1 || true
}

verify_isolation(){
  local lib="$1"
  local resolved
  resolved="$(readlink -f "$lib")"
  [[ -f "$resolved" ]] || fail "RocketMQ shared library does not resolve: $lib"

  local deps
  deps="$(ldd "$resolved" 2>&1)" || fail "ldd failed for $resolved: $deps"
  if grep -E 'libgrpc|libprotobuf|libabsl' <<<"$deps" >/dev/null; then
    printf '%s\n' "$deps" >&2
    fail "RocketMQ SDK leaks dynamic gRPC/protobuf/Abseil dependencies; isolation boundary violated"
  fi

  local reloc
  if ! reloc="$(ldd -r "$resolved" 2>&1)"; then
    printf '%s\n' "$reloc" >&2
    fail "RocketMQ SDK has unresolved dynamic symbols"
  fi
  if grep -E 'undefined symbol:' <<<"$reloc" >/dev/null; then
    printf '%s\n' "$reloc" >&2
    fail "RocketMQ SDK has unresolved dynamic symbols"
  fi
}

isolated_dependency_family_ready(){
  [[ -f "$GRPC_META" ]] || return 1
  [[ -x "$GRPC_PREFIX/bin/protoc" ]] || return 1
  find_config_dir "$GRPC_PREFIX" 'protobuf-config.cmake' >/dev/null || return 1
  find_config_dir "$GRPC_PREFIX" 'gRPCConfig.cmake' >/dev/null || return 1
  find_config_dir "$GRPC_PREFIX" 'abslConfig.cmake' >/dev/null || return 1

  local pv
  pv="$($GRPC_PREFIX/bin/protoc --version 2>/dev/null || true)"
  [[ "$pv" == libprotoc\ 3.19.* ]] || return 1
  return 0
}

existing_lib="$(rocketmq_library)"
if [[ "$FORCE_REBUILD" != "1" && -f "$TOOLCHAIN_META" && \
      -f "$ROCKETMQ_PREFIX/include/rocketmq/Producer.h" && \
      -f "$ROCKETMQ_PREFIX/include/rocketmq/SimpleConsumer.h" && \
      -n "$existing_lib" ]]; then
  if grep -Fxq "rocketmq_tag=${ROCKETMQ_TAG}" "$TOOLCHAIN_META" && \
     grep -Fxq "grpc_tag=${GRPC_TAG}" "$TOOLCHAIN_META"; then
    verify_isolation "$existing_lib"
    log "PASS cached isolated SDK tag=$ROCKETMQ_TAG grpc=$GRPC_TAG prefix=$ROCKETMQ_PREFIX library=$existing_lib"
    exit 0
  fi
fi

log "prepare isolated dependency family: protobuf 3.19.x + gRPC ${GRPC_TAG} for RocketMQ ${ROCKETMQ_TAG}"
clone_or_checkout_tag https://github.com/grpc/grpc.git "$GRPC_TAG" "$GRPC_SRC_DIR"
prepare_grpc_required_submodules "$GRPC_SRC_DIR"

if [[ "$FORCE_REBUILD" == "1" ]] || ! isolated_dependency_family_ready; then
  log "isolated dependency prefix is absent/incomplete; rebuilding protobuf + gRPC family"
  rm -rf "$PROTOBUF_BUILD_DIR" "$GRPC_BUILD_DIR" "$GRPC_PREFIX"
  mkdir -p "$PROTOBUF_BUILD_DIR" "$GRPC_BUILD_DIR" "$GRPC_PREFIX"

  [[ -f "$PROTOBUF_SRC_DIR/cmake/CMakeLists.txt" ]] || \
    fail "required protobuf submodule missing: $PROTOBUF_SRC_DIR"

  run_logged protobuf-configure \
    cmake -S "$PROTOBUF_SRC_DIR/cmake" -B "$PROTOBUF_BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$GRPC_PREFIX" \
      -DCMAKE_INSTALL_LIBDIR=lib \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DBUILD_SHARED_LIBS=OFF \
      -Dprotobuf_BUILD_SHARED_LIBS=OFF \
      -Dprotobuf_BUILD_TESTS=OFF \
      -Dprotobuf_BUILD_CONFORMANCE=OFF \
      -Dprotobuf_BUILD_EXAMPLES=OFF \
      -Dprotobuf_BUILD_PROTOC_BINARIES=ON \
      -Dprotobuf_BUILD_LIBPROTOC=ON \
      -Dprotobuf_WITH_ZLIB=OFF

  run_logged protobuf-build \
    cmake --build "$PROTOBUF_BUILD_DIR" -j"$JOBS"
  run_logged protobuf-install \
    cmake --install "$PROTOBUF_BUILD_DIR"

  PROTOC="$GRPC_PREFIX/bin/protoc"
  [[ -x "$PROTOC" ]] || \
    fail "explicit protobuf install completed but protoc is missing: $PROTOC"
  PROTOC_VERSION="$($PROTOC --version 2>/dev/null || true)"
  [[ "$PROTOC_VERSION" == libprotoc\ 3.19.* ]] || \
    fail "unexpected isolated protobuf version after explicit install: ${PROTOC_VERSION:-unknown}"
  log "protobuf gate: ${PROTOC_VERSION}"

  PROTOBUF_DIR="$(find_config_dir "$GRPC_PREFIX" 'protobuf-config.cmake')" || \
    fail "explicit protobuf CMake package missing below $GRPC_PREFIX"

  # gRPC consumes the already-installed protobuf package. This guarantees the
  # compiler and libraries are from exactly the same protobuf 3.19.x build.
  run_logged grpc-configure \
    cmake -S "$GRPC_SRC_DIR" -B "$GRPC_BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$GRPC_PREFIX" \
      -DCMAKE_INSTALL_LIBDIR=lib \
      -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
      -DCMAKE_PREFIX_PATH="$GRPC_PREFIX" \
      -DProtobuf_DIR="$PROTOBUF_DIR" \
      -Dprotobuf_DIR="$PROTOBUF_DIR" \
      -DgRPC_PROTOBUF_PACKAGE_TYPE=CONFIG \
      -DBUILD_SHARED_LIBS=OFF \
      -DgRPC_INSTALL=ON \
      -DgRPC_BUILD_TESTS=OFF \
      -DgRPC_BUILD_CODEGEN=ON \
      -DgRPC_ABSL_PROVIDER=module \
      -DgRPC_CARES_PROVIDER=module \
      -DgRPC_PROTOBUF_PROVIDER=package \
      -DgRPC_RE2_PROVIDER=module \
      -DgRPC_SSL_PROVIDER=package \
      -DgRPC_ZLIB_PROVIDER=package

  run_logged grpc-build \
    cmake --build "$GRPC_BUILD_DIR" -j"$JOBS"
  run_logged grpc-install \
    cmake --install "$GRPC_BUILD_DIR"

  PROTOBUF_DIR="$(find_config_dir "$GRPC_PREFIX" 'protobuf-config.cmake')" || \
    fail "isolated protobuf CMake package missing below $GRPC_PREFIX after gRPC install"
  GRPC_DIR="$(find_config_dir "$GRPC_PREFIX" 'gRPCConfig.cmake')" || \
    fail "isolated gRPC CMake package missing below $GRPC_PREFIX"
  ABSL_DIR="$(find_config_dir "$GRPC_PREFIX" 'abslConfig.cmake')" || \
    fail "isolated Abseil CMake package missing below $GRPC_PREFIX"

  PROTOC="$GRPC_PREFIX/bin/protoc"
  [[ -x "$PROTOC" ]] || fail "isolated protoc missing after gRPC install: $PROTOC"
  PROTOC_VERSION="$($PROTOC --version 2>/dev/null || true)"
  [[ "$PROTOC_VERSION" == libprotoc\ 3.19.* ]] || \
    fail "unexpected isolated protobuf version: ${PROTOC_VERSION:-unknown}; cpp-5.1.1 requires protobuf 3.19.x"

  {
    printf 'schema=2\n'
    printf 'grpc_tag=%s\n' "$GRPC_TAG"
    printf 'protobuf=%s\n' "$PROTOC_VERSION"
    printf 'protobuf_mode=explicit-package\n'
    printf 'grpc_linkage=static-pic\n'
  } > "$GRPC_META"
else
  log "PASS cached isolated protobuf/gRPC dependency family"
fi

PROTOBUF_DIR="$(find_config_dir "$GRPC_PREFIX" 'protobuf-config.cmake')" || \
  fail "isolated protobuf CMake package missing below $GRPC_PREFIX"
GRPC_DIR="$(find_config_dir "$GRPC_PREFIX" 'gRPCConfig.cmake')" || \
  fail "isolated gRPC CMake package missing below $GRPC_PREFIX"
ABSL_DIR="$(find_config_dir "$GRPC_PREFIX" 'abslConfig.cmake')" || \
  fail "isolated Abseil CMake package missing below $GRPC_PREFIX"

PROTOC="$GRPC_PREFIX/bin/protoc"
[[ -x "$PROTOC" ]] || fail "isolated protoc missing below $GRPC_PREFIX/bin"
PROTOC_VERSION="$($PROTOC --version 2>/dev/null || true)"
[[ "$PROTOC_VERSION" == libprotoc\ 3.19.* ]] || \
  fail "unexpected isolated protobuf version: ${PROTOC_VERSION:-unknown}; cpp-5.1.1 requires protobuf 3.19.x"

log "isolated dependency gate: ${PROTOC_VERSION}; gRPC package=$GRPC_DIR"

clone_or_checkout_tag https://github.com/apache/rocketmq-clients.git "$ROCKETMQ_TAG" "$ROCKETMQ_SRC_DIR"
prepare_rocketmq_required_submodules "$ROCKETMQ_SRC_DIR"

rm -rf "$ROCKETMQ_BUILD_DIR"
# Remove only the previous RocketMQ SDK install. Keep the isolated dependency
# prefix under ${ROCKETMQ_PREFIX}/deps intact so repeat runs stay cheap.
rm -rf "$ROCKETMQ_PREFIX/include/rocketmq" "$ROCKETMQ_PREFIX/lib/cmake/rocketmq" "$ROCKETMQ_PREFIX/lib64/cmake/rocketmq"
find "$ROCKETMQ_PREFIX/lib" "$ROCKETMQ_PREFIX/lib64" -maxdepth 1 \
  \( -type f -o -type l \) -name 'librocketmq.so*' -delete 2>/dev/null || true

run_logged rocketmq-configure \
  cmake -S "$ROCKETMQ_SRC_DIR/cpp" -B "$ROCKETMQ_BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$ROCKETMQ_PREFIX" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_PREFIX_PATH="$GRPC_PREFIX" \
    -Dprotobuf_DIR="$PROTOBUF_DIR" \
    -DProtobuf_DIR="$PROTOBUF_DIR" \
    -DgRPC_DIR="$GRPC_DIR" \
    -Dabsl_DIR="$ABSL_DIR" \
    -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
    -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_TESTS=OFF

run_logged rocketmq-build \
  cmake --build "$ROCKETMQ_BUILD_DIR" -j"$JOBS"
run_logged rocketmq-install \
  cmake --install "$ROCKETMQ_BUILD_DIR"

[[ -f "$ROCKETMQ_PREFIX/include/rocketmq/Producer.h" ]] || fail "Producer.h missing after install"
[[ -f "$ROCKETMQ_PREFIX/include/rocketmq/SimpleConsumer.h" ]] || fail "SimpleConsumer.h missing after install"
LIB="$(rocketmq_library)"
[[ -n "$LIB" ]] || fail "librocketmq.so missing after install"
verify_isolation "$LIB"

{
  printf 'schema=2\n'
  printf 'rocketmq_tag=%s\n' "$ROCKETMQ_TAG"
  printf 'grpc_tag=%s\n' "$GRPC_TAG"
  printf 'protobuf=%s\n' "$PROTOC_VERSION"
  printf 'dependency_prefix=%s\n' "$GRPC_PREFIX"
  printf 'linkage=rocketmq-shared-with-static-grpc-family\n'
} > "$TOOLCHAIN_META"

log "PASS isolated SDK tag=$ROCKETMQ_TAG grpc=$GRPC_TAG protobuf=$PROTOC_VERSION prefix=$ROCKETMQ_PREFIX library=$LIB"
log "bootstrap logs: $LOG_DIR"
