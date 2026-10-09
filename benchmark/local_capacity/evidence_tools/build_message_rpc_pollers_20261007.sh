#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,shlex,shutil,os,signal,re,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'message-rpc-pollers-build-20261007';assert not d.exists()
run=lambda a:subprocess.check_output(a,text=True,stderr=subprocess.STDOUT,timeout=30)
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
source=json.loads((b/'message-rpc-pollers-source-20261007/summary.json').read_text());head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/n)==h for n,h in source['files'].items())
base=json.loads((b/'group-get-boundary-build-20261007-attempt2/summary.json').read_text());assert base['status']=='GROUP_GET_BOUNDARY_NATIVE_AND_IMAGES_PASS' and base['native_checks']==145
frozen=b/'group-get-boundary-build-20261007';oldargv=json.loads((frozen/'message-runtime-link-process.json').read_text())['argv'];assert '--wrap' not in ' '.join(oldargv)
cache=r/'build/linux-release';resolve=lambda x:pathlib.Path(x).resolve() if pathlib.Path(x).is_absolute() else (cache/x).resolve()
main=[x for x in oldargv if x.endswith('/message-main.o')];server=[x for x in oldargv if x.endswith('/MessageServiceServer.o')];assert len(main)==len(server)==1
old_audit=json.loads((b/'group-get-boundary-build-20261007-attempt2/audit-before.json').read_text());assert all(sha(r/n)==h for n,h in old_audit['source_sha256'].items() if n.endswith(('.h','.cpp')) and n!='services/message/server/MessageServiceServer.cpp') and all(sha(p)==h for p,h in old_audit['borrowed_sha256'].items())
overlay=b/'group-completion-rpc-build-20261006/runtime-private/original-includes';generated=b/'group-completion-rpc-build-20261006/runtime-private/generated/rpc';assert sha(overlay/'common/logging/LogMacros.h')=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
borrowed={str(resolve(x)):sha(resolve(x)) for x in oldargv if resolve(x).is_file()};flags_path=cache/'CMakeFiles/tinyimx_message_grpc.dir/flags.make';borrowed[str(flags_path)]=sha(flags_path)
for root in [overlay,generated]:
 for p in root.rglob('*'):
  if p.is_file():borrowed[str(p)]=sha(p)
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'group-get-boundary-control-20261007-attempt2/restore-summary.json').read_text())['runtime'];assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
image_tag='tinyimx/runtime:codex-message-rpc-pollers-20261007';assert subprocess.run(['docker','image','inspect',image_tag],capture_output=True).returncode!=0;assert json.loads(run(['docker','image','inspect',base['images']['message']['image_tag']]))[0]['Id']==base['images']['message']['image_id']
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700);save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10))
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Compile only MessageServiceServer.cpp and own lifecycle native; replace only direct Server.o in exact diagnostic Message link. Default OFF exact library defaults, ON queues1/min4/max8. No business threads/query/auth/FIFO/timeout/durability/config/host change. Seal new image without deployment.','source_sha256':source['files'],'borrowed_sha256':borrowed,'runtime_before':before,'native_scope':'Five actual ENV cases, private ephemeral loopback fake service, 64 calls each plus invalidarg/readiness/lifecycle; no SQL/Redis data','performance_acceptance':False})
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
 obj=compilefile('services/message/server/MessageServiceServer.cpp','MessageServiceServer.o');test=compilefile('benchmark/local_capacity/message_rpc_pollers_test.cpp','pollers-native.o')
 def link(exe,test_main=None):
  a=[str(obj) if x==server[0] else str(test_main) if test_main is not None and x==main[0] else x for x in oldargv];a[a.index('-o')+1]=str(exe);invoke(a,exe.name+'-link',180)
 native=private/'pollers-native';link(native,test)
 for name,value,enabled in [('off',None,0),('on','1',1),('invalid','01',0),('zero','0',0),('other','true',0)]:
  env=dict(os.environ);env.pop('TINYIMX_MESSAGE_RPC_POLLERS_ENABLE',None)
  if value is not None:env['TINYIMX_MESSAGE_RPC_POLLERS_ENABLE']=value
  text=invoke([str(native),str(enabled)],'native-'+name,30,env);x=json.loads(next(v for v in text.splitlines() if v.startswith('{')));assert x['status']=='MESSAGE_RPC_POLLERS_NATIVE_PASS' and x['failures']==0 and x['completed_rpc']==64 and x['enabled']==enabled;(d/('native-'+name+'.log')).write_text(text);cases.append({'case':name,**x})
 save('native-cases.json',cases);exe=private/'message_service_demo';link(exe);assert '__wrap_' not in run(['nm','-C',str(exe)]) and 'group_get_boundary_phase side=' in run(['strings',str(exe)])
 context=private/'message-context';context.mkdir();shutil.copy2(exe,context/exe.name);os.chmod(context/exe.name,0o755);(context/'Dockerfile').write_text('FROM '+base['images']['message']['image_tag']+chr(10)+'COPY --chmod=0755 '+exe.name+' /opt/tinyimx/bin/'+chr(10)+'LABEL org.opencontainers.image.revision="'+head+'"'+chr(10));invoke(['docker','build','--pull=false','--network=none','-t',image_tag,str(context)],'message-image',120)
 for label,args,code in [('ldd',['/usr/bin/ldd','-r','/opt/tinyimx/bin/'+exe.name],0),('missing-config',['/opt/tinyimx/bin/'+exe.name,'/__codex_rpc_pollers_missing__.json'],1)]:
  name='codex-message-rpc-pollers-'+label+'-20261007';assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0;text=invoke(['docker','run','--name',name,'--label','codex.tinyimx.rpc_pollers=20261007','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges:true',image_tag,*args],label,30,expected=code)
  if label=='ldd':assert 'not found' not in text and 'undefined symbol' not in text
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in source['files'].items())
 images={'gateway':base['images']['gateway'],'message':{'image_tag':image_tag,'image_id':json.loads(run(['docker','image','inspect',image_tag]))[0]['Id'],'elf_sha256':sha(exe)}}
 save('summary.json',{'status':'MESSAGE_RPC_POLLERS_NATIVE_AND_IMAGE_PASS','head':head,'images':images,'native_checks':sum(x['checks'] for x in cases),'native_rpc_calls':320,'native_cases':cases,'inherited_boundary_native_checks':145,'existing_class_headers_unchanged':True,'protocol_unchanged':True,'only_server_object_changed':True,'borrowed_preserved':True,'runtime_deploy':False,'performance_acceptance':False});print((d/'summary.json').read_text(),flush=True)
except BaseException as error:save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'runtime_deploy':False});raise
finally:after=runtime();save('runtime-after.json',after);assert before==after
PY
