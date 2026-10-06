#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,shlex,shutil,re,os,signal,sys
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'redis-acquire-phase-build-20261006-attempt2';assert not d.exists()
def run(a,timeout=25):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
source=json.loads((b/'redis-acquire-build-repair-source-20261006/summary.json').read_text());head=run(['git','rev-parse','HEAD']).strip();assert head==source['head']
assert all(sha(r/p)==h for p,h in source['files'].items())
phase_source=json.loads((b/'redis-acquire-phase-source-20261006/summary.json').read_text());assert all(sha(r/p)==h for p,h in phase_source['files'].items())
prior=b/'conversation-unread-batch-build-20261006-attempt2'
prior_build=json.loads((prior/'summary.json').read_text());prior_audit=json.loads((prior/'audit-before.json').read_text())
assert prior_build['status']=='CONVERSATION_UNREAD_BATCH_BUILD_PASS' and sha(prior/'runtime-private/gateway_demo')==prior_build['gateway_elf_sha256']
assert sha(r/'services/cache/UnreadCountCache.cpp')==prior_audit['source_sha256'][str(r/'services/cache/UnreadCountCache.cpp')]
assert json.loads((b/'conversation-unread-batch-api-20261006/summary.json').read_text())['checks']==248
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
# Borrow only our already accepted unread API member; no static library overwrite.
api_archive=prior/'runtime-private/libtinyimx_cache_service.a';api_obj=prior/'runtime-private/UnreadCountCache.cpp.o'
assert run(['ar','t',str(api_archive)]).splitlines().count('UnreadCountCache.cpp.o')==1
assert hashlib.sha256(subprocess.check_output(['ar','p',str(api_archive),'UnreadCountCache.cpp.o'])).hexdigest()==sha(api_obj)
archives['libtinyimx_cache_service.a']=api_archive;borrowed[str(api_archive)]=sha(api_archive);borrowed[str(api_obj)]=sha(api_obj)
original_native=b/'conversation-unread-batch-api-20261006'
original_test=r/'benchmark/local_capacity/conversation_unread_batch_test.cpp'
for p in [original_test,original_native/'api.o',cache/'CMakeFiles/redis_pool_demo.dir/flags.make',cache/'CMakeFiles/redis_pool_demo.dir/link.txt',cache/'CMakeFiles/tinyimx_cache.dir/flags.make']:
 borrowed[str(p)]=sha(p)
