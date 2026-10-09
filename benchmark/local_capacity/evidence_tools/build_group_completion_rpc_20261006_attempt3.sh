#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,shlex,shutil,os,signal,re,datetime,tarfile
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-completion-rpc-build-20261006-attempt3';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
source=json.loads((b/'group-completion-rpc-source-20261006/summary.json').read_text())
tests=json.loads((b/'group-completion-native-source-20261006/summary.json').read_text())
helper=json.loads((b/'group-completion-rpc-build3-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==helper['head']
sources={**source['files'],**tests['files'],**helper['files']}
assert all(sha(r/n)==h for n,h in sources.items())
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'file-begin-snapshot-control-20261006-attempt3/restore-summary.json').read_text())['runtime']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
cache=r/'build/linux-release'
ml=json.loads((b/'private-batch-message-build-image-20261005/link-full-message-service-command.json').read_text())
gl=json.loads((b/'group-delivery-ordering-build-20261006-attempt3/gateway-link-process.json').read_text())['argv']
assert not any('--wrap' in x for x in ml+gl)
assert sha(b/'private-batch-message-build-image-20261005/message_service_demo')=='2548733766409d282d30f6ffbbbe47f9c12b5d4fa3cacfdcc3d3e47f72582699'
assert sha(b/'group-delivery-ordering-build-20261006-attempt3/runtime-private/gateway_demo')=='c246657a6152909b3fd8a780703123dbccc78a8b9d087f20970e52a83f864103'
oldga=list(dict.fromkeys(x for x in gl if x.endswith('libtinyimx_gateway.a')));gm=[x for x in gl if x.endswith('gateway_demo.cpp.o')];mm=[x for x in ml if x.endswith('/demo-main.o')]
assert len(oldga)==len(gm)==len(mm)==1
targets=['tinyimx_rpc_proto','tinyimx_rpc_client','tinyimx_message_core','tinyimx_message_grpc','tinyimx_gateway','message_service_demo','gateway_demo','tinyimx_repository']
borrowed={}
for token in ml+gl:
 p=pathlib.Path(token) if pathlib.Path(token).is_absolute() else cache/token
 if p.is_file():borrowed[str(p.resolve())]=sha(p)
for target in targets:
 p=cache/('CMakeFiles/'+target+'.dir/flags.make');assert p.exists();borrowed[str(p)]=sha(p)
for p in (cache/'generated/rpc').rglob('*'):
 if p.is_file():borrowed[str(p)]=sha(p)
