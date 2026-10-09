#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${ROOT}/deploy/production/.env"
COMPOSE_FILE="${ROOT}/deploy/production/docker-compose.yml"

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

echo "========== M21 ROCKETMQ VOLUME OWNERSHIP GATE =========="

"${COMPOSE[@]}" run   --rm   --no-deps   --user 0:0   --entrypoint sh   rocketmq-broker   -lc '
    set -eu
    uid="$(id -u rocketmq)"
    gid="$(id -g rocketmq)"

    install -d -o "$uid" -g "$gid" -m 0755       /home/rocketmq/logs       /home/rocketmq/store

    chown -R "$uid:$gid"       /home/rocketmq/logs       /home/rocketmq/store

    chmod 0755       /home/rocketmq/logs       /home/rocketmq/store

    test "$(stat -c %u /home/rocketmq/logs)" = "$uid"
    test "$(stat -c %g /home/rocketmq/logs)" = "$gid"
    test "$(stat -c %u /home/rocketmq/store)" = "$uid"
    test "$(stat -c %g /home/rocketmq/store)" = "$gid"

    echo "ROCKETMQ_UID=$uid"
    echo "ROCKETMQ_GID=$gid"
    stat -c "%u:%g %a %n"       /home/rocketmq/logs       /home/rocketmq/store
  '

echo "M21_ROCKETMQ_VOLUME_OWNERSHIP=PASS"
