#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,shlex,shutil,os,signal,re,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-route-integration-build-20261006';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t,stderr=subprocess.STDOUT)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
source=json.loads((b/'group-route-integration-source-20261006/summary.json').read_text())
primitive=json.loads((b/'group-route-read-batch-source-20261006/summary.json').read_text())
helper=json.loads((b/'group-route-integration-build-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==helper['head']
frozen=b/'group-completion-rpc-build-20261006-attempt4';old=b/'group-completion-rpc-build-20261006-attempt6'
original=json.loads((old/'summary.json').read_text());assert original['status']=='GROUP_COMPLETION_RPC_NATIVE_AND_IMAGES_PASS' and original['coordinator_checks']==128 and original['rpc_checks']==62
prior=json.loads((old/'audit-before.json').read_text())
sources={n:h for n,h in prior['source_sha256'].items() if not n.startswith('docs/')}
sources.update({n:h for n,h in primitive['files'].items() if not n.startswith('docs/')})
sources.update({n:h for n,h in source['files'].items() if not n.startswith('docs/')})
sources.update(helper['files']);assert all(sha(r/n)==h for n,h in sources.items())
native=b/'group-route-read-batch-native-20261006';passed=json.loads((native/'summary.json').read_text());assert passed['status']=='GROUP_ROUTE_READ_BATCH_NATIVE_COMPLETE' and passed['checks']==720
api=native/'api.o';api_sha=json.loads((native/'compiled-sha256.json').read_text())['api.o'];assert sha(api)==api_sha
cache=r/'build/linux-release';gl=json.loads((frozen/'gateway-runtime-link-process.json').read_text())['argv'];assert not any('--wrap' in x for x in gl)
ga=list(dict.fromkeys(x for x in gl if x.endswith('libtinyimx_gateway.a')));cs=list(dict.fromkeys(x for x in gl if x.endswith('libtinyimx_cache_service.a')));gm=[x for x in gl if x.endswith('/gateway-main.o')];assert len(ga)==len(cs)==len(gm)==1
targets=['tinyimx_gateway','gateway_demo','tinyimx_cache_service','redis_pool_demo']
def flagfields(target):
 text=(cache/('CMakeFiles/'+target+'.dir/flags.make')).read_text()
 return {key:next(x for x in text.splitlines() if x.startswith(key+' =')).split('=',1)[1] for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']}
assert all(flagfields('redis_pool_demo')[x]==flagfields('tinyimx_cache_service')[x] for x in ['CXX_DEFINES','CXX_FLAGS'])
overlay=b/'group-completion-rpc-build-20261006/runtime-private/original-includes'
assert sha(overlay/'common/logging/LogMacros.h')=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
assert str(overlay/'common/logging/LogMacros.h') in pathlib.Path(str(api)+'.d').read_text()
borrowed={str(api):api_sha,str(overlay/'common/logging/LogMacros.h'):sha(overlay/'common/logging/LogMacros.h')}
for x in gl:
 p=pathlib.Path(x) if pathlib.Path(x).is_absolute() else cache/x
 if p.is_file():borrowed[str(p.resolve())]=sha(p)
for t in targets:
 p=cache/('CMakeFiles/'+t+'.dir/flags.make');borrowed[str(p)]=sha(p)
for p in [frozen/'gateway-runtime-link-process.json',old/'summary.json',native/'summary.json',native/'compiled-sha256.json']:
 borrowed[str(p)]=sha(p)
for p in (b/'group-completion-rpc-build-20261006/runtime-private/generated/rpc').rglob('*'):
 if p.is_file():borrowed[str(p)]=sha(p)
testmap={'wake':'benchmark/local_capacity/group_fanout_commit_wakeup_test.cpp','pipeline':'benchmark/local_capacity/group_fanout_pipeline_test.cpp','partial':'benchmark/local_capacity/group_fanout_partial_drain_test.cpp','order':'benchmark/local_capacity/group_delivery_ordering_test.cpp','executor':'tests/gateway/business_executor_test.cpp','completion':'benchmark/local_capacity/group_fanout_completion_batch_test.cpp','lease':'tests/gateway/group_fanout_coordinator_test.cpp','route':'benchmark/local_capacity/group_fanout_route_batch_test.cpp','resolver':'benchmark/local_capacity/group_route_resolver_batch_test.cpp'}
for n in ['examples/gateway_demo.cpp',*testmap.values(),'common/cache/RedisConnectionPool.h','common/cache/RedisConnection.h','services/registry/GatewayDiscovery.h','services/registry/GatewayRegistry.h']:
 sources[n]=sha(r/n)
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 containers=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in containers)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in containers},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'group-completion-batch-control-20261006-attempt2/restore-summary.json').read_text())['runtime']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
pre=json.loads((b/'all-feature-optimization-preflight-20261006-attempt2/summary.json').read_text());assert pre['redis_cluster_enabled']==0
redis=json.loads(run(['docker','inspect','tinyimx-m21-redis-1']))[0];assert redis['Id']==pre['runtime']['/tinyimx-m21-redis-1']['id'] and {v['IPAddress'] for v in redis['NetworkSettings']['Networks'].values() if v.get('IPAddress')}=={pre['native_redis_ip']}
image='tinyimx/runtime:codex-group-route-read-batch-gateway-v1-20261006';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
assert json.loads(run(['docker','image','inspect',original['images']['gateway']['image_tag']]))[0]['Id']==original['images']['gateway']['image_id']
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'source_sha256':sources,'borrowed_sha256':borrowed,'runtime_before':before,'operation':'Recompile GatewayServer,RouteResolver,Coordinator and allocating GatewayMain plus every coordinator test TU. Reuse720PASS OnlineStatusAPI object only after source/macros/defines/CXXFLAGS exact verification. Copy original accepted archives and replace only named members with unchanged-member proof; no cache overwritten. Recheck original128 plus new route36/coordinator and actual ownedRedis/registry resolver tests. Seal new Gateway image only, Message exact62PASS unchanged. No deployment.','native_data_scope':'<=12 new own presence keys and2 registry records/index perpool under codex:group-route-resolver-native-20261006:p1/p4:, everyinitialkeymustabsent,TTL900 inclindex. Two precise own presence/endpoint replacements only. No productionkeys/DEL/FLUSH/CONFIG/SQL/globalrestart','ABI':'Gateway and RouteResolver data/virtual layout unchanged; Coordinator fifthcallback requires all allocating main/nativeTUs recompiled','limits':'Native semantics/image only; real ABBA and allfunction10k50k acceptance pending','rollback':'Keep all raw/preimages/sources/ELFs/images/ownTTL data, only verified own child PGID stop on watchdog; no deletion/reset/prune/push orhostapps change','performance_acceptance':False})
phase='initial';nativecases=[];resolvercases=[]
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024 and shutil.disk_usage(r).free>2*1024**3
def interrupted(sig,frame):raise RuntimeError('Own route build interrupted '+str(sig))
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
    save(label+'-stop-audit.json',{'operation':'Only verified own PGID','pid':child.pid,'start_ticks':ticks});os.killpg(child.pid,signal.SIGTERM)
    try:child.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
 assert code==expected,label+' failed; full raw retained'
 print(json.dumps({'completed':label,'exit_code':code}),flush=True);return (private/(label+'.log')).read_text()
