#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/tinyimx_capstone_common.sh"
STAMP="$(date +%Y%m%d-%H%M%S)"
ART="${TINYIMX_CAPSTONE_ARTIFACT_DIR:-$HOME/tinyimx-final-evidence/capstone-backpressure-${STAMP}}"
BUILD="${TINYIMX_BUILD_DIR:-$ROOT/build/linux-release}"
BIN="${TINYIMX_LOADGEN_BIN:-$BUILD/tinyimx_im_loadgen}"
TARGET="${TINYIMX_TARGET_HOST:-127.0.0.1}"
CONNS="${TINYIMX_BACKPRESSURE_CONNECTIONS:-1000}"
STRESS_RATE="${TINYIMX_BACKPRESSURE_STRESS_RATE:-500}"
STRESS_DURATION="${TINYIMX_BACKPRESSURE_STRESS_DURATION:-45}"
RECOVERY_RATE="${TINYIMX_BACKPRESSURE_RECOVERY_RATE:-100}"
RECOVERY_DURATION="${TINYIMX_BACKPRESSURE_RECOVERY_DURATION:-30}"
mkdir -p "$ART"

capstone_require_clean_benchmark_host
if [[ "${TINYIMX_CAPSTONE_SKIP_BUILD:-0}" != 1 ]]; then
  cmake --build "$BUILD" --target tinyimx_im_loadgen -- -j1 | tee "$ART/build.log"
else
  echo "CAPSTONE_BUILD_SKIPPED=1 bin=$BIN" | tee "$ART/build.log"
fi
[[ -x "$BIN" ]] || capstone_fail LOADGEN_MISSING "$BIN"
for svc in gateway-a gateway-b message-service mysql redis nginx; do
  capstone_wait_healthy "$svc" 60 || capstone_fail SERVICE_NOT_HEALTHY "$svc"
done

containers=(tinyimx-m21-nginx-1 tinyimx-m21-gateway-a-1 tinyimx-m21-gateway-b-1 tinyimx-m21-message-service-1 tinyimx-m21-mysql-1 tinyimx-m21-redis-1)
python3 - "$ART/restart-before.json" "${containers[@]}" <<'PY'
import json,subprocess,sys
out={}
for c in sys.argv[2:]:
    out[c]=int(subprocess.check_output(['docker','inspect','-f','{{.RestartCount}}',c],text=True).strip())
json.dump(out,open(sys.argv[1],'w'),indent=2)
PY

run_case(){
  local name="$1" rate="$2" dur="$3" log="$ART/$name.log"
  set +e
  "$BIN" --host "$TARGET" --port 9000 --connections "$CONNS" \
    --user-id-base 500000 --username-prefix m21b500000_ --password 123456 \
    --mode private --peer-mode ring --duration "$dur" --rate "$rate" --ramp-per-sec 100 \
    --payload-bytes 128 --heartbeat-seconds 30 --drain-seconds 10 --max-outstanding 1 \
    2>&1 | tee "$log"
  local rc=${PIPESTATUS[0]}
  set -e
  [[ "$rc" == 0 || "$rc" == 2 ]] || capstone_fail BACKPRESSURE_LOADGEN_RUNTIME "case=$name rc=$rc"
  local result
  result="$(grep '^TINYIMX_LOADGEN_RESULT ' "$log" | tail -1 | sed 's/^TINYIMX_LOADGEN_RESULT //')"
  [[ -n "$result" ]] || capstone_fail BACKPRESSURE_RESULT_MISSING "$name"
  printf '%s\n' "$result" > "$ART/$name-result.json"
}

"$ROOT/scripts/m21_resource_sampler.sh" "$ART/resources.log" 1 & SAMPLE_PID=$!
trap 'set +e; kill "${SAMPLE_PID:-}" 2>/dev/null || true; wait "${SAMPLE_PID:-}" 2>/dev/null || true' EXIT
run_case stress "$STRESS_RATE" "$STRESS_DURATION"
sleep 15
run_case recovery "$RECOVERY_RATE" "$RECOVERY_DURATION"
kill "$SAMPLE_PID" 2>/dev/null || true; wait "$SAMPLE_PID" 2>/dev/null || true; SAMPLE_PID=""
trap - EXIT

python3 - "$ART/restart-after.json" "${containers[@]}" <<'PY'
import json,subprocess,sys
out={}
for c in sys.argv[2:]:
    out[c]=int(subprocess.check_output(['docker','inspect','-f','{{.RestartCount}}',c],text=True).strip())
json.dump(out,open(sys.argv[1],'w'),indent=2)
PY

python3 - "$ART/stress-result.json" "$ART/recovery-result.json" "$ART/restart-before.json" "$ART/restart-after.json" "$STRESS_RATE" "$RECOVERY_RATE" <<'PY' | tee "$ART/gate.log"
import json,sys
stress=json.load(open(sys.argv[1])); rec=json.load(open(sys.argv[2])); before=json.load(open(sys.argv[3])); after=json.load(open(sys.argv[4])); sr=float(sys.argv[5]); rr=float(sys.argv[6])
restart_ok=all(after.get(k)==v for k,v in before.items())
stress_pressure=(stress.get('overload_rejections',0)>0 or stress.get('chat_ack_fail',0)>0 or float(stress.get('p99_ms',0))>100 or float(stress.get('throughput_msg_s',0))<sr*0.95)
recovery_ok=(rec.get('setup_failed') is False and rec.get('connected')==rec.get('connections') and rec.get('login_ok')==rec.get('connections') and rec.get('login_fail')==0 and rec.get('chat_ack_fail')==0 and rec.get('overload_rejections')==0 and rec.get('protocol_errors')==0 and rec.get('server_errors')==0 and float(rec.get('success_rate',0))>=99.9 and float(rec.get('throughput_msg_s',0))>=rr*0.95 and float(rec.get('p99_ms',0))<=100.0)
checks={'stress_completed':stress.get('connected')==stress.get('connections') and stress.get('login_ok')==stress.get('connections'),'recovery':recovery_ok,'no_restart':restart_ok}
print('TINYIMX_CAPSTONE_BACKPRESSURE_RESULT stress_rate=%s stress_throughput=%s stress_success=%s stress_p99_ms=%s stress_overload=%s pressure_observed=%s recovery_rate=%s recovery_throughput=%s recovery_success=%s recovery_p99_ms=%s restarts_unchanged=%s checks=%s' % (sr,stress.get('throughput_msg_s'),stress.get('success_rate'),stress.get('p99_ms'),stress.get('overload_rejections'),int(stress_pressure),rr,rec.get('throughput_msg_s'),rec.get('success_rate'),rec.get('p99_ms'),int(restart_ok),','.join(k for k,v in checks.items() if not v) or 'all-pass'))
if not all(checks.values()): raise SystemExit(42)
PY

sha256sum "$ART"/* 2>/dev/null > "$ART/SHA256SUMS" || true
echo "TINYIMX_CAPSTONE_BACKPRESSURE_GATE=PASS" | tee "$ART/PASS.marker"
echo "ARTIFACT_DIR=$ART"
