#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,shlex,shutil,os,signal,re,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-partial-drain-build-20261006';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
source=json.loads((b/'group-partial-drain-source-20261006/summary.json').read_text());helper=json.loads((b/'group-partial-drain-build-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==helper['head']
assert all(sha(r/n)==h for n,h in source['files'].items()) and all(sha(r/n)==h for n,h in helper['files'].items())
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'group-claim-batch-control-20261006/restore-summary.json').read_text())['runtime']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
accepted=b/'group-delivery-ordering-build-20261006-attempt3';info=json.loads((accepted/'summary.json').read_text());privateold=accepted/'runtime-private'
assert info['gateway_image_id']=='sha256:1b70a623e1b2a14d91aeb6c2244b8249d904108e48e99fd155fc245a8c59b557' and info['native_tests_total']==56 and sha(privateold/'gateway_demo')==info['gateway_elf_sha256']
link=json.loads((accepted/'gateway-link-process.json').read_text())['argv'];assert not any('--wrap' in x for x in link)
archive_refs=[x for x in link if x.endswith('libtinyimx_gateway.a')];archives=list(dict.fromkeys(archive_refs));mains=[x for x in link if x.endswith('gateway_demo.cpp.o')]
assert len(archive_refs)==2 and len(archives)==len(mains)==1 and pathlib.Path(archives[0])==privateold/'libtinyimx_gateway.a'
test_sha={'benchmark/local_capacity/group_fanout_commit_wakeup_test.cpp': 'b3bf8118849c013c018d622e5818b1631d1da3a7536552d2c83cbef4d30483ce', 'tests/gateway/group_fanout_coordinator_test.cpp': 'b0902e69d3bedbc78ab893286178019d34bea5b6886c018d3d99e5dd7fd9f0de', 'benchmark/local_capacity/group_fanout_pipeline_test.cpp': '75548000d4b9ea8055124eb30e0d4ad0a01d870a7f7f5c091aeb00653bffc65a', 'benchmark/local_capacity/group_delivery_ordering_test.cpp': '95ad168703838dc7be16fdd91aee327bebeb17c8bbb6e7d1224a61f5f02bbca6', 'tests/gateway/business_executor_test.cpp': '110c24499c6d86049cdec0f980c6291b7586e60ccd346b95b16a2edfa2c03fc8'}
assert all(sha(r/n)==h for n,h in test_sha.items())
old_objects={'wake':'wakeup-test.cpp.o','lease':'original-lease-test.cpp.o','pipeline':'pipeline-test.cpp.o','order':'recipient-order-test.cpp.o','executor':'business-executor-test.cpp.o'}
borrowed={}
cache=r/'build/linux-release'
for token in link:
 p=pathlib.Path(token) if pathlib.Path(token).is_absolute() else cache/token
 if p.is_file():borrowed[str(p.resolve())]=sha(p)
for n in old_objects.values():borrowed[str(privateold/n)]=sha(privateold/n)
flagsfile=cache/'CMakeFiles/tinyimx_gateway.dir/flags.make';borrowed[str(flagsfile)]=sha(flagsfile);flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
 flags+=shlex.split(next(v for v in flagsfile.read_text().splitlines() if v.startswith(key+' =')).split('=',1)[1])
macro=subprocess.check_output(['git','show','30452a51d08561593a6aba2fbe1ef188ff06d1c7:common/logging/LogMacros.h'])
assert hashlib.sha256(macro).hexdigest()=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
base='tinyimx/runtime:codex-group-recipient-order-v1-20261006';assert json.loads(run(['docker','image','inspect',base]))[0]['Id']==info['gateway_image_id']
image='tinyimx/runtime:codex-group-partial-drain-gateway-v1-20261006'
assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700);phase='initial'
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'One coordinatorTU replacing exactcopied gatewayarchive member; old56native objects sourceSHAverified +partial9x4; noABI/headerlayoutchanges;newimage only','head':head,'source_head':source['head'],'source_sha256':source['files'],'borrowed_sha256':borrowed,'test_sources_sha256':test_sha,'runtime_before':before,'writes':'Ownobjects/copiedarchive/ELF/maps/native logs/image/stoppedprobes; no cache/originalarchive/runtime/config/host/data changes','rollback':'Preserveall input/failed artifacts; stoponlyown PID/start/argv verified childgroups; no deletion/reset/push','performance_acceptance':False})
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
 assert shutil.disk_usage(r).free>2*1024**3