protoc=r/'vcpkg_installed/x64-linux/tools/protobuf/protoc-33.4.0';plugin=r/'vcpkg_installed/x64-linux/tools/grpc/grpc_cpp_plugin'
assert protoc.is_file() and plugin.is_file()
borrowed[str(protoc)]=sha(protoc);borrowed[str(plugin)]=sha(plugin)
ready=b/'rpc-readiness-build-20261006-attempt2';ra=json.loads((ready/'audit-before.json').read_text());readyresult=json.loads((ready/'summary.json').read_text())
assert readyresult['native_checks']==97
ready_sources={n:h for n,h in ra['sources'].items() if n in ['examples/message_service_demo.cpp','services/message/server/MessageServiceServer.cpp','services/message/server/MessageServiceServer.h','common/runtime/RpcReadiness.h','common/runtime/RpcReadinessProbe.h','examples/rpc_readiness_probe.cpp']}
assert len(ready_sources)==6 and all(sha(r/n)==h for n,h in ready_sources.items())
extra=['examples/gateway_demo.cpp','tests/message/message_application_service_test.cpp','tests/gateway/group_fanout_coordinator_test.cpp','benchmark/local_capacity/group_fanout_commit_wakeup_test.cpp','benchmark/local_capacity/group_fanout_pipeline_test.cpp','benchmark/local_capacity/group_fanout_partial_drain_test.cpp','benchmark/local_capacity/group_delivery_ordering_test.cpp','tests/gateway/business_executor_test.cpp','services/repository/MessageRepository.cpp','services/repository/MessageRepository.h','benchmark/local_capacity/group_delivery_completion_batch_test.cpp']
extra_sha={n:sha(r/n) for n in extra};sources.update(ready_sources);sources.update(extra_sha)
assert extra_sha['examples/gateway_demo.cpp']=='7797c8d9043d23d5cc27770f9bf0c8d760a036f176801cd08bdaba80069bd8e6'
assert extra_sha['services/repository/MessageRepository.cpp']=='e3992e0b3379f33c53feaba5f328acf3f1349289e6bb8ee5773626b9f36fe487'
probe=ready/'runtime-private/rpc_readiness_probe';assert sha(probe)==readyresult['probe_elf_sha256'];borrowed[str(probe)]=sha(probe)
repoobj=b/'group-completion-batch-build-test-20261006/runtime-private/MessageRepository.o';assert repoobj.is_file();borrowed[str(repoobj)]=sha(repoobj)
macro=subprocess.check_output(['git','show','30452a51d08561593a6aba2fbe1ef188ff06d1c7:common/logging/LogMacros.h'])
assert hashlib.sha256(macro).hexdigest()=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
def sql(q):
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-completion-rpc',q])
durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'
assert sql(durability).strip()=='1\t1\t1\t0\t0'
schemas={pool:f'codex_group_complete_20261006_rpc_p{pool}' for pool in [1,4]}
assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME IN ('"+"','".join(schemas.values())+"')").strip()=='0'
tables=['im_users','im_groups','im_group_messages','im_group_message_deliveries'];ddl={}
for table in tables:
 create=sql('SHOW CREATE TABLE '+table).split('\t',1)[1].strip()
 assert create.startswith('CREATE TABLE '+chr(96)+table+chr(96)) and ';' not in create
 assert not re.search(r'REFERENCES\s+'+chr(96)+r'[^'+chr(96)+r']+'+chr(96)+r'\.',create)
 assert all(x in tables for x in re.findall(r'REFERENCES\s+'+chr(96)+r'([^'+chr(96)+r']+)'+chr(96),create));ddl[table]=create
mc=json.loads(run(['docker','inspect','tinyimx-m21-mysql-1']))[0];menv=dict(x.split('=',1) for x in mc['Config']['Env'] if '=' in x)
plans={schema:'CREATE DATABASE '+schema+' CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;\nUSE '+schema+';\n'+';\n'.join(ddl.values())+';\n' for schema in schemas.values()}
images={'gateway':'tinyimx/runtime:codex-group-completion-batch-gateway-v1-20261006','message':'tinyimx/runtime:codex-group-completion-batch-message-v1-20261006'}
assert all(subprocess.run(['docker','image','inspect',tag],capture_output=True).returncode!=0 for tag in images.values())
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700);phase='initial';completed=[]
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'source_head':source['head'],'source_sha256':sources,'borrowed_sha256':borrowed,'runtime_before':before,'operation':'Private pinned codegen/archives. Recompile allocating Gateway+Message mains, changed client/app/adapter/impl/coordinator and coordinator test TUs. Exact old eager logging overlay. No cache overwrites. Message tested readiness source fixed both ABBA modes. Owned RPC SQL schemas only. Seal images but no deployment.','schema_plan_sha256':{n:hashlib.sha256(v.encode()).hexdigest() for n,v in plans.items()},'rollback':'All originals, objects, rows and failed raw preserved; only verified own build child stopped on timeout; no DELETE/DROP/prune/reset/push','performance_acceptance':False})
save('runtime-before.json',before)
reuse=b/'group-completion-rpc-build-20261006';failure=json.loads((reuse/'failed.json').read_text());original_audit=json.loads((reuse/'audit-before.json').read_text())
assert failure=={'status':'FAIL','phase':'message-runtime-link','type':'AssertionError','message':'','runtime_deploy':False}
assert original_audit['head']=='b9761a5086559822c7b4c229fdb4393dad4fa9bc' and all(sha(r/n)==h for n,h in original_audit['source_sha256'].items() if not n.startswith('docs/'))
assert all(sha(p)==h for p,h in original_audit['borrowed_sha256'].items())
reuse_labels=['message_service.pb.cc','message_service.grpc.pb.cc','MessageRpcClient.cpp','GroupFanoutCoordinator.cpp','gateway-main','MessageRepositoryAdapter','MessageApplicationService','MessageServiceImpl','MessageServiceServer','message-main']
reused={label:reuse/'runtime-private'/(label+'.o') for label in reuse_labels};reused_sha={str(p):sha(p) for p in reused.values()}
for label,p in reused.items():
 assert p.is_file() and pathlib.Path(str(p)+'.d').is_file()
 assert json.loads((reuse/('compile-'+label+'-audit-before.json')).read_text())['argv'][-1]==str(p)
