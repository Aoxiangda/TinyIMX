#!/usr/bin/env bash
set -Eeuo pipefail
[[ $# -ge 1 ]] || { echo "usage: $0 <prompt...>"; exit 2; }
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${ROOT}/deploy/production/.env"
COMPOSE="${ROOT}/deploy/production/docker-compose.yml"
set -a; source "$ENV_FILE"; set +a
export TINYIMX_M21_STATE_DIR="${TINYIMX_M21_STATE_DIR:-${HOME}/.local/share/tinyimx/m21}"
export TINYIMX_RUNTIME_UID="${TINYIMX_RUNTIME_UID:-$(id -u)}"
export TINYIMX_RUNTIME_GID="${TINYIMX_RUNTIME_GID:-$(id -g)}"
docker compose --env-file "$ENV_FILE" -f "$COMPOSE" --profile ai run --rm ai-agent "$*"
