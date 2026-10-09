#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="${1:-}"
[[ -n "$ROOT" ]] || { echo "usage: $0 /path/to/TinyIMX_publish [gateway-config]" >&2; exit 2; }
ROOT="$(cd -- "$ROOT" && pwd)"
CONFIG="${2:-$ROOT/config/gateway-a.local.json}"
export VCPKG_ROOT="${VCPKG_ROOT:-$ROOT/toolchains/vcpkg-tinyimx}"
export VCPKG_BINARY_SOURCES="${VCPKG_BINARY_SOURCES:-clear;default,readwrite}"
unset X_VCPKG_ASSET_SOURCES || true
unset VCPKG_DOWNLOADS || true
fail(){ echo "[M14-FINAL][FAIL] $*" >&2; exit 1; }
run(){ echo; echo "[M14-FINAL] >>> $*"; "$@"; }
run_root(){ echo; echo "[M14-FINAL] >>> (cd $ROOT && $*)"; (cd "$ROOT" && "$@"); }
[[ -f "$CONFIG" ]] || fail "missing gateway config: $CONFIG"
[[ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]] || fail "VCPKG_ROOT/toolchain missing: $VCPKG_ROOT"

for required_script in \
  run_m14_c3_message_state_vertical_slice.sh \
  run_m14_c2_reliable_send_vertical_slice.sh \
  run_m14_c1_message_read_vertical_slice.sh \
  run_m14_b3_user_profile_vertical_slice.sh \
  run_m14_b2_login_vertical_slice.sh \
  run_m14_a3_vertical_slice.sh \
  run_m13_final_acceptance.sh \
  run_m12_reliability_regression.sh; do
  [[ -f "$ROOT/scripts/$required_script" ]] || fail "missing acceptance script: $ROOT/scripts/$required_script"
done

run_root cmake --preset linux-debug
run cmake --build "$ROOT/build/linux-debug" --target \
  rpc_contract_tests message_application_service_tests message_service_integration_tests \
  message_service_demo user_service_demo social_service_demo gateway_demo \
  gateway_session_client_demo gateway_pending_replay_pagination_demo gateway_login_auth_client_demo -j1
run "$ROOT/build/linux-debug/rpc_contract_tests"
run "$ROOT/build/linux-debug/message_application_service_tests"
run "$ROOT/build/linux-debug/message_service_integration_tests"

# M14 vertical slices, newest first then retained regression slices.
run bash "$ROOT/scripts/run_m14_c3_message_state_vertical_slice.sh" "$CONFIG"
run bash "$ROOT/scripts/run_m14_c2_reliable_send_vertical_slice.sh" "$CONFIG"
run bash "$ROOT/scripts/run_m14_c1_message_read_vertical_slice.sh" "$CONFIG"
run bash "$ROOT/scripts/run_m14_b3_user_profile_vertical_slice.sh" "$CONFIG"
run bash "$ROOT/scripts/run_m14_b2_login_vertical_slice.sh" "$CONFIG"
run bash "$ROOT/scripts/run_m14_a3_vertical_slice.sh" "$CONFIG"

# Frozen lower-layer reliability/runtime regressions.
run bash "$ROOT/scripts/run_m13_final_acceptance.sh" "$CONFIG"
run bash "$ROOT/scripts/run_m12_reliability_regression.sh" "$CONFIG"

# Release gate is not optional for closure.
run_root cmake --preset linux-release
run cmake --build "$ROOT/build/linux-release" --target \
  rpc_contract_tests tinyimx_rpc_client tinyimx_message_core tinyimx_message_grpc \
  message_service_demo gateway_demo message_application_service_tests message_service_integration_tests -j1
run "$ROOT/build/linux-release/rpc_contract_tests"
run "$ROOT/build/linux-release/message_application_service_tests"
run "$ROOT/build/linux-release/message_service_integration_tests"

if grep -E -n 'message_repository_|HasMessageRepository|SetMessageRepository|MessageRepository' \
  "$ROOT/gateway/GatewayServer.cpp" "$ROOT/gateway/GatewayServer.h" "$ROOT/examples/gateway_demo.cpp"; then
  fail "final ownership gate failed"
fi
if grep -E -n 'packet_seq|delivery_seq|session_epoch|connection_id|gateway_id' \
  "$ROOT/proto/tinyimx/message/v1/message_service.proto"; then
  fail "MessageService contract leaked Gateway runtime identity"
fi

echo
echo '[M14-FINAL][PASS] Debug + C3 + retained M14/M13/M12 regression + Release gates completed'
echo '[M14-FINAL] Now run Git audit before commit/push: git status; git diff; git diff --stat; git diff --summary; git ls-files -s scripts/*.sh'