def compilefile(name,label,target='tinyimx_gateway',member=None):
 fields=flagfields(target);flags=[]
 for x in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(fields[x])
 obj=private/(member or label+'.o')
 generated=b/'group-completion-rpc-build-20261006/runtime-private/generated/rpc'
 invoke(['/usr/bin/c++','-I'+str(overlay),'-I'+str(generated),*flags,'-MD','-MF',str(obj)+'.d','-c',str(r/name),'-o',str(obj)],'compile-'+label)
 return obj
def copiedarchive(original_path,name,objects):
 original_path=pathlib.Path(original_path);target=private/name;shutil.copy2(original_path,target);members=run(['ar','t',str(original_path)]).splitlines()
 assert all(o.name in members for o in objects)
 invoke(['ar','rcs',str(target),*map(str,objects)],'archive-'+name,90);assert run(['ar','t',str(target)]).splitlines()==members
 for member in members:
  if member not in {o.name for o in objects}:assert hashlib.sha256(subprocess.check_output(['ar','p',str(original_path),member])).digest()==hashlib.sha256(subprocess.check_output(['ar','p',str(target),member])).digest()
 return target
try:
 server=compilefile('gateway/GatewayServer.cpp','GatewayServer',member='GatewayServer.cpp.o')
 route=compilefile('gateway/GatewayRouteResolver.cpp','GatewayRouteResolver',member='GatewayRouteResolver.cpp.o')
 coord=compilefile('gateway/GroupFanoutCoordinator.cpp','GroupFanoutCoordinator',member='GroupFanoutCoordinator.cpp.o')
 main=compilefile('examples/gateway_demo.cpp','gateway-main','gateway_demo')
 assert all(str(overlay/'common/logging/LogMacros.h') in pathlib.Path(str(x)+'.d').read_text() for x in [server,coord,main])
 api_copy=private/'OnlineStatusCache.cpp.o';shutil.copy2(api,api_copy);assert sha(api_copy)==api_sha
 gateway_archive=copiedarchive(ga[0],'libtinyimx_gateway.a',[server,route,coord])
 cache_service_archive=copiedarchive(cs[0],'libtinyimx_cache_service.a',[api_copy])
 replacements={ga[0]:str(gateway_archive),cs[0]:str(cache_service_archive),gm[0]:str(main)}
 def link(out,label,repl=None,extra=()):
  argv=[(repl or replacements).get(x,x) for x in gl if not x.startswith('-Wl,-Map=')]
  argv[argv.index('-o')+1]=str(out)
  if extra:argv[argv.index('-o'):argv.index('-o')]=list(map(str,extra))
  argv.append('-Wl,-Map='+str(out)+'.map');invoke(argv,label)
 exe=private/'gateway_demo';link(exe,'gateway-runtime-link')
 symbols=run(['nm','-C',str(exe)])
 required=['tinyimx::OnlineStatusCache::GetOnlineStatusBatch(','tinyimx::GatewayRouteResolver::ResolveBatch(','tinyimx::GatewayServer::DispatchGroupFanoutDeliveries(','tinyimx::rpc::MessageRpcClient::CompleteGroupMessageDeliveryAttempts(']
 assert all(x in symbols for x in required) and '__wrap_' not in symbols
 save('runtime-symbols.json',{'symbols':{x:True for x in required},'wrappers_absent':True,'allocating_main_recompiled':True})
 objs={kind:compilefile(name,kind+'-test') for kind,name in testmap.items()}
 for kind in testmap:
  if kind=='lease':continue
  link(private/(kind+'-native'),kind+'-link',{**replacements,gm[0]:str(objs[kind])},[objs['lease']] if kind in ['wake','pipeline'] else ())
 def envbase():
  env=dict(os.environ)
  for k in list(env):
   if k.startswith('TINYIMX_GROUP_'):env.pop(k)
  return env
 def execute(kind,args,env,label):
  text=invoke([str(private/(kind+'-native')),*args],label,90,env)
  (d/(label+'.log')).write_text(text)
  if kind=='executor':assert 'total=14, failed=0' in text;result={'status':'ORIGINAL_EXECUTOR_PASS','checks':14}
  else:result=json.loads(next(x for x in text.splitlines() if x.startswith('{')));assert 'PASS' in result['status'] and result.get('failures',0)==0
  assert '[FAIL]' not in text;nativecases.append({'kind':kind,'case':label,**result});save('native-cases.json',nativecases)
 for kind,key in [('wake','TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE'),('pipeline','TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE'),('order','TINYIMX_GROUP_DELIVERY_RECIPIENT_ORDER_ENABLE')]:
  for case,value,enabled in [('OFF',None,False),('ON','1',True),('INVALID','true',False)]:
   env=envbase()
   if value is not None:env[key]=value
   execute(kind,['1' if enabled else '0'],env,kind+'-'+case)
 execute('executor',[],envbase(),'executor-original')
 for case,value,wake,enabled in [('OFF',None,True,False),('ON','1',True,True),('INVALID','01',True,False),('WAKE_OFF','1',False,True)]:
  env=envbase();env['TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE']='1' if wake else '0';env['TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE']='1'
  if value is not None:env['TINYIMX_GROUP_FANOUT_PARTIAL_DRAIN_ENABLE']=value
  execute('partial',['1' if enabled else '0','1' if wake else '0'],env,'partial-'+case)
 for case,value,defer,enabled in [('OFF',None,True,False),('ON','1',True,True),('INVALID','01',True,False),('DEFER_OFF','1',False,True)]:
  env=envbase();env['TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE']='1' if defer else '0'
  if value is not None:env['TINYIMX_GROUP_FANOUT_COMPLETION_BATCH_ENABLE']=value
  execute('completion',['1' if enabled else '0','1' if defer else '0'],env,'completion-'+case)
 assert sum(x.get('checks',x.get('tests',0)) for x in nativecases)==128
 for case,value,defer,enabled in [('OFF',None,True,False),('ON','1',True,True),('INVALID','01',True,False),('DEFER_OFF','1',False,True)]:
  env=envbase();env['TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE']='1' if defer else '0';env['TINYIMX_GROUP_FANOUT_COMPLETION_BATCH_ENABLE']='1'
  if value is not None:env['TINYIMX_GROUP_FANOUT_ROUTE_BATCH_ENABLE']=value
  execute('route',['1' if enabled else '0','1' if defer else '0'],env,'route-'+case)
 for mode,pool in [('p1',1),('p4',4)]:
  case=d/mode;case.mkdir(mode=0o700)
  argv=[str(private/'resolver-native'),mode,'/home/jackson7/.local/share/tinyimx/m21/config',pre['native_redis_ip'],str(case)]
  save(mode+'-native-audit-before.json',{'operation':'Own real Redis and own two-gateway discovery, exact scalar/batch routes and next-snapshot replacements','namespace':'codex:group-route-resolver-native-20261006:'+mode+':','TTL900_index_included':True,'production_write':False})
  invoke(argv,'resolver-'+mode,120);result=json.loads((case/'result.json').read_text());checks=json.loads((case/'checks.json').read_text());assert result['status']=='GROUP_ROUTE_RESOLVER_NATIVE_PASS' and result['checks']==len(checks)>50 and all(x['pass'] for x in checks)
  resolvercases.append(result);save('resolver-cases.json',resolvercases)
 context=private/'gateway-context';context.mkdir();shutil.copy2(exe,context/exe.name);os.chmod(context/exe.name,0o755)
 (context/'Dockerfile').write_text('FROM '+original['images']['gateway']['image_tag']+'\nCOPY --chmod=0755 gateway_demo /opt/tinyimx/bin/\nLABEL org.opencontainers.image.revision="'+head+'"\n')
 invoke(['docker','build','--pull=false','--network=none','-t',image,str(context)],'gateway-image',120)
 for kind,argv,code in [('ldd',['/usr/bin/ldd','-r','/opt/tinyimx/bin/gateway_demo'],0),('missing-config',['/opt/tinyimx/bin/gateway_demo','/__codex_route_missing__.json'],1)]:
  name='codex-group-route-'+kind+'-20261006';assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0
  text=invoke(['docker','run','--name',name,'--label','codex.tinyimx.group_route=20261006','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges:true',image,*argv],kind,30,expected=code)
  if kind=='ldd':assert 'not found' not in text and 'undefined symbol' not in text
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in sources.items())
 total=sum(x.get('checks',x.get('tests',0)) for x in nativecases);assert total==164
 images={'gateway':{'image_tag':image,'image_id':json.loads(run(['docker','image','inspect',image]))[0]['Id'],'elf_sha256':sha(exe)},'message':original['images']['message']}
 save('summary.json',{'status':'GROUP_ROUTE_INTEGRATION_NATIVE_AND_IMAGE_PASS','head':head,'images':images,'original_coordinator_checks':128,'new_route_coordinator_checks':36,'coordinator_checks':164,'resolver_checks':sum(x['checks'] for x in resolvercases),'primitive_checks':720,'inherited_message_rpc_checks':62,'unchanged_message_image':True,'original_message_unit_pass':True,'protocol_exact_preservation_checks':1,'borrowed_preserved':True,'runtime_deploy':False,'performance_acceptance':False})
 print((d/'summary.json').read_text(),flush=True)
except BaseException as error:save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'runtime_deploy':False});raise
finally:
 after=runtime();save('runtime-after.json',after);assert before==after
PY
