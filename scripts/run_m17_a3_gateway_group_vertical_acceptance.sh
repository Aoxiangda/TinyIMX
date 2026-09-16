#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
SOURCE_CONFIG="${1:-${ROOT_DIR}/config/gateway-a.local.json}"
BUILD_DIR="${TINYIMX_BUILD_DIR:-${ROOT_DIR}/build/linux-debug}"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
ROCKETMQ_PREFIX="${TINYIMX_ROCKETMQ_PREFIX:-${ROOT_DIR}/toolchains/rocketmq-cpp-5.1.1}"
export VCPKG_ROOT="${VCPKG_ROOT:-${ROOT_DIR}/toolchains/vcpkg-tinyimx}"
export VCPKG_BINARY_SOURCES="${VCPKG_BINARY_SOURCES:-clear;default,readwrite}"
unset X_VCPKG_ASSET_SOURCES || true
unset VCPKG_DOWNLOADS || true

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${ROOT_DIR}/artifacts/m17-a3-${TIMESTAMP}"
mkdir -p "$ARTIFACT_DIR"

log(){ printf '[M17-A3] %s\n' "$*"; }
fail(){ log "FAIL: $*"; exit 1; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }

[[ -f "$SOURCE_CONFIG" ]] || fail "missing config: $SOURCE_CONFIG"
[[ -f "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" ]] || \
  fail "vcpkg toolchain missing: ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
[[ -f "${ROCKETMQ_PREFIX}/.tinyimx-rocketmq-isolated-toolchain" ]] || \
  fail "RocketMQ isolated SDK missing: ${ROCKETMQ_PREFIX}"
for c in cmake grep python3; do require_cmd "$c"; done

log "static A3 architecture/security hard gate"
grep -q 'kGroup' "$ROOT_DIR/services/rpc/ServiceEndpointProvider.h" || fail "ServiceKind::kGroup missing"
grep -q 'GroupRpcClient.cpp' "$ROOT_DIR/CMakeLists.txt" || fail "GroupRpcClient not wired into build"
grep -q '"group"' "$ROOT_DIR/services/registry/zookeeper/ZooKeeperServiceDiscovery.cpp" || fail "Group missing from discovery known-services"
grep -q 'std::array<std::atomic<std::uint64_t>, 4>' "$ROOT_DIR/services/rpc/ZooKeeperServiceEndpointProvider.h" || fail "Group discovery counter missing"
grep -q 'kCreateGroupRequest = 2023' "$ROOT_DIR/common/protocol/Packet.h" || fail "Group packet start id missing"
grep -q 'kListMyGroupsResponse = 2048' "$ROOT_DIR/common/protocol/Packet.h" || fail "Group packet end id missing"
grep -q 'SetGroupRpcClient' "$ROOT_DIR/gateway/GatewayServer.h" || fail "Gateway GroupRpcClient setter missing"
grep -q 'HandleGroupControlRequest' "$ROOT_DIR/gateway/GatewayServer.cpp" || fail "Gateway Group control handler missing"
grep -q 'FindSessionByConnection' "$ROOT_DIR/gateway/GatewayServer.cpp" || fail "Session authoritative identity gate missing"
grep -q 'const UserId actor_user_id = session_snapshot->user_id;' "$ROOT_DIR/gateway/GatewayServer.cpp" || \
  fail "Group actor_user_id is not derived from authenticated Session snapshot"
if grep -R -E -n 'GroupRepository|im_group_members|im_groups' "$ROOT_DIR/gateway" >/dev/null; then
  fail "Gateway directly depends on Group durable storage"
fi
# Security rule: actor_user_id must never be parsed from a client JSON key.
# Match the literal JSON key instead of broad text such as `actor_user_id.*get`,
# which falsely matches safe identifiers like `target_user_id`.
if grep -n -E '"actor_user_id"|\["actor_user_id"\]|\.at\("actor_user_id"\)|\.value\("actor_user_id"' \
  "$ROOT_DIR/gateway/GatewayServer.cpp" >/dev/null; then
  fail "Gateway appears to parse client-supplied actor_user_id"
