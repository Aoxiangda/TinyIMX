#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/tinyimx_capstone_common.sh"

STAMP="$(date +%Y%m%d-%H%M%S)"
ART="${TINYIMX_CAPSTONE_ARTIFACT_DIR:-$HOME/tinyimx-final-evidence/capstone-user-scale-${STAMP}}"
BUILD="${TINYIMX_BUILD_DIR:-$ROOT/build/linux-release}"
BIN="${TINYIMX_LOADGEN_BIN:-$BUILD/tinyimx_im_loadgen}"
TARGET="${TINYIMX_TARGET_HOST:-127.0.0.1}"
mkdir -p "$ART/single" "$ART/dual"

capstone_require_clean_benchmark_host
if [[ "${TINYIMX_CAPSTONE_SKIP_BUILD:-0}" != 1 ]]; then
  cmake --build "$BUILD" --target tinyimx_im_loadgen -- -j1 | tee "$ART/build.log"
else
  echo "CAPSTONE_BUILD_SKIPPED=1 bin=$BIN" | tee "$ART/build.log"
fi
[[ -x "$BIN" ]] || capstone_fail LOADGEN_MISSING "$BIN"

SCALE_COMPOSE=(
  docker compose
  --env-file "$ROOT/deploy/production/.env"
  -f "$ROOT/deploy/production/docker-compose.yml"
  -f "$ROOT/deploy/production/docker-compose.capstone-user-scale.yml"
)

cleanup_scale(){
  set +e
  "${SCALE_COMPOSE[@]}" stop user-service-b >/dev/null 2>&1 || true
  "${SCALE_COMPOSE[@]}" rm -f user-service-b >/dev/null 2>&1 || true
  capstone_compose restart gateway-a gateway-b >/dev/null 2>&1 || true
}
trap cleanup_scale EXIT

prepare_user_b() {
  python3 - "$TINYIMX_M21_STATE_DIR/config/user.json" "$TINYIMX_M21_STATE_DIR/config/user-b.json" <<'PY'
import json,sys
src,dst=sys.argv[1:3]
x=json.load(open(src))
x['app']['instance_id']='tinyimx-user-2'
x['logger']['file']='/tmp/tinyimx/tinyimx-user-2.log'
x['zookeeper']['advertise_host']='user-service-b'
open(dst,'w').write(json.dumps(x,indent=2)+'\n')
PY
  chmod 600 "$TINYIMX_M21_STATE_DIR/config/user-b.json"
}

restart_gateways() {
  capstone_compose restart gateway-a gateway-b >/dev/null
  capstone_wait_healthy gateway-a 60 || capstone_fail GATEWAY_A_RESTART_FAILED
  capstone_wait_healthy gateway-b 60 || capstone_fail GATEWAY_B_RESTART_FAILED
  sleep 5
}

run_sweep() {
  local label="$1" rates="$2" out="$ART/$label"
  rm -f "$out"/*.log "$out"/results.jsonl 2>/dev/null || true
  TINYIMX_TARGET_HOST="$TARGET" \
  TINYIMX_LOADGEN_BIN="$BIN" \
  TINYIMX_LOADGEN_OUT="$out" \
  TINYIMX_AUTH_RATES="$rates" \
  TINYIMX_AUTH_CONNECTIONS="${TINYIMX_AUTH_CONNECTIONS:-1000}" \
  "$ROOT/scripts/tinyimx_final_loadgen_suite.sh" auth | tee "$out/runner.log"
  python3 "$ROOT/scripts/tinyimx_capstone_auth_report.py" "$out/results.jsonl" --output "$out/report.json" | tee "$out/report.txt"
}

# Single UserService baseline.
"${SCALE_COMPOSE[@]}" stop user-service-b >/dev/null 2>&1 || true
"${SCALE_COMPOSE[@]}" rm -f user-service-b >/dev/null 2>&1 || true
restart_gateways
run_sweep single "${TINYIMX_AUTH_RATES_SINGLE:-350 500 650 750 900 1000}"

# Dual UserService, distinct ZooKeeper endpoint, Gateway client-side round-robin.
prepare_user_b
"${SCALE_COMPOSE[@]}" up -d user-service-b >/dev/null
for _ in $(seq 1 90); do
  cid="$("${SCALE_COMPOSE[@]}" ps -q user-service-b 2>/dev/null || true)"
  [[ -n "$cid" ]] || { sleep 1; continue; }
  health="$(docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' "$cid" 2>/dev/null || true)"
  [[ "$health" == healthy ]] && break
  sleep 1
done
cid="$("${SCALE_COMPOSE[@]}" ps -q user-service-b)"
[[ "$(docker inspect -f '{{.State.Health.Status}}' "$cid")" == healthy ]] || capstone_fail USER_SERVICE_B_NOT_HEALTHY
restart_gateways

# Verify both user endpoints registered before A/B measurement.
FOUND_TWO=0
for _ in $(seq 1 30); do
  if capstone_compose logs --no-color --since 2m gateway-a gateway-b 2>/dev/null | grep -E 'service=user.*instance_count=2' >/dev/null; then FOUND_TWO=1; break; fi
  sleep 1
done
[[ "$FOUND_TWO" == 1 ]] || capstone_fail USER_SERVICE_DISCOVERY_DID_NOT_REACH_TWO_INSTANCES
run_sweep dual "${TINYIMX_AUTH_RATES_DUAL:-500 650 750 900 1000 1200}"

python3 - "$ART/single/report.json" "$ART/dual/report.json" <<'PY' | tee "$ART/summary.txt"
import json,sys
s=json.load(open(sys.argv[1])); d=json.load(open(sys.argv[2]))
a=int(s['last_stable_rate']); b=int(d['last_stable_rate'])
eff=(b/(2*a)) if a else 0
print(f"TINYIMX_CAPSTONE_USER_SCALE single_stable={a} dual_stable={b} scaling_efficiency={eff:.4f} single_first_unstable={s['first_unstable_rate']} dual_first_unstable={d['first_unstable_rate']}")
if a<=0 or b<=0: raise SystemExit(42)
PY
if [[ "${TINYIMX_REQUIRE_SCALE_GAIN:-0}" == 1 ]]; then
  SINGLE="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["last_stable_rate"])' "$ART/single/report.json")"
  DUAL="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["last_stable_rate"])' "$ART/dual/report.json")"
  (( DUAL > SINGLE )) || capstone_fail USER_SERVICE_SCALE_GAIN_NOT_OBSERVED "single=$SINGLE dual=$DUAL"
fi

cleanup_scale
trap - EXIT
capstone_wait_healthy gateway-a 60 || capstone_fail GATEWAY_A_RESTORE_AFTER_SCALE_FAILED
capstone_wait_healthy gateway-b 60 || capstone_fail GATEWAY_B_RESTORE_AFTER_SCALE_FAILED
sha256sum "$ART"/single/* "$ART"/dual/* "$ART"/summary.txt 2>/dev/null > "$ART/SHA256SUMS" || true
echo "TINYIMX_CAPSTONE_USER_SCALE_GATE=PASS" | tee "$ART/PASS.marker"
echo "ARTIFACT_DIR=$ART"
