#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,shlex,shutil,os,signal,datetime,re,sys
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-fanout-pipeline-build-20261006';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
source=json.loads((b/'group-fanout-pipeline-source-20261006/summary.json').read_text());helper=source
assert run(['git','rev-parse','HEAD']).strip()==helper['head'] and all(sha(r/n)==h for n,h in source['files'].items()) and all(sha(r/n)==h for n,h in helper['files'].items())
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'accepted-group-fanout-wake-20261006/summary.json').read_text())['runtime']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
cache=r/'build/linux-release';accepted=b/'group-fanout-wake-build-20261006'
summary=json.loads((accepted/'summary.json').read_text());assert summary['gateway_image_id']=='sha256:5c2645b1e8512bdd3fe68d4fea229a9418a5e115c9d96d636871639dba41a405' and sha(accepted/'runtime-private/gateway_demo')==summary['gateway_elf_sha256']
# First preflight rejected duplicated static archive tokens before any build writes.
# Preserve both verified references in link order; require one unique accepted archive.
link= json.loads((accepted/'gateway-link-process.json').read_text())['argv'];assert not any('--wrap' in x for x in link)
main=[v for v in link if v.endswith('gateway_demo.cpp.o')];archive_refs=[v for v in link if v.endswith('libtinyimx_gateway.a')];archives=list(dict.fromkeys(archive_refs));cachelibs=[v for v in link if v.endswith('libtinyimx_cache_service.a')]
assert len(archive_refs)==2 and len(main)==len(archives)==len(cachelibs)==1 and pathlib.Path(archives[0])==accepted/'runtime-private/libtinyimx_gateway.a' and pathlib.Path(cachelibs[0])==b/'conversation-unread-batch-build-20261006-attempt2/runtime-private/libtinyimx_cache_service.a'
borrowed={str(accepted/'gateway-link-process.json'):sha(accepted/'gateway-link-process.json')}
for token in link:
 p=pathlib.Path(token) if pathlib.Path(token).is_absolute() else cache/token
 if p.is_file():borrowed[str(p.resolve())]=sha(p)
flagsfile=cache/'CMakeFiles/tinyimx_gateway.dir/flags.make';borrowed[str(flagsfile)]=sha(flagsfile)
flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(v for v in flagsfile.read_text().splitlines() if v.startswith(key+' =')).split('=',1)[1])
macro=subprocess.check_output(['git','show','30452a51d08561593a6aba2fbe1ef188ff06d1c7:common/logging/LogMacros.h']);assert hashlib.sha256(macro).hexdigest()=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
base='tinyimx/runtime:codex-group-commit-wake-gateway-v1-20261006';assert json.loads(run(['docker','image','inspect',base]))[0]['Id']==summary['gateway_image_id']
image='tinyimx/runtime:codex-group-dispatch-pipeline-v1-20261006';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
for member in ['GroupFanoutCoordinator.cpp.o']:assert run(['ar','t',archives[0]]).splitlines().count(member)==1
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Isolated1GroupFanoutCoordinator TU in accepted gateway/cache archive and exact acceptedmain/link; native OFF/ON/invalid strictflag protocol/concurrency +existinglease tests; newimage only','head':helper['head'],'source_head':source['head'],'source_sha256':source['files'],'borrowed_sha256':borrowed,'runtime_before':before,'base_image':summary['gateway_image_id'],'writes':'Ownobjects/copiedgatewayarchive/linkmaps/ELF/6wake+6pipeline nativecasesx3strict modes/ownimage andstoppedloader probes','eager_logging':'Original verifiedmacro overlay, no additional Redisdiag env enabled','preserve':'No CMake cache/originalarchives/main/acceptedimage overwritten; no deployment/data/config/host changes','rollback':'Keep allinput/object/failedartifacts; timeout closes onlyown PGID/startticks/argv verified children','performance_acceptance':False})
phase='initial'
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
 assert shutil.disk_usage(r).free>3*1024**3
def fail(k,e,t):save('failed.json',{'status':'FAIL','phase':phase,'type':k.__name__,'message':str(e),'deployment':False,'runtime':runtime()});sys.__excepthook__(k,e,t)
sys.excepthook=fail
def invoke(a,label,timeout=240,expected=0,env=None):
 global phase
 phase=label;resources();save(label+'-audit-before.json',{'argv':a,'timeout':timeout,'operation':'Only own compiler/link/native/probe child'})
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(a,cwd=cache,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
  proc=pathlib.Path('/proc')/str(p.pid);ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19];save(label+'-process.json',{'pid':p.pid,'start_ticks':ticks,'argv':a})
  try:code=p.wait(timeout=timeout)
  finally:
   if p.poll() is None:
    assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(a)]==[str(x).encode() for x in a]
    save(label+'-stop-audit-before.json',{'operation':'Stoponly verifiedownchildgroup','pid':p.pid});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed; privateownlog retained'
 print(json.dumps({'completed':label,'exit':code}),flush=True);return (private/(label+'.log')).read_text()
overlay=private/'original-includes/common/logging';overlay.mkdir(parents=True);(overlay/'LogMacros.h').write_bytes(macro)
def compilefile(n,member):
 obj=private/member
 invoke(['/usr/bin/c++','-I'+str(overlay.parents[1]),*flags,'-MD','-MF',str(obj)+'.d','-c',str(r/n),'-o',str(obj)],'compile-'+pathlib.Path(n).stem)
 return obj
