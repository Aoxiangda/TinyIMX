#!/usr/bin/env bash

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_USER="${MYSQL_USER:-root}"
MYSQL_PASSWORD="${MYSQL_PASSWORD:-}"

MYSQL_ARGS=(
    -h "${MYSQL_HOST}"
    -P "${MYSQL_PORT}"
    -u "${MYSQL_USER}"
)

if [[ -n "${MYSQL_PASSWORD}" ]]; then
    MYSQL_ARGS+=("-p${MYSQL_PASSWORD}")
fi

cd "${PROJECT_ROOT}"

echo "[MySQL] host=${MYSQL_HOST}"
echo "[MySQL] port=${MYSQL_PORT}"
echo "[MySQL] user=${MYSQL_USER}"

echo "[MySQL] apply schema..."
mysql "${MYSQL_ARGS[@]}" < "${PROJECT_ROOT}/schema.sql"

echo "[MySQL] apply seed data..."
mysql "${MYSQL_ARGS[@]}" < "${PROJECT_ROOT}/seed.sql"

echo "[MySQL] verify tables..."
mysql "${MYSQL_ARGS[@]}" -D tinyimx -e "SHOW TABLES;"

echo "[MySQL] verify demo users..."
mysql "${MYSQL_ARGS[@]}" -D tinyimx -e \
    "SELECT user_id, username, nickname, status FROM im_users ORDER BY user_id;"

echo "[MySQL] schema initialization done."