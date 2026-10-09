#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,shlex,shutil,os,signal,re,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-message-runtime-build-20261006';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t,stderr=subprocess.STDOUT)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
source=json.loads((b/'group-message-runtime-source-20261006/summary.json').read_text())
helper=json.loads((b/'group-message-runtime-build-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==helper['head']
old=b/'group-route-integration-build-20261006';original=json.loads((old/'summary.json').read_text())
assert original['status']=='GROUP_ROUTE_INTEGRATION_NATIVE_AND_IMAGE_PASS' and original['coordinator_checks']==164 and original['resolver_checks']==128 and original['primitive_checks']==720
prior=json.loads((old/'audit-before.json').read_text())
sources={n:h for n,h in prior['source_sha256'].items() if not n.startswith('docs/')}
sources.update({n:h for n,h in source['files'].items() if not n.startswith('docs/')});sources.update(helper['files'])
for n in ['tests/gateway/receiver_delivery_tracker_test.cpp','gateway/ReceiverDeliveryTracker.h','gateway/ReceiverDeliveryTracker.cpp','gateway/GatewayServer.h','gateway/DeliveryIdentity.h','gateway/GroupDeliveryOrdering.h']:
 sources[n]=sha(r/n)
assert sources['gateway/GatewayServer.h']=='1fd51f5ab7512ad970e67887d017f99805246553f94851f0637b644dce47874a'
assert all(sha(r/n)==h for n,h in sources.items())
cache=r/'build/linux-release';gl=json.loads((old/'gateway-runtime-link-process.json').read_text())['argv']
assert not any('--wrap' in x for x in gl)
ga=list(dict.fromkeys(x for x in gl if x.endswith('libtinyimx_gateway.a')));gm=[x for x in gl if x.endswith('/gateway-main.o')];assert len(ga)==len(gm)==1
overlay=b/'group-completion-rpc-build-20261006/runtime-private/original-includes'
assert sha(overlay/'common/logging/LogMacros.h')=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
borrowed={str(overlay/'common/logging/LogMacros.h'):sha(overlay/'common/logging/LogMacros.h')}
for x in gl:
 p=pathlib.Path(x) if pathlib.Path(x).is_absolute() else cache/x
 if p.is_file():borrowed[str(p.resolve())]=sha(p)
for p in [old/'summary.json',old/'audit-before.json',old/'gateway-runtime-link-process.json',cache/'CMakeFiles/tinyimx_gateway.dir/flags.make',old/'runtime-private/executor-native',old/'runtime-private/order-native']:
 borrowed[str(p)]=sha(p)
for p in (b/'group-completion-rpc-build-20261006/runtime-private/generated/rpc').rglob('*'):
 if p.is_file():borrowed[str(p)]=sha(p)
assert all(sha(p)==h for p,h in prior['borrowed_sha256'].items())
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'group-route-partial-drain-control-20261006/restore-summary.json').read_text())['runtime']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
image='tinyimx/runtime:codex-group-message-runtime-gateway-v1-20261006';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
assert json.loads(run(['docker','image','inspect',original['images']['gateway']['image_tag']]))[0]['Id']==original['images']['gateway']['image_id']
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'source_sha256':sources,'borrowed_sha256':borrowed,'runtime_before':before,'operation':'Recompile only GatewayServer and new selector/old tracker native TUs. Gateway class layout unchanged and original allocatingMain retained exact SHA. Copy sealed route Gateway archive and replace only Server member, every other member byte-identical. OriginalMessage/route/cache/SQL/Proto exact and tests inherited. Seal own newGateway image; no deployment.','native_scope':'Two own bounded executor pools, blocked peer/ACK FIFO/control vsprivate contention, hotkey/BeginDrain/stopped noalternate orderingdomain, all27 originaltracker tests; no external writes/network/DDL/deletion','new_threads_in_product':0,'host_apps_changed':False,'performance_acceptance':False})
phase='initial';cases=[]
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024 and shutil.disk_usage(r).free>2*1024**3
def interrupted(sig,frame):raise RuntimeError('Own runtime build interrupted '+str(sig))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def invoke(argv,label,seconds=180,env=None,expected=0):
 global phase
 phase=label;resources();save(label+'-audit-before.json',{'argv':argv,'timeout_seconds':seconds})
 with (private/(label+'.log')).open('w') as f:
  child=subprocess.Popen(argv,cwd=cache,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path('/proc')/str(child.pid)
  try:ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
  except FileNotFoundError:assert child.poll() is not None;ticks=None
  save(label+'-process.json',{'pid':child.pid,'start_ticks':ticks,'argv':argv})
  try:code=child.wait(timeout=seconds)
  finally:
   if child.poll() is None:
    assert ticks and (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(child.pid)==child.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(argv)]==[str(x).encode() for x in argv]
    save(label+'-stop-audit.json',{'operation':'Stop only exact own verified processgroup','pid':child.pid,'start_ticks':ticks});os.killpg(child.pid,signal.SIGTERM)
    try:child.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
 assert code==expected,label+' failed; full raw retained'
 print(json.dumps({'completed':label,'exit_code':code}),flush=True);return (private/(label+'.log')).read_text()
def compilefile(name,label,member=None):
 fields=(cache/'CMakeFiles/tinyimx_gateway.dir/flags.make').read_text();flags=[]
 for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in fields.splitlines() if x.startswith(key+' =')).split('=',1)[1])
 obj=private/(member or label+'.o');generated=b/'group-completion-rpc-build-20261006/runtime-private/generated/rpc'
 invoke(['/usr/bin/c++','-I'+str(overlay),'-I'+str(generated),*flags,'-MD','-MF',str(obj)+'.d','-c',str(r/name),'-o',str(obj)],'compile-'+label)
 return obj
