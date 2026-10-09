#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,shlex,signal,os
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-maintenance-lifecycle-test-20261005';assert not d.exists()
source=json.loads((b/'online-maintenance-batcher-integration-source-20261005/summary.json').read_text());head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head==source['head']
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def memory_guard():assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
memory_guard()
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}},cs
before,containers=runtime();assert before==json.loads((b/'online-maintenance-batcher-integration-source-20261005/runtime-after.json').read_text())
prior=b/'online-restore-missing-fence-test-20261005';assert json.loads((prior/'summary.json').read_text())['status']=='ONLINE_RESTORE_MISSING_FENCE_TEST_COMPLETE';hashes=json.loads((prior/'compiled-sha256.json').read_text());fixed=b/'redis-command-timeout-fixed-probe-20261005';connection=fixed/'connection.o';api=prior/'api.o';executor=prior/'executor.o'
for p in [connection,api,executor]:assert hashlib.sha256(p.read_bytes()).hexdigest()==hashes[str(p.relative_to(b))]
assert (r/'common/cache/RedisConnection.cpp').read_bytes()==(fixed/'fixed-RedisConnection.cpp').read_bytes()
assert (r/'gateway/business/BusinessExecutor.cpp').read_bytes()==subprocess.check_output(['git','show','33fc9bbbf7b8a8e828ce70d1ae0035d8e8c71ce8:gateway/business/BusinessExecutor.cpp'])
assert hashlib.sha256((r/'services/cache/OnlineStatusCache.cpp').read_bytes()).hexdigest()==json.loads((prior/'audit-before.json').read_text())['source_sha256']['api']
gateway=(r/'gateway/GatewayServer.cpp').read_text();demo=(r/'examples/gateway_demo.cpp').read_text();assert '#include "gateway/OnlineStatusMaintenance.h"' in gateway and 'TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE' in demo and 'loop.Loop();\n        gateway.StopOnlineMaintenance();' in demo
preflight=json.loads((b/'online-maintenance-mechanism-preflight-20261005/summary.json').read_text());ip=preflight['public_connection_fields']['actual_native_target_ip'];assert ip=='172.18.0.13';redis=next(c for c in containers if c['Name']=='/tinyimx-m21-redis-1');assert {net['IPAddress'] for net in redis['NetworkSettings']['Networks'].values() if net.get('IPAddress')}=={ip}
build=r/'build/linux-release';cpp=r/'benchmark/local_capacity/online_maintenance_lifecycle_test.cpp';binary=d/'online_maintenance_lifecycle_test';flags_path=build/'CMakeFiles/redis_pool_demo.dir/flags.make';link_path=build/'CMakeFiles/redis_pool_demo.dir/link.txt'
assert hashlib.sha256(flags_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['flags.make'] and hashlib.sha256(link_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['link.txt']
def flags(path):
 values=[]
 for field in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:values+=shlex.split(next(line for line in path.read_text().splitlines() if line.startswith(field+' =')).split('=',1)[1])
 return values
link=shlex.split(link_path.read_text());old='CMakeFiles/redis_pool_demo.dir/examples/redis_pool_demo.cpp.o';assert link.count(old)==1;link[link.index(old)]=str(d/'test.o');link[link.index('-o')+1]=str(binary);link[1:1]=[str(connection),str(api)];link.insert(1,'-Wl,-Map='+str(d/'link.map'))
linked={part:hashlib.sha256((build/part).read_bytes()).hexdigest() for part in link if part.startswith('libtinyimx_')}
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Actual boundedmaintenance component plus cache API isolated lifecycle tests','head':head,'cpp_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest(),'component_sha256':hashlib.sha256((r/'gateway/OnlineStatusMaintenance.h').read_bytes()).hexdigest(),'retained_green_object_sha256':{str(p.relative_to(b)):hashes[str(p.relative_to(b))] for p in [connection,api,executor]},'cached_library_sha256':linked,'keys':'codex:online-maintenance-lifecycle-test-20261005:p1/p4: max160newkeys, TTL300/120, privatepool1/4; no productionkeys/delete/SQL/globalsettings','controlled_schedules':'Backend gate and wrappers for I/O/callback cancellation, deadline, throwingprobe/backend/callback and malformed cardinality. Default5ms capacity test; batching fourheldworkers flush500ms only toforcefullgroups without scheduler/filesystem assumptions, not performance measurement','resources':'Native90s/compilelink180s ownPID identity guards, allapps retained, no cleanup','scope':'Ownfrontend/objects only, fullGW compile next separateownstage; no deployment/liveTCP/fullcapacity acceptance','runtime_before':before},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
def interrupted(signum,frame):raise KeyboardInterrupt('Own maintenance verification interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def stop(child,proc,identity,args,label,stage):
 if child.poll() is None:
  assert identity is not None and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid
  assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[str(value).encode() for value in args]
  (stage/(label+'-stop-audit.json')).write_text(json.dumps({'operation':'Stoponlyownverifiedprocessgroup','pid':child.pid,'cmdline_starttime_pgid_verified':True})+'\n');os.killpg(child.pid,signal.SIGTERM)
  try:child.wait(timeout=3)
  except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
def invoke(args,label,timeout,stage):
 print('OWN_STAGE='+label,flush=True);memory_guard();(stage/(label+'-command.json')).write_text(json.dumps(args,indent=2)+'\n');child=None;identity=None;proc=None
 with (stage/(label+'.log')).open('w') as output:
  try:
   child=subprocess.Popen(args,cwd=build,stdout=output,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path(f'/proc/{child.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   (stage/(label+'-own-process.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n');code=child.wait(timeout=timeout);assert code==0,label+' failed exit '+str(code)
  finally:
   if child is not None:stop(child,proc,identity,args,label,stage)
phase='compile-test';reports=[];gw=None
try:
 invoke(['/usr/bin/c++',*flags(flags_path),'-c',str(cpp),'-o',str(d/'test.o')],phase,180,d);phase='link';invoke(link,'link',180,d);(d/'compiled-sha256.json').write_text(json.dumps({str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [binary,d/'test.o',connection,api]},indent=2)+'\n')
 for mode,pool_size in [('p1',1),('p4',4)]:
  phase='native-'+mode;case=d/mode;case.mkdir();(case/'audit-before.json').write_text(json.dumps({'operation':'Ownrealcache controlledmaintenance lifecycle','pool_size':pool_size,'timeout_seconds':90,'namespace':'codex:online-maintenance-lifecycle-test-20261005:'+mode,'no_productionkeys_or_globalfaults':True})+'\n');invoke([str(binary),mode,'/home/jackson7/.local/share/tinyimx/m21/config',ip,str(case)],phase,90,d)
  data=json.loads((case/'result.json').read_text());checks=json.loads((case/'checks.json').read_text());assert data['status']=='ONLINE_MAINTENANCE_LIFECYCLE_FUNCTIONAL_PASS' and data['checks']==len(checks) and len(checks)>=90 and all(row['pass'] for row in checks);reports.append(data);(d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n')
 (d/'summary.json').write_text(json.dumps({'status':'ONLINE_MAINTENANCE_LIFECYCLE_TEST_COMPLETE','head':head,'total_checks':sum(row['checks'] for row in reports),'cases':reports,'all19_runtime_configs_health_preserved':runtime()[0]==before,'performance_acceptance':False},indent=2)+'\n')
 phase='gateway-build-audit';gw=b/'online-maintenance-gateway-candidate-build-20261005';assert not gw.exists()
 gf=build/'CMakeFiles/tinyimx_gateway.dir/flags.make';df=build/'CMakeFiles/gateway_demo.dir/flags.make';gl=build/'CMakeFiles/gateway_demo.dir/link.txt';gw_link=shlex.split(gl.read_text());old_demo='CMakeFiles/gateway_demo.dir/examples/gateway_demo.cpp.o';assert gw_link.count(old_demo)==1
 gw_sources={'gateway':r/'gateway/GatewayServer.cpp','demo':r/'examples/gateway_demo.cpp'};gw_libraries={str(p):hashlib.sha256((build/p).read_bytes()).hexdigest() for p in gw_link if p.startswith('libtinyimx_')};gw_cached_binary=build/'gateway_demo';cached_before=hashlib.sha256(gw_cached_binary.read_bytes()).hexdigest()
 gw.mkdir();(gw/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'FullGateway/demo source compile into freshownobjects andsealedcandidate ELF, no cachedtarget overwrite','head':head,'source_sha256':{label:hashlib.sha256(path.read_bytes()).hexdigest() for label,path in gw_sources.items()},'header_sha256':{name:hashlib.sha256((r/name).read_bytes()).hexdigest() for name in ['gateway/GatewayServer.h','gateway/OnlineStatusMaintenance.h','gateway/PresenceOrderingKey.h']},'cached_inputs_sha256':{str(p.relative_to(build)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [gf,df,gl]},'cached_project_libraries_sha256':gw_libraries,'cached_gateway_binary_before':cached_before,'own_overrides':'FullGatewayServer anddemo plus actual33fc restoredExecutor/deefCache/506finiteidleConnection objects precede SDKlibs; stalefairExecutor not used. No MessageService84/sourcebuild','resources':'SequentialCXX compile/link180s each, memoryguard2GiB andownidentity termination only, keeppartial/failures','deployment':False,'scope':'Compilation proof only, liveTCP/session/endpointcapacity not tested, defaultOFF until separateaudit','runtime_before':before},indent=2)+'\n')
 (gw/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
 for label,path in gw_sources.items():phase='gateway-compile-'+label;invoke(['/usr/bin/c++',*flags(df if label=='demo' else gf),'-c',str(path),'-o',str(gw/(label+'.o'))],phase,180,gw)
 candidate=gw/'gateway_demo';gw_link[gw_link.index(old_demo)]=str(gw/'demo.o');gw_link[gw_link.index('-o')+1]=str(candidate);gw_link[1:1]=[str(gw/'gateway.o'),str(executor),str(api),str(connection),'-Wl,-Map='+str(gw/'link.map')];phase='gateway-link';invoke(gw_link,phase,180,gw)
 assert hashlib.sha256(gw_cached_binary.read_bytes()).hexdigest()==cached_before
 for name,sha in gw_libraries.items():assert hashlib.sha256((build/name).read_bytes()).hexdigest()==sha
 (gw/'compiled-sha256.json').write_text(json.dumps({str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [candidate,gw/'gateway.o',gw/'demo.o',executor,api,connection]},indent=2)+'\n')
 (gw/'summary.json').write_text(json.dumps({'status':'ONLINE_MAINTENANCE_GATEWAY_CANDIDATE_BUILD_COMPLETE','head':head,'candidate_sha256':hashlib.sha256(candidate.read_bytes()).hexdigest(),'candidate_relative_path':str(candidate.relative_to(r)),'cached_gateway_binary_preserved':True,'cached_project_libraries_preserved':True,'runtime_preserved':runtime()[0]==before,'full_gateway_demo_linked':True,'deployed':False,'performance_acceptance':False},indent=2)+'\n')
except BaseException as error:
 data={'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'completed_native_cases':len(reports),'deployment':False,'capacity_acceptance':False};(d/'helper-failed.json').write_text(json.dumps(data,indent=2)+'\n')
 if gw is not None and gw.exists():(gw/'helper-failed.json').write_text(json.dumps(data,indent=2)+'\n')
 raise
finally:
 after,_=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
 if gw is not None and gw.exists():(gw/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
assert subprocess.run(['pgrep','-f','^'+re.escape(str(binary))+r'( |$)'],capture_output=True).returncode==1
print(json.dumps({'lifecycle':json.loads((d/'summary.json').read_text()),'gateway_build':json.loads((gw/'summary.json').read_text())},indent=2))
PY