borrowed.update(reused_sha)
save('reuse-audit-before.json',{'source_sha256':original_audit['source_sha256'],'sha256':reused_sha,'reason':'Bothlinks succeeded, only roleassertion failed. Source and all original inputs match. Fresh generated headers must match byte for byte before any reuse. No old object modifications.'})
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
 assert shutil.disk_usage(r).free>2*1024**3
def interrupted(sig,frame):raise RuntimeError('Owned completion RPC build interrupted '+str(sig))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def invoke(a,label,seconds=240,env=None,expected=0):
 global phase
 phase=label;resources();save(label+'-audit-before.json',{'argv':a,'timeout':seconds})
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(a,cwd=cache,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path('/proc')/str(p.pid)
  try:ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
  except FileNotFoundError:assert p.poll() is not None;ticks=None
  save(label+'-process.json',{'pid':p.pid,'start_ticks':ticks,'argv':a})
  try:code=p.wait(timeout=seconds)
  finally:
   if p.poll() is None:
    assert ticks and (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(a)]==[str(x).encode() for x in a]
    save(label+'-stop-audit.json',{'pid':p.pid,'operation':'Only verified own PGID'});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed; raw retained'
 print(json.dumps({'completed':label,'exit':code}),flush=True);return (private/(label+'.log')).read_text()
try:
 overlay=private/'original-includes/common/logging';overlay.mkdir(parents=True);(overlay/'LogMacros.h').write_bytes(macro)
 generated=private/'generated/rpc';generated.mkdir(parents=True)
 proto='tinyimx/message/v1/message_service.proto'
 invoke([str(protoc),'--cpp_out='+str(generated),'--grpc_out='+str(generated),'--plugin=protoc-gen-grpc='+str(plugin),'-I'+str(r/'proto'),str(r/'proto'/proto)],'pinned-message-codegen',60)
 baseline=private/'baseline-proto'/proto;baseline.parent.mkdir(parents=True)
 preimage=b/'group-completion-rpc-source-20261006/runtime-private/source-before.tar'
 with tarfile.open(preimage) as tf:raw=tf.extractfile('proto/'+proto).read()
 assert hashlib.sha256(raw).hexdigest()==json.loads((b/'group-completion-rpc-source-20261006/audit-before.json').read_text())['source_before']['proto/'+proto]
 baseline.write_bytes(raw);descold=private/'old-descriptor.pb';descnew=private/'new-descriptor.pb'
 invoke([str(protoc),'--descriptor_set_out='+str(descold),'--include_imports','-I'+str(private/'baseline-proto'),'-I'+str(r/'proto'),str(baseline)],'old-protocol-descriptor',60)
 invoke([str(protoc),'--descriptor_set_out='+str(descnew),'--include_imports','-I'+str(r/'proto'),str(r/'proto'/proto)],'new-protocol-descriptor',60)
 def compilefile(path,label,target='tinyimx_gateway'):
  path=pathlib.Path(path);path=path if path.is_absolute() else r/path
  flags=[];text=(cache/('CMakeFiles/'+target+'.dir/flags.make')).read_text()
  for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in text.splitlines() if x.startswith(key+' =')).split('=',1)[1])
  if label in reused:
   for name in ['message_service.pb.h','message_service.grpc.pb.h']:
    assert sha(generated/'tinyimx/message/v1'/name)==sha(reuse/'runtime-private/generated/rpc/tinyimx/message/v1'/name)
   assert sha(reused[label])==reused_sha[str(reused[label])]
   print(json.dumps({'reused_verified_TU':label}),flush=True)
   return reused[label]
  obj=private/(label+'.o')
  invoke(['/usr/bin/c++','-I'+str(overlay.parents[1]),'-I'+str(generated),*flags,'-MD','-MF',str(obj)+'.d','-c',str(path),'-o',str(obj)],'compile-'+label)
  return obj
 pb=compilefile(generated/'tinyimx/message/v1/message_service.pb.cc','message_service.pb.cc','tinyimx_rpc_proto')
 grpcpb=compilefile(generated/'tinyimx/message/v1/message_service.grpc.pb.cc','message_service.grpc.pb.cc','tinyimx_rpc_proto')
 client=compilefile('services/rpc/MessageRpcClient.cpp','MessageRpcClient.cpp','tinyimx_rpc_client')
 coord=compilefile('gateway/GroupFanoutCoordinator.cpp','GroupFanoutCoordinator.cpp')
 mainG=compilefile('examples/gateway_demo.cpp','gateway-main','gateway_demo')
 adapter=compilefile('services/message/repository/MessageRepositoryAdapter.cpp','MessageRepositoryAdapter','tinyimx_message_core')
 app=compilefile('services/message/application/MessageApplicationService.cpp','MessageApplicationService','tinyimx_message_core')
 impl=compilefile('services/message/service/MessageServiceImpl.cpp','MessageServiceImpl','tinyimx_message_grpc')
 server=compilefile('services/message/server/MessageServiceServer.cpp','MessageServiceServer','tinyimx_message_grpc')
 mainM=compilefile('examples/message_service_demo.cpp','message-main','message_service_demo')
 for obj in [coord,mainG,adapter,impl,mainM]:
  actual_overlay=(reuse/'runtime-private/original-includes/common/logging/LogMacros.h') if obj in reused.values() else overlay/'LogMacros.h'
  assert sha(actual_overlay)==hashlib.sha256(macro).hexdigest()
  assert str(actual_overlay) in pathlib.Path(str(obj)+'.d').read_text()
 def copiedarchive(original,name,objects):
  target=private/name;shutil.copy2(original,target)
  beforemembers=run(['ar','t',str(original)]).splitlines()
  assert all(x.name in beforemembers for x in objects)
  invoke(['ar','rcs',str(target),*map(str,objects)],'archive-'+name,90)
  assert run(['ar','t',str(target)]).splitlines()==beforemembers
  # Prove all unchanged archive members retain their exact bytes.
  for member in beforemembers:
   if member not in {x.name for x in objects}:
    assert hashlib.sha256(subprocess.check_output(['ar','p',str(original),member])).digest()==hashlib.sha256(subprocess.check_output(['ar','p',str(target),member])).digest()
  return target
 protoa=copiedarchive(cache/'libtinyimx_rpc_proto.a','libtinyimx_rpc_proto.a',[pb,grpcpb])
 clienta=copiedarchive(cache/'libtinyimx_rpc_client.a','libtinyimx_rpc_client.a',[client])
 ga=copiedarchive(pathlib.Path(oldga[0]),'libtinyimx_gateway.a',[coord])
 def linkfile(argv,out,label,replacements,extra=(),wrap=False):
  args=[replacements.get(x,x) for x in argv if not x.startswith('-Wl,-Map=')]
  args[args.index('-o')+1]=str(out)
  if extra:args[args.index('-o'):args.index('-o')]=list(map(str,extra))
  args+=['-Wl,-Map='+str(out)+'.map']
  if wrap:args+=['-Wl,--wrap=mysql_query','-Wl,--wrap=mysql_ping']
  invoke(args,label)
 replacementsG={oldga[0]:str(ga),gm[0]:str(mainG),'libtinyimx_rpc_client.a':str(clienta),'libtinyimx_rpc_proto.a':str(protoa)}
 replacementsM={'libtinyimx_rpc_client.a':str(clienta),'libtinyimx_rpc_proto.a':str(protoa),mm[0]:str(mainM)}
 for x in ml:
  if x.endswith('/MessageRepository.o'):replacementsM[x]=str(repoobj)
  elif x.endswith('/MessageRepositoryAdapter.o'):replacementsM[x]=str(adapter)
  elif x.endswith('/MessageApplicationService.o'):replacementsM[x]=str(app)
  elif x.endswith('/service-impl.o'):replacementsM[x]=str(impl)
 exeG=private/'gateway_demo';exeM=private/'message_service_demo'
 linkfile(gl,exeG,'gateway-runtime-link',replacementsG)
 linkfile(ml,exeM,'message-runtime-link',replacementsM,[server])
 symbol_proof={}
 for exe,required in [(exeG,['MessageRpcClient::CompleteGroupMessageDeliveryAttempts(']),(exeM,['MessageServiceImpl::CompleteGroupMessageDeliveryAttempts(','MessageApplicationService::CompleteGroupMessageDeliveryAttempts(','MessageRepositoryAdapter::CompleteGroupMessageDeliveryAttempts(','MessageRepository::CompleteGroupMessageDeliveryAttempts(','MessageServiceServer::SetReady('])]:
  symbols=run(['nm','-C',str(exe)],40)
  assert '__wrap_mysql' not in symbols and '__real_mysql' not in symbols
  assert all(name in symbols for name in required)
  symbol_proof[exe.name]={'required_symbols':{name:True for name in required},'wrappers_absent':True}
 save('runtime-role-symbol-verification.json',symbol_proof)
 protocol=private/'protocol.cpp'
 protocol.write_text('#include <google/protobuf/descriptor.pb.h>\n#include <fstream>\n#include <iostream>\nint main(int n,char**v){if(n!=3)return 2;google::protobuf::FileDescriptorSet a,b;std::ifstream x(v[1],std::ios::binary),y(v[2],std::ios::binary);if(!a.ParseFromIstream(&x)||!b.ParseFromIstream(&y))return 3;auto*f=b.mutable_file(b.file_size()-1);if(f->service_size()!=1||f->service(0).method_size()!=18||f->service(0).method(17).name()!="CompleteGroupMessageDeliveryAttempts")return 4;f->mutable_service(0)->mutable_method()->RemoveLast();if(f->message_type(f->message_type_size()-1).name()!="CompleteGroupMessageDeliveryAttemptsRequest")return 5;f->mutable_message_type()->RemoveLast();if(a.SerializeAsString()!=b.SerializeAsString())return 6;std::cout<<"{\\\"status\\\":\\\"EXISTING_PROTOCOL_EXACTLY_PRESERVED\\\",\\\"checks\\\":1}\\n";return 0;}\n')
 pobj=compilefile(protocol,'protocol-test','tinyimx_rpc_proto');pex=private/'protocol-test'
 linkfile(ml,pex,'protocol-link',{**replacementsM,mm[0]:str(pobj)},[server])
 text=invoke([str(pex),str(descold),str(descnew)],'protocol-preservation',60);(d/'protocol-preservation.log').write_text(text)
 assert json.loads(next(x for x in text.splitlines() if x.startswith('{')))['status']=='EXISTING_PROTOCOL_EXACTLY_PRESERVED'
 testmap={'wake':'benchmark/local_capacity/group_fanout_commit_wakeup_test.cpp','pipeline':'benchmark/local_capacity/group_fanout_pipeline_test.cpp','partial':'benchmark/local_capacity/group_fanout_partial_drain_test.cpp','order':'benchmark/local_capacity/group_delivery_ordering_test.cpp','executor':'tests/gateway/business_executor_test.cpp','batch':'benchmark/local_capacity/group_fanout_completion_batch_test.cpp','lease':'tests/gateway/group_fanout_coordinator_test.cpp'}
 to={kind:compilefile(path,kind+'-test') for kind,path in testmap.items()}
 nativecases=[]
 def native(kind,args,env,label):
  text=invoke([str(private/(kind+'-native')),*args],label,60,env);(d/(label+'.log')).write_text(text)
  if kind=='executor':assert 'total=14, failed=0' in text;result={'checks':14,'status':'ORIGINAL_EXECUTOR_PASS'}
  else:result=json.loads(next(x for x in text.splitlines() if x.startswith('{')));assert result.get('failures',0)==0 and 'PASS' in result['status']
  assert '[FAIL]' not in text
  nativecases.append({'kind':kind,'case':label,**result});save('native-cases.json',nativecases)
 for kind in testmap:
  if kind=='lease':continue
  extraobjs=[to['lease']] if kind in ['wake','pipeline'] else []
  linkfile(gl,private/(kind+'-native'),kind+'-native-link',{**replacementsG,gm[0]:str(to[kind])},extraobjs)
 for kind,key in [('wake','TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE'),('pipeline','TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE'),('order','TINYIMX_GROUP_DELIVERY_RECIPIENT_ORDER_ENABLE')]:
  for case,value,enabled in [('OFF',None,False),('ON','1',True),('INVALID','true',False)]:
   env=dict(os.environ)
   for v in ['TINYIMX_GROUP_FANOUT_COMPLETION_BATCH_ENABLE','TINYIMX_GROUP_FANOUT_PARTIAL_DRAIN_ENABLE','TINYIMX_GROUP_FANOUT_PHASE_TRACE_ENABLE']:env.pop(v,None)
   if value is None:env.pop(key,None)
   else:env[key]=value
   native(kind,['1' if enabled else '0'],env,kind+'-'+case)
 env=dict(os.environ);native('executor',[],env,'executor-original')
 for case,value,wake,enabled in [('OFF',None,True,False),('ON','1',True,True),('INVALID','01',True,False),('WAKE_OFF','1',False,True)]:
  env=dict(os.environ);env['TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE']='1' if wake else '0';env['TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE']='1';env.pop('TINYIMX_GROUP_FANOUT_PHASE_TRACE_ENABLE',None);env.pop('TINYIMX_GROUP_FANOUT_COMPLETION_BATCH_ENABLE',None)
  if value is None:env.pop('TINYIMX_GROUP_FANOUT_PARTIAL_DRAIN_ENABLE',None)
  else:env['TINYIMX_GROUP_FANOUT_PARTIAL_DRAIN_ENABLE']=value
  native('partial',['1' if enabled else '0','1' if wake else '0'],env,'partial-'+case)
 for case,value,defer,enabled in [('OFF',None,True,False),('ON','1',True,True),('INVALID','01',True,False),('DEFER_OFF','1',False,True)]:
  env=dict(os.environ);env['TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE']='1' if defer else '0';env.pop('TINYIMX_GROUP_FANOUT_PHASE_TRACE_ENABLE',None)
  if value is None:env.pop('TINYIMX_GROUP_FANOUT_COMPLETION_BATCH_ENABLE',None)
  else:env['TINYIMX_GROUP_FANOUT_COMPLETION_BATCH_ENABLE']=value
  native('batch',['1' if enabled else '0','1' if defer else '0'],env,'batch-'+case)
 unitobj=compilefile('tests/message/message_application_service_test.cpp','original-message-unit','tinyimx_message_core');unit=private/'message-unit'
 linkfile(ml,unit,'message-unit-link',{**replacementsM,mm[0]:str(unitobj)},[server])
 text=invoke([str(unit)],'original-message-unit',60);assert 'failed=0' in text and '[FAIL]' not in text;(d/'original-message-unit.log').write_text(text)
 rpcobj=compilefile('benchmark/local_capacity/group_delivery_completion_rpc_test.cpp','rpc-integration','tinyimx_message_core');rpcbinary=private/'completion-rpc-native'
 linkfile(ml,rpcbinary,'rpc-native-link',{**replacementsM,mm[0]:str(rpcobj)},[server],True)
 rpccases=[]
 for pool,schema in schemas.items():
  phase='schema-'+schema
  assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME='"+schema+"'").strip()=='0'
  (d/(schema+'-create.sql')).write_text(plans[schema]);save(schema+'-ddl-audit-before.json',{'schema':schema,'absent_before':True,'plan_sha256':hashlib.sha256(plans[schema].encode()).hexdigest(),'production_mutations':False})
  sql(plans[schema])
  cfg=json.loads(pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json').read_text())
  cfg['mysql'].update({'host':next(v['IPAddress'] for v in mc['NetworkSettings']['Networks'].values() if v.get('IPAddress')),'port':3306,'user':'root','password':menv['MYSQL_ROOT_PASSWORD'],'database':schema,'pool_size':pool})
  cfgpath=private/(schema+'.json');cfgpath.write_text(json.dumps(cfg));os.chmod(cfgpath,0o600)
  env=dict(os.environ);env['TINYIMX_GROUP_DELIVERY_CLAIM_BATCH_ENABLE']='1'
  label='rpc-p'+str(pool);text=invoke([str(rpcbinary),str(cfgpath)],label,120,env);(d/(label+'.log')).write_text(text)
  result=json.loads(next(x for x in text.splitlines() if x.startswith('{"checks":')))
  assert result['status']=='GROUP_COMPLETION_RPC_REAL_MYSQL_PASS' and result['checks']==text.count('[PASS]') and result['pool']==pool and '[FAIL]' not in text
  rpccases.append({'schema':schema,**result});save('rpc-native-cases.json',rpccases)
  print(json.dumps(result),flush=True)
 assert sql(durability).strip()=='1\t1\t1\t0\t0'
 bases={'gateway':'tinyimx/runtime:codex-group-recipient-order-v1-20261006','message':'tinyimx/runtime:codex-private-begin-insert-read-batch-v1'}
 for kind,exe in [('gateway',exeG),('message',exeM)]:
  context=private/(kind+'-context');context.mkdir();shutil.copy2(exe,context/exe.name)
  if kind=='message':shutil.copy2(probe,context/'rpc_readiness_probe')
  for p in context.iterdir():os.chmod(p,0o755)
  (context/'Dockerfile').write_text('FROM '+bases[kind]+'\nCOPY --chmod=0755 '+exe.name+(' rpc_readiness_probe' if kind=='message' else '')+' /opt/tinyimx/bin/\nLABEL org.opencontainers.image.revision="'+head+'"\n')
  invoke(['docker','build','--pull=false','--network=none','-t',images[kind],str(context)],kind+'-image',120)
  for check,args,code in [('ldd',['/usr/bin/ldd','-r','/opt/tinyimx/bin/'+exe.name],0),('missing-config',['/opt/tinyimx/bin/'+exe.name,'/__codex_completion_missing__.json'],1)]:
   name='codex-group-complete-'+kind+'-'+check+'-20261006'
   assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0
   text=invoke(['docker','run','--name',name,'--label','codex.tinyimx.group_complete=20261006','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges:true',images[kind],*args],kind+'-'+check,30,expected=code)
   if check=='ldd':assert 'not found' not in text and 'undefined symbol' not in text
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in sources.items())
 info={kind:{'image_tag':images[kind],'image_id':json.loads(run(['docker','image','inspect',images[kind]]))[0]['Id'],'elf_sha256':sha(exe)} for kind,exe in [('gateway',exeG),('message',exeM)]}
 x={'status':'GROUP_COMPLETION_RPC_NATIVE_AND_IMAGES_PASS','head':head,'source_head':source['head'],'images':info,'native_cases':nativecases,'coordinator_checks':sum(x.get('checks',x.get('tests',0)) for x in nativecases),'rpc_cases':rpccases,'rpc_checks':sum(x['checks'] for x in rpccases),'protocol_exact_preservation_checks':1,'original_message_unit_pass':True,'probe_elf_sha256':sha(probe),'borrowed_artifacts_preserved':True,'durability':'1/1/1/0/0','runtime_deploy':False,'performance_acceptance':False}
 save('summary.json',x);print(json.dumps({k:v for k,v in x.items() if k not in ['native_cases','rpc_cases']},indent=2),flush=True)
except BaseException as error:
 save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'runtime_deploy':False});raise
finally:
 after=runtime();assert before==after;save('runtime-after.json',after)
PY
