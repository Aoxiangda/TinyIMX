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
before=runtime();assert before==json.loads((b/'private-batch-regression-attempt3-source-20261005/runtime-after.json').read_text())
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
d=b/'private-batch-regression-build-20261005-attempt3';assert not d.exists();d.mkdir()
build=r/'build/linux-release';cm=build/'CMakeFiles';objects={};checks={};borrowed={}
prior=b/'private-batch-regression-build-20261005-attempt2';assert json.loads((prior/'failed.json').read_text())['type']=='StopIteration'
assert not subprocess.check_output(['git','diff','--name-only','9be7f8a1055ad2b862f566599f290e680eae528b',head,'--','common','services','tests'],text=True).strip(), 'Do not reuse across code/header changes'
reused={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in prior.glob('*.o')};assert all(k in reused for k in ['MySqlConnection.o','MessageRepository.o','MessageRepositoryAdapter.o','MessageApplicationService.o'])
(d/'reused-own-object-audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_stage':str(prior),'compiled_head':'9be7f8a1055ad2b862f566599f290e680eae528b','code_headers_unchanged':True,'objects_sha256':reused,'effects':'Readonly use of earlier successfully compiled own objects, no source/cached/original artifact mutation'},indent=2)+'\n')
units=['private_persistence_trace_tests','message_application_service_tests','m16_crash_window_contract_tests','private_receiver_ack_tests','private_chat_ack_boundary_tests']
targets=units+['message_outbox_integration_tests','unread_snapshot_aggregate_tests','private_begin_insert_read_batch_tests']
sources=[('MySqlConnection','common/db/MySqlConnection.cpp','tinyimx_db'),('MessageRepository','services/repository/MessageRepository.cpp','tinyimx_repository'),('MessageRepositoryAdapter','services/message/repository/MessageRepositoryAdapter.cpp','tinyimx_message_core'),('MessageApplicationService','services/message/application/MessageApplicationService.cpp','tinyimx_message_core')]
def flags(target):
 p=cm/(target+'.dir')/'flags.make';text=p.read_text();(d/(target+'-flags.make')).write_text(text)
 return sum([shlex.split(next(x for x in text.splitlines() if x.startswith(k+' =')).split('=',1)[1]) for k in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']],[])
base=shlex.split((cm/'message_outbox_integration_tests.dir/link.txt').read_text());first=next(i for i,x in enumerate(base) if x.startswith('libtinyimx_'));base_libraries=base[first:]
(d/'preflight-audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Plan exact main plus retained extra cached objects before compilation','head':head,'targets':targets,'effects':'Only fresh link snapshots/audit in newattempt2, no cache/runtime/SQL mutation','fix':'Select only matching target.dir/tests/main, retain and hash all additional objects','runtime_before':before},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
try:
 plans=[]
 for target in targets:
  template='message_outbox_integration_tests' if target=='private_begin_insert_read_batch_tests' else target
  link=shlex.split((cm/(template+'.dir')/'link.txt').read_text());(d/(target+'-cached-link.txt')).write_text(' '.join(link)+'\n')
  old=[x for x in link if x.startswith('CMakeFiles/'+template+'.dir/tests/') and x.endswith('.cpp.o')];assert len(old)==1,'Exact test main missing or ambiguous'
  source='tests/outbox/private_begin_insert_read_batch_test.cpp' if target=='private_begin_insert_read_batch_tests' else old[0].split('.dir/',1)[1][:-2]
  plans.append((target,template,link,old[0],source))
  for token in link+base_libraries:
   if token.endswith('.a') or (token.endswith('.o') and token!=old[0]):
    p=(build/token).resolve();borrowed[str(p)]=hashlib.sha256(p.read_bytes()).hexdigest()
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','phase':'link-planning-before-compiler','type':type(e).__name__,'message':str(e),'SQL_started':False})+'\n');assert before==runtime();raise
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Compile affected sources and eight test executables into fresh own stage only','head':head,'source_objects':sources,'targets':targets,'borrowed_sha256':borrowed,'effects':'No CMake configuration/SDK/cache object/archive/ELF overwrite, one compiler max180s per call, own changed objects before all cached archives','runtime_before':before,'SQL':'None in build; real schemas separately audited','rollback':'Keep all artifacts/logs and source history; only own guarded process stop'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
try:
 for name,source,target in sources:
  p=prior/(name+'.o');assert hashlib.sha256(p.read_bytes()).hexdigest()==reused[p.name];objects[name]=p
 for target,template,link,old,source in plans:
  p=prior/(target+'.o')
  if p.exists():assert hashlib.sha256(p.read_bytes()).hexdigest()==reused[p.name]
  else:
   p=d/(target+'.o');guarded_run(['/usr/bin/c++',*flags(template),'-c',str(r/source),'-o',str(p)],'compile-'+target,180)
  link[link.index(old)]=str(p);link[link.index('-o')+1]=str(d/target)
  i=next((j for j,x in enumerate(link) if x.startswith('libtinyimx_')),len(link));link[i:i]=[str(x) for x in objects.values()]
  # Changed objects introduce DB/outbox references even into header-only tests.
  link+=base_libraries;guarded_run(link,'link-'+target,180)
 env=dict(os.environ)
 for key in ['TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE','TINYIMX_PERSIST_PHASE_TRACE_ENABLE','TINYIMX_STORAGE_WAIT_TRACE_ENABLE']:env.pop(key,None)
 for name in units:
  text=guarded_run([str(d/name)],name,60,env);checks[name]={'pass':text.count('[PASS]')+sum(x.startswith('PASS ') for x in text.splitlines()),'fail':text.count('[FAIL]')+sum(x.startswith('FAIL ') for x in text.splitlines())};assert checks[name]['pass']>0 and checks[name]['fail']==0
 assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h for p,h in borrowed.items())
 assert all(hashlib.sha256((prior/p).read_bytes()).hexdigest()==h for p,h in reused.items())
 binaries={x:hashlib.sha256((d/x).read_bytes()).hexdigest() for x in targets}
 x={'status':'PRIVATE_BATCH_OWN_BUILD_UNIT_PASS','head':head,'candidate_code_head':'ed90d8aaf7cc7d8357fabb7c5f36e12f5626e747','binaries':binaries,'own_objects':{k:hashlib.sha256(v.read_bytes()).hexdigest() for k,v in objects.items()},'checks':checks,'borrowed_libraries_unchanged':True,'runtime_deploy':False,'real_SQL_regression':'NOT_RUN','commit_response_fault':'NOT_RUN'}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e),'checks':checks})+'\n');raise
finally:
 after=runtime();assert before==after;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
PY
