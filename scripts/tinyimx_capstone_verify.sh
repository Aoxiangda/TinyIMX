#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="${1:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}"
cd "$ROOT"

echo '========== CAPSTONE STATIC =========='
for f in \
  scripts/tinyimx_capstone_common.sh \
  scripts/tinyimx_capstone_failover.sh \
  scripts/tinyimx_capstone_user_scale.sh \
  scripts/tinyimx_capstone_mq_fault.sh \
  scripts/tinyimx_capstone_hotspot_group.sh \
  scripts/tinyimx_capstone_file.sh \
  scripts/tinyimx_capstone_soak.sh \
  scripts/tinyimx_capstone_backpressure.sh \
  scripts/tinyimx_capstone_local_acceptance.sh; do
  bash -n "$f"
done
python3 -m py_compile scripts/tinyimx_capstone_presence_audit.py scripts/tinyimx_capstone_auth_report.py scripts/tinyimx_capstone_report.py
python3 - <<'PY'
from pathlib import Path
s=Path('CMakeLists.txt').read_text()
assert 'tinyimx_failover_loadgen' in s
assert Path('benchmark/tinyimx_failover_loadgen.cpp').exists()
assert Path('deploy/production/docker-compose.capstone-user-scale.yml').exists()
gw = Path('examples/gateway_demo.cpp').read_text()
server = Path('gateway/GatewayServer.cpp').read_text()
assert 'gateway-presence-runtime' in gw
assert 'SetPresenceExecutor' in gw
assert 'PresenceExecutor()->Submit' in server
assert 'PresenceExecutor(),' in server
assert 'gateway restoring missing online status' in server
print('CAPSTONE_STATIC_GATE=PASS')
PY

echo '========== CAPSTONE CONFIGURE/BUILD =========='
cmake --preset linux-debug
cmake --build build/linux-debug --target tinyimx_failover_loadgen tinyimx_im_loadgen gateway_demo gateway_tests -- -j1

echo '========== CAPSTONE REGRESSION =========='
./build/linux-debug/gateway_tests

echo 'CAPSTONE_BUILD_GATE=PASS'
echo 'TINYIMX_FINAL_CAPSTONE_VERIFY=PASS'
