#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,re,shlex,os,signal,shutil,sys,time
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-actor-snapshot-build-20261006';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
source=json.loads((b/'group-actor-snapshot-source-20261006/summary.json').read_text());head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/n)==h for n,h in source['files'].items())
pre=json.loads((b/'group-actor-snapshot-preflight-20261006-attempt2/summary.json').read_text());assert pre['status']=='GROUP_ACTOR_SNAPSHOT_READONLY_PREFLIGHT_PASS'
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 return {c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
before=runtime();assert before==pre['runtime'];cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');cfgsha={p.name:sha(p) for p in cfg.glob('*.json')}
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
 assert shutil.disk_usage(r).free>3*1024**3
resources()
build=r/'build/linux-release';linkpath=pathlib.Path(pre['link_path']);assert sha(linkpath)==pre['link_sha256']
link=shlex.split(linkpath.read_text());main=[x for x in link if x.endswith('examples/group_service_demo.cpp.o')];assert len(main)==1
borrowed={str(linkpath):sha(linkpath),str(build/main[0]):sha(build/main[0])}
archives={}
for token in link:
 if token.startswith('libtinyimx_') and token.endswith('.a'):
  p=build/token;assert p.exists();archives[token]=p;borrowed[str(p)]=sha(p)
 elif token.endswith(('.so','.a','.o')) and (build/token).is_file():borrowed[str((build/token).resolve())]=sha((build/token).resolve())
for n,p in pre['plans'].items():
 assert p['archive'] in archives and sha(archives[p['archive']])==p['archive_sha256'] and sha(p['flags_path'])==p['flags_sha256']
 assert run(['ar','t',str(archives[p['archive']])]).splitlines().count(p['member'])==1
 borrow=pathlib.Path(p['flags_path']);borrowed[str(borrow)]=sha(borrow)
macro=subprocess.check_output(['git','show','30452a51d08561593a6aba2fbe1ef188ff06d1c7:common/logging/LogMacros.h'])
assert hashlib.sha256(macro).hexdigest()=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
image='tinyimx/runtime:codex-group-actor-snapshot-v1-20261006';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
assert json.loads(run(['docker','image','inspect',pre['group_image']]))[0]['Id']==pre['group_image']
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Isolated3TUs/copied2archives, native4interfaces sameELF8cases OFF/ON ABBA pool1/4, group candidate image only','head':head,'sources_sha256':source['files'],'borrowed_sha256':borrowed,'runtime_before':before,'private_config_sha256':cfgsha,'native_SQL':'Existingownedgroup/member corpus readonly SELECT/EXPLAIN andoriginalBEGIN/COMMIT only; no DDL/DML/cleanup, no old DELETE tests','compile':'Onecompiler atatime no dependencyinstall/CMake/cache/libraryoverwrite; >2GiB available >3GiB disk','rollback':'No deployment; all19 andbaseimage retained, closeonlyownverifiedchildren; allsource/object/error/case results retained','performance_acceptance':False})
phase='initial'
def onfailure(k,e,t):
 save('failed.json',{'status':'FAIL','phase':phase,'type':k.__name__,'message':str(e),'runtime_changes':False,'evidence_retained':True});sys.__excepthook__(k,e,t)
sys.excepthook=onfailure
def invoke(a,label,timeout=240,cwd=None,env=None,expected=0):
 global phase
 phase=label;resources();save(label+'-audit-before.json',{'operation':'Onlyownbuild/testprocess','argv':a,'timeout':timeout})
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(a,cwd=cwd or build,stdout=f,stderr=subprocess.STDOUT,start_new_session=True,env=env)
  proc=pathlib.Path('/proc')/str(p.pid);ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
  save(label+'-process.json',{'pid':p.pid,'pgid':p.pid,'start_ticks':ticks,'argv':a})
  try:code=p.wait(timeout=timeout)
  finally:
   if p.poll() is None:
    assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(a)]==[str(v).encode() for v in a]
    save(label+'-stop-audit-before.json',{'operation':'Stoponlyverifiedownchildgroup','pid':p.pid,'start_ticks':ticks});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed:ownlog retained'
 print(json.dumps({'completed':label,'exit':code}),flush=True)
 return (private/(label+'.log')).read_text()
def flags(path):
 vals=[]
 for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:vals+=shlex.split(next(x for x in pathlib.Path(path).read_text().splitlines() if x.startswith(key+' =')).split('=',1)[1])
 return vals
overlay=private/'control-includes/common/logging';overlay.mkdir(parents=True);(overlay/'LogMacros.h').write_bytes(macro)
ownarchives={};objects={}
for n,plan in pre['plans'].items():
 obj=private/plan['member'];invoke(['/usr/bin/c++','-I'+str(overlay.parents[1]),*flags(plan['flags_path']),'-MD','-MF',str(obj)+'.d','-c',str(r/n),'-o',str(obj)],'compile-'+pathlib.Path(n).stem)
 objects[n]=obj
 if plan['archive'] not in ownarchives:
  target=private/plan['archive'];shutil.copy2(archives[plan['archive']],target);ownarchives[plan['archive']]=target
 invoke(['ar','rcs',str(ownarchives[plan['archive']]),str(obj)],'archive-'+pathlib.Path(n).stem,90)
