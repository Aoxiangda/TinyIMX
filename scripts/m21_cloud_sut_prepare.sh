#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)";cd "$ROOT_DIR";COUNT="${1:-10000}";BASE="${2:-500000}"
for C in tinyimx-m21-nginx-1 tinyimx-m21-gateway-a-1 tinyimx-m21-gateway-b-1;do H="$(docker inspect -f '{{.State.Health.Status}}' "$C")";[[ "$H" == healthy ]]||{ echo "$C health=$H" >&2;exit 1;};L="$(docker exec "$C" sh -c 'awk "/Max open files/{print \$4\":\"\$5}" /proc/1/limits')";[[ "$L" == 65536:65536 ]]||{ echo "$C nofile=$L" >&2;exit 1;};done
docker exec tinyimx-m21-nginx-1 nginx -T 2>&1|grep -q 'worker_connections 16384;'
"$ROOT_DIR/scripts/m21_verify_production_schema_complete.sh"
M21_BENCH_USER_BASE="$BASE" "$ROOT_DIR/scripts/m21_prepare_benchmark_users.sh" "$COUNT"
echo "M21_CLOUD_SUT_PREPARE=PASS users=$COUNT base=$BASE"
echo "NEXT=start scripts/m21_resource_sampler.sh on SUT, then run m21_run_remote_loadgen.sh from a separate load-generator host"
