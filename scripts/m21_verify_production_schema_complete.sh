#!/usr/bin/env bash
set -Eeuo pipefail
MYSQL_CONTAINER="${M21_MYSQL_CONTAINER:-tinyimx-m21-mysql-1}"
MYSQL_USER="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}'|sed -n 's/^MYSQL_USER=//p')"; MYSQL_PASSWORD="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}'|sed -n 's/^MYSQL_PASSWORD=//p')"; MYSQL_DATABASE="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}'|sed -n 's/^MYSQL_DATABASE=//p')"
q(){ docker exec -e MYSQL_PWD="$MYSQL_PASSWORD" "$MYSQL_CONTAINER" mysql -N -B -u"$MYSQL_USER" "$MYSQL_DATABASE" -e "$1"; }
TABLES=(im_users im_user_relations im_private_messages im_event_outbox im_groups im_group_members im_group_messages im_group_message_deliveries im_files im_file_upload_sessions im_file_upload_chunks)
for t in "${TABLES[@]}";do x="$(q "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema='${MYSQL_DATABASE}' AND table_name='${t}';")";echo "schema_table=$t exists=$x";[[ "$x" == 1 ]]||exit 1;done
[[ "$(q "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema='${MYSQL_DATABASE}' AND table_name='im_file_upload_sessions' AND index_name='uk_im_file_upload_client' AND non_unique=0;")" == 2 ]]
[[ "$(q "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema='${MYSQL_DATABASE}' AND table_name='im_file_upload_chunks' AND index_name='PRIMARY';")" == 2 ]]
echo M21_PRODUCTION_SCHEMA_COMPLETE=PASS
