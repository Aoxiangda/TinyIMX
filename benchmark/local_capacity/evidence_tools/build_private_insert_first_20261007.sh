#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,shlex,shutil,os,signal,re,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'insert-first-build-20261007';assert not d.exists()
run=lambda a:subprocess.check_output(a,text=True,stderr=subprocess.STDOUT,timeout=30)
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
source=json.loads((b/'insert-first-build-source-20261007/summary.json').read_text());head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/n)==h for n,h in source['files'].items())
product=json.loads((b/'insert-first-source-20261007/summary.json').read_text());assert all(sha(r/n)==h for n,h in product['files'].items())
base=json.loads((b/'group-get-boundary-build-20261007-attempt2/summary.json').read_text());assert base['status']=='GROUP_GET_BOUNDARY_NATIVE_AND_IMAGES_PASS' and base['native_checks']==145
frozen=b/'group-get-boundary-build-20261007';oldargv=json.loads((frozen/'message-runtime-link-process.json').read_text())['argv'];assert '--wrap' not in ' '.join(oldargv)
cache=r/'build/linux-release';resolve=lambda x:pathlib.Path(x).resolve() if pathlib.Path(x).is_absolute() else (cache/x).resolve()
main=[x for x in oldargv if x.endswith('/message-main.o')];adapter=[x for x in oldargv if x.endswith('/MessageRepositoryAdapter.o')];assert len(main)==len(adapter)==1
old_audit=json.loads((b/'group-get-boundary-build-20261007-attempt2/audit-before.json').read_text());assert all(sha(r/n)==h for n,h in old_audit['source_sha256'].items() if n.endswith(('.h','.cpp')) and n not in ['services/message/server/MessageServiceServer.cpp','services/message/repository/MessageRepositoryAdapter.cpp']) and all(sha(p)==h for p,h in old_audit['borrowed_sha256'].items())
overlay=b/'group-completion-rpc-build-20261006/runtime-private/original-includes';generated=b/'group-completion-rpc-build-20261006/runtime-private/generated/rpc';assert sha(overlay/'common/logging/LogMacros.h')=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
borrowed={str(resolve(x)):sha(resolve(x)) for x in oldargv if resolve(x).is_file()};flags_path=cache/'CMakeFiles/tinyimx_message_core.dir/flags.make';borrowed[str(flags_path)]=sha(flags_path)
for root in [overlay,generated]:
 for p in root.rglob('*'):
  if p.is_file():borrowed[str(p)]=sha(p)
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'current20k-baseline-20261007-attempt4/preservation-summary.json').read_text())['runtime'];assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
image_tag='tinyimx/runtime:codex-private-insert-first-20261007';assert subprocess.run(['docker','image','inspect',image_tag],capture_output=True).returncode!=0;assert json.loads(run(['docker','image','inspect',base['images']['message']['image_tag']]))[0]['Id']==base['images']['message']['image_id']
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700);save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10))
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Compile only defaultOFF MessageRepositoryAdapter candidate plus three real SQL test/probe mains; replace direct Adapter.o in sealed GGB runtime link, all class headers/ABI unchanged. No deployment or SQL during build; own frozen artifacts preserved.','source_sha256':{**product['files'],**source['files']},'borrowed_sha256':borrowed,'runtime_before':before,'native_scope':'Prepare current integration/unread/probe test ELFs; real SQL needs separate owned schema audit; no test DB action during build','performance_acceptance':False})
phase='initial';cases=[]
def resources():assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text())[1])>2*1024*1024 and shutil.disk_usage(r).free>2*1024**3
def interrupted(sig,frame):raise RuntimeError('Owned build interrupted '+str(sig))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def invoke(a,label,seconds=180,env=None,expected=0):
 global phase
 phase=label;resources();save(label+'-audit-before.json',{'argv':a,'timeout_seconds':seconds})
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(a,cwd=cache,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path('/proc')/str(p.pid)
  try:ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
  except FileNotFoundError:assert p.poll() is not None;ticks=None
  save(label+'-process.json',{'pid':p.pid,'start_ticks':ticks,'argv':a})
  try:code=p.wait(timeout=seconds)
  finally:
   if p.poll() is None:
    assert ticks and (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(bytes([0]))[:len(a)]==[str(x).encode() for x in a];save(label+'-stop-audit.json',{'operation':'Stop only verified own PGID','pid':p.pid,'start_ticks':ticks});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed; full raw retained';print(json.dumps({'completed':label,'exit':code}),flush=True);return (private/(label+'.log')).read_text()
def compilefile(src,name):
 fields=flags_path.read_text();flags=[]
 for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in fields.splitlines() if x.startswith(key+' =')).split('=',1)[1])
 obj=private/name;invoke(['/usr/bin/c++','-I'+str(overlay),'-I'+str(generated),*flags,'-MD','-MF',str(obj)+'.d','-c',str(r/src),'-o',str(obj)],'compile-'+name,240);return obj
