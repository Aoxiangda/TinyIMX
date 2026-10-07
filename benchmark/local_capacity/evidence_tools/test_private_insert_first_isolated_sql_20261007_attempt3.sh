#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,shlex,re,datetime,os,signal,shutil
r=pathlib.Path.cwd();b=r/'.local/codex';head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
source=json.loads((b/'insert-first-build-repair-source-20261007/summary.json').read_text());assert source['head']==head and all(hashlib.sha256((r/n).read_bytes()).hexdigest()==h for n,h in source['files'].items())
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
assert shutil.disk_usage(r).free>1024**3
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
 cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();built_runtime=json.loads((b/'insert-first-build-20261007-attempt2/runtime-after.json').read_text());assert before=={'containers':built_runtime['identities'],'config_sha256':built_runtime['config_sha256']}
def guarded_run(args,label,seconds,env=None):
 child=None;proc=None;identity=None;code=None;error=None
 (d/(label+'-command.json')).write_text(json.dumps(args,indent=2)+'\n')
 with (d/(label+'.log')).open('w') as f:
  try:
   child=subprocess.Popen(args,cwd=r/'build/linux-release',stdout=f,stderr=subprocess.STDOUT,start_new_session=True,env=env)
   proc=pathlib.Path(f'/proc/{child.pid}')
   try:identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   except FileNotFoundError:assert child.poll() is not None
   (d/(label+'-own-process.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args,'timeout_seconds':seconds})+'\n')
   code=child.wait(timeout=seconds)
  except BaseException as e:error=e
  finally:
   if child is not None and child.poll() is None:
    assert identity is not None and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid
    assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
    (d/(label+'-stop-audit.json')).write_text(json.dumps({'operation':'Stop only own PID/start/argv/PGID verified process group','pid':child.pid})+'\n');os.killpg(child.pid,signal.SIGTERM)
    try:child.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
 if error:raise error
 assert code==0,label+' failed; exact logs and partial artifacts retained'
 return (d/(label+'.log')).read_text()
def interrupted(signum,frame):raise KeyboardInterrupt('Own regression interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
build=b/'insert-first-build-20261007-attempt2';info=json.loads((build/'summary.json').read_text());assert info['status']=='PRIVATE_INSERT_FIRST_BUILD_LOADER_PASS' and info['head']==head;built=pathlib.Path(info['tests_runtime_private'])
assert all(hashlib.sha256((built/n).read_bytes()).hexdigest()==h for n,h in info['binaries'].items())
d=b/'insert-first-isolated-sql-20261007';assert not d.exists()
def sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-private-batch-regression',q],text=True,timeout=20)
durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count';assert sql(durability).strip()=='1\t1\t1\t0\t0'
disk=subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','df','-Pk','/var/lib/mysql'],text=True);assert int(disk.splitlines()[-1].split()[3])*1024>1024**3
c=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-mysql-1'],text=True))[0];env=dict(v.split('=',1) for v in c['Config']['Env'] if '=' in v);production=env['MYSQL_DATABASE']
schemas={(mode,pool):f'codex_insert_first_20261007_{mode}_p{pool}' for mode,pool in [('A1',1),('B1',1),('B2',1),('A2',1),('OFF4',4),('ON4',4),('INVALID',1)]};mode_flags={'A1':None,'B1':'1','B2':'1','A2':'0','OFF4':'0','ON4':'1','INVALID':'01'}
assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME IN ('"+"','".join(schemas.values())+"')").strip()=='0'
tables=['im_users','im_private_messages','im_event_outbox'];ddl={}
for table in tables:
 create=sql('SHOW CREATE TABLE `'+table+'`').split('\t',1)[1].strip();assert create.startswith('CREATE TABLE `'+table+'`') and ';' not in create
 assert not re.search(r'REFERENCES\s+`[^`]+`\.',create)
 assert all(v=='im_users' for v in re.findall(r'REFERENCES\s+`([^`]+)`',create));ddl[table]=create
d.mkdir();private=d/'runtime-private';private.mkdir();os.chmod(private,0o700);plans={};checks={}
for key,schema in schemas.items():
 assert schema!=production and schema.startswith('codex_insert_first_20261007_')
 plan='CREATE DATABASE `'+schema+'` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;\nUSE `'+schema+'`;\n'+';\n'.join(ddl.values())+";\nINSERT INTO im_users(user_id,username,nickname,status) VALUES (10001,'codex_private_batch_a','Owned batch A',1),(10002,'codex_private_batch_b','Owned batch B',1);\n"
 plans[key]=plan;(d/(schema+'-create.sql')).write_text(plan)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Seven absent owned SQL schemas: actual new Adapter sameELF integration OFF/ON pools1/4, malformed flag; four serial repository cost ABBA cases pool1; no deployment or production row change','head':head,'schemas':list(schemas.values()),'DDL_sha256':{schemas[k]:hashlib.sha256(v.encode()).hexdigest() for k,v in plans.items()},'source':'Only original schema metadata copied with FKs within each new schema; only two new own users; no production row copy/update/delete or grants','negative_cases':'Only own schema and session-local TEMP shadows: duplicate INSERT, third SQL error, unexpected extra result, quoted semicolon bytes, valid mismatched type, malformed timestamp, original race/outbox rollback/read/receiver contracts','private_configs':'Root credentials RAM to owned 0600 runtime-private JSON with only own database/loopback Docker host; never exported or printed','runtime_before':before,'rollback':'Preserve all owned schemas/raw/partial artifacts; no DROP/DELETE/TRUNCATE/reset. Close own connection frees own temp objects, own transactions rollback; guarded child stops only.'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
try:
 for (mode,pool),schema in schemas.items():
  (d/(schema+'-DDL-audit-before.json')).write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'schema':schema,'before_absent':sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME='"+schema+"'").strip()=='0','plan_sha256':hashlib.sha256(plans[(mode,pool)].encode()).hexdigest(),'production_ddl':False})+'\n');sql(plans[(mode,pool)])
  assert sql("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA='"+schema+"'").strip()=='3'
  cfg=json.loads(pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json').read_text());cfg['mysql'].update({'host':next(iter(c['NetworkSettings']['Networks'].values()))['IPAddress'],'port':3306,'user':'root','password':env['MYSQL_ROOT_PASSWORD'],'database':schema,'pool_size':pool});cfg['logger']['console']=False;cfg['logger']['file']=str(private/(schema+'.log'))
  path=private/(schema+'.json');path.write_text(json.dumps(cfg));os.chmod(path,0o600)
  childenv=dict(os.environ);childenv['TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE']='1';childenv.pop('TINYIMX_PRIVATE_INSERT_FIRST_ENABLE',None)
  if mode_flags[mode] is not None:childenv['TINYIMX_PRIVATE_INSERT_FIRST_ENABLE']=mode_flags[mode]
  for k in ['TINYIMX_PERSIST_PHASE_TRACE_ENABLE','TINYIMX_STORAGE_WAIT_TRACE_ENABLE']:childenv.pop(k,None)
  targets=['message_outbox_integration_tests']+(['unread_snapshot_aggregate_tests','private_begin_insert_read_batch_tests'] if pool==1 else [])
  for target in targets:
   label=f'{target}-{mode}-pool{pool}';(d/(label+'-test-audit-before.json')).write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'schema':schema,'target':target,'flag':childenv.get('TINYIMX_PRIVATE_INSERT_FIRST_ENABLE'), 'begin_insert_read_batch_flag':childenv['TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE'],'pool':pool,'binary_sha256':info['binaries'][target],'production_mutations':False})+'\n')
   text=guarded_run([str(built/target),str(path)],label,90,childenv);checks[label]={'pass':text.count('[PASS]')+sum(v.startswith('PASS ') for v in text.splitlines()),'fail':text.count('[FAIL]')+sum(v.startswith('FAIL ') for v in text.splitlines())};assert checks[label]['pass']>0 and checks[label]['fail']==0
   (d/'completed-checks.json').write_text(json.dumps(checks,indent=2)+'\n');print(json.dumps({'completed':label,'checks':checks[label]}),flush=True)
  if pool==1 and mode in ['A1','B1','B2','A2']:
   target='private_insert_first_probe';label='cost-'+mode;output=d/(label+'-result.json');argv=[str(built/target),str(path),mode,'1' if mode_flags[mode]=='1' else '0',str(output)]
   (d/(label+'-audit-before.json')).write_text(json.dumps({'operation':'Actual sameELF220serial repository calls only in own schema; created/retry exactMID and conflicting body; SHOW SESSION counters pool1. Retain100 logical messages and100 outbox; no wire/capacity claim','schema':schema,'argv':argv,'binary_sha256':info['binaries'][target],'flag':mode_flags[mode],'performance_acceptance':False})+'\n')
   text=guarded_run(argv,label,90,childenv);cost=json.loads(output.read_text());assert cost['status']=='PRIVATE_INSERT_FIRST_REAL_SQL_COST_PASS' and cost['logical_messages']==cost['outbox_events']==100 and cost['conflicts_checked']==20
   print(json.dumps({'completed':label,'created':cost['created'],'reused':cost['reused'],'sql_counts':cost['sql_counts']}),flush=True)
  (d/(schema+'-durable-counts.tsv')).write_text(sql('SELECT COUNT(*) FROM `'+schema+'`.im_users;SELECT COUNT(*) FROM `'+schema+'`.im_private_messages;SELECT COUNT(*) FROM `'+schema+'`.im_event_outbox'))
  assert sql(durability).strip()=='1\t1\t1\t0\t0'
 x={'status':'PRIVATE_INSERT_FIRST_REAL_ISOLATED_SQL_PASS','head':head,'checks':checks,'total_native_checks':sum(x['pass'] for x in checks.values()),'cost_cases':{n:json.loads((d/('cost-'+n+'-result.json')).read_text()) for n in ['A1','B1','B2','A2']},'schemas_preserved':list(schemas.values()),'runtime_deploy':False,'production_row_or_DDL_mutation':False,'durability':'1/1/1/0/0 preserved','transport_commit_response_loss':'NOT_RUN','performance_acceptance':False}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({k:v for k,v in x.items() if k!='cost_cases'},indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e),'checks':checks,'total_native_checks':sum(x['pass'] for x in checks.values()),'cost_cases':{n:json.loads((d/('cost-'+n+'-result.json')).read_text()) for n in ['A1','B1','B2','A2'] if (d/('cost-'+n+'-result.json')).is_file()},'schemas_preserved':list(schemas.values())})+'\n');raise
finally:
 after=runtime();assert before==after;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
PY
