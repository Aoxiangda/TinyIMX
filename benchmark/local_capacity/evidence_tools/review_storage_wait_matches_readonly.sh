#!/usr/bin/env bash
set -euo pipefail
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,statistics,subprocess
b=pathlib.Path('.local/codex');d=b/'storage-wait-completed-analysis-20261005'
matched=json.loads((d/'matched-message-numeric.json').read_text());repo=json.loads((d/'repository-numeric.json').read_text());byid={x['mid']:x for x in repo}
for x in matched:
 if 'gateway_rpc_us' in x:print(json.dumps({**x,'commit_us':byid[x['mid']]['commit_us'],'acquire_us':byid[x['mid']]['acquire_us']}))
for kind in ['repository','pool','handler']:
 rows=json.loads((d/(kind+'-numeric.json')).read_text());print(json.dumps({'kind':kind,'count':len(rows),'tids':sorted({x.get('tid',0) for x in rows})[:35],'first':rows[:2]}))
query="SELECT @@version,@@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@innodb_flush_method; SELECT EVENT_NAME,COUNT_STAR,SUM_TIMER_WAIT,AVG_TIMER_WAIT,MAX_TIMER_WAIT FROM performance_schema.events_waits_summary_global_by_event_name WHERE COUNT_STAR>0 AND (EVENT_NAME LIKE 'wait/io/file/innodb/%' OR EVENT_NAME LIKE 'wait/io/file/sql/binlog%' OR EVENT_NAME LIKE 'wait/synch/%/innodb/%') ORDER BY SUM_TIMER_WAIT DESC LIMIT 12;"
out=subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw "$MYSQL_DATABASE" -e "$1"','readonly-numeric-storage-settings',query],text=True,timeout=20);print(out)
PY
