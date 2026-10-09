#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
STATE_DIR="${TINYIMX_M21_STATE_DIR:-${HOME}/.local/share/tinyimx/m21}"
BUNDLE="${STATE_DIR}/bundle"
CONTEXT="${STATE_DIR}/image-context"
IMAGE="${TINYIMX_RUNTIME_IMAGE:-tinyimx/runtime:m21}"

command -v docker >/dev/null 2>&1 || {
  echo "ERROR: docker not found"
  exit 10
}

if ! docker info >/dev/null 2>&1; then
  echo "ERROR: Docker CLI is installed but the current shell cannot access the daemon."
  echo "Run exactly:"
  echo "  newgrp docker"
  echo "Then verify:"
  echo "  docker info >/dev/null && echo M21_DOCKER_DAEMON_ACCESS=PASS"
  exit 11
fi

echo "M21_DOCKER_DAEMON_ACCESS=PASS"

# Always run the bundle gate. It is incremental and guarantees RocketMQ
# targets cannot be silently omitted from an old Release CMake cache.
"$ROOT/scripts/m21_build_runtime_bundle.sh"

rm -rf "$CONTEXT"
mkdir -p "$CONTEXT/bundle"

cp -a "$BUNDLE/." "$CONTEXT/bundle/"
cp "$ROOT/deploy/production/Dockerfile.runtime" "$CONTEXT/Dockerfile"

docker build --pull -t "$IMAGE" "$CONTEXT"
docker image inspect "$IMAGE" >/dev/null

echo "M21_RUNTIME_IMAGE_BUILD=PASS"
echo "RUNTIME_IMAGE=$IMAGE"
