#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,os
r=pathlib.Path.cwd();d=r/'.local/codex/storage-wait-diagnostics-isolated-mysql-20261005';assert not d.exists()
build=json.loads((r/'.local/codex/storage-wait-diagnostics-build-20261005/summary.json').read_text());assert build['status']=='BUILD_UNIT_PASS' and build['compiled_head']=='a315d5d2edfa29daccdc9ae1c39466d164f8fd9a'
assert hashlib.sha256((r/'build/linux-release/message_service_demo').read_bytes()).hexdigest()==build['message_binary_sha256']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','owned-isolated-receiver-confirm-test',q],text=True,timeout=20)
c=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-mysql-1'],text=True))[0];env=dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x);production=env['MYSQL_DATABASE'];assert re.fullmatch('[a-zA-Z0-9_]+',production)
schemas={1:'codex_storage_wait_20261005_p1',4:'codex_storage_wait_20261005_p4'}
assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME IN ('"+"','".join(schemas.values())+"')").strip()=='0','Never overwrite a prior test schema'
tables=['im_users','im_private_messages','im_event_outbox'];ddl={}
for table in tables:
 text=sql('SHOW CREATE TABLE `'+table+'`');create=text.split('\t',1)[1].strip();assert create.startswith('CREATE TABLE `'+table+'`') and ';' not in create
 assert not re.search(r'REFERENCES\s+`[^`]+`\.',create),'Refuse cross-schema FK'
 refs=re.findall(r'REFERENCES\s+`([^`]+)`',create);assert all(x=='im_users' for x in refs)
 ddl[table]=create
d.mkdir();private=d/'runtime-private';private.mkdir();plans={}
for pool,schema in schemas.items():
 assert schema.startswith('codex_storage_wait_20261005_') and schema!=production
 plan='CREATE DATABASE `'+schema+'` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;\nUSE `'+schema+'`;\n'+';\n'.join(ddl.values())+";\nINSERT INTO im_users(user_id,username,nickname,status) VALUES (10001,'codex_lease_a','Owned lease A',1),(10002,'codex_lease_b','Owned lease B',1);\n"
 plans[pool]=plan;(d/(schema+'-create.sql')).write_text(plan)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Create two empty owned schemas; validate restored confirmation semantics and diagnostic-enabled transactions with pool1/pool4','compiled_head':build['compiled_head'],'message_binary_sha256':build['message_binary_sha256'],'schemas':schemas,'production_database':production,'production_reads':'OnlySHOW CREATE metadata; no production row copy/update/delete or grants','writes':'Only new owned schemas/users10001/10002 within them, normal owned messages/outbox/confirm/read/claim rows plus owned status3/status9 fault records, all retained; private root test configs0600; exact plans saved before execution','ddl_sha256':{schemas[k]:hashlib.sha256(v.encode()).hexdigest() for k,v in plans.items()},'validation':'Existing outbox atomicity/idempotency/rollback plus receiver wrong-owner, Pending-confirm-repeat-Read, owned Failed/status9 rejection, 4-thread exact-one confirmation and confirmation-vs-Read race, every lease return, pool1/pool4; original snapshot checks in pool1','diagnostic_flags':['TINYIMX_STORAGE_WAIT_TRACE_ENABLE=1','TINYIMX_MYSQL_POOL_TRACE=1','TINYIMX_PERSIST_PHASE_TRACE_ENABLE=1'],'runtime_changes':[],'other_apps':'Preserved','rollback':'Keep exact schema and failure evidence, no DROP/delete/credential reset; timeout kills only exact owned test subprocess. No changes to production service/config/durability.'},indent=2)+'\n')
checks={}; childenv={**os.environ,'TINYIMX_STORAGE_WAIT_TRACE_ENABLE':'1','TINYIMX_MYSQL_POOL_TRACE':'1','TINYIMX_PERSIST_PHASE_TRACE_ENABLE':'1'}
try:
 for pool,schema in schemas.items():
  sql(plans[pool]);assert sql("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA='"+schema+"'").strip()=='3'
  cfg=json.loads(pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json').read_text());cfg['mysql'].update({'host':next(iter(c['NetworkSettings']['Networks'].values()))['IPAddress'],'port':3306,'user':'root','password':env['MYSQL_ROOT_PASSWORD'],'database':schema,'pool_size':pool});cfg['logger']['console']=False;cfg['logger']['level']='warn';cfg['logger']['file']=str(private/(schema+'.log'))
  p=private/(schema+'.json');p.write_text(json.dumps(cfg));os.chmod(p,0o600)
  targets=['message_outbox_integration_tests']+(['unread_snapshot_aggregate_tests'] if pool==1 else[])
  for target in targets:
   log=d/(target+'-pool'+str(pool)+'.log')
   with log.open('w') as f:result=subprocess.run([str(r/'build/linux-release'/target),str(p)],stdout=f,stderr=subprocess.STDOUT,timeout=90,env=childenv)
   text=log.read_text();key=target+'-pool'+str(pool);checks[key]={'exit_code':result.returncode,'pass':text.count('[PASS]')+sum(line.startswith('PASS ') for line in text.splitlines()),'fail':text.count('[FAIL]')+sum(line.startswith('FAIL ') for line in text.splitlines())};assert result.returncode==0 and checks[key]['pass']>0 and checks[key]['fail']==0,key+' failed'
  (d/(schema+'-counts.tsv')).write_text(sql('SELECT COUNT(*) FROM `'+schema+'`.im_users; SELECT COUNT(*) FROM `'+schema+'`.im_private_messages; SELECT COUNT(*) FROM `'+schema+'`.im_event_outbox'))
 x={'status':'REAL_ISOLATED_MYSQL_PASS','checks':checks,'schemas_preserved':list(schemas.values()),'production_mutations':False,'runtime_deployment':False,'compiled_head':build['compiled_head']};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'summary.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e),'checks':checks,'schemas_preserved':list(schemas.values()),'production_mutations':False},indent=2)+'\n');raise
PY
