#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${1:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}"
ENV_FILE="$ROOT/deploy/production/.env"
IMAGE="${TINYIMX_FINAL_IMAGE:-tinyimx/runtime:m21-final}"
STATE="${TINYIMX_M21_STATE_DIR:-$HOME/.local/share/tinyimx/m21}"
BACKUP="$ENV_FILE.before-final-candidate"

[[ -f "$ENV_FILE" ]] || { echo "FIRST_FAILURE=PRODUCTION_ENV_MISSING path=$ENV_FILE"; exit 10; }
docker image inspect "$IMAGE" >/dev/null 2>&1 || { echo "FIRST_FAILURE=FINAL_IMAGE_MISSING image=$IMAGE"; exit 11; }

"$ROOT/scripts/tinyimx_final_preflight.sh" benchmark

if [[ ! -f "$BACKUP" ]]; then
  cp -a "$ENV_FILE" "$BACKUP"
fi
python3 - "$ENV_FILE" "$IMAGE" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); image=sys.argv[2]
lines=p.read_text().splitlines()
out=[]; found=False
for line in lines:
    if line.startswith('TINYIMX_RUNTIME_IMAGE='):
        out.append('TINYIMX_RUNTIME_IMAGE='+image); found=True
    else:
        out.append(line)
if not found: out.append('TINYIMX_RUNTIME_IMAGE='+image)
p.write_text('\n'.join(out)+'\n')
PY

export TINYIMX_M21_STATE_DIR="$STATE"
export TINYIMX_BUILD_JOBS=1
"$ROOT/scripts/m21_production_up.sh"

COMPOSE=(docker compose --env-file "$ENV_FILE" -f "$ROOT/deploy/production/docker-compose.yml")

for svc in mysql redis zookeeper user-service gateway-a gateway-b nginx; do
  ok=0
  for i in $(seq 1 90); do
    cid="$("${COMPOSE[@]}" ps -q "$svc")"
    if [[ -n "$cid" ]]; then
      status="$(docker inspect -f '{{.State.Status}}' "$cid")"
      health="$(docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' "$cid")"
      if [[ "$status" == "running" && ( "$health" == "healthy" || "$health" == "none" ) ]]; then ok=1; break; fi
    fi
    sleep 1
  done
  [[ "$ok" == 1 ]] || { echo "FIRST_FAILURE=SERVICE_HEALTH service=$svc"; "${COMPOSE[@]}" ps; exit 20; }
  echo "service=$svc status=PASS"
done

"$ROOT/scripts/tinyimx_final_schema_apply.sh"
TINYIMX_ALLOW_BENCH_FIXTURE=1 "$ROOT/scripts/tinyimx_final_prepare_benchmark_fixture.sh" 20000

EXPECTED_IMAGE_ID="$(docker image inspect -f '{{.Id}}' "$IMAGE")"
for svc in user-service gateway-a gateway-b; do
  cid="$("${COMPOSE[@]}" ps -q "$svc")"
  actual="$(docker inspect -f '{{.Image}}' "$cid")"
  echo "service=$svc image=$actual"
  [[ "$actual" == "$EXPECTED_IMAGE_ID" ]] || { echo "FIRST_FAILURE=IMAGE_IDENTITY service=$svc"; exit 30; }
done

python3 - "$STATE/config/gateway-a.json" <<'PY'
import json,sys
with open(sys.argv[1]) as f: d=json.load(f)
assert d['server']['backlog']==16384,d['server']['backlog']
print('gateway_backlog=16384')
PY

for svc in gateway-a gateway-b nginx; do
  cid="$("${COMPOSE[@]}" ps -q "$svc")"
  echo "--- $svc ulimit ---"
  docker exec "$cid" sh -c 'ulimit -n'
done

curl -fsS http://127.0.0.1:18081/healthz >/dev/null
nc -zvw3 127.0.0.1 9000

echo "TINYIMX_FINAL_LOCAL_DEPLOY=PASS"
echo "TARGET_HOST_HOSTONLY=${TINYIMX_FINAL_HOSTONLY_IP:-192.168.58.129}"
