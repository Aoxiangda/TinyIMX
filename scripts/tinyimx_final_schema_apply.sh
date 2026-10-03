#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
MYSQL_CONTAINER="${M21_MYSQL_CONTAINER:-tinyimx-m21-mysql-1}"

docker inspect "$MYSQL_CONTAINER" >/dev/null 2>&1 || { echo "FIRST_FAILURE=MYSQL_CONTAINER_MISSING"; exit 10; }
MYSQL_USER="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}' | sed -n 's/^MYSQL_USER=//p')"
MYSQL_PASSWORD="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}' | sed -n 's/^MYSQL_PASSWORD=//p')"
MYSQL_DATABASE="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}' | sed -n 's/^MYSQL_DATABASE=//p')"
[[ -n "$MYSQL_USER" && -n "$MYSQL_PASSWORD" && -n "$MYSQL_DATABASE" ]] || { echo "FIRST_FAILURE=MYSQL_ENV_DISCOVERY"; exit 11; }

for migration in 009_create_file_domain.sql 010_create_file_upload_chunks.sql; do
  file="$ROOT/db/migrations/$migration"
  [[ -s "$file" ]] || { echo "FIRST_FAILURE=MIGRATION_MISSING file=$file"; exit 12; }
  echo "APPLY_MIGRATION=$migration"
  docker exec -i -e MYSQL_PWD="$MYSQL_PASSWORD" "$MYSQL_CONTAINER" \
    mysql -u"$MYSQL_USER" "$MYSQL_DATABASE" < "$file"
done

"$ROOT/scripts/m21_verify_production_schema_complete.sh"
echo "TINYIMX_FINAL_SCHEMA_APPLY=PASS"
