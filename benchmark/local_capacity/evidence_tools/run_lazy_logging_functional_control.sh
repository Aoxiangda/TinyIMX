#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export CODEX_LOGGING_PHASE="$1"
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
phase=os.environ['CODEX_LOGGING_PHASE'];assert phase in ['eager','lazy']
build=json.loads((b/'lazy-logging-service-build-20261005-attempt3/summary.json').read_text());assert build['status']=='LAZY_LOGGING_PAIRED_SERVICE_IMAGES_PASS' and build['unit_checks_per_variant']==130
entry=next(x for x in build['images'] if x['variant']==phase and x['target']=='message_service_demo');gateway=next(x for x in build['images'] if x['variant']==phase and x['target']=='gateway_demo')
image={'status':'PRIVATE_BATCH_FULL_MESSAGE_SEALED_IMAGE_PASS','image_id':entry['image_id'],'binary_sha256':entry['elf_sha256'],'head':entry['binary_build_revision']}
expected_image=image['image_id'];expected_binary=image['binary_sha256'];gateway_image=gateway['image_id']
before=runtime();original=json.loads((b/'lazy-logging-endpoint-controls-source-20261005/runtime-after.json').read_text());assert before['config_sha256']==original['config_sha256'];assert all(n in ['/tinyimx-m21-message-service-1','/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1'] or v==original['containers'][n] for n,v in before['containers'].items())
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


phase=os.environ['CODEX_LOGGING_PHASE'];assert phase in ['eager','lazy'];name='loggingfunc'+phase
d=b/('lazy-logging-functional-control-'+phase+'-20261005');assert not d.exists() and not (b/('cross-feature-'+name)).exists();d.mkdir();(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
try:
 target=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-message-service-1'],text=True))[0]
 assert target['Image']==expected_image and target['State']['Health']['Status']=='healthy'
 assert dict(x.split('=',1) for x in target['Config']['Env']).get('TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE')=='1'
 source=json.loads((b/'lazy-logging-endpoint-controls-source-20261005/summary.json').read_text());assert head==source['head']
 for role,exe,expected in [('gateway-a','gateway_demo',gateway),('gateway-b','gateway_demo',gateway),('message-service','message_service_demo',entry)]:
  c=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-'+role+'-1'],text=True))[0];assert c['Image']==expected['image_id'] and subprocess.check_output(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/'+exe],text=True).split()[0]==expected['elf_sha256']

 selected=guarded_run(['bash',str(r/'benchmark/local_capacity/evidence_tools/select_online_maintenance_actor_pairs_readonly.sh')],'select-actors-readonly',45)
 selection=json.loads(selected);assert selection['status']=='READONLY_FRESH_ACTOR_PAIRS_VERIFIED' and selection['writes'] is False
 users=selection['users'];(d/'selected-actors.json').write_text(json.dumps(selection,indent=2)+'\n')
 actor=r/'benchmark/local_capacity/cross_feature_actor.py'
 (d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Real functional chain with paired current-source eager/lazy Gateway andMessage; private SQL batch remainsON; freshly verified own offline nonadjacent empty socialpairs','head':head,'phase':phase,'users':users,'actor_sha256':hashlib.sha256(actor.read_bytes()).hexdigest(),'writes':'Normal own publicfriend/private/read/group/file chain, own newgroupdisband/uploadcancel, bytes/checksum; no SQL directwrite, fixture resets, config/system changes or otherapps closure','limit':'Functional samples only, not feature capacity, TLS/offline/MCP/AI/fault/soak certification'},indent=2)+'\n')
 guarded_run(['/usr/bin/python3',str(actor),'--run',name,'--users',*map(str,users)],'functional-actor',300)
 result=json.loads((b/('cross-feature-'+name)/'summary.json').read_text());assert result['status']=='PASS'
 x={'status':'LAZY_LOGGING_FUNCTIONAL_CHAIN_PASS','head':head,'phase':phase,'functional':result,'all19_configs_preserved':True,'performance_acceptance':False}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e)})+'\n');raise
finally:
 after=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
PY
