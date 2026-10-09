#!/usr/bin/env bash
set -Eeuo pipefail
OUT="${1:?output file required}"; INTERVAL="${2:-1}"; mkdir -p "$(dirname "$OUT")"
CONTAINERS=(tinyimx-m21-nginx-1 tinyimx-m21-gateway-a-1 tinyimx-m21-gateway-b-1 tinyimx-m21-user-service-1 tinyimx-m21-social-service-1 tinyimx-m21-message-service-1 tinyimx-m21-group-service-1 tinyimx-m21-file-service-1 tinyimx-m21-mysql-1 tinyimx-m21-redis-1 tinyimx-m21-rocketmq-broker-1 tinyimx-m21-rocketmq-proxy-1)
while :; do
  { echo "SAMPLE_BEGIN epoch_ms=$(date +%s%3N)"; free -k|sed 's/^/HOST_FREE /'; ss -s|sed 's/^/HOST_SS /'; docker stats --no-stream --format 'DOCKER_STATS name={{.Name}} cpu={{.CPUPerc}} mem={{.MemUsage}} pids={{.PIDs}}' "${CONTAINERS[@]}" 2>/dev/null||true; for c in tinyimx-m21-nginx-1 tinyimx-m21-gateway-a-1 tinyimx-m21-gateway-b-1; do docker inspect "$c" >/dev/null 2>&1||continue; echo "PROCESS_FD name=$c fds=$(docker exec "$c" sh -c 'ls /proc/1/fd 2>/dev/null|wc -l' 2>/dev/null||echo -1)"; done; echo SAMPLE_END; } >> "$OUT"
  sleep "$INTERVAL"
done