macro=run(['git','show','30452a51d08561593a6aba2fbe1ef188ff06d1c7:common/logging/LogMacros.h']).encode()
assert hashlib.sha256(macro).hexdigest()=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
sources=[r/'gateway/GatewayServer.cpp',r/'gateway/ChatRequestPhaseTrace.h',r/'gateway/LoginRequestPhaseTrace.h',r/'common/cache/RedisConnectionPool.cpp',r/'common/cache/RedisAcquirePhaseTrace.h',r/'common/cache/RedisConnectionPool.h',r/'services/cache/UnreadCountCache.h',r/'services/cache/UnreadCountCache.cpp']
inputs={str(p):sha(p) for p in sources};base_tag=prior_build['gateway_image_tag'];base_id=prior_build['gateway_image_id']
assert json.loads(run(['docker','image','inspect',base_tag]))[0]['Id']==base_id
image='tinyimx/runtime:codex-redis-acquire-phase-v2-20261006';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'DefaultOFF RedisAcquire andGatewaytrace TUs copiedfrozen controls; originalacceptedunread API, nativeOFF/ON496checks beforeownELF/image; no runtime overwrite','head':head,'source_sha256':inputs,'borrowed_sha256':borrowed,'runtime_before':before,'base_image':base_id,'writes':'Own objects/copied archive/ELFs and single image, stopped UID1000 loader probes','guards':'One compiler, >2GiB available, >3GiB disk, verified child PGID/startticks/argv timeout; no SDK installation','purpose':'Boundednumeric pool/Gatewayphase diagnosis only; no performanceacceptance or productiondeployment','rollback':'Original images/config/all19 services retained, no deletion/prune/reset'},indent=2)+'\n')
phase='initial'
def failure(k,e,t):
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','phase':phase,'type':k.__name__,'message':str(e),'deployment':False})+'\n');sys.__excepthook__(k,e,t)
sys.excepthook=failure
def invoke(argv,label,timeout=240,expected=0,cwd=None,env=None):
 global phase
 phase=label;resources();(d/(label+'-audit-before.json')).write_text(json.dumps({'argv':argv,'timeout_seconds':timeout,'writes':'Own stage only'})+'\n')
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(argv,cwd=cwd or r,stdout=f,stderr=subprocess.STDOUT,start_new_session=True,env=env);proc=pathlib.Path('/proc')/str(p.pid);ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
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
overlay=private/'control-includes/common/logging';overlay.mkdir(parents=True);(overlay/'LogMacros.h').write_bytes(macro)
cache_flags_path=cache/'CMakeFiles/tinyimx_cache.dir/flags.make'
cache_flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:cache_flags+=shlex.split(next(x for x in cache_flags_path.read_text().splitlines() if x.startswith(key+' =')).split('=',1)[1])
(d/'cache-api-compile-input-audit-before.json').write_text(json.dumps({'cache_flags_sha256':sha(cache_flags_path),'source_sha256':sha(r/'common/cache/RedisConnectionPool.cpp'),'original_macro_sha256':sha(overlay/'LogMacros.h'),'write':'Own cache object/archive only, all sources/libraries remain exact'})+'\n')
cache_obj=private/'RedisConnectionPool.cpp.o'
invoke(['/usr/bin/c++','-I'+str(overlay.parents[1]),*cache_flags,'-MD','-MF',str(cache_obj)+'.d','-c',str(r/'common/cache/RedisConnectionPool.cpp'),'-o',str(cache_obj)],'cache-api-eager-compile')
assert str(overlay/'LogMacros.h') in pathlib.Path(str(cache_obj)+'.d').read_text()
cache_archive=private/'libtinyimx_cache.a';cache_original=archives.get('libtinyimx_cache.a',cache/'libtinyimx_cache.a')
assert run(['ar','t',str(cache_original)]).splitlines().count('RedisConnectionPool.cpp.o')==1
shutil.copy2(cache_original,cache_archive);invoke(['ar','rcs',str(cache_archive),str(cache_obj)],'cache-own-archive')
assert run(['ar','t',str(cache_archive)]).splitlines()==run(['ar','t',str(cache_original)]).splitlines()
archives['libtinyimx_cache.a']=cache_archive

