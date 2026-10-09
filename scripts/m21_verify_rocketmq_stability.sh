#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${ROOT}/deploy/production/.env"
COMPOSE_FILE="${ROOT}/deploy/production/docker-compose.yml"
SECONDS_TO_CHECK="${TINYIMX_M21_RMQ_STABILITY_SECONDS:-60}"

cd "$ROOT"

set -a
source "$ENV_FILE"
set +a

export TINYIMX_M21_STATE_DIR="${TINYIMX_M21_STATE_DIR:-${HOME}/.local/share/tinyimx/m21}"
export TINYIMX_RUNTIME_UID="${TINYIMX_RUNTIME_UID:-$(id -u)}"
export TINYIMX_RUNTIME_GID="${TINYIMX_RUNTIME_GID:-$(id -g)}"

COMPOSE=(
  docker compose
  --env-file "$ENV_FILE"
  -f "$COMPOSE_FILE"
)

echo "========== M21 ROCKETMQ STABILITY GATE =========="

BROKER_CID="$("${COMPOSE[@]}" ps -q rocketmq-broker)"
PROXY_CID="$("${COMPOSE[@]}" ps -q rocketmq-proxy)"

test -n "$BROKER_CID" || {
  echo "ERROR: rocketmq-broker container not found"
  exit 20
}

test -n "$PROXY_CID" || {
  echo "ERROR: rocketmq-proxy container not found"
  exit 21
}

assert_running() {
  local name="$1"
  local cid="$2"
  local status exit_code oom restart

  status="$(docker inspect -f '{{.State.Status}}' "$cid")"
  exit_code="$(docker inspect -f '{{.State.ExitCode}}' "$cid")"
  oom="$(docker inspect -f '{{.State.OOMKilled}}' "$cid")"
  restart="$(docker inspect -f '{{.RestartCount}}' "$cid")"

  echo "$name status=$status exit=$exit_code oom=$oom restart=$restart"

  test "$status" = "running" || {
    echo "ERROR: $name is not running"
    exit 22
  }

  test "$oom" = "false" || {
    echo "ERROR: $name was OOM-killed"
    exit 23
  }
}

assert_running broker "$BROKER_CID"
assert_running proxy "$PROXY_CID"

B1="$(docker inspect -f '{{.RestartCount}}' "$BROKER_CID")"
P1="$(docker inspect -f '{{.RestartCount}}' "$PROXY_CID")"

echo "broker restart_before=$B1"
echo "proxy restart_before=$P1"
echo "stability_window_seconds=$SECONDS_TO_CHECK"

sleep "$SECONDS_TO_CHECK"

BROKER_CID_AFTER="$("${COMPOSE[@]}" ps -q rocketmq-broker)"
PROXY_CID_AFTER="$("${COMPOSE[@]}" ps -q rocketmq-proxy)"

test "$BROKER_CID" = "$BROKER_CID_AFTER" || {
  echo "ERROR: rocketmq-broker container identity changed"
  exit 24
}

test "$PROXY_CID" = "$PROXY_CID_AFTER" || {
  echo "ERROR: rocketmq-proxy container identity changed"
  exit 25
}

assert_running broker "$BROKER_CID"
assert_running proxy "$PROXY_CID"

B2="$(docker inspect -f '{{.RestartCount}}' "$BROKER_CID")"
P2="$(docker inspect -f '{{.RestartCount}}' "$PROXY_CID")"

echo "broker restart_after=$B2"
echo "proxy restart_after=$P2"

test "$B1" = "$B2" || {
  echo "ERROR: rocketmq-broker restarted during stability window"
  exit 26
}

test "$P1" = "$P2" || {
  echo "ERROR: rocketmq-proxy restarted during stability window"
  exit 27
}

echo "M21_ROCKETMQ_STABILITY=PASS seconds=$SECONDS_TO_CHECK"
