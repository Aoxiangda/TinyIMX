#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
COUNT="${1:-20000}"
BASE="${TINYIMX_FINAL_BENCH_BASE:-500000}"
PREFIX="${TINYIMX_FINAL_BENCH_PREFIX:-m21b500000_}"
TEMPLATE="${TINYIMX_FINAL_BENCH_TEMPLATE_ID:-499999}"
PASSWORD="${TINYIMX_FINAL_BENCH_PASSWORD:-123456}"
MYSQL_CONTAINER="${M21_MYSQL_CONTAINER:-tinyimx-m21-mysql-1}"

[[ "${TINYIMX_ALLOW_BENCH_FIXTURE:-0}" == "1" ]] || {
  echo "ERROR: set TINYIMX_ALLOW_BENCH_FIXTURE=1 explicitly; this script writes benchmark credentials." >&2
  exit 2
}
[[ "$COUNT" =~ ^[0-9]+$ ]] && (( COUNT >= 1000 && COUNT <= 20000 )) || { echo "invalid count" >&2; exit 64; }

docker inspect "$MYSQL_CONTAINER" >/dev/null 2>&1 || { echo "FIRST_FAILURE=MYSQL_CONTAINER_MISSING"; exit 10; }
MYSQL_USER="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}' | sed -n 's/^MYSQL_USER=//p')"
MYSQL_PASSWORD="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}' | sed -n 's/^MYSQL_PASSWORD=//p')"
MYSQL_DATABASE="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}' | sed -n 's/^MYSQL_DATABASE=//p')"
[[ -n "$MYSQL_USER" && -n "$MYSQL_PASSWORD" && -n "$MYSQL_DATABASE" ]] || { echo "FIRST_FAILURE=MYSQL_ENV_DISCOVERY"; exit 11; }

collision="$(docker exec -e MYSQL_PWD="$MYSQL_PASSWORD" "$MYSQL_CONTAINER" \
  mysql -N -B -u"$MYSQL_USER" "$MYSQL_DATABASE" \
  -e "SELECT COUNT(*) FROM im_users WHERE user_id=${TEMPLATE} AND username<>'tinyimx_final_bench_template';")"
[[ "$collision" == "0" ]] || { echo "FIRST_FAILURE=BENCH_TEMPLATE_ID_COLLISION user_id=$TEMPLATE"; exit 12; }

TMP_SQL="$(mktemp /tmp/tinyimx-final-template-XXXX.sql)"
trap 'rm -f "$TMP_SQL"' EXIT

python3 - "$TMP_SQL" "$TEMPLATE" "$PASSWORD" <<'PY'
from pathlib import Path
import hashlib,sys
out=Path(sys.argv[1]); uid=int(sys.argv[2]); password=sys.argv[3].encode()
salt=bytes.fromhex('00112233445566778899aabbccddeeff')
digest=hashlib.pbkdf2_hmac('sha256',password,salt,100000,dklen=32)
# UserRepository stores/compares hex strings for salt/hash in this benchmark fixture.
salt_hex=salt.hex(); hash_hex=digest.hex()
out.write_text(f"""
INSERT INTO im_users
(user_id,username,nickname,avatar_url,password_salt,password_hash,status)
VALUES
({uid},'tinyimx_final_bench_template','Final Bench Template','',
 '{salt_hex}','{hash_hex}',1)
ON DUPLICATE KEY UPDATE
 username=VALUES(username), nickname=VALUES(nickname),
 password_salt=VALUES(password_salt), password_hash=VALUES(password_hash), status=1;
""",encoding='utf-8')
PY

docker exec -i -e MYSQL_PWD="$MYSQL_PASSWORD" "$MYSQL_CONTAINER" \
  mysql -u"$MYSQL_USER" "$MYSQL_DATABASE" < "$TMP_SQL"

export M21_BENCH_USER_BASE="$BASE"
export M21_BENCH_USERNAME_PREFIX="$PREFIX"
export M21_BENCH_TEMPLATE_USER_ID="$TEMPLATE"
"$ROOT/scripts/m21_prepare_benchmark_users.sh" "$COUNT"

echo "TINYIMX_FINAL_BENCH_FIXTURE=PASS users=$COUNT base=$BASE prefix=$PREFIX"
