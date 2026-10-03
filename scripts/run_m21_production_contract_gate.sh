#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$ROOT"
echo "========== M21 PRODUCTION CONTRACT PRECHECK =========="
test "$(git branch --show-current)" = "feature/m21-production-release-v1"
grep -Fq '#include "common/observability/ProcessTelemetry.h"' examples/gateway_demo.cpp
grep -Fq '"tinyimx-gateway"' examples/gateway_demo.cpp
grep -Fq 'gateway_demo' cmake/TinyIMXObservability.cmake
python3 -m py_compile scripts/m21_render_production_configs.py
export TINYIMX_MYSQL_PASSWORD=contract_mysql_password
export TINYIMX_MYSQL_ROOT_PASSWORD=contract_mysql_root_password
export TINYIMX_REDIS_PASSWORD=contract_redis_password
export TINYIMX_MCP_TOKEN=contract_mcp_token
python3 scripts/m21_render_production_configs.py --output-dir "$TMP/config"
for f in "$TMP"/config/*.json; do python3 -m json.tool "$f" >/dev/null; done
for service in mysql redis zookeeper rocketmq-namesrv rocketmq-broker rocketmq-proxy rocketmq-init otel-collector prometheus user-service social-service message-service group-service file-service outbox-relay unread-projector gateway-a gateway-b mcp-server nginx ai-agent; do
  grep -Eq "^  ${service}:" deploy/production/docker-compose.yml || { echo "ERROR: compose service missing: $service"; exit 20; }
done
grep -Fq 'apache/rocketmq:5.5.0' deploy/production/docker-compose.yml
grep -Fq 'otel/opentelemetry-collector-contrib:0.161.0' deploy/production/docker-compose.yml
grep -Fq 'prom/prometheus:v3.14.0' deploy/production/docker-compose.yml
grep -Fq 'nginx:1.31.6' deploy/production/docker-compose.yml
grep -Fq 'redis:7.4.11-alpine' deploy/production/docker-compose.yml
grep -Fq 'listen 9000;' deploy/production/nginx/nginx.conf
grep -Fq 'listen 9443 ssl;' deploy/production/nginx/nginx.conf
PROTECTED="gateway/GatewayPeerTransport.h"
test -z "$(git diff --cached --name-only)"
! git diff --cached --name-only | grep -Fx "$PROTECTED"
git diff --check
echo "PROTECTED_GATEWAY_DELTA=PASS"
echo "M21_PRODUCTION_CONTRACT_GATE=PASS"
