#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,shlex,shutil,os,signal,re,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-get-boundary-build-20261007';assert not d.exists()
run=lambda a:subprocess.check_output(a,text=True,stderr=subprocess.STDOUT,timeout=30)
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
source=json.loads((b/'group-get-boundary-source-20261007/summary.json').read_text());head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'];assert all(sha(r/n)==h for n,h in source['files'].items())
audit=json.loads((b/'group-get-boundary-audit-20261007/summary.json').read_text());assert all(sha(r/n)==h for n,h in audit['source_sha256'].items() if n.endswith('.h'))
cache=r/'build/linux-release';gl=audit['links']['gateway']['argv'];ml=audit['links']['message']['argv'];assert not any('--wrap' in x for x in gl+ml)
resolve=lambda x:pathlib.Path(x).resolve() if pathlib.Path(x).is_absolute() else (cache/x).resolve()
rpcnames=list(dict.fromkeys(x for x in gl if x.endswith('libtinyimx_rpc_client.a')));gm=[x for x in gl if x.endswith('/gateway-main.o')];mm=[x for x in ml if x.endswith('/message-main.o')];mr=[x for x in ml if x.endswith('/MessageRepository.o')];mi=[x for x in ml if x.endswith('/MessageServiceImpl.o')]
assert all(len(x)==1 for x in [rpcnames,gm,mm,mr,mi])
overlay=b/'group-completion-rpc-build-20261006/runtime-private/original-includes';generated=b/'group-completion-rpc-build-20261006/runtime-private/generated/rpc';assert sha(overlay/'common/logging/LogMacros.h')=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
borrowed={str(resolve(x)):sha(resolve(x)) for x in gl+ml if resolve(x).is_file()}
for target in ['tinyimx_rpc_client','tinyimx_message_grpc','tinyimx_repository','tinyimx_message_core']:
 p=cache/('CMakeFiles/'+target+'.dir/flags.make');borrowed[str(p)]=sha(p)
for base in [overlay,generated]:
 for p in base.rglob('*'):
  if p.is_file():borrowed[str(p)]=sha(p)
required_source={**source['files'],'tests/message/message_application_service_test.cpp':sha(r/'tests/message/message_application_service_test.cpp')}
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==audit['runtime'];assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
prior_gateway=json.loads((b/'group-message-runtime-build-20261006/summary.json').read_text());prior_message=json.loads((b/'group-confirm-coalesce-build-20261006-attempt3/summary.json').read_text());assert prior_gateway['checks']==144 and prior_message['checks']==387
images={'gateway':{'image_tag':'tinyimx/runtime:codex-group-get-boundary-gateway-20261007','base':prior_gateway['images']['gateway']['image_tag']},'message':{'image_tag':'tinyimx/runtime:codex-group-get-boundary-message-20261007','base':prior_message['image_tag']}}
for z in images.values():assert subprocess.run(['docker','image','inspect',z['image_tag']],capture_output=True).returncode!=0
assert json.loads(run(['docker','image','inspect',images['gateway']['base']]))[0]['Id']==prior_gateway['images']['gateway']['image_id'];assert json.loads(run(['docker','image','inspect',images['message']['base']]))[0]['Id']==prior_message['image_id']
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10))
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Recompile only MessageRpcClient/MessageServiceImpl/MessageRepository plus diagnosticnative andoriginalmessage unit. Replace onlyRPCarchive member, allotherssamebytes; Message directobjects Repo/Impl only, no ABI/protocol/class layout change. Seal2freshownimages withoutdeployment. No SQL/network fixtures/delete/cache/config/hostchange','source_sha256':required_source,'borrowed_sha256':borrowed,'runtime_before':before,'native_scope':'actualENV OFF/ON/invalid/zero/otherUID andTLS/rate/zero clock coverage; originalapplicationunit. Bounded childPGID only ontimeout','performance_acceptance':False})
phase='initial';cases=[]
def resources():assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text())[1])>2*1024*1024 and shutil.disk_usage(r).free>2*1024**3
def interrupted(sig,frame):raise RuntimeError('Owned boundarybuild interrupted '+str(sig))
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
    assert ticks and (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(bytes([0]))[:len(a)]==[str(x).encode() for x in a];save(label+'-stop-audit.json',{'operation':'StoponlyverifiedownPGID','pid':p.pid,'start_ticks':ticks});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed; fullraw retained';print(json.dumps({'completed':label,'exit':code}),flush=True);return (private/(label+'.log')).read_text()
def compilefile(src,name,target):
 fields=(cache/('CMakeFiles/'+target+'.dir/flags.make')).read_text();flags=[]
 for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in fields.splitlines() if x.startswith(key+' =')).split('=',1)[1])
 obj=private/name;invoke(['/usr/bin/c++','-I'+str(overlay),'-I'+str(generated),*flags,'-MD','-MF',str(obj)+'.d','-c',str(r/src),'-o',str(obj)],'compile-'+name,240)
 if src!='tests/message/message_application_service_test.cpp':assert str(overlay/'common/logging/LogMacros.h') in pathlib.Path(str(obj)+'.d').read_text()
 return obj
