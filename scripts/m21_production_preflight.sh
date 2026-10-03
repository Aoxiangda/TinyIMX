#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_FILE="${ROOT}/deploy/production/.env"
STATE_DIR="${TINYIMX_M21_STATE_DIR:-${HOME}/.local/share/tinyimx/m21}"

cd "$ROOT"

test "$(git branch --show-current)" = "feature/m21-production-release-v1"

for cmd in docker python3 openssl curl nc; do
  command -v "$cmd" >/dev/null 2>&1 || {
    echo "ERROR: missing required command: $cmd"
    exit 10
  }
done

docker compose version >/dev/null

if ! docker info >/dev/null 2>&1; then
  echo "ERROR: Docker daemon/socket is not accessible for the current shell."
  echo "Your Docker installation may be correct, but group membership is not active."
  echo "Run: newgrp docker"
  echo "Then: docker info >/dev/null"
  exit 14
fi

echo "M21_DOCKER_DAEMON_ACCESS=PASS"

[[ -f "$ENV_FILE" ]] || {
  echo "ERROR: missing $ENV_FILE"
  echo "Copy deploy/production/.env.example to deploy/production/.env and replace placeholders."
  exit 11
}

set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a

for name in \
  TINYIMX_MYSQL_PASSWORD \
  TINYIMX_MYSQL_ROOT_PASSWORD \
  TINYIMX_REDIS_PASSWORD \
  TINYIMX_MCP_TOKEN
do
  value="${!name:-}"
  if [[ -z "$value" || "$value" == *CHANGE_ME* ]]; then
    echo "ERROR: unsafe placeholder/empty secret: $name"
    exit 12
  fi
done

FREE="$(df -B1 --output=avail / | tail -n1 | tr -d ' ')"
MIN_FREE=$((8 * 1024 * 1024 * 1024))

echo "FREE_BYTES=$FREE"

if (( FREE < MIN_FREE )); then
  echo "ERROR: less than 8 GiB free for Release build + Docker image pulls."
  echo "Run: scripts/m21_prepare_workspace.sh"
  exit 13
fi

mkdir -p "$STATE_DIR/config" "$STATE_DIR/tls" "$STATE_DIR/file-data"

echo "M21_PRODUCTION_PREFLIGHT=PASS"
echo "STATE_DIR=$STATE_DIR"
