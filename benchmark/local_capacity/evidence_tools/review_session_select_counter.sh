#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,subprocess,json,datetime
d=pathlib.Path('.local/codex/session-select-counter-review-20261005');assert not d.exists();d.mkdir()
query="SHOW SESSION STATUS LIKE 'Com_select'; SELECT 1; SHOW SESSION STATUS LIKE 'Com_select'"
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly session counter sanity check','SQL':query,'writes':'Fresh own evidence files only; no table/session/global setting changes','prior_attempt':'PowerShell quoting produced argument parsing failure before SSH or SQL execution; no credential value output'},indent=2)+'\n')
raw=subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-counter-review',query],text=True,timeout=10)
(d/'raw.tsv').write_text(raw);assert raw.splitlines()==['Com_select\t0','1','Com_select\t1']
(d/'summary.json').write_text(json.dumps({'status':'SESSION_COM_SELECT_SANITY_PASS','SHOW_does_not_increment_Com_select':True,'SELECT1_increments_by_one':True,'production_table_writes':False},indent=2)+'\n');print('SESSION_COM_SELECT_SANITY_PASS')
PY
