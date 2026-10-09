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
before=runtime();assert before==json.loads((b/'private-batch-confirm-state-fix-source-20261005/runtime-after.json').read_text())
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
d=b/'private-batch-regression-build-20261005-confirm-fix';assert not d.exists()
prior=b/'private-batch-regression-build-20261005-attempt3';final=b/'private-batch-regression-build-20261005-final';product=b/'private-batch-regression-build-20261005-attempt2'
assert json.loads((final/'failed.json').read_text())['message']=='message_application_service_tests failed; exact logs and partial artifacts retained'
changes=subprocess.check_output(['git','diff','--name-only','50346bef4494961926b674753f85f0cdd2c0287a',head,'--','common','services','tests'],text=True).splitlines();assert changes==['services/message/application/MessageApplicationService.cpp']
units=['private_persistence_trace_tests','message_application_service_tests','m16_crash_window_contract_tests','private_receiver_ack_tests','private_chat_ack_boundary_tests']
targets=units+['message_outbox_integration_tests','unread_snapshot_aggregate_tests','private_begin_insert_read_batch_tests'];plans={};borrowed={}
for n in targets:
 path=final/'link-new-test-command.json' if n=='private_begin_insert_read_batch_tests' else prior/('link-'+n+'-command.json')
 link=json.loads(path.read_text());assert link.count(str(product/'MessageApplicationService.o'))==1;plans[n]=link
 for token in link:
  if token.endswith('.o') or token.endswith('.a'):
   p=(r/'build/linux-release'/token).resolve();borrowed[str(p)]=hashlib.sha256(p.read_bytes()).hexdigest()
previous=json.loads((prior/'audit-before.json').read_text())['borrowed_sha256'];assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h for p,h in previous.items())
d.mkdir();checks={};(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Compile one corrected App object and relink eight regressions preserving three product objects/all mains/libraries','head':head,'changes_since503':changes,'borrowed_sha256':borrowed,'product_persist_code_head':'ed90d8aaf7cc7d8357fabb7c5f36e12f5626e747','fix':'Default invalid delivery enum returns InvalidRecord before mutation, preserve original read-first confirmation; two failedunit assertions one missingdefault cause','runtime_before':before,'SQL':'None until separate own schema audit','effects':'Fresh one App.o/eight ELFs only; no overwrite/delete/cache/download/runtime change'},indent=2)+'\n');(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
try:
 flags_text=(product/'tinyimx_message_core-flags.make').read_text();flags=[]
 for k in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in flags_text.splitlines() if x.startswith(k+' =')).split('=',1)[1])
 (d/'app-flags.make').write_text(flags_text);app=d/'MessageApplicationService.o'
 guarded_run(['/usr/bin/c++',*flags,'-c',str(r/'services/message/application/MessageApplicationService.cpp'),'-o',str(app)],'compile-corrected-App',180)
 for n,link in plans.items():
  link[link.index(str(product/'MessageApplicationService.o'))]=str(app);link[link.index('-o')+1]=str(d/n);guarded_run(link,'link-'+n,180)
 env=dict(os.environ)
 for k in ['TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE','TINYIMX_PERSIST_PHASE_TRACE_ENABLE','TINYIMX_STORAGE_WAIT_TRACE_ENABLE']:env.pop(k,None)
 for n in units:
  text=guarded_run([str(d/n)],n,60,env);checks[n]={'pass':text.count('[PASS]')+sum(v.startswith('PASS ') for v in text.splitlines()),'fail':text.count('[FAIL]')+sum(v.startswith('FAIL ') for v in text.splitlines())};assert checks[n]['pass']>0 and checks[n]['fail']==0
  (d/'completed-unit-checks.json').write_text(json.dumps(checks,indent=2)+'\n')
 assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h for p,h in borrowed.items())
 binaries={n:hashlib.sha256((d/n).read_bytes()).hexdigest() for n in targets}
 x={'status':'PRIVATE_BATCH_OWN_BUILD_UNIT_PASS','head':head,'persist_code_head':'ed90d8aaf7cc7d8357fabb7c5f36e12f5626e747','confirm_state_fix_head':head,'binaries':binaries,'checks':checks,'prior_objects_mains_libraries_unchanged':True,'runtime_deploy':False,'real_SQL_regression':'NOT_RUN','commit_response_fault':'NOT_RUN'}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e),'checks':checks})+'\n');raise
finally:
 after=runtime();assert before==after;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
PY
