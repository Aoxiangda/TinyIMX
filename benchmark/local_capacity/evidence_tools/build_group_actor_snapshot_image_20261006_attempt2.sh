#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,shutil,os,signal,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-actor-snapshot-image-20261006-attempt2';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
source=json.loads((b/'group-actor-snapshot-image-repair-source-20261006/summary.json').read_text());assert run(['git','rev-parse','HEAD']).strip()==source['head'] and all(sha(r/n)==h for n,h in source['files'].items())
original=json.loads((b/'group-actor-snapshot-source-20261006/summary.json').read_text());beforebuild=b/'group-actor-snapshot-build-20261006';audit=json.loads((beforebuild/'audit-before.json').read_text())
assert json.loads((beforebuild/'failed.json').read_text())['phase']=='candidate-image'
for n,h in original['files'].items():
 if not n.endswith('.md'):assert sha(r/n)==h
assert all(sha(p)==h for p,h in audit['borrowed_sha256'].items())
pre=json.loads((b/'group-actor-snapshot-preflight-20261006-attempt2/summary.json').read_text())
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 return {c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
before=runtime();assert before==pre['runtime'];cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');cfgsha={p.name:sha(p) for p in cfg.glob('*.json')};assert cfgsha==audit['private_config_sha256']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
nativecases=json.loads((beforebuild/'completed-native-cases.json').read_text());assert len(nativecases)==8 and sum(x['checks'] for x in nativecases)==3856
for pool in [1,4]:
 expected=None
 for case in ['A1','B1','B2','A2']:
  p=beforebuild/('p'+str(pool)+'-'+case);res=json.loads((p/'result.json').read_text());checks=json.loads((p/'checks.json').read_text());beh=json.loads((p/'adapter-behavior.json').read_text())
  assert res['status']=='GROUP_ACTOR_NATIVE_READONLY_PASS' and res['checks']==len(checks)==482 and all(x['pass'] for x in checks)
  if expected is None:expected=beh
  assert beh==expected
base_tag='tinyimx/runtime:m21-final';base_id=pre['group_image'];assert json.loads(run(['docker','image','inspect',base_tag]))[0]['Id']==base_id
image='tinyimx/runtime:codex-group-actor-snapshot-v2-20261006';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
reuse={str(p):sha(p) for p in (beforebuild/'runtime-private').iterdir() if p.is_file() and p.name in ['group_service_demo','readonly_test','GroupRepository.cpp.o','GroupRepositoryAdapter.cpp.o','GroupMembershipRepositoryAdapter.cpp.o','libtinyimx_repository.a','libtinyimx_group_core.a']}
assert len(reuse)==7
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Onlyimagepackagingrepair actualexistinglocaltag byphysicalID; reusegreen3856nativechecks and7compiledartifacts','head':source['head'],'compiled_source_head':original['head'],'freeze_before_reuse_sha256':reuse,'freeze_timing':'Firstfailure aftercompilednative; freeze now before reuse, notclaimed originalprecompile record','base_tag':base_tag,'base_image_id':base_id,'original_failure':'Docker FROM sha256 physicalID parsed asregistryrepo; no candidateimage ordeployment. Originallogs retained. No pull orSDK install','runtime_before':before,'configs_sha256':cfgsha,'writes':'Newownimagecontext/copiedELF/newv2image plus stoppedguardedloaderprobe containers','rollback':'No deployment; preserveoriginalfailedcontext/objects/cases/base19, no deletion','performance_acceptance':False})
def invoke(a,label,timeout=180,expected=0):
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(a,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
  proc=pathlib.Path('/proc')/str(p.pid);ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19];save(label+'-process.json',{'pid':p.pid,'start_ticks':ticks,'argv':a})
  try:code=p.wait(timeout=timeout)
  finally:
   if p.poll() is None:
    assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(a)]==[str(x).encode() for x in a]
    save(label+'-stop-audit-before.json',{'operation':'Stoponlyverifiedownedchildgroup','pid':p.pid});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed; ownlog preserved'
 print(json.dumps({'completed':label,'exit':code}),flush=True);return (private/(label+'.log')).read_text()
phase='image'
try:
 context=private/'image-context';context.mkdir();exe=beforebuild/'runtime-private/group_service_demo';shutil.copy2(exe,context/'group_service_demo')
 (context/'Dockerfile').write_text('FROM '+base_tag+'\nCOPY --chmod=0555 group_service_demo /opt/tinyimx/bin/group_service_demo\nLABEL codex.tinyimx.group_actor_snapshot.revision="'+original['head']+'"\n')
 invoke(['docker','build','--network','none','--pull=false','-t',image,str(context)],'candidate-image')
 assert json.loads(run(['docker','image','inspect',base_tag]))[0]['Id']==base_id
 iid=json.loads(run(['docker','image','inspect',image]))[0]['Id']
 for name,a,wanted in [('loader',['/usr/bin/ldd','/opt/tinyimx/bin/group_service_demo'],0),('missing-config',['/opt/tinyimx/bin/group_service_demo','/__codex_group_snapshot_missing__.json'],1)]:
  phase=name;own='codex-group-snapshot-v2-'+name+'-20261006';assert subprocess.run(['docker','container','inspect',own],capture_output=True).returncode!=0
  argv=['docker','run','--name',own,'--label','codex.tinyimx.group_actor_snapshot=20261006','--read-only','--network','none','--cap-drop','ALL','--security-opt','no-new-privileges:true','--user','1000:1000','--pids-limit','96','--memory','512m','--cpus','1','--tmpfs','/tmp:rw,noexec,nosuid,size=16m',iid,*a]
  log=invoke(argv,'image-'+name,60,wanted)
  c=json.loads(run(['docker','container','inspect',own]))[0];assert c['Image']==iid and not c['State']['Running'] and c['State']['ExitCode']==wanted and c['Config']['Labels'].get('codex.tinyimx.group_actor_snapshot')=='20261006'
  assert name!='loader' or 'not found' not in log
 assert runtime()==before and {p.name:sha(p) for p in cfg.glob('*.json')}==cfgsha and all(sha(p)==h for p,h in reuse.items())
 x={'status':'GROUP_ACTOR_SNAPSHOT_BUILD_AND_NATIVE_PASS','head':source['head'],'compiled_source_head':original['head'],'native_checks':3856,'native_corpus_cases_percase':149,'native_performance_samples':3200,'same_native_elf_sha256':sha(beforebuild/'runtime-private/readonly_test'),'sameELF_OFF_ON_full_adapter_responses':True,'native_cases':nativecases,'group_elf_sha256':sha(exe),'group_image_tag':image,'group_image_id':iid,'production_instances_configs_preserved':True,'original_artifacts_preserved':True,'data_writes':False,'performance_acceptance':False,'native_latency_limits':'GetGroup p1B2 andPrepare p1B1 havehigherthancontrol nativeP99; allrawretained, no blanketperformance acceptance'}
 save('summary.json',x);print(json.dumps({'status':x['status'],'native_checks':3856,'group_elf_sha256':x['group_elf_sha256'],'group_image_id':iid,'performance_acceptance':False},indent=2))
except BaseException as e:save('failed.json',{'status':'FAIL','phase':phase,'type':type(e).__name__,'message':str(e),'runtime':runtime(),'deployment':False});raise
PY
