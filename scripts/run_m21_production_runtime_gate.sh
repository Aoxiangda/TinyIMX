#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${ROOT}/deploy/production/.env"
COMPOSE="${ROOT}/deploy/production/docker-compose.yml"
STATE_DIR="${TINYIMX_M21_STATE_DIR:-${HOME}/.local/share/tinyimx/m21}"
cd "$ROOT"
set -a; source "$ENV_FILE"; set +a
export TINYIMX_M21_STATE_DIR="$STATE_DIR"
export TINYIMX_RUNTIME_UID="${TINYIMX_RUNTIME_UID:-$(id -u)}"
export TINYIMX_RUNTIME_GID="${TINYIMX_RUNTIME_GID:-$(id -g)}"
"$ROOT/scripts/run_m21_production_contract_gate.sh"
"$ROOT/scripts/m21_production_up.sh"
compose() { docker compose --env-file "$ENV_FILE" -f "$COMPOSE" "$@"; }
wait_state() {
  local service="$1" timeout_sec="${2:-240}" start now cid status
  start="$(date +%s)"
  while true; do
    cid="$(compose ps -q "$service")"
    if [[ -n "$cid" ]]; then
      status="$(docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "$cid" 2>/dev/null || true)"
      if [[ "$status" == healthy || "$status" == running ]]; then echo "M21_SERVICE_READY=PASS service=$service status=$status"; return 0; fi
      if [[ "$status" == unhealthy || "$status" == exited || "$status" == dead ]]; then echo "ERROR: service=$service status=$status"; compose logs --no-color --tail=120 "$service" || true; return 1; fi
    fi
    now="$(date +%s)"
    if (( now - start >= timeout_sec )); then echo "ERROR: timeout service=$service"; compose ps; compose logs --no-color --tail=120 "$service" || true; return 1; fi
    sleep 2
  done
}
for svc in mysql redis zookeeper otel-collector prometheus user-service social-service group-service file-service message-service gateway-a gateway-b mcp-server nginx; do wait_state "$svc" 240; done
cid="$(compose ps -aq rocketmq-init)"; test -n "$cid"; test "$(docker inspect -f '{{.State.ExitCode}}' "$cid")" = 0
echo "M21_ROCKETMQ_TOPIC_INIT=PASS"
compose exec -T gateway-a nc -z rocketmq-proxy 8081
echo "M21_ROCKETMQ_PROXY=PASS"
curl -fsS "http://127.0.0.1:${TINYIMX_NGINX_HEALTH_PORT:-18081}/healthz" | grep -qx ok
echo "M21_NGINX_HEALTH=PASS"
nc -z 127.0.0.1 "${TINYIMX_GATEWAY_PLAIN_PORT:-9000}"
echo "M21_GATEWAY_PLAIN_TCP=PASS"
openssl s_client -connect "127.0.0.1:${TINYIMX_GATEWAY_TLS_PORT:-9443}" -servername tinyimx.local -CAfile "$STATE_DIR/tls/tinyimx.crt" -verify_return_error </dev/null >/dev/null 2>&1
echo "M21_GATEWAY_TLS=PASS"
curl -fsS "http://127.0.0.1:${TINYIMX_MCP_HOST_PORT:-18080}/health" >/dev/null
echo "M21_MCP_HEALTH=PASS"
curl -fsS "http://127.0.0.1:${TINYIMX_PROMETHEUS_HOST_PORT:-19090}/-/ready" >/dev/null
echo "M21_PROMETHEUS_READY=PASS"
curl -fsS "http://127.0.0.1:${TINYIMX_COLLECTOR_HEALTH_PORT:-13133}/" >/dev/null
echo "M21_COLLECTOR_HEALTH=PASS"
compose exec -T mysql mysql -u"${TINYIMX_MYSQL_USER:-tinyimx}" -p"${TINYIMX_MYSQL_PASSWORD}" -D"${TINYIMX_MYSQL_DATABASE:-tinyimx}" -Nse "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='${TINYIMX_MYSQL_DATABASE:-tinyimx}'" | awk '$1 > 0 {ok=1} END {exit ok?0:1}'
echo "M21_MYSQL_SCHEMA=PASS"
compose exec -T redis redis-cli -a "${TINYIMX_REDIS_PASSWORD}" ping | grep -q PONG
echo "M21_REDIS_READY=PASS"
compose exec -T zookeeper zkCli.sh -server 127.0.0.1:2181 ls /tinyimx/services >"$STATE_DIR/zookeeper-services.txt" 2>&1
for service in user social message group file; do grep -q "$service" "$STATE_DIR/zookeeper-services.txt" || { echo "ERROR: ZooKeeper missing service=$service"; cat "$STATE_DIR/zookeeper-services.txt"; exit 30; }; done
echo "M21_ZOOKEEPER_SERVICE_REGISTRY=PASS"
PROM_JSON="$(curl -fsSG "http://127.0.0.1:${TINYIMX_PROMETHEUS_HOST_PORT:-19090}/api/v1/query" --data-urlencode 'query=up{job="tinyimx-otel-collector"}')"
python3 - "$PROM_JSON" <<'PY'
import json,sys
x=json.loads(sys.argv[1])
assert x['status']=='success',x
r=x['data']['result']
assert r and float(r[0]['value'][1])==1.0,r
PY
echo "M21_PROMETHEUS_COLLECTOR_QUERY=PASS"
PROTECTED="gateway/GatewayPeerTransport.h"
test -z "$(git diff --cached --name-only)"
! git diff --cached --name-only | grep -Fx "$PROTECTED"
echo "PROTECTED_GATEWAY_DELTA=PASS"
echo

echo "========== M21 ROCKETMQ STABILITY =========="
"$ROOT/scripts/m21_verify_rocketmq_stability.sh"

echo "M21_PRODUCTION_RUNTIME_GATE=PASS"
echo "NOTE: load/fault/soak/perf/final-release acceptance is the second and final M21 batch."
