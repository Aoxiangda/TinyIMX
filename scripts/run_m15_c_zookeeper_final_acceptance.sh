#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
SOURCE_CONFIG="${1:-$ROOT_DIR/config/gateway-a.local.json}"
DEBUG_DIR="$ROOT_DIR/build/linux-debug"
RELEASE_DIR="$ROOT_DIR/build/linux-release"
BUILD_JOBS="${TINYIMX_BUILD_JOBS:-1}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARTIFACT_DIR="${TINYIMX_M15_FINAL_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m15-final-$TIMESTAMP}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tinyimx-m15-final-XXXXXX")"
SUMMARY="$ARTIFACT_DIR/summary.tsv"

export VCPKG_ROOT="${VCPKG_ROOT:-$ROOT_DIR/toolchains/vcpkg-tinyimx}"
export VCPKG_BINARY_SOURCES="${VCPKG_BINARY_SOURCES:-clear;default,readwrite}"
unset X_VCPKG_ASSET_SOURCES || true
unset VCPKG_DOWNLOADS || true

mkdir -p "$ARTIFACT_DIR"/{m15-c,m15-a,m15-b,m14,m13,release,architecture}
printf 'case\tstatus\tlog\n' > "$SUMMARY"
PASS_COUNT=0; FAIL_COUNT=0

log(){ printf '[M15-FINAL] %s\n' "$*"; }
fail(){ log "FAIL: $*"; if [[ -n "${SUMMARY:-}" && -f "${SUMMARY:-}" ]]; then printf '__fatal__\tFAIL\t%s\n' "$*" >> "$SUMMARY"; fi; exit 1; }
record(){ local n="$1" s="$2" f="$3"; printf '%s\t%s\t%s\n' "$n" "$s" "$f" >> "$SUMMARY"; if [[ "$s" == PASS ]]; then PASS_COUNT=$((PASS_COUNT+1)); else FAIL_COUNT=$((FAIL_COUNT+1)); fi; }
require_cmd(){ command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }
for cmd in cmake python3 grep awk timeout tee; do require_cmd "$cmd"; done
[[ -f "$SOURCE_CONFIG" ]] || fail "source config missing: $SOURCE_CONFIG"

