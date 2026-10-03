#!/usr/bin/env bash
set -Eeuo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${ROOT}/deploy/production/.env"
COMPOSE="${ROOT}/deploy/production/docker-compose.yml"
test -f "$ENV_FILE"
set -a; source "$ENV_FILE"; set +a
export TINYIMX_M21_STATE_DIR="${TINYIMX_M21_STATE_DIR:-${HOME}/.local/share/tinyimx/m21}"
export TINYIMX_RUNTIME_UID="${TINYIMX_RUNTIME_UID:-$(id -u)}"
export TINYIMX_RUNTIME_GID="${TINYIMX_RUNTIME_GID:-$(id -g)}"
docker compose --env-file "$ENV_FILE" -f "$COMPOSE" down
echo "M21_PRODUCTION_DOWN=PASS"