objects={}
for n in ['gateway/GroupFanoutCoordinator.cpp']:objects[n]=compilefile(n,pathlib.Path(n).name+'.o')
archive=private/'libtinyimx_gateway.a';shutil.copy2(archives[0],archive)
for obj in objects.values():invoke(['ar','rcs',str(archive),str(obj)],'archive-'+obj.stem,90)
assert run(['ar','t',str(archive)]).splitlines()==run(['ar','t',archives[0]]).splitlines()
for obj in objects.values():assert str(r/'gateway/GroupFanoutWakeup.h') in pathlib.Path(str(obj)+'.d').read_text() and str(r/'gateway/GroupFanoutPipeline.h') in pathlib.Path(str(obj)+'.d').read_text() and str(overlay/'LogMacros.h') in pathlib.Path(str(obj)+'.d').read_text()
def linkfile(out,label,main_obj=None,extra=None):
 args=[str(archive) if v==archives[0] else str(main_obj) if main_obj and v==main[0] else v for v in link if not v.startswith('-Wl,-Map=')]
 args[args.index('-o')+1]=str(out)
 if extra:
  pos=args.index(str(main_obj))+1;args[pos:pos]=list(map(str,extra))
 args+=['-Wl,-Map='+str(out)+'.map'];invoke(args,label)
test=compilefile('benchmark/local_capacity/group_fanout_commit_wakeup_test.cpp','wakeup-test.cpp.o')
lease=compilefile('tests/gateway/group_fanout_coordinator_test.cpp','original-lease-test.cpp.o')
native=private/'group_fanout_wakeup_tests';linkfile(native,'native-link',test,[lease])
results=[]
flag='TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE'
for case,value,expected_enabled in [('OFF',None,False),('ON','1',True),('INVALID','true',False)]:
 env=dict(os.environ)
 if value is None:env.pop(flag,None)
 else:env[flag]=value
 log=invoke([str(native),'1' if expected_enabled else '0'],'native-'+case,60,env=env)
 parsed=[json.loads(v) for v in log.splitlines() if v.startswith('{')];assert len(parsed)==1 and parsed[0]=={'status':'GROUP_FANOUT_WAKE_NATIVE_PASS','enabled':expected_enabled,'tests':6,'failures':0} and '[FAIL]' not in log
 results.append({'case':case,**parsed[0]});save('completed-native-cases.json',results)
pipe_obj=compilefile('benchmark/local_capacity/group_fanout_pipeline_test.cpp','pipeline-test.cpp.o')
pipe_native=private/'group_fanout_pipeline_tests';linkfile(pipe_native,'pipeline-native-link',pipe_obj,[lease])
pipeline_results=[]
for case,value,enabled in [('OFF',None,False),('ON','1',True),('INVALID','true',False)]:
 env=dict(os.environ);env['TINYIMX_GROUP_FANOUT_PHASE_TRACE_ENABLE']='1';key='TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE'
 if value is None:env.pop(key,None)
 else:env[key]=value
 log=invoke([str(pipe_native),'1' if enabled else '0'],'pipeline-native-'+case,60,env=env)
 parsed=[json.loads(v) for v in log.splitlines() if v.startswith('{')]
 assert len(parsed)==1 and parsed[0]=={'status':'GROUP_FANOUT_PIPELINE_NATIVE_PASS','enabled':enabled,'tests':6,'failures':0} and '[FAIL]' not in log
 pipeline_results.append({'case':case,**parsed[0]});save('completed-pipeline-native-cases.json',pipeline_results)
exe=private/'gateway_demo';linkfile(exe,'gateway-link')
context=private/'image-context';context.mkdir();shutil.copy2(exe,context/'gateway_demo')
(context/'Dockerfile').write_text('FROM '+base+'\nCOPY --chmod=0555 gateway_demo /opt/tinyimx/bin/gateway_demo\nLABEL codex.tinyimx.group_dispatch_pipeline.revision="'+helper['head']+'"\n')
invoke(['docker','build','--network','none','--pull=false','-t',image,str(context)],'gateway-image',180)
iid=json.loads(run(['docker','image','inspect',image]))[0]['Id'];assert json.loads(run(['docker','image','inspect',base]))[0]['Id']==summary['gateway_image_id']
for name,argv,wanted in [('loader',['/usr/bin/ldd','-r','/opt/tinyimx/bin/gateway_demo'],0),('missing-config',['/opt/tinyimx/bin/gateway_demo','/__codex_group_wake_missing__.json'],1)]:
 own='codex-group-pipeline-'+name+'-20261006';assert subprocess.run(['docker','inspect',own],capture_output=True).returncode!=0
 args=['docker','run','--name',own,'--label','codex.tinyimx.group_dispatch_pipeline=20261006','--read-only','--network','none','--cap-drop','ALL','--security-opt','no-new-privileges:true','--user','1000:1000','--pids-limit','96','--memory','512m','--cpus','1','--tmpfs','/tmp:rw,noexec,nosuid,size=16m',iid,*argv]
 log=invoke(args,'image-'+name,60,wanted);c=json.loads(run(['docker','inspect',own]))[0]
 assert c['Image']==iid and not c['State']['Running'] and c['State']['ExitCode']==wanted and c['Config']['Labels'].get('codex.tinyimx.group_dispatch_pipeline')=='20261006'
 if name=='loader':assert 'not found' not in log and 'undefined symbol' not in log
assert runtime()==before and all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in source['files'].items())
x={'status':'GROUP_FANOUT_PIPELINE_NATIVE_AND_IMAGE_PASS','head':helper['head'],'source_head':source['head'],'native_cases':results,'native_tests_total':36,'pipeline_native_cases':pipeline_results,'pipeline_native_elf_sha256':sha(pipe_native),'native_elf_sha256':sha(native),'gateway_image_id':iid,'gateway_image_tag':image,'gateway_elf_sha256':sha(exe),'acceptedmain_andcache_unread_preserved':True,'runtime_configs_preserved':True,'borrowed_artifacts_preserved':True,'deployment':False,'performance_acceptance':False}
save('summary.json',x);print(json.dumps(x,indent=2))
PY
