#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-message-runtime-sql-readonly-20261006';assert not d.exists()
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def run(a):return subprocess.check_output(a,text=True,stderr=subprocess.STDOUT,timeout=30)
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 return {c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in json.loads(run(['docker','inspect',*names]))}
before=runtime();assert before==json.loads((b/'group-message-runtime-control-20261006/restore-summary.json').read_text())['runtime']
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');configs={p.name:sha(p) for p in cfg.glob('*.json')}
d.mkdir(mode=0o700)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Only SELECT Performance Schema aggregate digest and counters, proc resource reads and strings existing sealed Message ELF. No SET/consumer change/new load/runtime/production mutation','runtime':before,'config_sha256':configs,'limits':'Cumulative historical digest/window not per-request attribution; table-lock timing is not InnoDB row-lock time; no causal fsync claim'},indent=2)+'\n')
shell='MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"'
queries={
 'enabled':'SELECT @@performance_schema',
 'group-digests':"SELECT SCHEMA_NAME,DIGEST_TEXT,COUNT_STAR,ROUND(SUM_TIMER_WAIT/1000000000,3),ROUND(AVG_TIMER_WAIT/1000000000,3),ROUND(MAX_TIMER_WAIT/1000000000,3),ROUND(SUM_LOCK_TIME/1000000000,3),SUM_ERRORS,SUM_ROWS_AFFECTED,SUM_ROWS_EXAMINED FROM performance_schema.events_statements_summary_by_digest WHERE SCHEMA_NAME=DATABASE() AND DIGEST_TEXT LIKE 0x"+('%im_group_message_deliveries%'.encode().hex())+" ORDER BY SUM_TIMER_WAIT DESC LIMIT 30",
 'file-waits':"SELECT EVENT_NAME,COUNT_STAR,ROUND(SUM_TIMER_WAIT/1000000000,3),ROUND(AVG_TIMER_WAIT/1000000000,3),ROUND(MAX_TIMER_WAIT/1000000000,3) FROM performance_schema.events_waits_summary_global_by_event_name WHERE EVENT_NAME LIKE 'wait/io/file/%' AND COUNT_STAR>0 ORDER BY SUM_TIMER_WAIT DESC LIMIT 30",
 'consumers':'SELECT NAME,ENABLED FROM performance_schema.setup_consumers',
 'file-instruments':"SELECT NAME,ENABLED,TIMED FROM performance_schema.setup_instruments WHERE NAME LIKE 'wait/io/file/%' ORDER BY NAME",
 'status':"SELECT VARIABLE_NAME,VARIABLE_VALUE FROM performance_schema.global_status WHERE VARIABLE_NAME IN ('Com_commit','Com_update','Innodb_os_log_fsyncs','Innodb_data_fsyncs','Binlog_commits','Binlog_group_commits','Innodb_row_lock_waits','Innodb_row_lock_time','Threads_running','Threads_connected','Innodb_buffer_pool_wait_free','Uptime') ORDER BY VARIABLE_NAME"}
for name,q in queries.items():
 assert q.startswith('SELECT ') and ';' not in q
 (d/(name+'-query.txt')).write_text(q+'\n')
 text=run(['docker','exec','tinyimx-m21-mysql-1','sh','-c',shell,'group-runtime-readonly',q]);(d/(name+'.tsv')).write_text(text)
 print(json.dumps({'query':name,'rows':len(text.splitlines())}),flush=True)
candidates=[]
for p in (b/'group-completion-rpc-build-20261006-attempt4/runtime-private').glob('*message*'):
 if p.is_file() and p.read_bytes()[:4]==b'\x7fELF' and sha(p)=='baa2ec54332c5a2bce2d9bf7fe130231450870a26082552308541cddcceee103':candidates.append(p)
assert len(candidates)==1
text=run(['strings',str(candidates[0])]);has_pool='mysql_pool_acquire_phase' in text and 'TINYIMX_MYSQL_POOL_TRACE' in text
for name in ['meminfo','stat']:(d/(name+'.txt')).write_text((pathlib.Path('/proc')/name).read_text())
for name in ['cpu','memory','io']:(d/('psi-'+name+'.txt')).write_text((pathlib.Path('/proc/pressure')/name).read_text())
mcp=json.loads((cfg/'mcp.json').read_text())['mcp'];(d/'mcp-principal-safe.json').write_text(json.dumps({k:mcp[k] for k in ['static_user_id','static_subject','worker_threads','queue_capacity']},indent=2)+'\n')
assert runtime()==before and {p.name:sha(p) for p in cfg.glob('*.json')}==configs
summary={'status':'READONLY_SQL_AND_DIAGNOSTIC_CAPABILITY_CAPTURED','head':run(['git','rev-parse','HEAD']).strip(),'message_candidate_elf_sha256':sha(candidates[0]),'mysql_pool_phase_already_compiled':has_pool,'runtime_and_configs_preserved':True,'no_load_or_settings_change':True,'allfeature_acceptance':False}
(d/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
print((d/'group-digests.tsv').read_text())
print((d/'status.tsv').read_text())
PY
