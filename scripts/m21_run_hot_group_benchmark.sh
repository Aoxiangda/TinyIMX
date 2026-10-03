#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT_DIR"
MEMBERS="${1:-100}"; MESSAGES="${2:-5}"; HOLD_SECONDS="${3:-45}"; BASE="${4:-360000}"
((MEMBERS>=2 && MEMBERS<=1000)) || { echo "members must be 2..1000 for local gate" >&2; exit 64; }
((MESSAGES>=1 && MESSAGES<=100)) || exit 64
BUILD_DIR="${M21_BUILD_DIR:-$ROOT_DIR/build/linux-release}"; PREFIX="${M21_BENCH_USERNAME_PREFIX:-m21b${BASE}_}"; MYSQL_CONTAINER="${M21_MYSQL_CONTAINER:-tinyimx-m21-mysql-1}"; STAMP="$(date +%Y%m%d-%H%M%S)"; ART="${M21_HOT_GROUP_ARTIFACT_DIR:-$ROOT_DIR/artifacts/m21-hot-group-$STAMP}"; mkdir -p "$ART"
M21_BENCH_USER_BASE="$BASE" M21_BENCH_USERNAME_PREFIX="$PREFIX" "$ROOT_DIR/scripts/m21_prepare_benchmark_users.sh" "$MEMBERS"|tee "$ART/fixture.log"
cmake --build "$BUILD_DIR" --target tinyimx_im_loadgen gateway_group_message_client_demo -- -j1|tee "$ART/build.log"
MYSQL_USER="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}'|sed -n 's/^MYSQL_USER=//p')"; MYSQL_PASSWORD="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}'|sed -n 's/^MYSQL_PASSWORD=//p')"; MYSQL_DATABASE="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}'|sed -n 's/^MYSQL_DATABASE=//p')"
q(){ docker exec -e MYSQL_PWD="$MYSQL_PASSWORD" "$MYSQL_CONTAINER" mysql -N -B -u"$MYSQL_USER" "$MYSQL_DATABASE" -e "$1"; }
NAME="m21-hot-group-${STAMP}-$$"; MAX=$((MEMBERS+10)); ((MAX>5000))&&MAX=5000
q "INSERT INTO im_groups(name,description,avatar_url,owner_user_id,status,join_policy,max_members,version,member_version) VALUES('${NAME}','M21 hotspot fanout','','10001',1,1,${MAX},1,$((MEMBERS+1)));"
GROUP_ID="$(q "SELECT group_id FROM im_groups WHERE name='${NAME}' ORDER BY group_id DESC LIMIT 1;")"; [[ "$GROUP_ID" =~ ^[1-9][0-9]*$ ]]
TMP="$(mktemp /tmp/tinyimx-group-members-XXXX.sql)"; trap 'rm -f "$TMP"; [[ -n "${LOAD_PID:-}" ]] && kill "$LOAD_PID" 2>/dev/null || true; [[ -n "${SAMPLE_PID:-}" ]] && kill "$SAMPLE_PID" 2>/dev/null || true' EXIT
python3 - "$TMP" "$GROUP_ID" "$MEMBERS" "$BASE" <<'PY'
from pathlib import Path
import sys
a=Path(sys.argv[1]);g=int(sys.argv[2]);n=int(sys.argv[3]);base=int(sys.argv[4]);rows=[f"({g},10001,1,1,1)"]+[f"({g},{base+i},3,1,1)" for i in range(1,n+1)];a.write_text("INSERT INTO im_group_members(group_id,user_id,role,status,membership_epoch) VALUES\n"+",\n".join(rows)+"\nON DUPLICATE KEY UPDATE role=VALUES(role),status=1,membership_epoch=1;\n")
PY
docker exec -i -e MYSQL_PWD="$MYSQL_PASSWORD" "$MYSQL_CONTAINER" mysql -u"$MYSQL_USER" "$MYSQL_DATABASE" < "$TMP"
[[ "$(q "SELECT COUNT(*) FROM im_group_members WHERE group_id=${GROUP_ID} AND status=1;")" == "$((MEMBERS+1))" ]]
ulimit -Sn 65535
"$ROOT_DIR/scripts/m21_resource_sampler.sh" "$ART/resources.log" 1 & SAMPLE_PID=$!
"$BUILD_DIR/tinyimx_im_loadgen" --host 127.0.0.1 --port 9000 --connections "$MEMBERS" --user-id-base "$BASE" --username-prefix "$PREFIX" --password 123456 --mode hold --peer-mode ring --duration "$HOLD_SECONDS" --ramp-per-sec "${M21_GROUP_RAMP_PER_SEC:-100}" --heartbeat-seconds 30 --drain-seconds 5 > "$ART/loadgen.log" 2>&1 & LOAD_PID=$!
READY=0
for i in $(seq 1 180); do grep -q '^TINYIMX_LOADGEN_READY ' "$ART/loadgen.log" 2>/dev/null && { READY=1; break; }; kill -0 "$LOAD_PID" 2>/dev/null || break; sleep 1; done
[[ "$READY" == 1 ]] || { cat "$ART/loadgen.log"; exit 1; }
: > "$ART/fanout-latency-ms.txt"
for i in $(seq 1 "$MESSAGES"); do
  CID="m21-hot-group-${STAMP}-${i}"; BODY="$(printf '{\"group_id\":%s,\"client_message_id\":\"%s\",\"message_type\":1,\"content\":\"hot-group-%s\"}' "$GROUP_ID" "$CID" "$i")"; START="$(date +%s%3N)"; OUT="$(timeout 15s "$BUILD_DIR/gateway_group_message_client_demo" 127.0.0.1 9000 user10001 123456 "$BODY")"; echo "$OUT"|tee "$ART/send-$i.json"; MID="$(python3 -c 'import json,sys;o=json.loads(sys.stdin.read());assert o.get("success") is True;print(o["message_id"])' <<<"$OUT")"; DONE=0
  for x in $(seq 1 200); do C="$(q "SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id=${MID} AND delivery_status=3;")"; if [[ "$C" == "$MEMBERS" ]]; then DONE=1; break; fi; sleep 0.05; done
  [[ "$DONE" == 1 ]] || { echo "fanout incomplete message_id=$MID delivered=$C expected=$MEMBERS" >&2; exit 1; }
  END="$(date +%s%3N)"; echo $((END-START)) >> "$ART/fanout-latency-ms.txt"
