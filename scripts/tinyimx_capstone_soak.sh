#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/scripts/tinyimx_capstone_common.sh"
STAMP="$(date +%Y%m%d-%H%M%S)"
ART="${TINYIMX_CAPSTONE_ARTIFACT_DIR:-$HOME/tinyimx-final-evidence/capstone-soak-${STAMP}}"
BUILD="${TINYIMX_BUILD_DIR:-$ROOT/build/linux-release}"
BIN="${TINYIMX_LOADGEN_BIN:-$BUILD/tinyimx_im_loadgen}"
CONNS="${TINYIMX_SOAK_CONNECTIONS:-10000}"
RATE="${TINYIMX_SOAK_MESSAGE_RATE:-150}"
DURATION="${TINYIMX_SOAK_DURATION_SECONDS:-1800}"
TARGET="${TINYIMX_TARGET_HOST:-127.0.0.1}"
mkdir -p "$ART"

capstone_require_clean_benchmark_host
cmake --build "$BUILD" --target tinyimx_im_loadgen -- -j1 | tee "$ART/build.log"

snapshot(){
  local out="$1"
  python3 - "$out" <<'PY'
import json,subprocess,sys
services=['tinyimx-m21-nginx-1','tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1','tinyimx-m21-user-service-1','tinyimx-m21-message-service-1','tinyimx-m21-mysql-1','tinyimx-m21-redis-1','tinyimx-m21-outbox-relay-1']
r={}
for c in services:
    try:
        status=subprocess.check_output(['docker','inspect','-f','{{.State.Status}}',c],text=True).strip()
        rss=subprocess.check_output(['docker','exec',c,'sh','-c',"awk '/VmRSS:/{print $2}' /proc/1/status"],text=True).strip()
        fd=subprocess.check_output(['docker','exec',c,'sh','-c','ls /proc/1/fd | wc -l'],text=True).strip()
        pids=subprocess.check_output(['docker','inspect','-f','{{.State.Pid}}',c],text=True).strip()
        r[c]={'status':status,'rss_kb':int(rss or 0),'fd':int(fd or 0),'host_pid':int(pids or 0)}
    except Exception as e:
        r[c]={'error':str(e)}
json.dump(r,open(sys.argv[1],'w'),indent=2)
PY
}

snapshot "$ART/before.json"
"$ROOT/scripts/m21_resource_sampler.sh" "$ART/resources.log" 5 & SAMPLE_PID=$!
cleanup(){ set +e; [[ -n "${SAMPLE_PID:-}" ]] && kill "$SAMPLE_PID" 2>/dev/null || true; }
trap cleanup EXIT

LOG="$ART/loadgen.log"
ulimit -Sn 524288 2>/dev/null || true
set +e
"$BIN" --host "$TARGET" --port 9000 --connections "$CONNS" \
  --user-id-base 500000 --username-prefix m21b500000_ --password 123456 \
  --mode private --peer-mode ring --duration "$DURATION" --rate "$RATE" --ramp-per-sec 100 \
  --payload-bytes 128 --heartbeat-seconds 30 --drain-seconds 15 --max-outstanding 1 \
  2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}
set -e
[[ "$RC" -eq 0 || "$RC" -eq 2 ]] || capstone_fail SOAK_LOADGEN_RUNTIME "rc=$RC"

kill "$SAMPLE_PID" 2>/dev/null || true; wait "$SAMPLE_PID" 2>/dev/null || true; SAMPLE_PID=""
snapshot "$ART/after.json"
RESULT="$(grep '^TINYIMX_LOADGEN_RESULT ' "$LOG" | tail -1 | sed 's/^TINYIMX_LOADGEN_RESULT //')"
[[ -n "$RESULT" ]] || capstone_fail SOAK_RESULT_MISSING
printf '%s\n' "$RESULT" > "$ART/result.json"

python3 - "$ART/before.json" "$ART/after.json" "$RESULT" "$CONNS" "$RATE" <<'PY' | tee "$ART/gate.log"
import json,sys
before=json.load(open(sys.argv[1])); after=json.load(open(sys.argv[2])); r=json.loads(sys.argv[3]); n=int(sys.argv[4]); rate=float(sys.argv[5])
hb_sent=int(r.get('heartbeat_sent',0)); hb_ratio=1 if hb_sent==0 else int(r.get('heartbeat_ack',0))/hb_sent
resource_ok=True; deltas={}
for c,b in before.items():
    a=after.get(c,{})
    if 'rss_kb' not in b or 'rss_kb' not in a: resource_ok=False; continue
    rss_delta=a['rss_kb']-b['rss_kb']; fd_delta=a['fd']-b['fd']; deltas[c]={'rss_delta_kb':rss_delta,'fd_delta':fd_delta}
    # After the load generator disconnects, resources should return close to baseline. Allow allocator/cache retention but not runaway growth.
    if rss_delta > max(131072, b['rss_kb']): resource_ok=False
    if fd_delta > 1000: resource_ok=False
checks={
 'connection': r.get('connected')==n and r.get('login_ok')==n and r.get('login_fail')==0,
 'messages': r.get('chat_ack_fail')==0 and float(r.get('success_rate',0))>=99.9 and float(r.get('throughput_msg_s',0))>=rate*0.95,
 'latency': float(r.get('p99_ms',0))<=100.0,
 'heartbeat': hb_ratio>=0.9999,
 'errors': r.get('disconnects')==0 and r.get('protocol_errors')==0 and r.get('server_errors')==0 and r.get('overload_rejections')==0,
 'resources': resource_ok,
}
print('TINYIMX_CAPSTONE_SOAK_RESULT connections=%s rate=%s duration_s=%s success=%s throughput=%s p99_ms=%s heartbeat_ratio=%.6f resource_deltas=%s checks=%s' % (n,rate,r.get('duration_s'),r.get('success_rate'),r.get('throughput_msg_s'),r.get('p99_ms'),hb_ratio,json.dumps(deltas,separators=(',',':')),','.join(k for k,v in checks.items() if not v) or 'all-pass'))
if not all(checks.values()): raise SystemExit(42)
PY

sha256sum "$ART"/* 2>/dev/null > "$ART/SHA256SUMS" || true
echo "TINYIMX_CAPSTONE_SOAK_GATE=PASS" | tee "$ART/PASS.marker"
echo "ARTIFACT_DIR=$ART"
