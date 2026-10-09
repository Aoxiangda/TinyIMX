#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="${1:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}"
ENV_FILE="$ROOT/deploy/production/.env"
OUT="${TINYIMX_FINAL_EVIDENCE_DIR:-$HOME/tinyimx-final-evidence/$(date +%Y%m%d-%H%M%S)}"
DRAIN="${TINYIMX_FINAL_DRAIN:-0}"
mkdir -p "$OUT"

[[ -f "$ENV_FILE" ]] || { echo "FIRST_FAILURE=PRODUCTION_ENV_MISSING"; exit 10; }
COMPOSE=(docker compose --env-file "$ENV_FILE" -f "$ROOT/deploy/production/docker-compose.yml")

date --iso-8601=seconds > "$OUT/collected-at.txt"
uname -a > "$OUT/uname.txt"
free -h > "$OUT/free.txt"
df -h / > "$OUT/df-root.txt"
swapon --show > "$OUT/swapon.txt"
ss -s > "$OUT/ss-summary.txt"
sysctl net.core.somaxconn net.ipv4.tcp_max_syn_backlog > "$OUT/sysctl.txt"
"${COMPOSE[@]}" ps > "$OUT/compose-ps.txt"
docker stats --no-stream > "$OUT/docker-stats.txt"

for svc in nginx gateway-a gateway-b user-service social-service message-service outbox-relay mysql redis; do
  cid="$("${COMPOSE[@]}" ps -q "$svc")"
  [[ -n "$cid" ]] || continue
  docker inspect "$cid" > "$OUT/${svc}-inspect.json"
  docker top "$cid" -eo pid,ppid,args > "$OUT/${svc}-top.txt" || true
done

NGINX_CID="$("${COMPOSE[@]}" ps -q nginx)"
if [[ -n "$NGINX_CID" ]]; then
  NGINX_PID="$(docker inspect -f '{{.State.Pid}}' "$NGINX_CID")"
  nsenter -t "$NGINX_PID" -n ss -s > "$OUT/nginx-netns-ss-summary.txt" || true
  nsenter -t "$NGINX_PID" -n ss -Htan state established '( sport = :9000 )' > "$OUT/nginx-established-9000.txt" || true
fi

for svc in gateway-a gateway-b user-service social-service message-service; do
  topfile="$OUT/${svc}-top.txt"
  [[ -f "$topfile" ]] || continue
  case "$svc" in
    user-service) exe='/opt/tinyimx/bin/user_service_demo' ;;
    social-service) exe='/opt/tinyimx/bin/social_service_demo' ;;
    message-service) exe='/opt/tinyimx/bin/message_service_demo' ;;
    *) exe='/opt/tinyimx/bin/gateway_demo' ;;
  esac
  pid="$(awk -v x="$exe" '$3==x {print $1; exit}' "$topfile")"
  if [[ "$pid" =~ ^[0-9]+$ && -d "/proc/$pid/fd" ]]; then
    echo "pid=$pid fd_count=$(find "/proc/$pid/fd" -mindepth 1 -maxdepth 1 2>/dev/null | wc -l)" > "$OUT/${svc}-fd.txt"
    ps -L -p "$pid" -o pid,tid,psr,pcpu,stat,wchan:28,comm > "$OUT/${svc}-threads.txt" || true
  fi
done

curl -fsS 'http://127.0.0.1:19090/api/v1/label/__name__/values' > "$OUT/prometheus-metric-names.json" 2>/dev/null || true
if [[ -s "$OUT/prometheus-metric-names.json" ]]; then
  python3 - "$OUT/prometheus-metric-names.json" > "$OUT/tinyimx-metric-names.txt" <<'PY'
import json,sys
try:
 d=json.load(open(sys.argv[1]))
 for x in d.get('data',[]):
  if 'tinyimx' in x.lower(): print(x)
except Exception: pass
PY
fi

if [[ "$DRAIN" == "1" ]]; then
  START="$(date -d '10 minutes ago' --iso-8601=seconds)"
  for svc in gateway-a gateway-b; do
    "${COMPOSE[@]}" restart "$svc"
    for i in $(seq 1 60); do
      cid="$("${COMPOSE[@]}" ps -q "$svc")"
      status="$(docker inspect -f '{{.State.Status}}' "$cid")"
      health="$(docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' "$cid")"
      [[ "$status" == running && "$health" == healthy ]] && break
      sleep 1
    done
    "${COMPOSE[@]}" logs --no-color --since "$START" "$svc" > "$OUT/${svc}-drain.log" 2>&1 || true
  done
  {
    grep 'gateway message runtime drained' "$OUT"/gateway-*-drain.log || true
    grep 'gateway business runtime drained' "$OUT"/gateway-*-drain.log || true
    grep 'gateway replay runtime drained' "$OUT"/gateway-*-drain.log || true
  } > "$OUT/executor-drain-summary.txt"

  if grep -Eq 'accepted_unaccounted=[1-9][0-9]*' "$OUT/executor-drain-summary.txt"; then
    cat "$OUT/executor-drain-summary.txt"
    echo "FIRST_FAILURE=ACCEPTED_UNACCOUNTED"
    exit 30
  fi
fi

(
  cd "$OUT"
  find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS
)

echo "TINYIMX_FINAL_SUT_EVIDENCE=PASS"
echo "EVIDENCE_DIR=$OUT"
