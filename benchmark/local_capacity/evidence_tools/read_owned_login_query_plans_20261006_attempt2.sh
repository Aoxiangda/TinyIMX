#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,time
r=pathlib.Path.cwd();d=r/'.local/codex/login-regression-query-plans-20261006-attempt2';assert not d.exists()
def run(a,timeout=25):return subprocess.check_output(a,text=True,timeout=timeout)
assert run(['git','rev-parse','HEAD']).strip()=='395a4a1fcf60ca10e7f1288592d893810688c42d'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
queries={'owned_users': "SELECT /*+ MAX_EXECUTION_TIME(5000) */ COUNT(*) FROM im_users WHERE user_id BETWEEN 700001 AND 710000 AND username=CONCAT('codex50k_20261004_',LPAD(user_id-700000,6,'0')) AND status=1", 'failed_history_aggregate_plan': 'EXPLAIN FORMAT=JSON SELECT delivery_status,COUNT(*),COUNT(DISTINCT to_user_id) FROM im_private_messages WHERE from_user_id BETWEEN 700001 AND 710000 AND to_user_id BETWEEN 700001 AND 710000 GROUP BY delivery_status ORDER BY delivery_status', 'pending_private_query_plan': 'EXPLAIN FORMAT=JSON SELECT message_id FROM im_private_messages WHERE to_user_id=700001 AND delivery_status=0 AND message_id>0 ORDER BY message_id ASC LIMIT 100', 'group_recipient_query_plan': 'EXPLAIN FORMAT=JSON SELECT d.message_id FROM im_group_message_deliveries d JOIN im_group_messages m ON m.message_id=d.message_id WHERE d.recipient_user_id=700001 AND d.delivery_status=2 AND d.next_retry_at<=NOW(3) AND (d.lease_until IS NULL OR d.lease_until<=NOW(3)) ORDER BY d.message_id ASC LIMIT 100', 'public_index_inventory': "SELECT table_name,index_name,seq_in_index,column_name FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name IN ('im_private_messages','im_group_message_deliveries','im_users') ORDER BY table_name,index_name,seq_in_index", 'approximate_table_metadata': "SELECT table_name,table_rows,data_length,index_length FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name IN ('im_private_messages','im_group_message_deliveries','im_users') ORDER BY table_name", 'bounded_pending_private_count': 'SELECT /*+ MAX_EXECUTION_TIME(5000) */ COUNT(*) FROM (SELECT message_id FROM im_private_messages WHERE to_user_id BETWEEN 700001 AND 710000 AND delivery_status=0 LIMIT 10001) q', 'bounded_deferred_group_count': 'SELECT /*+ MAX_EXECUTION_TIME(5000) */ COUNT(*) FROM (SELECT message_id FROM im_group_message_deliveries WHERE recipient_user_id BETWEEN 700001 AND 710000 AND delivery_status=2 LIMIT 10001) q'}
def identity():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));public={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt'],'health':c['State'].get('Health',{}).get('Status')} for c in cs}
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');return public,{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}
before,cfg_before=identity();assert before['/tinyimx-m21-user-service-1']['id']=='5319ccda4485e06b16e666ffd58d7162e26d5af7984945bb68e1e14a1d8a0b74'
d.mkdir();audit={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly aggregates of existing owned 700001-710000 cohort, SQL EXPLAIN without execution, public index metadata, bounded 10001 row counts, no pressure','queries':queries,'reads':'No passwords, message bodies, keys, content or private env values','writes':'Fresh own evidence directory only','no_database_mutation':True,'no_service_restart':True,'runtime':before,'private_config_sha256':cfg_before};(d/'audit-before.json').write_text(json.dumps(audit,indent=2)+'\n')
try:
 results={}
 for k,q in queries.items():
  t=time.monotonic();raw=run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-owned-login-review',q]);results[k]={'rows':[x.split('\t') for x in raw.splitlines()],'elapsed_seconds_including_docker':time.monotonic()-t};(d/'query-progress.json').write_text(json.dumps(results,indent=2)+'\n')
 assert results['owned_users']['rows']==[['10000']]
 after,cfg_after=identity();assert before==after and cfg_before==cfg_after
 x={'status':'READONLY_OWNED_LOGIN_QUERY_PLANS_REVIEW_COMPLETE','ended_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_head':run(['git','rev-parse','HEAD']).strip(),'results':results,'runtime_preserved':True,'config_preserved':True,'pressure_tests':False,'causal_bounds':'Current post-run state cannot reconstruct earlier baseline; count growth or empty replay work alone is not a causal performance proof'}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'failed_query':k,'completed_queries':list(results)})+'\n');raise
PY