try:
 obj=compilefile('services/message/repository/MessageRepositoryAdapter.cpp','MessageRepositoryAdapter.o')
 def link(exe,test_main=None):
  args=[str(obj) if x==adapter[0] else str(test_main) if test_main is not None and x==main[0] else x for x in oldargv];args[args.index('-o')+1]=str(exe);invoke(args,exe.name+'-link',180)
 binaries={}
 for target,src in [('message_outbox_integration_tests','tests/outbox/message_outbox_integration_test.cpp'),('unread_snapshot_aggregate_tests','tests/outbox/unread_snapshot_aggregate_test.cpp'),('private_begin_insert_read_batch_tests','tests/outbox/private_begin_insert_read_batch_test.cpp'),('private_insert_first_probe','benchmark/local_capacity/private_insert_first_probe.cpp')]:
  mainobj=compilefile(src,target+'.o');exe=private/target;link(exe,mainobj);binaries[target]=sha(exe)
 exe=private/'message_service_demo';link(exe);assert '__wrap_' not in run(['nm','-C',str(exe)]) and 'TINYIMX_PRIVATE_INSERT_FIRST_ENABLE' in run(['strings',str(exe)])
 base_image=json.loads(run(['docker','image','inspect','sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060']))[0];base_tags=base_image.get('RepoTags') or [];assert base_tags;accepted_base_tag=base_tags[0];assert json.loads(run(['docker','image','inspect',accepted_base_tag]))[0]['Id']==base_image['Id']
 context=private/'message-context';context.mkdir();shutil.copy2(exe,context/exe.name);os.chmod(context/exe.name,0o755);(context/'Dockerfile').write_text('FROM '+accepted_base_tag+chr(10)+'COPY --chmod=0755 '+exe.name+' /opt/tinyimx/bin/'+chr(10)+'LABEL org.opencontainers.image.revision="'+head+'"'+chr(10));invoke(['docker','build','--pull=false','--network=none','-t',image_tag,str(context)],'message-image',120)
 for label,args,code in [('ldd',['/usr/bin/ldd','-r','/opt/tinyimx/bin/'+exe.name],0),('missing-config',['/opt/tinyimx/bin/'+exe.name,'/__codex_insert_first_missing__.json'],1)]:
  name='codex-private-insert-first-'+label+'-20261007';assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0;text=invoke(['docker','run','--name',name,'--label','codex.tinyimx.insert_first=20261007','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges:true',image_tag,*args],label,30,expected=code)
  if label=='ldd':assert 'not found' not in text and 'undefined symbol' not in text
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in {**product['files'],**source['files']}.items())
 image={'image_tag':image_tag,'image_id':json.loads(run(['docker','image','inspect',image_tag]))[0]['Id'],'elf_sha256':sha(exe)}
 save('summary.json',{'status':'PRIVATE_INSERT_FIRST_BUILD_LOADER_PASS','head':head,'image':image,'binaries':binaries,'tests_runtime_private':str(private),'only_adapter_object_changed':True,'borrowed_preserved':True,'runtime_deploy':False,'real_sql_tests':'NOT_RUN','performance_acceptance':False});print((d/'summary.json').read_text(),flush=True)
except BaseException as error:save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'runtime_deploy':False});raise
finally:after=runtime();save('runtime-after.json',after);assert before==after
PY
