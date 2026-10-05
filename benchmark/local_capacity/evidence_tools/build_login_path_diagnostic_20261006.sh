#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,shlex,shutil,re,os,signal,sys
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'login-path-diagnostic-build-20261006';assert not d.exists()
def run(a,timeout=25):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
source=json.loads((b/'login-path-diagnostic-source-20261006/summary.json').read_text());head=run(['git','rev-parse','HEAD']).strip();assert head==source['head']
assert all(sha(r/p)==h for p,h in source['files'].items())
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
 assert shutil.disk_usage(r).free>3*1024**3
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
resources();before=runtime()
prev=b/'lazy-logging-service-build-20261005-attempt3';pa=json.loads((prev/'audit-before.json').read_text());reuse=json.loads((prev/'eager-reuse-audit-before.json').read_text())['artifacts_sha256']
eager=b/'lazy-logging-service-build-20261005-attempt2/runtime-private/eager';assert eager.is_dir()
plan=pa['plans']['gateway/GatewayServer.cpp'];assert plan=={'target':'tinyimx_gateway','archive':'libtinyimx_gateway.a','member':'GatewayServer.cpp.o'}
cache=r/'build/linux-release';flagfile=cache/'CMakeFiles/tinyimx_gateway.dir/flags.make';linkfile=cache/'CMakeFiles/gateway_demo.dir/link.txt'
borrowed={}
for p in [flagfile,linkfile]:
 assert sha(p)==pa['borrowed_sha256'][str(p)];borrowed[str(p)]=sha(p)
flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in flagfile.read_text().splitlines() if x.startswith(key+' =')).split('=',1)[1])
link=shlex.split(linkfile.read_text());main_sources=[(i,k,v) for i,(k,v) in enumerate(sorted(pa['plans'].items())) if v.get('object_for')=='gateway_demo'];assert len(main_sources)==1
i,name,mp=main_sources[0];main=eager/(str(i)+'-'+pathlib.Path(name).name+'.o');assert sha(main)==reuse[str(main)];borrowed[str(main)]=sha(main)
archives={}
for token in link:
 if token.startswith('libtinyimx_') and token.endswith('.a'):
  p=eager/token
  if p.exists():assert sha(p)==reuse[str(p)];archives[token]=p;borrowed[str(p)]=sha(p)
 if token.endswith(('.a','.so','.o')) and token not in archives and (cache/token).is_file():
  p=(cache/token).resolve()
  if str(p) in pa['borrowed_sha256']:assert sha(p)==pa['borrowed_sha256'][str(p)]
  borrowed[str(p)]=sha(p)
assert plan['archive'] in archives and run(['ar','t',str(archives[plan['archive']])]).splitlines().count(plan['member'])==1
macro=run(['git','show','30452a51d08561593a6aba2fbe1ef188ff06d1c7:common/logging/LogMacros.h']).encode()
assert hashlib.sha256(macro).hexdigest()=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
sources=[r/'gateway/GatewayServer.cpp',r/'gateway/LoginRequestPhaseTrace.h',r/'benchmark/local_capacity/capacity_worker.cpp',r/'benchmark/local_capacity/login_phase_regression.cpp',r/'common/net/Buffer.cpp',r/'common/protocol/Packet.cpp',r/'common/protocol/ProtocolCodec.cpp']
inputs={str(p):sha(p) for p in sources};base_tag='tinyimx/runtime:codex-online-maintenance-gateway-v1';base_id='sha256:a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9'
assert json.loads(run(['docker','image','inspect',base_tag]))[0]['Id']==base_id
image='tinyimx/runtime:codex-login-path-gateway-v1-20261006';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'One changed Gateway TU and copied frozen eager archive; same original logging macro. Own standalone worker/regression; no cache/source/config/runtime overwrite','head':head,'source_sha256':inputs,'borrowed_sha256':borrowed,'runtime_before':before,'base_image':base_id,'writes':'Own objects/copied archive/ELFs and single image, stopped UID1000 loader probes','guards':'One compiler, >2GiB available, >3GiB disk, verified child PGID/startticks/argv timeout; no SDK installation','purpose':'DefaultOFF association diagnostics; not a performance candidate','rollback':'Original images/config/all19 services retained, no deletion/prune/reset'},indent=2)+'\n')
phase='initial'
def failure(k,e,t):
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','phase':phase,'type':k.__name__,'message':str(e),'deployment':False})+'\n');sys.__excepthook__(k,e,t)
sys.excepthook=failure
def invoke(argv,label,timeout=240,expected=0,cwd=None):
 global phase
 phase=label;resources();(d/(label+'-audit-before.json')).write_text(json.dumps({'argv':argv,'timeout_seconds':timeout,'writes':'Own stage only'})+'\n')
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(argv,cwd=cwd or r,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path('/proc')/str(p.pid);ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
  (d/(label+'-process.json')).write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'start_ticks':ticks,'argv':argv})+'\n')
  try:code=p.wait(timeout=timeout)
  finally:
   if p.poll() is None:
    assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(argv)]==[x.encode() for x in argv]
    (d/(label+'-stop-audit-before.json')).write_text(json.dumps({'operation':'Stop only exact owned child group','pid':p.pid,'start_ticks':ticks})+'\n');os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed; own log retained'
 print(json.dumps({'completed':label,'exit':code}),flush=True);return (private/(label+'.log')).read_text()