cleanup(){
  set +e
  TINYIMX_LOCAL_ZK_BASE="$TMP_DIR/standalone" TINYIMX_LOCAL_ZK_PORT=24181 "$ROOT_DIR/scripts/local_zookeeper.sh" stop >/dev/null 2>&1 || true
  TINYIMX_ZK_ENSEMBLE_BASE="$TMP_DIR/release-ensemble" \
  TINYIMX_ZK_ENSEMBLE_CLIENT_BASE=25181 TINYIMX_ZK_ENSEMBLE_PEER_BASE=25881 TINYIMX_ZK_ENSEMBLE_ELECTION_BASE=26881 \
    "$ROOT_DIR/scripts/local_zookeeper_ensemble.sh" stop >/dev/null 2>&1 || true
  rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT INT TERM

run_case(){
  local name="$1" seconds="$2" logfile="$3"; shift 3
  log "RUN $name"
  set +e
  timeout "${seconds}s" "$@" 2>&1 | tee "$logfile"
  local status=${PIPESTATUS[0]}
  set -e
  if [[ $status -ne 0 ]]; then
    record "$name" FAIL "$logfile"
    fail "$name failed, status=$status"
  fi
  record "$name" PASS "$logfile"
  log "PASS $name"
}

log "C1/C2: enterprise ZooKeeper ensemble + service/discovery failure matrix"
export TINYIMX_M15_C_ARTIFACT_DIR="$ARTIFACT_DIR/m15-c/failure-matrix"
run_case m15-c-failure-matrix 900 "$ARTIFACT_DIR/m15-c/failure-matrix.console.log" \
  bash "$ROOT_DIR/scripts/run_m15_c_failover_recovery.sh" "$SOURCE_CONFIG"
unset TINYIMX_M15_C_ARTIFACT_DIR
grep -q '\[M15-C PASS\] ZooKeeper ensemble + failover/recovery matrix passed' "$ARTIFACT_DIR/m15-c/failure-matrix.console.log" || fail "M15-C failure matrix PASS marker missing"

log "M15-A/B retained focused gates on isolated standalone ZooKeeper"
export TINYIMX_LOCAL_ZK_BASE="$TMP_DIR/standalone"
export TINYIMX_LOCAL_ZK_PORT=24181
"$ROOT_DIR/scripts/local_zookeeper.sh" start | tee "$ARTIFACT_DIR/m15-a/standalone.log"
export TINYIMX_ZOOKEEPER_CONNECT='127.0.0.1:24181'
run_case m15-a-registry 420 "$ARTIFACT_DIR/m15-a/acceptance.log" \
  bash "$ROOT_DIR/scripts/run_m15_a_zookeeper_registry.sh" "$SOURCE_CONFIG"
grep -q '\[M15-A PASS\]' "$ARTIFACT_DIR/m15-a/acceptance.log" || fail "M15-A retained PASS marker missing"
run_case m15-b-discovery 480 "$ARTIFACT_DIR/m15-b/acceptance.log" \
  bash "$ROOT_DIR/scripts/run_m15_b_dynamic_discovery.sh" "$SOURCE_CONFIG"
grep -q '\[M15-B PASS\]' "$ARTIFACT_DIR/m15-b/acceptance.log" || fail "M15-B retained PASS marker missing"
"$ROOT_DIR/scripts/local_zookeeper.sh" stop | tee -a "$ARTIFACT_DIR/m15-a/standalone.log"
unset TINYIMX_LOCAL_ZK_BASE TINYIMX_LOCAL_ZK_PORT TINYIMX_ZOOKEEPER_CONNECT

log "M14 retained ownership/vertical-slice regression"
run_case m14-a3 300 "$ARTIFACT_DIR/m14/a3.log" bash "$ROOT_DIR/scripts/run_m14_a3_vertical_slice.sh" "$SOURCE_CONFIG"
run_case m14-b2 300 "$ARTIFACT_DIR/m14/b2.log" bash "$ROOT_DIR/scripts/run_m14_b2_login_vertical_slice.sh" "$SOURCE_CONFIG"
run_case m14-b3 300 "$ARTIFACT_DIR/m14/b3.log" bash "$ROOT_DIR/scripts/run_m14_b3_user_profile_vertical_slice.sh" "$SOURCE_CONFIG"
run_case m14-c1 360 "$ARTIFACT_DIR/m14/c1.log" bash "$ROOT_DIR/scripts/run_m14_c1_message_read_vertical_slice.sh" "$SOURCE_CONFIG"
run_case m14-c2 420 "$ARTIFACT_DIR/m14/c2.log" bash "$ROOT_DIR/scripts/run_m14_c2_reliable_send_vertical_slice.sh" "$SOURCE_CONFIG"
run_case m14-c3 420 "$ARTIFACT_DIR/m14/c3.log" bash "$ROOT_DIR/scripts/run_m14_c3_message_state_vertical_slice.sh" "$SOURCE_CONFIG"
for marker in \
  'M14-A3 FriendList vertical slice' \
  'M14-B2 Login vertical slice' \
  'M14-B3 User Profile vertical slice' \
  'M14-C1 MessageService read vertical slice' \
  'M14-C2 Reliable Send Acceptance vertical slice' \
  'M14-C3 Message state vertical slice'; do
  grep -R -q "\[PASS\].*$marker" "$ARTIFACT_DIR/m14" || fail "missing M14 marker: $marker"
done

log "M13 final retained regression (includes full M12 19/19)"
run_case m13-final 1200 "$ARTIFACT_DIR/m13/final.log" bash "$ROOT_DIR/scripts/run_m13_final_acceptance.sh"
grep -q '\[M13\] PASS=16 FAIL=0' "$ARTIFACT_DIR/m13/final.log" || fail "M13 16/0 invariant missing"
grep -q '\[M12\] PASS=19 FAIL=0' "$ARTIFACT_DIR/m13/final.log" || fail "nested M12 19/0 invariant missing"

log "Release configure/build + focused M15 runtime"
(cd "$ROOT_DIR" && cmake --preset linux-release) >"$ARTIFACT_DIR/release/configure.log" 2>&1
cmake --build "$RELEASE_DIR" --target \
  config_zookeeper_tests service_instance_tests \
  zookeeper_service_registry_integration_tests \
  config_service_discovery_tests rpc_multi_target_cache_tests \
  zookeeper_service_discovery_integration_tests \
  zookeeper_failover_recovery_integration_tests \
  zookeeper_registry_probe zookeeper_discovery_probe \
  user_service_demo social_service_demo message_service_demo gateway_demo \
  -j"$BUILD_JOBS" >"$ARTIFACT_DIR/release/build.log" 2>&1
record release-build PASS "$ARTIFACT_DIR/release/build.log"

"$RELEASE_DIR/config_zookeeper_tests" >"$ARTIFACT_DIR/release/config-zookeeper.log" 2>&1
"$RELEASE_DIR/service_instance_tests" >"$ARTIFACT_DIR/release/service-instance.log" 2>&1
"$RELEASE_DIR/config_service_discovery_tests" >"$ARTIFACT_DIR/release/config-discovery.log" 2>&1
"$RELEASE_DIR/rpc_multi_target_cache_tests" >"$ARTIFACT_DIR/release/rpc-cache.log" 2>&1
record release-unit-focused PASS "$ARTIFACT_DIR/release"

export TINYIMX_ZK_ENSEMBLE_BASE="$TMP_DIR/release-ensemble"
export TINYIMX_ZK_ENSEMBLE_CLIENT_BASE=25181
export TINYIMX_ZK_ENSEMBLE_PEER_BASE=25881
export TINYIMX_ZK_ENSEMBLE_ELECTION_BASE=26881
"$ROOT_DIR/scripts/local_zookeeper_ensemble.sh" start >"$ARTIFACT_DIR/release/ensemble.log" 2>&1
RELEASE_ZK_CONNECT="$("$ROOT_DIR/scripts/local_zookeeper_ensemble.sh" connect-string)"
"$RELEASE_DIR/zookeeper_service_registry_integration_tests" "$RELEASE_ZK_CONNECT" "/tinyimx-m15-final-release-registry-$$/services" >"$ARTIFACT_DIR/release/registry-integration.log" 2>&1
"$RELEASE_DIR/zookeeper_service_discovery_integration_tests" "$RELEASE_ZK_CONNECT" "/tinyimx-m15-final-release-discovery-$$/services" >"$ARTIFACT_DIR/release/discovery-integration.log" 2>&1
"$RELEASE_DIR/zookeeper_failover_recovery_integration_tests" "$RELEASE_ZK_CONNECT" "/tinyimx-m15-final-release-failover-$$/services" >"$ARTIFACT_DIR/release/failover-integration.log" 2>&1
"$ROOT_DIR/scripts/local_zookeeper_ensemble.sh" stop >>"$ARTIFACT_DIR/release/ensemble.log" 2>&1
unset TINYIMX_ZK_ENSEMBLE_BASE TINYIMX_ZK_ENSEMBLE_CLIENT_BASE TINYIMX_ZK_ENSEMBLE_PEER_BASE TINYIMX_ZK_ENSEMBLE_ELECTION_BASE
for f in registry-integration discovery-integration failover-integration; do grep -q '\[PASS\]' "$ARTIFACT_DIR/release/$f.log" || fail "Release $f has no PASS evidence"; done
record release-zookeeper-integration PASS "$ARTIFACT_DIR/release"

log "Final architecture/repository hard gate"
ARCH="$ARTIFACT_DIR/architecture/final.log"
{
  echo '===== GatewayServer ZooKeeper leak ====='
  ! grep -RniE 'ZooKeeper|zookeeper|zhandle_t|zoo_|GetChildren|AddWatchObserver' gateway/GatewayServer.cpp gateway/GatewayServer.h
  echo '===== Business Proto registry leak ====='
  ! grep -RniE 'ZooKeeper|zookeeper|ephemeral_owner|registry_path|service_discovery' proto
  echo '===== Generic EndpointProvider registry leak ====='
  ! grep -RniE 'ZooKeeper|zookeeper|zhandle_t|zoo_' services/rpc/ServiceEndpointProvider.h services/rpc/StaticServiceEndpointProvider.h services/rpc/StaticServiceEndpointProvider.cpp
  echo '===== RpcClient direct ZooKeeper dependency ====='
  ! grep -RniE 'ZooKeeper|zookeeper|zhandle_t|zoo_' services/rpc/UserRpcClient.* services/rpc/SocialRpcClient.* services/rpc/MessageRpcClient.*
  echo '===== MessageRepository Gateway ownership regression ====='
  ! grep -RniE 'MessageRepository|message_repository_|SetMessageRepository|HasMessageRepository' gateway/GatewayServer.cpp gateway/GatewayServer.h examples/gateway_demo.cpp
  echo '===== whitespace ====='
  git diff --check
  echo '===== executable source audit ====='
  bad="$(git ls-files -s | awk '$1=="100755" && $4 ~ /\.(cpp|cc|cxx|c|h|hpp|proto|cmake)$/ {print}')"
  [[ -z "$bad" ]] || { printf '%s\n' "$bad"; exit 1; }
  for script in scripts/local_zookeeper_ensemble.sh scripts/run_m15_c_failover_recovery.sh scripts/run_m15_c_zookeeper_final_acceptance.sh; do
    mode="$(stat -c '%a' "$script")"; [[ "$mode" == 755 ]] || { echo "$script mode=$mode expected=755"; exit 1; }
  done
  echo '[PASS] M15 final architecture/repository hard gate'
} >"$ARCH" 2>&1
record architecture-final PASS "$ARCH"

cat > "$ARTIFACT_DIR/summary.txt" <<TXT
TinyIMX M15 ZooKeeper Final Acceptance
=======================================
M15-C failure matrix          PASS
M15-A retained registry      PASS
M15-B retained discovery     PASS
M14 retained verticals       PASS
M13 final                    PASS=16 FAIL=0
M12 nested final             PASS=19 FAIL=0
Release build                PASS
Release ZooKeeper integration PASS
Architecture/repository gate PASS

Total recorded gates: PASS=$PASS_COUNT FAIL=$FAIL_COUNT
TXT

if (( FAIL_COUNT != 0 )); then fail "final gate FAIL=$FAIL_COUNT"; fi
cat "$ARTIFACT_DIR/summary.txt"
printf '\n[M15 FINAL PASS] ZooKeeper registry/discovery/failover final acceptance passed.\n'
printf 'artifacts: %s\n' "$ARTIFACT_DIR"