fi
log "PASS architecture-security-boundary"

log "configure focused A3 build"
(cd "$ROOT_DIR" && cmake --preset linux-debug \
  -DTINYIMX_ENABLE_ROCKETMQ_CLIENT=ON \
  -DTINYIMX_ROCKETMQ_PREFIX="$ROCKETMQ_PREFIX") \
  | tee "$ARTIFACT_DIR/configure.log"

log "build focused A3 targets (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" --target \
  rpc_contract_tests \
  group_rpc_client_tests \
  rpc_multi_target_cache_tests \
  protocol_tests \
  gateway_tests \
  gateway_demo \
  gateway_group_client_demo \
  group_service_demo \
  user_service_demo \
  zookeeper_service_discovery_integration_tests \
  group_repository_integration_tests \
  group_membership_repository_integration_tests \
  -j"$BUILD_JOBS" \
  | tee "$ARTIFACT_DIR/build-focused.log"
log "PASS focused-build"

log "run deterministic A3 contract/unit gates"
ctest --test-dir "$BUILD_DIR" --output-on-failure -R \
  '^(rpc_contract_tests|group_rpc_client_tests|rpc_multi_target_cache_tests|tinyimx.protocol|tinyimx.gateway)$' \
  | tee "$ARTIFACT_DIR/ctest-focused.log"
log "PASS focused-ctest"

log "retained A1/A2 real-MySQL gates"
"$BUILD_DIR/group_repository_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/group-a1-repository-regression.log"
grep -q '\[PASS\] M17-A1 Group Repository integration tests' \
  "$ARTIFACT_DIR/group-a1-repository-regression.log" || fail "A1 repository regression marker missing"
"$BUILD_DIR/group_membership_repository_integration_tests" "$SOURCE_CONFIG" \
  | tee "$ARTIFACT_DIR/group-a2-membership-regression.log"
grep -q '\[PASS\] M17-A2 Membership Repository integration tests' \
  "$ARTIFACT_DIR/group-a2-membership-regression.log" || fail "A2 membership regression marker missing"
log "PASS retained-group-domain"

if [[ "${TINYIMX_M17_A3_RUN_ZK_INTEGRATION:-0}" == "1" ]]; then
  ZK_CONNECT="${TINYIMX_ZOOKEEPER_CONNECT:-127.0.0.1:2181}"
  ZK_ROOT="/tinyimx-m17-a3-${TIMESTAMP}/services"
  log "run scoped real ZooKeeper Group discovery integration"
  "$BUILD_DIR/zookeeper_service_discovery_integration_tests" "$ZK_CONNECT" "$ZK_ROOT" \
    | tee "$ARTIFACT_DIR/zookeeper-group-discovery.log"
  grep -q 'ServiceKind::kGroup resolves discovered GroupService' \
    "$ARTIFACT_DIR/zookeeper-group-discovery.log" || fail "Group ZooKeeper resolve marker missing"
  log "PASS zookeeper-group-discovery"
else
  log "SKIP real ZooKeeper integration; set TINYIMX_M17_A3_RUN_ZK_INTEGRATION=1 when ZooKeeper is available"
fi

log "build complete ordinary regression target graph (-j${BUILD_JOBS})"
cmake --build "$BUILD_DIR" -j"$BUILD_JOBS" \
  | tee "$ARTIFACT_DIR/build-ordinary.log"
log "PASS ordinary-build"

log "retained ordinary regression (non-destructive only)"
ctest --test-dir "$BUILD_DIR" --output-on-failure \
  | tee "$ARTIFACT_DIR/ctest-ordinary.log"
log "PASS ordinary-regression"

printf '\n[M17-A3 PASS] Gateway Group vertical integration code/contract acceptance passed.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
printf 'NOTE: real ZooKeeper gate is executed only when TINYIMX_M17_A3_RUN_ZK_INTEGRATION=1.\n'
printf 'NOTE: end-to-end TCP Group scenario is driven with gateway_group_client_demo after UserService/GroupService/Gateway are started.\n'
