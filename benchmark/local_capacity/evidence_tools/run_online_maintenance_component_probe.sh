#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,shlex,time,signal,os
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-maintenance-component-probe-20261005';assert not d.exists()
source=json.loads((b/'online-maintenance-component-probe-source-20261005/summary.json').read_text());head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head==source['head']
pre=b/'online-maintenance-mechanism-preflight-20261005';preflight=json.loads((pre/'summary.json').read_text());assert preflight['status']=='ONLINE_MAINTENANCE_READONLY_PREFLIGHT_COMPLETE' and preflight['redis']['cluster_enabled']=='0' and preflight['public_connection_fields']['pool_size']==8
assert hashlib.sha256((pre/'running-original-refresh.lua').read_bytes()).hexdigest()=='12b8fcb43720694347e0a781d9c7ed6adcff43bdb34c37e750cf2c38e26246b1'
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}},cs
before,containers=runtime();assert before==json.loads((b/'online-maintenance-component-probe-source-20261005/runtime-after.json').read_text())
redis=next(c for c in containers if c['Name']=='/tinyimx-m21-redis-1');ip=preflight['public_connection_fields']['actual_native_target_ip'];assert {net['IPAddress'] for net in redis['NetworkSettings']['Networks'].values() if net.get('IPAddress')}=={ip}
build=r/'build/linux-release';cpp=r/'benchmark/local_capacity/online_maintenance_batch_probe.cpp';binary=d/'online_maintenance_batch_probe'
flags_path=build/'CMakeFiles/redis_pool_demo.dir/flags.make';link_path=build/'CMakeFiles/redis_pool_demo.dir/link.txt'
assert hashlib.sha256(flags_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['flags.make'] and hashlib.sha256(link_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['link.txt']
flags_text=flags_path.read_text();flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(line for line in flags_text.splitlines() if line.startswith(key+' =')).split('=',1)[1])
link=shlex.split(link_path.read_text());old='CMakeFiles/redis_pool_demo.dir/examples/redis_pool_demo.cpp.o';assert link.count(old)==1;link[link.index(old)]=str(d/'probe.o');link[link.index('-o')+1]=str(binary)
assert 'libtinyimx_cache.a' in link and 'libtinyimx_message_grpc.a' not in link
linked={part:hashlib.sha256((build/part).read_bytes()).hexdigest() for part in link if part.startswith('libtinyimx_')}
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Isolated owned Redis online-maintenance conformance andfixed667persecond ABBA mechanics','source_head':head,'cpp_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest(),'compiled_scope':'OwnCPP only cachedSDK flags/libs, no CMake/rebuild/source/runtime deployment/networkinstall','linked_cached_project_sha256':linked,'runtime_before':before,'fixture_namespace':'codex:online-maintenance-probe-20261005:','fixture_writes':'10000 newowner-scopedperformancerecordkeysTTL300 plus precise15casesx2ownedfaultfixtures, expire/SETEX/RPUSH onlyownkeys; no DEL/FLUSH/CONFIG/reset/productionrecordwrite','expected_max_memory':'Approx fewMiB keys plus oneboundednativeprocess/16privateconnections/twoqueue512/batch16; guestavailable2GiBguard, no cleanup orappstop','sequence':'prepare original/vector15cases per-keystatus/TTL/rawrecord conformance andpreserveerroredindividualstatus, then single-A1/batch-B1/batch-B2/single-A2 each13340requests/667s^-1/20s','mechanism':'Originalper-keyLua bodyexactrunning33fc, vectorpcall wraps samebody perkey so wrongtype keydoesnot abortpeeroutcomes; bound16keys/5mscollector perlogicalGateway, bothsame4workersx2 andpool8x2; onehealthyAcquire/Ping perbatch notskiphealthchecks','native_route':'Native toexistingDockerRedisIP differsproductioncontainer route; isolatednoother10kheartbeats/message/MCP/AI/capacity proof','validation':'Everyplanned/attempted/issued/completed/result/rawID/status1, healthylease/EVALgroup count, CPUincludingcollectors, coalescingfreshness/queue/late/totalwall; servercgroup includesbackground/startup/verify/release overhead','timeout':'Compile/link180sownverifiedprocesssession, prepare60s, controlledcases90s ownPID/starttime/cmdline/PGID, allfailurelogs/raw/live retained; TERM/KILL onlyown childgroup onfinally','rollback':'No deployment; closeownconnections/processes, retainrecords and naturalownedTTLexpiry explicitnormaltest behavior; no delete/prune/reset/push, allapps kept'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');(d/'cached-flags.make').write_text(flags_text)
def interrupted(signum,frame):raise KeyboardInterrupt('Own online-maintenance diagnostic interrupted')
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
cg=next(row.split(':',2)[2] for row in pathlib.Path('/proc/'+str(redis['State']['Pid'])+'/cgroup').read_text().splitlines() if row.startswith('0::'));cg_cpu=pathlib.Path('/sys/fs/cgroup')/cg.lstrip('/')/'cpu.stat';assert cg_cpu.is_file()
def snapshot():
 start=time.monotonic_ns();values={key:int(value) for key,value in (row.split() for row in cg_cpu.read_text().splitlines())};end=time.monotonic_ns();return {'start_monotonic_ns':start,'end_monotonic_ns':end,'cpu_stat':values}
reports=[];phase='compile'
try:
 invoke(['/usr/bin/c++',*flags,'-c',str(cpp),'-o',str(d/'probe.o')],'compile',build,180);phase='link';invoke(link,'link',build,180)
 (d/'probe-binary-sha256.txt').write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'\n')
 phase='prepare';case=d/'prepare';case.mkdir();(case/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Own fixedprefixfault/TTL cases thennew10000fixture only, originalandvectorstatusconformance','keys':'codex:online-maintenance-probe-20261005:', 'before':'Allkeys mustnotexist beforeinitialization, rejectcollisions rather than overwrite','natural_ttl_expiry_seconds':300,'global_settings':'Unchanged','production_keys':'Never referenced'},indent=2)+'\n')
 args=[str(binary),'prepare','/home/jackson7/.local/share/tinyimx/m21/config',ip,str(pre),str(case)];invoke(args,'prepare',r,60)
 functional=json.loads((case/'result.json').read_text());checks=json.loads((case/'functional-checks.json').read_text());assert functional['status']=='ONLINE_REFRESH_OWNED_LUA_CONFORMANCE_PASS' and functional['checks']==len(checks)==84 and all(item['pass'] for item in checks) and functional['performance_owned_keys']==10000
 for name,mode in [('single-A1','single'),('batch-B1','batch'),('batch-B2','batch'),('single-A2','single')]:
  phase=name;case=d/name;case.mkdir();args=[str(binary),mode,'/home/jackson7/.local/share/tinyimx/m21/config',ip,str(pre),str(case)]
  (case/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Ownedkey667/s20s openloop maintenance control','argv':args,'planned':13340,'new_process_timeout_seconds':90,'scope':'Existingown10000records only normalTTLrefresh, no productionkeys/settings/faultwrites','same_workers':8,'same_pool_per_logical_gateway':8,'coalescing_batch':16 if mode=='batch' else 1,'batch_wait_ms':5 if mode=='batch' else 0},indent=2)+'\n')
  child=None;identity=None;proc=None
  with (case/'stdout.log').open('w') as output,(case/'stderr.log').open('w') as error:
   try:
    child=subprocess.Popen(args,stdout=output,stderr=error,start_new_session=True);proc=pathlib.Path(f'/proc/{child.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
    (case/'own-process.json').write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n');deadline=time.monotonic()+90
    while not (case/'ready.json').exists():assert child.poll() is None and time.monotonic()<deadline,'Owncomponentpreflightfailed';time.sleep(.05)
    first=snapshot();(case/'redis-cpu-before.json').write_text(json.dumps(first,indent=2)+'\n');(case/'start_ns').write_text(str(time.monotonic_ns()+500_000_000)+'\n')
    while not (case/'active-completed.json').exists():assert child.poll() is None and time.monotonic()<deadline,'Owncomponentactivefailed';time.sleep(.05)
    last=snapshot();(case/'redis-cpu-after.json').write_text(json.dumps(last,indent=2)+'\n');(case/'release').write_text('Countercaptured, closeonlyownconnections\n');code=child.wait(timeout=max(1,deadline-time.monotonic()));assert code==0,name+' nonzero, preserveeveryraw'
   finally:
    if child is not None:stop(child,proc,identity,args,name)
  data=json.loads((case/'result.json').read_text());assert data['status']=='ONLINE_MAINTENANCE_COMPONENT_COMPLETE' and data['planned']==data['attempted']==data['issued']==data['completed']==13340 and data['errors']==0
  assert len(data['raw'])==13340 and {row[0] for row in data['raw']}==set(range(13340)) and all(row[6]==1 and row[1]<=row[2]<=row[3]<=row[4]<=row[5] for row in data['raw'])
  assert data['fixed_workers']==8 and data['eval_groups']==data['successful_healthy_leases'] and data['max_group_size']<=16
  if mode=='single':assert data['eval_groups']==13340 and data['max_group_size']==1
  delta={key:last['cpu_stat'][key]-value for key,value in first['cpu_stat'].items()};assert all(value>=0 for value in delta.values());elapsed=(last['end_monotonic_ns']-first['end_monotonic_ns'])/1e9
  report={key:value for key,value in data.items() if key not in ['raw','raw_columns']};report['case']=name;report['redis_cgroup']={'elapsed_seconds':elapsed,'delta':delta,'mean_cpu_cores':delta['usage_usec']/1e6/elapsed,'limits':'WholeRedis cgroup including background/0.5sbarrier/finishedrawserialization, native preflight keyreads happenbeforefirstcounter; notexclusive RPCattribution'};reports.append(report);(d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n')
except BaseException as error:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'completed_cases':[row['case'] for row in reports],'ownedkeys_scope':'Onlyexplicitnamespace/TTL, no delete orcleanup','capacity_acceptance':False},indent=2)+'\n');raise
finally:
 after,_=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');(d/'guest-memory-after.txt').write_text(pathlib.Path('/proc/meminfo').read_text())
assert subprocess.run(['pgrep','-f','^'+re.escape(str(binary))+r'( |$)'],capture_output=True).returncode==1
x={'status':'ONLINE_MAINTENANCE_COMPONENT_ABBA_COMPLETE','head':head,'functional_checks':84,'cases':reports,'all19_runtime_configs_preserved':True,'production_keys_changed':False,'SQL_or_durability_changed':False,'userapps_preserved':True,'performance_acceptance':False,'limits':'Isolatednative→Dockermaintenancekeys, no10kPong/privatemessage/fullfeature/50kcapacityproof. Must evaluate allCPU, errors andasyncmaintenancefreshness before productimplementation; do not accept merelyEVALcount drop.'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
