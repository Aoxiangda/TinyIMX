#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
COUNT="${1:-${M21_BENCH_USER_COUNT:-1200}}"
BASE="${M21_BENCH_USER_BASE:-200000}"
PREFIX="${M21_BENCH_USERNAME_PREFIX:-m21b${BASE}_}"
TEMPLATE_USER_ID="${M21_BENCH_TEMPLATE_USER_ID:-10001}"
MYSQL_CONTAINER="${M21_MYSQL_CONTAINER:-tinyimx-m21-mysql-1}"
[[ "$COUNT" =~ ^[0-9]+$ ]] && ((COUNT>=3 && COUNT<=20000)) || { echo "invalid count" >&2; exit 64; }
[[ "$BASE" =~ ^[0-9]+$ ]] || exit 64
[[ "$PREFIX" =~ ^[A-Za-z0-9_-]+$ ]] || exit 64
MYSQL_USER="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}'|sed -n 's/^MYSQL_USER=//p')"
MYSQL_PASSWORD="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}'|sed -n 's/^MYSQL_PASSWORD=//p')"
MYSQL_DATABASE="$(docker inspect "$MYSQL_CONTAINER" --format '{{range .Config.Env}}{{println .}}{{end}}'|sed -n 's/^MYSQL_DATABASE=//p')"
mysql_scalar(){ docker exec -e MYSQL_PWD="$MYSQL_PASSWORD" "$MYSQL_CONTAINER" mysql -N -B -u"$MYSQL_USER" "$MYSQL_DATABASE" -e "$1"; }
[[ "$(mysql_scalar "SELECT COUNT(*) FROM im_users WHERE user_id=${TEMPLATE_USER_ID} AND LENGTH(password_salt)>0 AND LENGTH(password_hash)>0 AND status=1;")" == 1 ]] || { echo "template user credentials missing" >&2; exit 1; }
END=$((BASE+COUNT))
[[ "$(mysql_scalar "SELECT COUNT(*) FROM im_users WHERE user_id>${BASE} AND user_id<=${END} AND username NOT LIKE '${PREFIX}%'
  AND username NOT LIKE 'm21bench%'
  AND username NOT LIKE 'm21b%';")" == 0 ]] || { echo "benchmark id range collides with non-benchmark users" >&2; exit 1; }
TMP="$(mktemp /tmp/tinyimx-bench-users-XXXX.sql)"; trap 'rm -f "$TMP"' EXIT
python3 - "$TMP" "$COUNT" "$BASE" "$PREFIX" "$TEMPLATE_USER_ID" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); n=int(sys.argv[2]); base=int(sys.argv[3]); prefix=sys.argv[4]; template=int(sys.argv[5])
def u(i): return f"{prefix}{i:06d}"
with p.open('w',encoding='utf-8') as f:
    f.write(f"SET @s=(SELECT password_salt FROM im_users WHERE user_id={template});\nSET @h=(SELECT password_hash FROM im_users WHERE user_id={template});\n")
    for start in range(1,n+1,400):
        rows=[]
        for i in range(start,min(n,start+399)+1):
            rows.append(f"({base+i},'{u(i)}','M21 Bench {i:06d}','',@s,@h,1)")
        f.write("INSERT INTO im_users (user_id,username,nickname,avatar_url,password_salt,password_hash,status) VALUES\n"+",\n".join(rows)+"\nON DUPLICATE KEY UPDATE username=VALUES(username),nickname=VALUES(nickname),password_salt=@s,password_hash=@h,status=1;\n")
    pairs=set()
    for i in range(1,n+1):
        j=1 if i==n else i+1
        pairs.add((base+i,base+j));pairs.add((base+j,base+i))
        if i!=1:
            pairs.add((base+i,base+1));pairs.add((base+1,base+i))
    pairs=sorted(pairs)
    for start in range(0,len(pairs),800):
        rows=[f"({a},{b},1)" for a,b in pairs[start:start+800]]
        f.write("INSERT INTO im_user_relations (user_id,peer_user_id,relation_status) VALUES\n"+",\n".join(rows)+"\nON DUPLICATE KEY UPDATE relation_status=1;\n")
PY
docker exec -i -e MYSQL_PWD="$MYSQL_PASSWORD" "$MYSQL_CONTAINER" mysql -u"$MYSQL_USER" "$MYSQL_DATABASE" < "$TMP"
USERS="$(mysql_scalar "SELECT COUNT(*) FROM im_users WHERE user_id>${BASE} AND user_id<=${END} AND username LIKE '${PREFIX}%' AND status=1 AND LENGTH(password_salt)>0 AND LENGTH(password_hash)>0;")"
[[ "$USERS" == "$COUNT" ]] || { echo "fixture user verification failed expected=$COUNT actual=$USERS" >&2; exit 1; }
echo "M21_BENCH_FIXTURE=PASS users=$USERS base=$BASE prefix=$PREFIX"