done
wait "$LOAD_PID"; LOAD_PID=""; kill "$SAMPLE_PID" 2>/dev/null||true; wait "$SAMPLE_PID" 2>/dev/null||true; SAMPLE_PID=""
LINE="$(grep '^TINYIMX_LOADGEN_RESULT ' "$ART/loadgen.log"|tail -1)"; [[ -n "$LINE" ]]; printf '%s\n' "${LINE#TINYIMX_LOADGEN_RESULT }" > "$ART/loadgen-result.json"
python3 - "$ART/loadgen-result.json" "$ART/fanout-latency-ms.txt" "$MEMBERS" "$MESSAGES" <<'PY'
import json,math,statistics,sys
x=json.load(open(sys.argv[1]));lat=sorted(int(v.strip()) for v in open(sys.argv[2]) if v.strip());n=int(sys.argv[3]);m=int(sys.argv[4]);exp=n*m
if x.get('protocol_errors')!=0 or x.get('disconnects')!=0: raise SystemExit(f"loadgen errors protocol={x.get('protocol_errors')} disconnects={x.get('disconnects')}")
if int(x.get('group_delivery',0))<exp or int(x.get('group_ack_sent',0))<exp: raise SystemExit(f"group acks incomplete delivery={x.get('group_delivery')} ack={x.get('group_ack_sent')} expected={exp}")
def p(q): return lat[max(0,min(len(lat)-1,math.ceil(q*len(lat))-1))]
total_ms=sum(lat); delivery_ops_s=(n*m)/(total_ms/1000.0) if total_ms else 0; print(f"M21_HOT_GROUP_RESULT members={n} messages={m} deliveries={x['group_delivery']} ack_sent={x['group_ack_sent']} submit_to_all_delivered_p50_ms={p(.5)} submit_to_all_delivered_p95_ms={p(.95)} submit_to_all_delivered_p99_ms={p(.99)} submit_to_all_delivered_max_ms={max(lat)} effective_delivery_ops_s={delivery_ops_s:.3f}")
PY
echo "M21_HOT_GROUP_BENCHMARK=PASS group_id=$GROUP_ID"
echo "M21_HOT_GROUP_ARTIFACT_DIR=$ART"
