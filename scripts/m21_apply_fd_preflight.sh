#!/usr/bin/env bash
set -Eeuo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)";cd "$ROOT_DIR"
COMPOSE=deploy/production/docker-compose.yml;NGINX=deploy/production/nginx/nginx.conf;PROTECTED=gateway/GatewayPeerTransport.h;BEFORE="$(sha256sum "$PROTECTED"|awk '{print $1}')"
python3 - "$COMPOSE" <<'PY'
from pathlib import Path
import re,sys
p=Path(sys.argv[1]);s=p.read_text()
for service in ('gateway-a','gateway-b','nginx'):
 m=re.search(rf'(?ms)(^  {re.escape(service)}:\n)(.*?)(?=^  [A-Za-z0-9_-]+:\n|^networks:\n|\Z)',s)
 if not m:raise SystemExit(f'service missing: {service}')
 body=m.group(2)
 if 'ulimits:' in body:
  if 'nofile:' in body and 'soft: 65536' in body and 'hard: 65536' in body:continue
  raise SystemExit(f'existing ulimits need manual review: {service}')
 anchor='    restart: unless-stopped\n'
 if anchor not in body:raise SystemExit(f'restart anchor missing: {service}')
 body=body.replace(anchor,anchor+'    ulimits:\n      nofile:\n        soft: 65536\n        hard: 65536\n',1)
 s=s[:m.start(2)]+body+s[m.end(2):]
p.write_text(s)
PY
python3 - "$NGINX" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
if 'worker_rlimit_nofile 65536;' not in s:
 a='worker_processes auto;\n'
 if a not in s:raise SystemExit('worker_processes anchor missing')
 s=s.replace(a,a+'worker_rlimit_nofile 65536;\n',1)
old='events { worker_connections 8192; multi_accept on; }';new='events { worker_connections 16384; multi_accept on; }'
if old in s:s=s.replace(old,new,1)
elif new not in s:raise SystemExit('worker_connections anchor missing')
p.write_text(s)
PY
docker compose --env-file deploy/production/.env -f "$COMPOSE" config -q
git diff --check -- "$COMPOSE" "$NGINX"
[[ "$(sha256sum "$PROTECTED"|awk '{print $1}')" == "$BEFORE" ]]
echo M21_PERF_FD_CONFIG_PATCH=PASS
echo 'Recreate gateway-a gateway-b nginx before benchmarking if this script changed files.'
