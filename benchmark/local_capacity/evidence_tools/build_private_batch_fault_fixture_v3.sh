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
before=runtime();assert before==json.loads((b/'private-batch-fault-fixture-v3-source-20261005/runtime-after.json').read_text())
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
d=b/'private-batch-regression-build-20261005-sql-v3';assert not d.exists()
prior=b/'private-batch-regression-build-20261005-sql-v2';compiled=b/'private-batch-regression-build-20261005-attempt3'
info=json.loads((prior/'summary.json').read_text());assert info['status']=='PRIVATE_BATCH_OWN_BUILD_UNIT_PASS' and sum(v['pass'] for v in info['checks'].values())==130 and all(v['fail']==0 for v in info['checks'].values())
changes=subprocess.check_output(['git','diff','--name-only','dc36f63dabbf76f692d6652a423bd575069eca22',head,'--','common','services','tests'],text=True).splitlines();assert changes==['tests/outbox/private_begin_insert_read_batch_test.cpp']
assert all(hashlib.sha256((prior/n).read_bytes()).hexdigest()==h for n,h in info['binaries'].items())
link=json.loads((b/'private-batch-regression-build-20261005-confirm-fix/link-private_begin_insert_read_batch_tests-command.json').read_text());main=[p for p in link if p.endswith('/private_begin_insert_read_batch_tests.o')];assert len(main)==1
borrowed={}
for token in link:
 if token.endswith('.o') or token.endswith('.a'):
  p=(r/'build/linux-release'/token).resolve();borrowed[str(p)]=hashlib.sha256(p.read_bytes()).hexdigest()
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Fix failed selfLIKE temporary shadow with explicit columns and failfast; compile one fault main and copy seven sealed tests','head':head,'product_code':'Persist ed90, Confirm unknown-state b32 unchanged','changes_sincedc36':changes,'prior_ELF_sha256':info['binaries'],'borrowed_sha256':borrowed,'unit130PASS_reuse':'All unit sources/headers/product objects unchanged, actual prior completed logs retained','effects':'New own one main/ELF/copies only; no source product/cache/runtime mutation'},indent=2)+'\n');(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
try:
 for n,h in info['binaries'].items():
  if n!='private_begin_insert_read_batch_tests':shutil.copy2(prior/n,d/n);assert hashlib.sha256((d/n).read_bytes()).hexdigest()==h
 flags_text=(compiled/'message_outbox_integration_tests-flags.make').read_text();flags=[]
 for k in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in flags_text.splitlines() if x.startswith(k+' =')).split('=',1)[1])
 (d/'outbox-flags.make').write_text(flags_text);own=d/'private_begin_insert_read_batch_tests.o'
 guarded_run(['/usr/bin/c++',*flags,'-c',str(r/'tests/outbox/private_begin_insert_read_batch_test.cpp'),'-o',str(own)],'compile-explicit-fault-fixture',180)
 link[link.index(main[0])]=str(own);link[link.index('-o')+1]=str(d/'private_begin_insert_read_batch_tests');guarded_run(link,'link-explicit-fault-fixture',180)
 assert all(hashlib.sha256((prior/n).read_bytes()).hexdigest()==h for n,h in info['binaries'].items())
 assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h for p,h in borrowed.items())
 x={'status':'PRIVATE_BATCH_OWN_BUILD_UNIT_PASS','head':head,'persist_code_head':'ed90d8aaf7cc7d8357fabb7c5f36e12f5626e747','confirm_state_fix_head':'b32cae49f45e7b1b780cc3b3411c5cb8f4de9116','binaries':{n:hashlib.sha256((d/n).read_bytes()).hexdigest() for n in info['binaries']},'checks':info['checks'],'unit_results_reused_from_unchanged_sealed_binaries':True,'prior_artifacts_unchanged':True,'runtime_deploy':False,'real_SQL_regression':'NOT_RUN','commit_response_fault':'NOT_RUN'}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e)})+'\n');raise
finally:
 after=runtime();assert before==after;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
PY
