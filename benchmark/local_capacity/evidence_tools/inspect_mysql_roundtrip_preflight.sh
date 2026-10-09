#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib
r=pathlib.Path.cwd();d=r/'.local/codex/mysql-roundtrip-readonly-preflight-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
source=r/'common/db/MySqlConnection.cpp';assert 'CLIENT_MULTI_STATEMENTS' in source.read_text()
queries={'settings':'SELECT VERSION(),@@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count,@@autocommit,@@transaction_isolation',
'private-schema':'SHOW CREATE TABLE im_private_messages','outbox-schema':'SHOW CREATE TABLE im_event_outbox',
'objects':"SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('codex_sql_rtt_messages_20261005','codex_sql_rtt_outbox_20261005')",
'triggers':"SELECT EVENT_OBJECT_TABLE,COUNT(*) FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA=DATABASE() AND EVENT_OBJECT_TABLE IN ('im_private_messages','im_event_outbox') GROUP BY EVENT_OBJECT_TABLE",
'foreign-keys':"SELECT TABLE_NAME,COLUMN_NAME,REFERENCED_TABLE_NAME,REFERENCED_COLUMN_NAME FROM information_schema.KEY_COLUMN_USAGE WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('im_private_messages','im_event_outbox') AND REFERENCED_TABLE_NAME IS NOT NULL"}
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly SQL roundtrip feasibility preflight before any table creation or newload','source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'connection_cpp_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'queries':queries,'writes':'Freshowned schema/settings numeric reports only, no SQL/DDL/config/runtime mutation','scope':'Native driver already enablesCLIENT_MULTI_STATEMENTS, do not change security flags or healthyPING/durability','rollback':'Keep all reports, no deletion'},indent=2)+'\n')
results={}
for name,q in queries.items():
 text=subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','codex-readonly-rtt-preflight',q],text=True,timeout=15)
 (d/(name+'.tsv')).write_text(text);results[name]=text
assert not results['objects'].strip(),'Owned names already exist, do not recreate ordelete'
settings=results['settings'].strip().split('\t');assert settings[1:6]==['1','1','1','0','0']
x={'status':'MYSQL_ROUNDTRIP_READONLY_PREFLIGHT_COMPLETE','mysql_version':settings[0],'durability':'1/1/1/0/0','original_client_multi_statements_enabled':True,'owned_objects_absent':True,'original_triggers':results['triggers'].strip().splitlines(),'original_foreign_keys':results['foreign-keys'].strip().splitlines(),'private_schema':results['private-schema'],'outbox_schema':results['outbox-schema'],'limits':'CREATE LIKE omits foreign keys; any proposed fresh own table mechanism must identify this limitation, not prove whole production contract','real_table_writes':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