try:
 server=compilefile('gateway/GatewayServer.cpp','GatewayServer','GatewayServer.cpp.o')
 assert str(overlay/'common/logging/LogMacros.h') in pathlib.Path(str(server)+'.d').read_text()
 original_archive=pathlib.Path(ga[0]);archive=private/'libtinyimx_gateway.a';shutil.copy2(original_archive,archive)
 members=run(['ar','t',str(original_archive)]).splitlines();assert members.count(server.name)==1
 invoke(['ar','rcs',str(archive),str(server)],'gateway-archive',90);assert run(['ar','t',str(archive)]).splitlines()==members
 for member in members:
  if member!=server.name:assert hashlib.sha256(subprocess.check_output(['ar','p',str(original_archive),member])).digest()==hashlib.sha256(subprocess.check_output(['ar','p',str(archive),member])).digest()
 save('archive-members.json',{'members':members,'only_changed':server.name,'all_other_member_bytes_identical':True,'original_archive_sha256':sha(original_archive),'candidate_archive_sha256':sha(archive)})
 def link(out,label,main=None,extra=()):
  argv=[str(archive) if x==ga[0] else str(main) if main is not None and x==gm[0] else x for x in gl if not x.startswith('-Wl,-Map=')]
  argv[argv.index('-o')+1]=str(out)
  if extra:argv[argv.index('-o'):argv.index('-o')]=list(map(str,extra))
  argv.append('-Wl,-Map='+str(out)+'.map');invoke(argv,label)
 exe=private/'gateway_demo';link(exe,'gateway-runtime-link')
 symbols=run(['nm','-C',str(exe)]);required=['tinyimx::OnlineStatusCache::GetOnlineStatusBatch(','tinyimx::GatewayRouteResolver::ResolveBatch(','tinyimx::GatewayServer::DispatchGroupFanoutDeliveries(','tinyimx::rpc::MessageRpcClient::CompleteGroupMessageDeliveryAttempts(']
 assert all(x in symbols for x in required) and '__wrap_' not in symbols
 save('runtime-symbols.json',{'all_prior_required_symbols_preserved':True,'wrappers_absent':True,'main_exact_reused_after_class_layout_unchanged':True})
 newtest=compilefile('benchmark/local_capacity/group_message_runtime_test.cpp','group-message-runtime-test')
 trackertest=compilefile('tests/gateway/receiver_delivery_tracker_test.cpp','original-tracker-test')
 native=private/'group-message-runtime-native';link(native,'runtime-native-link',newtest,[trackertest])
 def envbase():
  env=dict(os.environ)
  for key in list(env):
   if key.startswith('TINYIMX_GROUP_'):env.pop(key)
  return env
 for label,value,enabled in [('OFF',None,False),('ON','1',True),('INVALID','01',False),('ZERO','0',False)]:
  env=envbase();env['TINYIMX_GROUP_DELIVERY_RECIPIENT_ORDER_ENABLE']='1'
  if value is not None:env['TINYIMX_GROUP_DELIVERY_MESSAGE_RUNTIME_ENABLE']=value
  text=invoke([str(native),'1' if enabled else '0'],'runtime-'+label,90,env)
  (d/('runtime-'+label+'.log')).write_text(text);result=json.loads(next(x for x in text.splitlines() if x.startswith('{')))
  assert result['status']=='GROUP_MESSAGE_RUNTIME_NATIVE_PASS' and result['tests']==31 and result['failures']==0 and '[FAIL]' not in text
  cases.append({'case':label,**result});save('native-cases.json',cases)
 text=invoke([str(old/'runtime-private/executor-native')],'executor-original',90,envbase());assert 'total=14, failed=0' in text
 (d/'executor-original.log').write_text(text)
 for label,value,enabled in [('OFF',None,False),('ON','1',True),('INVALID','true',False)]:
  env=envbase()
  if value is not None:env['TINYIMX_GROUP_DELIVERY_RECIPIENT_ORDER_ENABLE']=value
  text=invoke([str(old/'runtime-private/order-native'),'1' if enabled else '0'],'order-'+label,90,env)
  (d/('order-'+label+'.log')).write_text(text);assert 'GROUP_DELIVERY_ORDER_NATIVE_PASS' in text and '[FAIL]' not in text
 context=private/'gateway-context';context.mkdir();shutil.copy2(exe,context/exe.name);os.chmod(context/exe.name,0o755)
 (context/'Dockerfile').write_text('FROM '+original['images']['gateway']['image_tag']+'\nCOPY --chmod=0755 gateway_demo /opt/tinyimx/bin/\nLABEL org.opencontainers.image.revision="'+head+'"\n')
 invoke(['docker','build','--pull=false','--network=none','-t',image,str(context)],'gateway-image',120)
 for label,argv,code in [('ldd',['/usr/bin/ldd','-r','/opt/tinyimx/bin/gateway_demo'],0),('missing-config',['/opt/tinyimx/bin/gateway_demo','/__codex_group_runtime_missing__.json'],1)]:
  name='codex-group-message-runtime-'+label+'-20261006';assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0
  text=invoke(['docker','run','--name',name,'--label','codex.tinyimx.group_message_runtime=20261006','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges:true',image,*argv],label,30,expected=code)
  if label=='ldd':assert 'not found' not in text and 'undefined symbol' not in text
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in sources.items())
 images={'gateway':{'image_tag':image,'image_id':json.loads(run(['docker','image','inspect',image]))[0]['Id'],'elf_sha256':sha(exe)},'message':original['images']['message']}
 save('summary.json',{'status':'GROUP_MESSAGE_RUNTIME_NATIVE_AND_IMAGE_PASS','head':head,'images':images,'runtime_and_original_tracker_checks':124,'original_executor_checks':14,'original_recipient_checks':6,'checks':144,'coordinator_checks_inherited':164,'resolver_checks_inherited':128,'primitive_checks_inherited':720,'inherited_message_rpc_checks':62,'original_message_unit_pass':True,'protocol_exact_preservation_checks':1,'borrowed_preserved':True,'no_new_product_threads':True,'runtime_deploy':False,'performance_acceptance':False})
 print((d/'summary.json').read_text(),flush=True)
except BaseException as error:save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'runtime_deploy':False});raise
finally:
 after=runtime();save('runtime-after.json',after);assert before==after
PY
