#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,shlex,signal,os
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-restore-missing-fence-test-20261005';assert not d.exists()
source=json.loads((b/'online-restore-missing-fence-fix-source-20261005/summary.json').read_text());head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head==source['head']
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}},cs
before,containers=runtime();assert before==json.loads((b/'online-restore-missing-fence-fix-source-20261005/runtime-after.json').read_text())
fixed=b/'redis-command-timeout-fixed-probe-20261005';assert json.loads((fixed/'summary.json').read_text())['status']=='REDIS_COMMAND_IDLE_TIMEOUT_FIXED_PROBE_COMPLETE';hashes=json.loads((fixed/'compiled-sha256.json').read_text());connection=fixed/'connection.o';assert hashlib.sha256(connection.read_bytes()).hexdigest()==hashes[str(connection.relative_to(b))]
assert (r/'common/cache/RedisConnection.cpp').read_bytes()==(fixed/'fixed-RedisConnection.cpp').read_bytes()
executor=r/'gateway/business/BusinessExecutor.cpp';assert executor.read_bytes()==subprocess.check_output(['git','show','33fc9bbbf7b8a8e828ce70d1ae0035d8e8c71ce8:gateway/business/BusinessExecutor.cpp'])
gateway=(r/'gateway/GatewayServer.cpp').read_text();heartbeat=re.search(r'void GatewayServer::HandleHeartbeat\((.*?)\n\}',gateway,re.S).group(1);refresh=re.search(r'void GatewayServer::RefreshUserOnlineIfMatch\((.*?)\n\}',gateway,re.S).group(1);offline=re.search(r'task\.request\.operation =\s*"gateway\.presence\.offline";(.*?)const BusinessSubmitStatus status',gateway,re.S).group(1)
assert 'PresenceUserOrderingKey(user_id)' in heartbeat and 'PresenceUserOrderingKey(unbind_result.user_id)' in offline and 'SetOnlineIfMissing(' in refresh and 'SetUserOnline(user_id, connection);' not in refresh
preflight=json.loads((b/'online-maintenance-mechanism-preflight-20261005/summary.json').read_text());ip=preflight['public_connection_fields']['actual_native_target_ip'];assert ip=='172.18.0.13';redis=next(c for c in containers if c['Name']=='/tinyimx-m21-redis-1');assert {net['IPAddress'] for net in redis['NetworkSettings']['Networks'].values() if net.get('IPAddress')}=={ip}
build=r/'build/linux-release';flags_path=build/'CMakeFiles/redis_pool_demo.dir/flags.make';link_path=build/'CMakeFiles/redis_pool_demo.dir/link.txt'
assert hashlib.sha256(flags_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['flags.make'] and hashlib.sha256(link_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['link.txt']
flags=[]
for field in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(line for line in flags_path.read_text().splitlines() if line.startswith(field+' =')).split('=',1)[1])
old='CMakeFiles/redis_pool_demo.dir/examples/redis_pool_demo.cpp.o';base_link=shlex.split(link_path.read_text());assert base_link.count(old)==1 and 'libtinyimx_cache.a' in base_link
linked={part:hashlib.sha256((build/part).read_bytes()).hexdigest() for part in base_link if part.startswith('libtinyimx_')}
sources={'api':r/'services/cache/OnlineStatusCache.cpp','executor':executor,'pool':r/'common/concurrency/ThreadPool.cpp','pool_types':r/'common/concurrency/ThreadPoolTypes.cpp','test':r/'benchmark/local_capacity/online_restore_missing_fence_test.cpp'}
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Actual missing-only Redis cache plus restored real Executor/ThreadPool isolated compile and lifecycle tests','head':head,'source_sha256':{k:hashlib.sha256(p.read_bytes()).hexdigest() for k,p in sources.items()},'retained_connection_object_sha256':hashes[str(connection.relative_to(b))],'cached_project_library_sha256':linked,'scope':'Ownobjects/ELF/stages, no CMake/cachedtarget overwrite or deployedruntime change; real Gateway source not compiled/live-session tested here','keys':'codex:online-restore-missing-fence-test-20261005:p1/p4: max160newkeys TTL300/120/expiry1, privatepool1/4; original350 regression freshprefix separately','deletion':'Onlyexplicitownfixture SetOfflineIfMatch for lifecycle positive cleanup, no arbitraryDEL/FLUSH/CONFIG/SQL/productionkeys','model':'Local-current predicate controlled model; actualcache/Executor cancellation/userordering/mustRun drain/accounting','resources':'Sequentialcompile/link180s/native90s ownidentity watchdog, cachedsource33fc exact, allapps kept, no cleanup','rollback':'Closeonlyownpool/thread/process, retainallpartial/failures/source/Git, otherfixturekeys naturalTTL','runtime_before':before},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');(d/'heartbeat-source.txt').write_text(heartbeat);(d/'refresh-source.txt').write_text(refresh);(d/'offline-source.txt').write_text(offline)
def interrupted(signum,frame):raise KeyboardInterrupt('Own missing-fence test interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def stop(child,proc,identity,args,label):
 if child.poll() is None:
  assert identity is not None and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid
  assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[str(value).encode() for value in args]
  (d/(label+'-stop-audit.json')).write_text(json.dumps({'operation':'Stoponlyownverifiedprocessgroup','pid':child.pid,'cmdline_starttime_pgid_verified':True})+'\n');os.killpg(child.pid,signal.SIGTERM)
  try:child.wait(timeout=3)
  except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
def invoke(args,label,timeout):
 print('OWN_STAGE='+label,flush=True);(d/(label+'-command.json')).write_text(json.dumps(args,indent=2)+'\n');child=None;identity=None;proc=None
 with (d/(label+'.log')).open('w') as output:
  try:
   child=subprocess.Popen(args,cwd=build,stdout=output,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path(f'/proc/{child.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   (d/(label+'-own-process.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n');code=child.wait(timeout=timeout);assert code==0,label+' failed exit '+str(code)
  finally:
   if child is not None:stop(child,proc,identity,args,label)
def make_link(test_object,binary,objects):
 link=list(base_link);link[link.index(old)]=str(test_object);link[link.index('-o')+1]=str(binary);link[1:1]=[str(p) for p in objects];return link
reports=[];regression_reports=[];phase='compile'
try:
 for label,path in sources.items():phase='compile-'+label;invoke(['/usr/bin/c++',*flags,'-c',str(path),'-o',str(d/(label+'.o'))],phase,180)
 binary=d/'online_restore_missing_fence_test';phase='link';objects=[connection,*[d/(label+'.o') for label in ['api','executor','pool','pool_types']]];invoke(make_link(d/'test.o',binary,objects),'link',180)
 (d/'compiled-sha256.json').write_text(json.dumps({str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [binary,*objects,d/'test.o']},indent=2)+'\n')
 for mode,pool_size in [('p1',1),('p4',4)]:
  phase='native-'+mode;case=d/mode;case.mkdir();args=[str(binary),mode,'/home/jackson7/.local/share/tinyimx/m21/config',ip,str(case)];(case/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Ownnamespace missing-only restore plus user-ordered actualExecutor lifecycle','pool_size':pool_size,'timeout_seconds':90,'keys_scope':'Fixedownprefix only, explicitownfixture ownercheckedcleanup; no productionrecords/config/globalfaults'},indent=2)+'\n');invoke(args,phase,90)
  data=json.loads((case/'result.json').read_text());checks=json.loads((case/'checks.json').read_text());assert data['status']=='ONLINE_RESTORE_MISSING_FENCE_FUNCTIONAL_PASS' and data['pool_size']==pool_size and data['checks']==len(checks) and len(checks)>=60 and all(row['pass'] for row in checks);reports.append(data);(d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n')
 phase='api-regression-prepare';reg=b/'online-restore-missing-fence-api-regression-20261005';assert not reg.exists();reg.mkdir();harness=r/'benchmark/local_capacity/online_maintenance_batch_api_test.cpp';original=harness.read_bytes();generated=original.decode().replace('online-maintenance-batch-api-20261005','online-restore-missing-fence-api-regression-20261005').encode()
 (reg/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Exact350 actualAPI regression after cache API extension, newstage/prefix only','original_harness_sha256':hashlib.sha256(original).hexdigest(),'generated_sha256':hashlib.sha256(generated).hexdigest(),'transformation':'Onlystage/prefix string substitution, originalharness unchanged','keys':'codex:online-restore-missing-fence-api-regression-20261005:p1/p4: max120newkeys TTL300/120/expiry1, no productionkeys/delete/SQL/globalconfig/runtimechanges'},indent=2)+'\n');(reg/'test.cpp').write_bytes(generated)
 phase='regression-compile-test';invoke(['/usr/bin/c++',*flags,'-c',str(reg/'test.cpp'),'-o',str(reg/'test.o')],phase,180);reg_binary=reg/'online_restore_missing_fence_api_regression';phase='regression-link';invoke(make_link(reg/'test.o',reg_binary,[connection,d/'api.o']),phase,180)
 (reg/'compiled-sha256.json').write_text(json.dumps({str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [reg_binary,reg/'test.o',d/'api.o',connection]},indent=2)+'\n')
 for mode,pool_size in [('p1',1),('p4',4)]:
  phase='api-regression-'+mode;case=reg/mode;case.mkdir();(case/'audit-before.json').write_text(json.dumps({'operation':'Own350 APIregression pool'+str(pool_size),'namespace':'codex:online-restore-missing-fence-api-regression-20261005:'+mode,'timeout_seconds':90})+'\n');invoke([str(reg_binary),mode,'/home/jackson7/.local/share/tinyimx/m21/config',ip,str(case)],phase,90)
  data=json.loads((case/'result.json').read_text());checks=json.loads((case/'checks.json').read_text());assert data['status']=='ONLINE_MAINTENANCE_BATCH_API_FUNCTIONAL_PASS' and data['checks']==len(checks)==175 and all(row['pass'] for row in checks);regression_reports.append(data);(reg/'completed-case-summaries.json').write_text(json.dumps(regression_reports,indent=2)+'\n')
 (reg/'summary.json').write_text(json.dumps({'status':'ONLINE_RESTORE_MISSING_FENCE_API_REGRESSION_COMPLETE','head':head,'total_checks':350,'cases':regression_reports,'all19_configs_unchanged':runtime()[0]==before,'production_keys_changed':False,'performance_acceptance':False},indent=2)+'\n')
except BaseException as error:
 (d/'helper-failed.json').write_text(json.dumps({'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'completed_native_cases':len(reports),'completed_regression_cases':len(regression_reports),'capacity_acceptance':False},indent=2)+'\n');raise
finally:
 after,_=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
for executable in [binary,reg_binary]:assert subprocess.run(['pgrep','-f','^'+re.escape(str(executable))+r'( |$)'],capture_output=True).returncode==1
x={'status':'ONLINE_RESTORE_MISSING_FENCE_TEST_COMPLETE','head':head,'native_checks':sum(row['checks'] for row in reports),'native_cases':reports,'api_regression_checks':350,'api_regression_cases':regression_reports,'all19_runtime_configs_health_preserved':True,'userapps_preserved':True,'performance_acceptance':False,'limits':'Actualmissing-onlycacheAPI andrestoredExecutor controlledmodel lifecycle, no fullGateway build/liveTCPsession proof here. No deployedruntime/capacityacceptance. Next asyncbatcher mustreturnmissing restores touserordered canceledpresence domain; allfunctions/full10k50k stillpending.'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
