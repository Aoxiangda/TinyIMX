#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,os,signal
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'receiver-confirm-guarded-update-mysql-20261005';assert not d.exists()
build=json.loads((b/'receiver-confirm-guarded-update-build-20261005/summary.json').read_text());assert build['status']=='GUARDED_CONFIRM_BUILD_UNIT_PASS'
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert build['compiled_head']==head
assert hashlib.sha256((r/'build/linux-release/message_outbox_integration_tests').read_bytes()).hexdigest()==build['test_binary_sha256']
red=b/'receiver-confirm-guarded-update-build-20261005/original_adapter_new_tests';assert hashlib.sha256(red.read_bytes()).hexdigest()==build['red_binary_sha256']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'receiver-confirm-guarded-update-build-20261005/runtime-after.json').read_text())
def sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','owned-guarded-confirm-test',q],text=True,timeout=20)
durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count';assert sql(durability).strip()=='1\t1\t1\t0\t0'
container=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-mysql-1'],text=True))[0];env=dict(x.split('=',1) for x in container['Config']['Env'] if '=' in x);production=env['MYSQL_DATABASE'];assert re.fullmatch('[a-zA-Z0-9_]+',production)
cases={'red1':(1,'codex_receiver_guard_20261005_red1'),'green1':(1,'codex_receiver_guard_20261005_p1'),'green4':(4,'codex_receiver_guard_20261005_p4')}
schemas=[x[1] for x in cases.values()];assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME IN ('"+"','".join(schemas)+"')").strip()=='0','Never overwrite test schema'
ddl={}
for table in ['im_users','im_private_messages','im_event_outbox']:
 create=sql('SHOW CREATE TABLE `'+table+'`').split('\t',1)[1].strip();assert create.startswith('CREATE TABLE `'+table+'`') and ';' not in create
 assert not re.search(r'REFERENCES\s+`[^`]+`\.',create);assert all(x=='im_users' for x in re.findall(r'REFERENCES\s+`([^`]+)`',create));ddl[table]=create
# The fast path assumes the existing typed numeric/date schema, not arbitrary
# string columns that could parse differently from guarded numeric comparisons.
private_ddl=ddl['im_private_messages'].lower()
for key in ['message_id','from_user_id','to_user_id']:
 assert re.search('`'+key+r'`\s+bigint(?:\(\d+\))?\s+unsigned\s+not null',private_ddl),key
assert re.search(r'`message_type`\s+tinyint(?:\(\d+\))?\s+unsigned\s+not null',private_ddl)
assert re.search(r'`created_at`\s+(?:timestamp|datetime)(?:\(\d+\))?\s+not null',private_ddl)
d.mkdir();private=d/'runtime-private';private.mkdir();plans={}
for label,(pool,schema) in cases.items():
 assert schema.startswith('codex_receiver_guard_20261005_') and schema!=production
 plan='CREATE DATABASE `'+schema+'` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;\nUSE `'+schema+'`;\n'+';\n'.join(ddl.values())+";\nINSERT INTO im_users(user_id,username,nickname,status) VALUES (10001,'codex_guard_a','Owned guard A',1),(10002,'codex_guard_b','Owned guard B',1);\n"
 plans[label]=plan;(d/(schema+'-create.sql')).write_text(plan)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'New isolated red/green receiver confirmation schemas only; all plans written before CREATE','compiled_head':head,'schemas':schemas,'production_reads':'SHOW CREATE metadata only, no row copies','writes':'Only 3 fresh owned schemas/users/messages/outbox and audited own failing triggers/status9/type0/type4/read/confirm mutations; preserve every schema and failure. Existing test trigger removal only inside new schema, no DROP DATABASE or production writes','ddl_sha256':{label:hashlib.sha256(plan.encode()).hexdigest() for label,plan in plans.items()},'red_expectation':'Exactly one avoid-SELECT assertion fails against original adapter; all original identity/state/outbox/concurrency checks pass','green_expectation':'All original plus new assertions pass pool1/pool4 and snapshotpool1','timeouts':'Each own test120s with cmdline/starttime/PGID verified shutdown only','runtime_before':before,'runtime_deployment':False,'durability':'1/1/1/0/0 unchanged','rollback':'Keep ownschemas/logs/configs0600, originalproduction untouched; no delete/prune/reset/push'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
def interrupted(signum,frame):raise KeyboardInterrupt('Own isolated SQL test interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def test(binary,label,config):
 args=[str(binary),str(config)];child=None;identity=None
 with (d/(label+'.log')).open('w') as out:
  try:
   child=subprocess.Popen(args,stdout=out,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path(f'/proc/{child.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   (d/(label+'-own-process.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n');return child.wait(timeout=120)
  finally:
   if child is not None and child.poll() is None:
    assert identity is not None and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid
    assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
    (d/(label+'-stop-audit.json')).write_text(json.dumps({'operation':'Stop onlyownverifiedisolatedSQLtest','pid':child.pid,'identity_verified':True})+'\n');os.killpg(child.pid,signal.SIGTERM)
    try:child.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
checks={}
try:
 for label,(pool,schema) in cases.items():
  sql(plans[label]);assert sql("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA='"+schema+"'").strip()=='3'
  cfg=json.loads(pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json').read_text());cfg['mysql'].update({'host':next(iter(container['NetworkSettings']['Networks'].values()))['IPAddress'],'port':3306,'user':'root','password':env['MYSQL_ROOT_PASSWORD'],'database':schema,'pool_size':pool});cfg['logger']['console']=False;cfg['logger']['file']=str(private/(schema+'.log'))
  config=private/(schema+'.json');config.write_text(json.dumps(cfg));os.chmod(config,0o600)
  targets=[red] if label=='red1' else [r/'build/linux-release/message_outbox_integration_tests']+([r/'build/linux-release/unread_snapshot_aggregate_tests'] if pool==1 else [])
  for binary in targets:
   key=label+'-'+binary.name;code=test(binary,key,config);text=(d/(key+'.log')).read_text();passes=[line for line in text.splitlines() if line.startswith('[PASS]')];fails=[line for line in text.splitlines() if line.startswith('[FAIL]')];checks[key]={'exit_code':code,'pass':len(passes),'fail':len(fails),'failed_assertions':fails}
   if label=='red1':assert code==1 and len(fails)==1 and 'Pending normal path avoids SELECT before update' in fails[0] and passes,'Original red must fail only new performance behavior assertion'
   else:assert code==0 and passes and not fails,key+' failed'
  (d/(schema+'-counts.tsv')).write_text(sql('SELECT COUNT(*) FROM `'+schema+'`.im_users; SELECT COUNT(*) FROM `'+schema+'`.im_private_messages; SELECT COUNT(*) FROM `'+schema+'`.im_event_outbox'))
 assert sql(durability).strip()=='1\t1\t1\t0\t0'
 after=runtime();assert after==before
 x={'status':'GUARDED_CONFIRM_REAL_MYSQL_RED_GREEN_PASS','compiled_head':head,'checks':checks,'schemas_preserved':schemas,'all19_runtime_configs_preserved':True,'production_table_mutations':False,'runtime_deployment':False,'performance_acceptance':False}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as error:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(error).__name__,'message':str(error),'checks':checks,'schemas_preserved':schemas})+'\n');raise
finally:
 after=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
PY
