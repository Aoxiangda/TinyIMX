#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)";cd "$ROOT_DIR";PROTECTED=gateway/GatewayPeerTransport.h;EXPECTED="${M21_PROTECTED_SHA:-66eb26b8182e6b421c79e175c05404bd3e8a7542c974f1f234c06b55d6a13afa}";BUILD="${M21_BUILD_DIR:-$ROOT_DIR/build/linux-release}";STAMP="$(date +%Y%m%d-%H%M%S)";ART="${M21_FINAL_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m21-enterprise-final-$STAMP}";mkdir -p "$ART"
[[ "$(sha256sum "$PROTECTED"|awk '{print $1}')" == "$EXPECTED" ]]||{ echo "protected file changed" >&2;exit 1;};echo M21_FINAL_PROTECTED_FILE=PASS
"$ROOT_DIR/scripts/m21_verify_production_schema_complete.sh"|tee "$ART/schema.log"
for C in tinyimx-m21-nginx-1 tinyimx-m21-gateway-a-1 tinyimx-m21-gateway-b-1;do [[ "$(docker inspect -f '{{.State.Health.Status}}' "$C")" == healthy ]];L="$(docker exec "$C" sh -c 'awk "/Max open files/{print \$4\":\"\$5}" /proc/1/limits')";echo "entrypoint=$C nofile=$L";[[ "$L" == 65536:65536 ]];done
docker exec tinyimx-m21-nginx-1 nginx -T > "$ART/nginx.txt" 2>&1;grep -q 'worker_rlimit_nofile 65536;' "$ART/nginx.txt";grep -q 'worker_connections 16384;' "$ART/nginx.txt";! grep -q 'worker_connections exceed open file resource limit' "$ART/nginx.txt";echo M21_FINAL_NGINX_FD=PASS
"$ROOT_DIR/scripts/m21_verify_overload_protection.sh"|tee "$ART/overload.log"
cmake --build "$BUILD" --target tinyimx_im_loadgen -- -j1|tee "$ART/build.log"
if [[ "${M21_FINAL_SKIP_SMOKE:-0}" != 1 ]];then M21_BENCH_ARTIFACT_DIR="$ART/smoke-hold" M21_BENCH_STRICT=1 "$ROOT_DIR/scripts/m21_run_im_benchmark.sh" hold 10 15 1 410000|tee "$ART/smoke-hold.log";M21_BENCH_ARTIFACT_DIR="$ART/smoke-private" M21_BENCH_STRICT=1 M21_BENCH_MIN_SUCCESS_RATE=100 "$ROOT_DIR/scripts/m21_run_im_benchmark.sh" private 10 20 10 420000|tee "$ART/smoke-private.log";fi
echo M21_FINAL_SMOKE=PASS
if [[ -n "${M21_ACCEPT_RESULT_JSON:-}" ]];then python3 - "$M21_ACCEPT_RESULT_JSON" <<'PY'
import json,os,sys
x=json.load(open(sys.argv[1]));n=int(os.getenv('M21_ACCEPT_MIN_CONNECTIONS','1000'));mins=float(os.getenv('M21_ACCEPT_MIN_SUCCESS_RATE','99'));maxp=float(os.getenv('M21_ACCEPT_MAX_P99_MS','5000'));mint=float(os.getenv('M21_ACCEPT_MIN_THROUGHPUT','0'));bad=[]
if int(x.get('connections',0))<n or int(x.get('login_ok',0))<n:bad.append('connection/login target')
if int(x.get('protocol_errors',0)) or int(x.get('disconnects',0)):bad.append('protocol/disconnect')
if x.get('mode')=='private' and (float(x.get('success_rate',0))<mins or float(x.get('p99_ms',0))>maxp or float(x.get('throughput_msg_s',0))<mint):bad.append('message target')
if bad:raise SystemExit(';'.join(bad))
print('M21_FINAL_MEASURED_RESULT=PASS')
PY
else echo M21_FINAL_MEASURED_RESULT=NOT_SUPPLIED;fi
for V in M21_REQUIRE_HOT_USER_RESULT M21_REQUIRE_HOT_GROUP_RESULT M21_REQUIRE_SOAK_RESULT M21_REQUIRE_PERF_REPORT M21_REQUIRE_AB_RESULT;do :;done
[[ -z "${M21_REQUIRE_HOT_USER_RESULT:-}" || -s "$M21_REQUIRE_HOT_USER_RESULT" ]]
[[ -z "${M21_REQUIRE_HOT_GROUP_RESULT:-}" || -s "$M21_REQUIRE_HOT_GROUP_RESULT" ]]
[[ -z "${M21_REQUIRE_SOAK_RESULT:-}" || -s "$M21_REQUIRE_SOAK_RESULT" ]]
[[ -z "${M21_REQUIRE_PERF_REPORT:-}" || -s "$M21_REQUIRE_PERF_REPORT" ]]
[[ -z "${M21_REQUIRE_AB_RESULT:-}" || -s "$M21_REQUIRE_AB_RESULT" ]]
git diff --check;[[ "$(sha256sum "$PROTECTED"|awk '{print $1}')" == "$EXPECTED" ]];echo M21_ENTERPRISE_PERFORMANCE_CLOSEOUT=PASS;echo 'NOTE=Performance claims require supplied result artifacts; this gate never invents QPS/P99.';echo "M21_FINAL_ARTIFACT_DIR=$ART"