# Reuse all original real Redis assertions. Only own output-root and namespace
# change, explicitly keyed by off/on. No skipped checks or timeout changes.
nt=original_test.read_text();nt='#include <cstdlib>\n'+nt
old='const t::fs::path root="/home/jackson7/projects/TinyIMX_publish/.local/codex/conversation-unread-batch-api-20261006";'
new='const std::string testcase=std::getenv("CODEX_REDIS_PHASE_TEST_CASE")?std::getenv("CODEX_REDIS_PHASE_TEST_CASE"):"";if(testcase!="off"&&testcase!="on")throw std::runtime_error("OwnTraceCase");const t::fs::path root=t::fs::path("/home/jackson7/projects/TinyIMX_publish/.local/codex/redis-acquire-phase-build-20261006-attempt2")/testcase;'
assert nt.count(old)==1;nt=nt.replace(old,new)
old='"codex:conversation-unread-batch-20261006:"+mode+":"';new='"codex:redis-acquire-phase-20261006:"+testcase+":"+mode+":"'
assert nt.count(old)==1;nt=nt.replace(old,new)
# Keep the original owned-only guard, adapting its exact namespace consistently.
# First attempt stopped at NonownedPrefix before any Redis fixture writes.
assert nt.count('codex:conversation-unread-batch-20261006:')==1
nt=nt.replace('codex:conversation-unread-batch-20261006:','codex:redis-acquire-phase-20261006:')
assert 'NonownedPrefix' in nt and 'codex:conversation-unread-batch-20261006:' not in nt
testcpp=private/'owned_conformance.cpp';testcpp.write_text(nt)
(d/'native-source-audit-before.json').write_text(json.dumps({'operation':'Onlyoutputpath/ownnamespace variants oforiginal124checksemantics perpool, oneELF bothflagmodes','original_test_sha256':sha(original_test),'variant_test_sha256':sha(testcpp),'normal_assertions_removed':False})+'\n')
nf=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:nf+=shlex.split(next(x for x in (cache/'CMakeFiles/redis_pool_demo.dir/flags.make').read_text().splitlines() if x.startswith(key+' =')).split('=',1)[1])
testobj=private/'owned_conformance.o'
invoke(['/usr/bin/c++','-I'+str(overlay.parents[1]),*nf,'-c',str(testcpp),'-o',str(testobj)],'native-test-compile')
nl=shlex.split((cache/'CMakeFiles/redis_pool_demo.dir/link.txt').read_text());oldmain='CMakeFiles/redis_pool_demo.dir/examples/redis_pool_demo.cpp.o';assert nl.count(oldmain)==1
nl=[str(testobj) if x==oldmain else str(archives[x]) if x in archives else x for x in nl];nl.insert(1,str(original_native/'api.o'));nbin=private/'owned_conformance';nl[nl.index('-o')+1]=str(nbin);nl+=['-Wl,-Map='+str(private/'native.map')]
invoke(nl,'native-link',cwd=cache)
redis=json.loads((b/'all-feature-optimization-preflight-20261006-attempt2/summary.json').read_text());assert redis['redis_cluster_enabled']==0
native_cases=[]
for enabled in ['off','on']:
 target=d/enabled;target.mkdir(mode=0o700)
 for pool in ['p1','p4']:
  case=target/pool;case.mkdir(mode=0o700);env={**os.environ,'CODEX_REDIS_PHASE_TEST_CASE':enabled,'TINYIMX_REDIS_ACQUIRE_TRACE_ENABLE':'1' if enabled=='on' else '0'}
  data=invoke([str(nbin),pool,'/home/jackson7/.local/share/tinyimx/m21/config',redis['native_redis_ip'],str(case)],'native-'+enabled+'-'+pool,120,cwd=r,env=env)
  result=json.loads((case/'result.json').read_text());checks=json.loads((case/'checks.json').read_text());assert result['status']=='CONVERSATION_UNREAD_BATCH_API_PASS' and result['checks']==len(checks)==124 and all(x['pass'] for x in checks)
  count=data.count('redis_acquire_phase ')
  assert (count>0) if enabled=='on' else (count==0)
  native_cases.append({'mode':enabled,'pool':pool,'checks':124,'status':'PASS','numeric_acquire_samples':count})
(d/'native-results.json').write_text(json.dumps({'checks':496,'cases':native_cases,'same_native_elf_sha256':sha(nbin),'healthy_and_all_original_semantics_preserved':True,'performance_acceptance':False},indent=2)+'\n')

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
common=['docker','run','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges','--label','tinyimx.codex.task=redis-acquire-phase-20261006']
text=invoke(common+['--name','tinyimx-codex-redis-acquire-loader-20261006-attempt2','--entrypoint','/bin/sh',image,'-ec','ldd -r /opt/tinyimx/bin/gateway_demo'],'gateway-loader',30)
assert not any(x in text.lower() for x in ['not found','undefined symbol'])
invoke(common+['--name','tinyimx-codex-redis-acquire-exec-20261006-attempt2','--entrypoint','/opt/tinyimx/bin/gateway_demo',image,'/tmp/codex-owned-nonexistent-login-config-20261006.json'],'gateway-missing-config',30,expected=1)
assert runtime()==before and all(sha(path)==h for path,h in borrowed.items()) and all(sha(path)==h for path,h in inputs.items())
im=json.loads(run(['docker','image','inspect',image]))[0];assert im['Config']['Labels']['org.opencontainers.image.revision']==head and im['Config']['Labels']['tinyimx.binary.sha256']==sha(exe)
x={'status':'REDIS_ACQUIRE_PHASE_BUILD_PASS','real_redis_native_checks':496,'trace_modes_tested':['off','on'],'head':head,'gateway_image_tag':image,'gateway_image_id':im['Id'],'gateway_elf_sha256':sha(exe),'all19_runtime_configs_preserved':True,'deployment':False,'capacity_acceptance':False}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