def interrupted(sig,frame):raise RuntimeError('Own partial build interrupted '+str(sig))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def invoke(a,label,seconds=240,env=None,expected=0):
 global phase
 phase=label;resources();save(label+'-audit-before.json',{'argv':a,'timeout':seconds})
 with (private/(label+'.log')).open('w') as f:
  child=subprocess.Popen(a,cwd=cache,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path('/proc')/str(child.pid)
  try:ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
  except FileNotFoundError:assert child.poll() is not None;ticks=None
  save(label+'-process.json',{'pid':child.pid,'start_ticks':ticks,'argv':a})
  try:code=child.wait(timeout=seconds)
  finally:
   if child.poll() is None:
    assert ticks and (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(child.pid)==child.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(a)]==[str(x).encode() for x in a]
    save(label+'-stop-audit.json',{'pid':child.pid,'operation':'StopverifiedownPGID'});os.killpg(child.pid,signal.SIGTERM)
    try:child.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
 assert code==expected,label+' failed; rawretained'
 print(json.dumps({'completed':label,'exit':code}),flush=True);return (private/(label+'.log')).read_text()
try:
 overlay=private/'original-includes/common/logging';overlay.mkdir(parents=True);(overlay/'LogMacros.h').write_bytes(macro)
 def compilefile(src,label):
  obj=private/(label+'.o')
  invoke(['/usr/bin/c++','-I'+str(overlay.parents[1]),*flags,'-MD','-MF',str(obj)+'.d','-c',str(r/src),'-o',str(obj)],'compile-'+label)
  return obj
 obj=compilefile('gateway/GroupFanoutCoordinator.cpp','GroupFanoutCoordinator.cpp')
 archive=private/'libtinyimx_gateway.a';shutil.copy2(archives[0],archive);invoke(['ar','rcs',str(archive),str(obj)],'archive-coordinator',90)
 assert run(['ar','t',str(archive)]).splitlines()==run(['ar','t',archives[0]]).splitlines()
 assert str(overlay/'LogMacros.h') in pathlib.Path(str(obj)+'.d').read_text()
 def linkfile(out,label,main=None,extra=None):
  args=[str(archive) if x==archives[0] else str(main) if main and x==mains[0] else x for x in link if not x.startswith('-Wl,-Map=')]
  args[args.index('-o')+1]=str(out)
  if extra:pos=args.index(str(main))+1;args[pos:pos]=list(map(str,extra))
  args+=['-Wl,-Map='+str(out)+'.map'];invoke(args,label)
 completed=[];native_paths={}
 for kind,total,key,status in [('wake',6,'TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE','GROUP_FANOUT_WAKE_NATIVE_PASS'),('pipeline',6,'TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE','GROUP_FANOUT_PIPELINE_NATIVE_PASS'),('order',2,'TINYIMX_GROUP_DELIVERY_RECIPIENT_ORDER_ENABLE','GROUP_DELIVERY_ORDER_NATIVE_PASS')]:
  native=private/(kind+'-native');native_paths[kind]=native
  linkfile(native,kind+'-native-link',privateold/old_objects[kind],[privateold/old_objects['lease']] if kind in ['wake','pipeline'] else None)
  for case,value,enabled in [('OFF',None,False),('ON','1',True),('INVALID','true',False)]:
   env=dict(os.environ);env.pop('TINYIMX_GROUP_FANOUT_PARTIAL_DRAIN_ENABLE',None);env.pop('TINYIMX_GROUP_FANOUT_PHASE_TRACE_ENABLE',None)
   if value is None:env.pop(key,None)
   else:env[key]=value
   log=invoke([str(native),'1' if enabled else '0'],kind+'-'+case,60,env)
   (d/(kind+'-'+case+'.log')).write_text(log)
   result=json.loads(next(v for v in log.splitlines() if v.startswith('{')))
   assert result=={'status':status,'enabled':enabled,'tests':total,'failures':0} and '[FAIL]' not in log
   completed.append({'kind':kind,'case':case,**result});save('completed-native-cases.json',completed)
 executor=private/'executor-native';linkfile(executor,'executor-native-link',privateold/old_objects['executor'])
 log=invoke([str(executor)],'executor-native',60);assert 'total=14, failed=0' in log and '[FAIL]' not in log
 (d/'executor-native.log').write_text(log)
 partial_obj=compilefile('benchmark/local_capacity/group_fanout_partial_drain_test.cpp','partial-test')
 native=private/'partial-native';linkfile(native,'partial-native-link',partial_obj)
 partial=[]
 for case,value,wake,enabled in [('OFF',None,True,False),('ON','1',True,True),('INVALID','01',True,False),('WAKE_OFF','1',False,True)]:
  env=dict(os.environ);env['TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE']='1' if wake else '0'
  env['TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE']='1';env.pop('TINYIMX_GROUP_FANOUT_PHASE_TRACE_ENABLE',None)
  if value is None:env.pop('TINYIMX_GROUP_FANOUT_PARTIAL_DRAIN_ENABLE',None)
  else:env['TINYIMX_GROUP_FANOUT_PARTIAL_DRAIN_ENABLE']=value
  log=invoke([str(native),'1' if enabled else '0','1' if wake else '0'],'partial-'+case,60,env)
  (d/('partial-'+case+'.log')).write_text(log)
  result=json.loads(next(v for v in log.splitlines() if v.startswith('{')))
  assert result['status']=='GROUP_PARTIAL_DRAIN_NATIVE_PASS' and result['enabled']==enabled and result['wake']==wake and result['checks']==9 and log.count('[PASS]')==9 and '[FAIL]' not in log
  partial.append({'case':case,**result});save('completed-partial-native-cases.json',partial)
 exe=private/'gateway_demo';linkfile(exe,'gateway-link')
 context=private/'image-context';context.mkdir();shutil.copy2(exe,context/'gateway_demo')
 (context/'Dockerfile').write_text('FROM '+base+'\nCOPY --chmod=0555 gateway_demo /opt/tinyimx/bin/gateway_demo\nLABEL codex.tinyimx.group_partial_drain.revision="'+head+'"\n')
 invoke(['docker','build','--network','none','--pull=false','-t',image,str(context)],'gateway-image',180)
 iid=json.loads(run(['docker','image','inspect',image]))[0]['Id']
 for name,argv,wanted in [('loader',['/usr/bin/ldd','-r','/opt/tinyimx/bin/gateway_demo'],0),('missing-config',['/opt/tinyimx/bin/gateway_demo','/__codex_partial_missing__.json'],1)]:
  own='codex-group-partial-'+name+'-20261006';assert subprocess.run(['docker','inspect',own],capture_output=True).returncode!=0
  args=['docker','run','--name',own,'--label','codex.tinyimx.group_partial_drain=20261006','--read-only','--network','none','--cap-drop','ALL','--security-opt','no-new-privileges:true','--user','1000:1000','--pids-limit','96','--memory','512m','--cpus','1','--tmpfs','/tmp:rw,noexec,nosuid,size=16m',iid,*argv]
  log=invoke(args,'image-'+name,60,expected=wanted)
  if name=='loader':assert 'not found' not in log and 'undefined symbol' not in log
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in source['files'].items())
 x={'status':'GROUP_PARTIAL_DRAIN_NATIVE_AND_IMAGE_PASS','head':head,'source_head':source['head'],'previous_checks':56,'partial_checks':36,'native_total_checks':92,'partial_cases':partial,'gateway_image_id':iid,'gateway_image_tag':image,'gateway_elf_sha256':sha(exe),'acceptedmain_cache_and_other_members_preserved':True,'borrowed_artifacts_preserved':True,'deployment':False,'performance_acceptance':False}
 save('summary.json',x);print(json.dumps(x,indent=2),flush=True)
except BaseException as error:
 save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'deployment':False});raise
finally:
 after=runtime();assert before==after;save('runtime-after.json',after)
PY
