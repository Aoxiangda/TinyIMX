#!/usr/bin/env bash
# Shared helpers for TinyIMX Final Capstone acceptance scripts.

# Enable strict mode only when executed from a non-interactive script.
# Do not poison an interactive shell if this helper is sourced manually.
if [[ $- != *i* ]]; then
  if [[ $- != *i* ]]; then
  if [[ $- != *i* ]]; then
  set -Eeuo pipefail
fi
fi
fi

TINYIMX_CAPSTONE_ROOT="${TINYIMX_CAPSTONE_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}"
export TINYIMX_M21_STATE_DIR="${TINYIMX_M21_STATE_DIR:-$HOME/.local/share/tinyimx/m21}"
export TINYIMX_RUNTIME_UID="${TINYIMX_RUNTIME_UID:-$(id -u)}"
export TINYIMX_RUNTIME_GID="${TINYIMX_RUNTIME_GID:-$(id -g)}"

TINYIMX_CAPSTONE_COMPOSE_BASE=(
  docker compose
  --env-file "$TINYIMX_CAPSTONE_ROOT/deploy/production/.env"
  -f "$TINYIMX_CAPSTONE_ROOT/deploy/production/docker-compose.yml"
)

capstone_compose() {
  "${TINYIMX_CAPSTONE_COMPOSE_BASE[@]}" "$@"
}

capstone_fail() {
  echo "FIRST_FAILURE=$1" >&2
  shift || true
  (($#)) && echo "$*" >&2
  exit 1
}

capstone_epoch_ms() {
  date +%s%3N
}

capstone_wait_healthy() {
  local service="$1" timeout_s="${2:-120}"
  local start now cid status health
  start="$(date +%s)"
  while true; do
    cid="$(capstone_compose ps -q "$service" 2>/dev/null || true)"
    if [[ -n "$cid" ]]; then
      status="$(docker inspect -f '{{.State.Status}}' "$cid" 2>/dev/null || true)"
      health="$(docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' "$cid" 2>/dev/null || true)"
      if [[ "$status" == running && ( "$health" == healthy || "$health" == none ) ]]; then
        return 0
      fi
    fi
    now="$(date +%s)"
    if (( now - start >= timeout_s )); then
      echo "service=$service status=${status:-missing} health=${health:-missing}" >&2
      return 1
    fi
    sleep 1
  done
}

capstone_container_env() {
  local container="$1" key="$2"
  docker inspect "$container" --format '{{range .Config.Env}}{{println .}}{{end}}' \
    | sed -n "s/^${key}=//p" | tail -1
}

capstone_mysql_container() { echo "${TINYIMX_MYSQL_CONTAINER:-tinyimx-m21-mysql-1}"; }
capstone_redis_container() { echo "${TINYIMX_REDIS_CONTAINER:-tinyimx-m21-redis-1}"; }

capstone_mysql_scalar() {
  local query="$1" container user password database
  container="$(capstone_mysql_container)"
  user="$(capstone_container_env "$container" MYSQL_USER)"
  password="$(capstone_container_env "$container" MYSQL_PASSWORD)"
  database="$(capstone_container_env "$container" MYSQL_DATABASE)"
  docker exec -e MYSQL_PWD="$password" "$container" \
    mysql -N -B -u"$user" "$database" -e "$query"
}

capstone_redis_password() {
  local container password
  container="$(capstone_redis_container)"

  password="$(capstone_container_env "$container" TINYIMX_REDIS_PASSWORD || true)"

  # Backward-compatible fallback only.
  if [[ -z "$password" ]]; then
    password="$(capstone_container_env "$container" REDIS_PASSWORD || true)"
  fi

  printf '%s\n' "$password"
}

capstone_redis() {
  local container password
  container="$(capstone_redis_container)"
  password="$(capstone_redis_password)"

  if [[ -z "$password" ]]; then
    capstone_fail REDIS_PASSWORD_DISCOVERY_FAILED       "container=$container expected_env=TINYIMX_REDIS_PASSWORD"
  fi

  docker exec "$container" \
    redis-cli --raw --no-auth-warning -a "$password" "$@"
}

capstone_reset_swap_if_needed() {
  local threshold_kib="${TINYIMX_CAPSTONE_SWAP_RESET_THRESHOLD_KIB:-65536}"
  local used_kib avail_kib required_kib
  used_kib="$(free -k | awk '/Swap:/ {print $3+0}')"
  (( used_kib <= threshold_kib )) && return 0

  avail_kib="$(awk '/MemAvailable:/ {print $2+0}' /proc/meminfo)"
  required_kib=$(( used_kib + 2097152 ))
  if (( avail_kib < required_kib )); then
    capstone_fail SWAP_RESET_NOT_ENOUGH_RAM "swap_used_kib=$used_kib mem_available_kib=$avail_kib required_kib=$required_kib"
  fi

  mapfile -t swap_devices < <(swapon --show --noheadings --output NAME)
  ((${#swap_devices[@]})) || return 0
  sudo -v
  for dev in "${swap_devices[@]}"; do sudo swapoff "$dev"; done
  for dev in "${swap_devices[@]}"; do sudo swapon "$dev"; done
  used_kib="$(free -k | awk '/Swap:/ {print $3+0}')"
  (( used_kib <= threshold_kib )) || capstone_fail SWAP_RESET_FAILED "swap_used_kib=$used_kib"
  echo "TINYIMX_CAPSTONE_SWAP_RESET=PASS swap_used_kib=$used_kib"
}

capstone_require_clean_benchmark_host() {
  ulimit -Sn 524288 2>/dev/null || true
  capstone_reset_swap_if_needed
  "$TINYIMX_CAPSTONE_ROOT/scripts/tinyimx_final_preflight.sh" benchmark
}

capstone_gateway_restart_policy() {
  local service="$1" cid
  cid="$(capstone_compose ps -q "$service")"
  docker inspect -f '{{.HostConfig.RestartPolicy.Name}}' "$cid"
}

capstone_gateway_container_name() {
  local service="$1" cid
  cid="$(capstone_compose ps -q "$service")"
  docker inspect -f '{{.Name}}' "$cid" | sed 's#^/##'
}

capstone_gateway_pid() {
  local service="$1" cid
  cid="$(capstone_compose ps -q "$service")"
  docker inspect -f '{{.State.Pid}}' "$cid"
}

capstone_ns_conn_count() {
  local service="$1" port="${2:-9000}" pid
  pid="$(capstone_gateway_pid "$service")"
  if [[ ! "$pid" =~ ^[1-9][0-9]*$ ]]; then
    echo -1
    return 0
  fi
  nsenter -t "$pid" -n ss -Htn state established "sport = :$port" 2>/dev/null | wc -l
}