test=private/'login_phase_regression'
invoke(['/usr/bin/c++','-std=c++17','-O2','-I'+str(r),str(r/'benchmark/local_capacity/login_phase_regression.cpp'),'-o',str(test)],'trace-regression-compile')
text=invoke([str(test)],'trace-regression',30);checks=text.count('[PASS]');assert checks==16 and '[FAIL]' not in text
(d/'trace-regression.log').write_text(text)
worker=private/'tinyimx_capacity_worker_login_diag'
invoke(['/usr/bin/c++','-std=c++17','-O3','-DNDEBUG','-DBOOST_BIND_GLOBAL_PLACEHOLDERS','-I'+str(r),str(r/'benchmark/local_capacity/capacity_worker.cpp'),str(r/'common/net/Buffer.cpp'),str(r/'common/protocol/Packet.cpp'),str(r/'common/protocol/ProtocolCodec.cpp'),'-o',str(worker)],'worker-compile')
invoke([str(worker),'--help'],'worker-help',20)
overlay=private/'control-includes/common/logging';overlay.mkdir(parents=True);(overlay/'LogMacros.h').write_bytes(macro)
obj=private/'GatewayServer.cpp.o'
invoke(['/usr/bin/c++','-I'+str(overlay.parents[1]),*flags,'-MD','-MF',str(obj)+'.d','-c',str(r/'gateway/GatewayServer.cpp'),'-o',str(obj)],'gateway-compile')
deps=pathlib.Path(str(obj)+'.d').read_text();assert str(overlay/'LogMacros.h') in deps and str(r/'gateway/LoginRequestPhaseTrace.h') in deps
archive=private/plan['archive'];shutil.copy2(archives[plan['archive']],archive);invoke(['ar','rcs',str(archive),str(obj)],'gateway-own-archive')
assert run(['ar','t',str(archive)]).splitlines()==run(['ar','t',str(archives[plan['archive']])]).splitlines()
args=[str(archive) if x==plan['archive'] else str(archives[x]) if x in archives else str(main) if x==mp['old'] else x for x in link]
exe=private/'gateway_demo';args[args.index('-o')+1]=str(exe);args+=['-Wl,-Map='+str(private/'gateway_demo.map')];assert not any('--wrap' in x for x in args)
invoke(args,'gateway-link',240,cwd=cache)
context=private/'image-context';context.mkdir();shutil.copy2(exe,context/'gateway_demo');(context/'gateway_demo').chmod(0o755)
df=private/'Dockerfile';df.write_text('FROM '+base_tag+'\nCOPY --chmod=0755 gateway_demo /opt/tinyimx/bin/gateway_demo\nLABEL org.opencontainers.image.revision="'+head+'"\nLABEL tinyimx.binary.sha256="'+sha(exe)+'"\n')
invoke(['docker','build','--network=none','--pull=false','-f',str(df),'-t',image,str(context)],'image-build',120)
common=['docker','run','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges','--label','tinyimx.codex.task=login-path-diagnostic-20261006']
text=invoke(common+['--name','tinyimx-codex-login-path-loader-20261006','--entrypoint','/bin/sh',image,'-ec','ldd -r /opt/tinyimx/bin/gateway_demo'],'gateway-loader',30)
assert not any(x in text.lower() for x in ['not found','undefined symbol'])
invoke(common+['--name','tinyimx-codex-login-path-exec-20261006','--entrypoint','/opt/tinyimx/bin/gateway_demo',image,'/tmp/codex-owned-nonexistent-login-config-20261006.json'],'gateway-missing-config',30,expected=1)
assert runtime()==before and all(sha(path)==h for path,h in borrowed.items()) and all(sha(path)==h for path,h in inputs.items())
im=json.loads(run(['docker','image','inspect',image]))[0];assert im['Config']['Labels']['org.opencontainers.image.revision']==head and im['Config']['Labels']['tinyimx.binary.sha256']==sha(exe)
x={'status':'LOGIN_PATH_DIAGNOSTIC_BUILD_PASS','head':head,'trace_checks':checks,'gateway_image_tag':image,'gateway_image_id':im['Id'],'gateway_elf_sha256':sha(exe),'worker_path':str(worker),'worker_sha256':sha(worker),'trace_test_sha256':sha(test),'all19_runtime_configs_preserved':True,'deployment':False,'capacity_acceptance':False}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
