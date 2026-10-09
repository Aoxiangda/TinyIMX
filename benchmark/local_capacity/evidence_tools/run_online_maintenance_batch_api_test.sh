#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,shlex,signal,os
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-maintenance-batch-api-20261005';assert not d.exists()
source=json.loads((b/'online-maintenance-batch-api-source-20261005/summary.json').read_text());head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head==source['head']
pre=b/'online-maintenance-mechanism-preflight-20261005';preflight=json.loads((pre/'summary.json').read_text());assert preflight['redis']['cluster_enabled']=='0'
assert hashlib.sha256((r/'gateway/business/BusinessExecutor.cpp').read_bytes()).hexdigest()=='3b707e2e5a63cd7671a92c977ea75fc7fcaad29de1c90e92e41bcb8f9017cb22'
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}},cs
before,containers=runtime();assert before==json.loads((b/'online-maintenance-batch-api-source-20261005/runtime-after.json').read_text())
redis=next(c for c in containers if c['Name']=='/tinyimx-m21-redis-1');ip=preflight['public_connection_fields']['actual_native_target_ip'];assert {net['IPAddress'] for net in redis['NetworkSettings']['Networks'].values() if net.get('IPAddress')}=={ip}
build=r/'build/linux-release';cpp=r/'benchmark/local_capacity/online_maintenance_batch_api_test.cpp';api=r/'services/cache/OnlineStatusCache.cpp';binary=d/'online_maintenance_batch_api_test'
single_script=re.search(r'constexpr const char\* kRefreshOnlineIfMatchScript = R"lua\((.*?)\)lua";',api.read_text(),re.S).group(1);assert hashlib.sha256(single_script.encode()).hexdigest()=='12b8fcb43720694347e0a781d9c7ed6adcff43bdb34c37e750cf2c38e26246b1'
flags_path=build/'CMakeFiles/redis_pool_demo.dir/flags.make';link_path=build/'CMakeFiles/redis_pool_demo.dir/link.txt'
assert hashlib.sha256(flags_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['flags.make'] and hashlib.sha256(link_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['link.txt']
flags_text=flags_path.read_text();flags=[]
for field in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(line for line in flags_text.splitlines() if line.startswith(field+' =')).split('=',1)[1])
link=shlex.split(link_path.read_text());old='CMakeFiles/redis_pool_demo.dir/examples/redis_pool_demo.cpp.o';assert link.count(old)==1;link[link.index(old)]=str(d/'test.o');link[link.index('-o')+1]=str(binary);link.insert(1,str(d/'api.o'));link.append('-Wl,-Map,'+str(d/'link-map.txt'))
assert 'libtinyimx_cache.a' in link and 'libtinyimx_message_grpc.a' not in link
linked={part:hashlib.sha256((build/part).read_bytes()).hexdigest() for part in link if part.startswith('libtinyimx_')}
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Compile own product cacheAPI+test objects and realRedis functional pool1/4 tests','head':head,'source_sha256':{str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [cpp,api,r/'services/cache/OnlineStatusCache.h']},'cached_linked_libraries':linked,'compile_scope':'Own stage2objects/ELF/map only, no CMake/cache/product binary rebuild or deployment','namespace':'codex:online-maintenance-batch-api-20261005:p1/p4:','data_scope':'At most120new own keys,TTL300 or dedicated1sec expiry/120refresh; precise own replacement fixture only, all initialkeys mustabsent. No productionkeys/DEL/FLUSH/CONFIG/global faults/SQL','own_connections':'Private pool1 then4 and1observer to verify ownshutdown. Shutdown only ownpool, no Redis server restart/outage','tests':'18actualsingle/batch mixed case status/TTL/bytes comparison; sizes0/1/16/17, duplicateuserowner order, replacement safety, null/uninitialized/ownshutdown pool andinvaliditems,100concurrentmixed3itemsbatches eachpool1/4 with4callers, leasesreturned','limits':'Functional only, not capacity/Pong/Gatewaycancel/selfheal/shutdown proof; original single Lua stays exact. Same healthyAcquire/Ping contract; no skip/retry','watchdog':'CompileAPI/test/link each180s, testcases90s; only ownPID/starttime/cmdline/PGID signal group, preserveallpartial/log/check/failures','rollback':'No deployment. Closeownprocess/pools; keepallrecords naturalexpiry andallsource/raw/Git, no delete/reset/prune/push/appsstop','runtime_before':before},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');(d/'cached-flags.make').write_text(flags_text)
def interrupted(signum,frame):raise KeyboardInterrupt('Own API diagnostic interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def stop(child,proc,identity,args,label):
 if child.poll() is None:
  assert identity is not None and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid
  assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[str(value).encode() for value in args]
  (d/(label+'-stop-audit.json')).write_text(json.dumps({'operation':'Stoponlyownverifiedprocessgroup','pid':child.pid,'cmdline_starttime_pgid_verified':True})+'\n');os.killpg(child.pid,signal.SIGTERM)
  try:child.wait(timeout=3)
  except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
def invoke(args,label,cwd,timeout):
 (d/(label+'-command.json')).write_text(json.dumps(args,indent=2)+'\n');child=None;identity=None;proc=None
 with (d/(label+'.log')).open('w') as output:
  try:
   child=subprocess.Popen(args,cwd=cwd,stdout=output,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path(f'/proc/{child.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   (d/(label+'-own-process.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n');code=child.wait(timeout=timeout);assert code==0,label+' failed exit '+str(code)
  finally:
   if child is not None:stop(child,proc,identity,args,label)
reports=[];phase='compile-api'
try:
 invoke(['/usr/bin/c++',*flags,'-c',str(api),'-o',str(d/'api.o')],'compile-api',build,180);phase='compile-test';invoke(['/usr/bin/c++',*flags,'-c',str(cpp),'-o',str(d/'test.o')],'compile-test',build,180);phase='link';invoke(link,'link',build,180)
 symbols=subprocess.check_output(['nm','-C',str(binary)],text=True);assert 'tinyimx::OnlineStatusCache::RefreshOnlineIfMatchBatch(' in symbols
 (d/'compiled-sha256.json').write_text(json.dumps({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [d/'api.o',d/'test.o',binary]},indent=2)+'\n')
 for mode,pool in [('p1',1),('p4',4)]:
  phase=mode;case=d/mode;case.mkdir();args=[str(binary),mode,'/home/jackson7/.local/share/tinyimx/m21/config',ip,str(case)]
  (case/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Own fixed-prefix cacheAPI realRedis functional/fault result tests','mode':mode,'pool_size':pool,'argv':args,'timeout_seconds':90,'keys':'codex:online-maintenance-batch-api-20261005:'+mode+':','mutation':'Onlyownnewfixture/TTL andexplicit replacement, no productionkeys/settings/deletion.100concurrentmixedbatches isboundedfunctional test, notnewcapacitytest'},indent=2)+'\n');invoke(args,mode,r,90)
  result=json.loads((case/'result.json').read_text());checks=json.loads((case/'checks.json').read_text());assert result['status']=='ONLINE_MAINTENANCE_BATCH_API_FUNCTIONAL_PASS' and result['pool_size']==pool and result['checks']==len(checks)>150 and all(item['pass'] for item in checks)
  assert result['original_single_cases']==result['batch_comparison_cases']==18 and result['concurrent_batches']==100;reports.append(result);(d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n')
except BaseException as error:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'completed_cases':reports,'capacity_acceptance':False},indent=2)+'\n');raise
finally:
 after,_=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');(d/'guest-memory-after.txt').write_text(pathlib.Path('/proc/meminfo').read_text())
assert subprocess.run(['pgrep','-f','^'+re.escape(str(binary))+r'( |$)'],capture_output=True).returncode==1
x={'status':'ONLINE_MAINTENANCE_BATCH_API_TEST_COMPLETE','head':head,'cases':reports,'total_checks':sum(item['checks'] for item in reports),'all19_runtime_configs_health_preserved':True,'production_keys_changed':False,'SQL_or_durability_changed':False,'userapps_preserved':True,'performance_acceptance':False,'limits':'Real product API functional proof only; asyncGateway lifecycle andfull-feature10k50k not yet implemented or accepted'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