for n,p in ownarchives.items():assert run(['ar','t',str(p)]).splitlines()==run(['ar','t',str(archives[n])]).splitlines()
args=[str(ownarchives.get(x,archives.get(x,x))) for x in link];exe=private/'group_service_demo';args[args.index('-o')+1]=str(exe);args+=['-Wl,-Map='+str(private/'group_service_demo.map')];invoke(args,'group-link')
test=r/'benchmark/local_capacity/group_actor_snapshot_readonly_test.cpp';testobj=private/'readonly_test.o'
invoke(['/usr/bin/c++',*flags(next(iter(pre['plans'].values()))['flags_path']),'-c',str(test),'-o',str(testobj)],'native-test-compile')
nl=[str(testobj) if x==main[0] else str(ownarchives.get(x,archives.get(x,x))) for x in link];native=private/'readonly_test';nl[nl.index('-o')+1]=str(native);nl+=['-Wl,-Map='+str(private/'native.map')];invoke(nl,'native-test-link')
reports=[]
for pool in [1,4]:
 expected_behavior=None
 for case,enabled in [('A1',False),('B1',True),('B2',True),('A2',False)]:
  casepath=d/('p'+str(pool)+'-'+case);casepath.mkdir(mode=0o700)
  env={**os.environ,'TINYIMX_GROUP_ACTOR_SNAPSHOT_ENABLE':'1' if enabled else '0'}
  a=[str(native),str(cfg/'group.json'),str(pool),pre['mysql_native_ip'],str(casepath),str(b/'group-actor-snapshot-preflight-20261006-attempt2/summary.json'),'1' if enabled else '0']
  invoke(a,'native-p'+str(pool)+'-'+case,300,env=env)
  result=json.loads((casepath/'result.json').read_text());checks=json.loads((casepath/'checks.json').read_text());behavior=json.loads((casepath/'adapter-behavior.json').read_text())
  assert result['status']=='GROUP_ACTOR_NATIVE_READONLY_PASS' and len(checks)==result['checks'] and all(x['pass'] for x in checks) and result['cases']==149
  if expected_behavior is None:expected_behavior=behavior
  assert behavior==expected_behavior,'OFF/ON completeadapterresponse mismatch; evidence retained'
  for row in result['performance']:assert row['metrics']['samples']==100 and len(row['raw'])==100
  reports.append({'pool':pool,'case':case,'enabled':enabled,'checks':result['checks'],'adapter_behavior_rows':result['adapter_behavior_rows'],'interfaces':[{'operation':v['operation'],'metrics':v['metrics']} for v in result['performance']]})
  save('completed-native-cases.json',reports)
assert runtime()==before and {p.name:sha(p) for p in cfg.glob('*.json')}==cfgsha and all(sha(p)==h for p,h in borrowed.items())
context=private/'image-context';context.mkdir();shutil.copy2(exe,context/'group_service_demo')
(context/'Dockerfile').write_text('FROM '+pre['group_image']+'\nCOPY --chmod=0555 group_service_demo /opt/tinyimx/bin/group_service_demo\nLABEL codex.tinyimx.group_actor_snapshot.revision="'+head+'"\n')
invoke(['docker','build','--network','none','--pull=false','-t',image,str(context)],'candidate-image',180,cwd=r)
iid=json.loads(run(['docker','image','inspect',image]))[0]['Id']
label='codex.tinyimx.group_actor_snapshot=20261006'
for name,a,wanted in [('loader',['/usr/bin/ldd','/opt/tinyimx/bin/group_service_demo'],0),('missing-config',['/opt/tinyimx/bin/group_service_demo','/__codex_group_snapshot_missing__.json'],1)]:
 own='codex-group-snapshot-'+name+'-20261006'
 assert subprocess.run(['docker','container','inspect',own],capture_output=True).returncode!=0
 argv=['docker','run','--name',own,'--label',label,'--read-only','--network','none','--cap-drop','ALL','--security-opt','no-new-privileges:true','--user','1000:1000','--pids-limit','96','--memory','512m','--cpus','1','--tmpfs','/tmp:rw,noexec,nosuid,size=16m',iid,*a]
 log=invoke(argv,'image-'+name,60,cwd=r,expected=wanted)
 c=json.loads(run(['docker','container','inspect',own]))[0];assert c['Image']==iid and c['State']['Running'] is False and c['State']['ExitCode']==wanted and c['Config']['Labels'].get('codex.tinyimx.group_actor_snapshot')=='20261006'
 assert name!='loader' or 'not found' not in log
save('runtime-after.json',runtime());assert runtime()==before and {p.name:sha(p) for p in cfg.glob('*.json')}==cfgsha and all(sha(p)==h for p,h in borrowed.items())
x={'status':'GROUP_ACTOR_SNAPSHOT_BUILD_AND_NATIVE_PASS','head':head,'native_checks':sum(v['checks'] for v in reports),'native_corpus_cases_percase':149,'native_performance_samples':3200,'same_native_elf_sha256':sha(native),'sameELF_OFF_ON_full_adapter_responses':True,'native_cases':reports,'group_elf_sha256':sha(exe),'group_image_tag':image,'group_image_id':iid,'production_instances_configs_preserved':True,'original_artifacts_preserved':True,'data_writes':False,'performance_acceptance':False}
save('summary.json',x);print(json.dumps(x,indent=2))
PY
