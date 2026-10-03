#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
COMPOSE="${ROOT}/deploy/production/docker-compose.yml"
ENV_FILE="${ROOT}/deploy/production/.env"
STATE_DIR="${TINYIMX_M21_STATE_DIR:-${HOME}/.local/share/tinyimx/m21}"

cd "$ROOT"

set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a

export TINYIMX_M21_STATE_DIR="$STATE_DIR"
export TINYIMX_RUNTIME_UID="${TINYIMX_RUNTIME_UID:-$(id -u)}"
export TINYIMX_RUNTIME_GID="${TINYIMX_RUNTIME_GID:-$(id -g)}"

"$ROOT/scripts/m21_production_preflight.sh"

python3 "$ROOT/scripts/m21_render_production_configs.py" \
  --output-dir "$STATE_DIR/config"

if [[ ! -s "$STATE_DIR/tls/tinyimx.crt" || ! -s "$STATE_DIR/tls/tinyimx.key" ]]; then
  "$ROOT/scripts/m21_generate_dev_tls.sh"
fi

if ! docker image inspect "${TINYIMX_RUNTIME_IMAGE:-tinyimx/runtime:m21}" >/dev/null 2>&1; then
  "$ROOT/scripts/m21_build_runtime_image.sh"
fi

"$ROOT/scripts/m21_pull_runtime_images.sh"

"$ROOT/scripts/m21_prepare_rocketmq_volumes.sh"

docker compose \
  --env-file "$ENV_FILE" \
  -f "$COMPOSE" \
  up -d --remove-orphans

echo "M21_PRODUCTION_UP=PASS"
