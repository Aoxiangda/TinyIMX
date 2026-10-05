#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,shlex,re,datetime,os,signal,shutil
r=pathlib.Path.cwd();b=r/'.local/codex';head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['git','merge-base','--is-ancestor','ed90d8aaf7cc7d8357fabb7c5f36e12f5626e747',head]).returncode==0
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
assert shutil.disk_usage(r).free>1024**3
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
 cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'private-batch-postcommit-fault-source-20261005/runtime-after.json').read_text())
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

d=b/'private-batch-postcommit-fault-20261005';assert not d.exists();d.mkdir()
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
checks={}
try:
 built=b/'private-batch-regression-build-20261005-sql-v3'
 info=json.loads((built/'summary.json').read_text());assert info['status']=='PRIVATE_BATCH_OWN_BUILD_UNIT_PASS'
 assert all(hashlib.sha256((built/n).read_bytes()).hexdigest()==h for n,h in info['binaries'].items())
 changes=subprocess.check_output(['git','diff','--name-only','64bee3465f34fa7ded8dc94bfba4cef0118a0009',head,'--','common','services','tests'],text=True).splitlines()
 assert changes==['tests/outbox/private_begin_insert_read_batch_test.cpp']
 link=json.loads((built/'link-explicit-fault-fixture-command.json').read_text())
 main=[p for p in link if p.endswith('/private_begin_insert_read_batch_tests.o')];assert len(main)==1
 borrowed={str((r/'build/linux-release'/p).resolve()):hashlib.sha256((r/'build/linux-release'/p).resolve().read_bytes()).hexdigest() for p in link if p.endswith(('.o','.a'))}
 def sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-postcommit-regression',q],text=True,timeout=20)
 durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'
 assert sql(durability).strip()=='1\t1\t1\t0\t0'
 tables=['im_users','im_private_messages','im_event_outbox']
 def production_ddl():return {t:re.sub(r'AUTO_INCREMENT=\d+','AUTO_INCREMENT=<counter>',sql('SHOW CREATE TABLE `'+t+'`')) for t in tables}
 ddl_before=production_ddl()
 schemas={mode:'codex_private_batch_20261005_v3_'+mode+'_p1' for mode in ['off','on']}
 def counts(schema):return [int(x) for x in sql('SELECT COUNT(*) FROM `'+schema+'`.im_private_messages;SELECT COUNT(*) FROM `'+schema+'`.im_event_outbox').splitlines()]
 old_counts={mode:counts(schema) for mode,schema in schemas.items()}
 (d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Own native ELF real commit then once-only synthetic failure and own TCP shutdown; no production fault hooks','head':head,'borrowed_sha256':borrowed,'owned_schemas':schemas,'counts_before':old_counts,'production_ddl_before':ddl_before,'durability_before':sql(durability).strip(),'mutations':'Two new own durable message/outbox pairs expected; fresh objects/ELF/config/logs only. No runtime/cache/production changes or deletes','limit':'mysql_commit success consumed before failure; not real in-flight wire response loss or RPC chaos/performance proof'},indent=2)+'\n')
 flags_text=(built/'outbox-flags.make').read_text();flags=[]
 for k in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in flags_text.splitlines() if x.startswith(k+' =')).split('=',1)[1])
 (d/'outbox-flags.make').write_text(flags_text)
 mainown=d/'private_begin_insert_read_batch_tests.o';wrap=d/'mysql_owned_postcommit_fault.o'
 for src,obj,label in [(r/'tests/outbox/private_begin_insert_read_batch_test.cpp',mainown,'compile-main'),(r/'benchmark/local_capacity/mysql_owned_postcommit_fault.cpp',wrap,'compile-native-wrapper')]:
  guarded_run(['/usr/bin/c++',*flags,'-c',str(src),'-o',str(obj)],label,180)
 link[link.index(main[0])]=str(mainown);link[link.index('-o')+1]=str(d/'private_begin_insert_read_batch_tests')
 firstlib=next(i for i,x in enumerate(link) if x.endswith('.a'))
 link[firstlib:firstlib]=[str(wrap),'-Wl,--wrap=mysql_commit']
 guarded_run(link,'link-native-wrapper',180)
 private=d/'runtime-private';private.mkdir();os.chmod(private,0o700)
 for mode,schema in schemas.items():
  cfg=json.loads((b/'private-batch-isolated-mysql-20261005-v3/runtime-private'/(schema+'.json')).read_text())
  assert cfg['mysql']['database']==schema and cfg['mysql']['pool_size']==1 and cfg['mysql']['port']==3306
  cfg['logger']['console']=False;cfg['logger']['file']=str(private/(schema+'.log'))
  cfgpath=private/(schema+'.json');cfgpath.write_text(json.dumps(cfg));os.chmod(cfgpath,0o600)
  env=dict(os.environ);env['TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE']='1' if mode=='on' else '0';env['TINYIMX_TEST_OWN_POSTCOMMIT_FAILURE']='1';env['TINYIMX_TEST_OWN_MYSQL_IPV4']=cfg['mysql']['host']
  for k in ['TINYIMX_PERSIST_PHASE_TRACE_ENABLE','TINYIMX_STORAGE_WAIT_TRACE_ENABLE']:env.pop(k,None)
  label='postcommit-'+mode+'-pool1'
  (d/(label+'-audit-before.json')).write_text(json.dumps({'schema':schema,'pool':1,'gate':env['TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE'],'owned_connection_fault':True,'counts_before':old_counts[mode],'new_private_log':True})+'\n')
  text=guarded_run([str(d/'private_begin_insert_read_batch_tests'),str(cfgpath),'post-commit-fault'],label,90,env)
  checks[mode]={'pass':text.count('[PASS]'),'fail':text.count('[FAIL]'),'observed_faults':text.count('[OBSERVE] OWN_POSTCOMMIT_FAULT real_commit_success=1 own_socket_shutdown=1 failure_return=1')}
  assert checks[mode]=={'pass':4,'fail':0,'observed_faults':1}
  now=counts(schema);assert now==[x+1 for x in old_counts[mode]]
  checks[mode]['counts_after']=now
  (d/'completed-checks.json').write_text(json.dumps(checks,indent=2)+'\n')
 assert production_ddl()==ddl_before and sql(durability).strip()=='1\t1\t1\t0\t0'
 assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h for p,h in borrowed.items())
 x={'status':'PRIVATE_BATCH_OWN_POSTCOMMIT_RECOVERY_PASS','head':head,'checks':checks,'production_metadata_preserved':True,'borrowed_artifacts_preserved':True,'binary_sha256':hashlib.sha256((d/'private_begin_insert_read_batch_tests').read_bytes()).hexdigest(),'durability':'1/1/1/0/0','runtime_deploy':False,'transport_response_loss':'NOT_TESTED: real commit returns success inside wrapper before own connection fault and synthetic failure','performance_acceptance':False}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e),'checks':checks})+'\n');raise
finally:
 after=runtime();assert before==after;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
PY