try:
 client=compilefile('services/rpc/MessageRpcClient.cpp','MessageRpcClient.cpp.o','tinyimx_rpc_client');impl=compilefile('services/message/service/MessageServiceImpl.cpp','MessageServiceImpl.o','tinyimx_message_grpc');repo=compilefile('services/repository/MessageRepository.cpp','MessageRepository.o','tinyimx_repository')
 old=resolve(rpcnames[0]);archive=private/'libtinyimx_rpc_client.a';shutil.copy2(old,archive);members=run(['ar','t',str(old)]).splitlines();assert members.count(client.name)==1;invoke(['ar','rcs',str(archive),str(client)],'rpc-archive');assert run(['ar','t',str(archive)]).splitlines()==members
 for n in members:
  if n!=client.name:assert hashlib.sha256(subprocess.check_output(['ar','p',str(old),n])).digest()==hashlib.sha256(subprocess.check_output(['ar','p',str(archive),n])).digest()
 save('archive-members.json',{'members':members,'changed':client.name,'others_byte_identical':True,'original_sha256':sha(old),'candidate_sha256':sha(archive)})
 def link(template,out,label,replacements):
  a=[str(replacements.get(x,x)) for x in template if not x.startswith('-Wl,-Map=')];a[a.index('-o')+1]=str(out);a+=['-Wl,-Map='+str(out)+'.map'];invoke(a,label);return out
 gateway=link(gl,private/'gateway_demo','gateway-runtime-link',{rpcnames[0]:archive});message=link(ml,private/'message_service_demo','message-runtime-link',{mr[0]:repo,mi[0]:impl})
 test=compilefile('benchmark/local_capacity/group_get_boundary_trace_test.cpp','boundary-native.o','tinyimx_rpc_client');native=link(gl,private/'boundary-native','boundary-native-link',{gm[0]:test,rpcnames[0]:archive})
 for label,value,expected in [('OFF',None,'0'),('ON','519862','519862'),('INVALID','0519862','0'),('ZERO','0','0'),('OTHER','519800','519800')]:
  env=dict(os.environ);env.pop('TINYIMX_GROUP_GET_BOUNDARY_TRACE_UID',None)
  if value is not None:env['TINYIMX_GROUP_GET_BOUNDARY_TRACE_UID']=value
  text=invoke([str(native),expected],'native-'+label,90,env);(d/('native-'+label+'.log')).write_text(text);v=json.loads(next(x for x in text.splitlines() if x.startswith('{')));assert v['status']=='GROUP_GET_BOUNDARY_NATIVE_PASS' and v['failures']==0 and v['checks']==29 and '[FAIL]' not in text;cases.append({'case':label,**v});save('native-cases.json',cases)
 unit=compilefile('tests/message/message_application_service_test.cpp','original-message-test.o','tinyimx_message_core');unitexe=link(ml,private/'original-message-unit','original-message-unit-link',{mm[0]:unit,mr[0]:repo,mi[0]:impl});env=dict(os.environ);env.pop('TINYIMX_GROUP_GET_BOUNDARY_TRACE_UID',None);text=invoke([str(unitexe)],'original-message-unit',90,env);assert 'failed=0' in text and '[FAIL]' not in text;(d/'original-message-unit.log').write_text(text)
 for kind,exe in [('gateway',gateway),('message',message)]:
  symbols=run(['nm','-C',str(exe)]);assert 'GroupGetBoundaryTrace::current_' in symbols and '__wrap_' not in symbols
  context=private/(kind+'-context');context.mkdir();shutil.copy2(exe,context/exe.name);os.chmod(context/exe.name,0o755);(context/'Dockerfile').write_text('FROM '+images[kind]['base']+chr(10)+'COPY --chmod=0755 '+exe.name+' /opt/tinyimx/bin/'+chr(10)+'LABEL org.opencontainers.image.revision="'+head+'"'+chr(10));invoke(['docker','build','--pull=false','--network=none','-t',images[kind]['image_tag'],str(context)],kind+'-image',120)
  for label,args,code in [('ldd',['/usr/bin/ldd','-r','/opt/tinyimx/bin/'+exe.name],0),('missing-config',['/opt/tinyimx/bin/'+exe.name,'/__codex_group_get_boundary_missing__.json'],1)]:
   name='codex-group-get-boundary-'+kind+'-'+label+'-20261007';assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0;text=invoke(['docker','run','--name',name,'--label','codex.tinyimx.group_get_boundary=20261007','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges:true',images[kind]['image_tag'],*args],kind+'-'+label,30,expected=code)
   if label=='ldd':assert 'not found' not in text and 'undefined symbol' not in text
  images[kind].update(image_id=json.loads(run(['docker','image','inspect',images[kind]['image_tag']]))[0]['Id'],elf_sha256=sha(exe))
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in required_source.items())
 save('summary.json',{'status':'GROUP_GET_BOUNDARY_NATIVE_AND_IMAGES_PASS','head':head,'images':images,'native_checks':145,'native_cases':cases,'original_message_unit_pass':True,'existing_class_headers_unchanged':True,'protocol_unchanged':True,'gateway_only_rpc_member_changed':True,'message_only_repo_and_impl_objects_changed':True,'other_archive_members_and_borrowed_preserved':True,'runtime_deploy':False,'performance_acceptance':False});print((d/'summary.json').read_text(),flush=True)
except BaseException as error:save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'runtime_deploy':False});raise
finally:after=runtime();save('runtime-after.json',after);assert before==after
PY
