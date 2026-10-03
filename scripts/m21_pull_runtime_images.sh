#!/usr/bin/env bash
set -Eeuo pipefail

IMAGES=(
  "mysql:8.0.40"
  "redis:7.4.11-alpine"
  "zookeeper:3.8.5"
  "apache/rocketmq:5.5.0"
  "otel/opentelemetry-collector-contrib:0.161.0"
  "prom/prometheus:v3.14.0"
  "nginx:1.31.6"
)

command -v docker >/dev/null 2>&1 || {
  echo "ERROR: docker not found"
  exit 10
}

docker info >/dev/null 2>&1 || {
  echo "ERROR: Docker daemon unavailable for current shell"
  exit 11
}

echo "========== M21 PRODUCTION IMAGE PULL GATE =========="

for image in "${IMAGES[@]}"; do
  echo "----- IMAGE ${image} -----"

  if docker image inspect "$image" >/dev/null 2>&1; then
    echo "M21_IMAGE_CACHE=PASS image=$image"
    continue
  fi

  if ! docker pull "$image"; then
    echo "ERROR: failed to pull required production image: $image"
    exit 20
  fi

  docker image inspect "$image" >/dev/null
  echo "M21_IMAGE_PULL=PASS image=$image"
done

echo "M21_PRODUCTION_IMAGE_PULL_GATE=PASS"
