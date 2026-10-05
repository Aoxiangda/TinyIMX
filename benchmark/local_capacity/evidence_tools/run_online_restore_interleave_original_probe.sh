#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,shlex,signal,os
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-restore-interleave-original-20261005';assert not d.exists()
source=json.loads((b/'online-restore-interleave-original-source-20261005/summary.json').read_text());head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head==source['head']
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}},cs
before,containers=runtime();assert before==json.loads((b/'online-restore-interleave-original-source-20261005/runtime-after.json').read_text())
fixed=b/'redis-command-timeout-fixed-probe-20261005';assert json.loads((fixed/'summary.json').read_text())['status']=='REDIS_COMMAND_IDLE_TIMEOUT_FIXED_PROBE_COMPLETE';hashes=json.loads((fixed/'compiled-sha256.json').read_text());objects=[fixed/'connection.o',b/'redis-command-timeout-api-regression-20261005/api.o']
for p in objects:assert hashlib.sha256(p.read_bytes()).hexdigest()==hashes[str(p.relative_to(b))]
api_inputs=json.loads((b/'online-maintenance-batch-api-20261005/audit-before.json').read_text())['source_sha256']
for path in ['services/cache/OnlineStatusCache.cpp','services/cache/OnlineStatusCache.h']:assert hashlib.sha256((r/path).read_bytes()).hexdigest()==api_inputs[path]
gateway=(r/'gateway/GatewayServer.cpp').read_text();heartbeat=re.search(r'void GatewayServer::HandleHeartbeat\((.*?)\n\}',gateway,re.S).group(1);refresh=re.search(r'void GatewayServer::RefreshUserOnlineIfMatch\((.*?)\n\}',gateway,re.S).group(1);offline=re.search(r'task\.request\.operation =\s*"gateway\.presence\.offline";(.*?)const BusinessSubmitStatus status',gateway,re.S).group(1)
assert 'std::nullopt' in heartbeat and 'ordering_key' not in offline and 'SetUserOnline(user_id, connection);' in refresh
preflight=json.loads((b/'online-maintenance-mechanism-preflight-20261005/summary.json').read_text());ip=preflight['public_connection_fields']['actual_native_target_ip'];assert ip=='172.18.0.13';redis=next(c for c in containers if c['Name']=='/tinyimx-m21-redis-1');assert {net['IPAddress'] for net in redis['NetworkSettings']['Networks'].values() if net.get('IPAddress')}=={ip}
build=r/'build/linux-release';cpp=r/'benchmark/local_capacity/online_restore_interleave_probe.cpp';binary=d/'online_restore_interleave_probe';flags_path=build/'CMakeFiles/redis_pool_demo.dir/flags.make';link_path=build/'CMakeFiles/redis_pool_demo.dir/link.txt'
assert hashlib.sha256(flags_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['flags.make'] and hashlib.sha256(link_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['link.txt']
flags=[];flags_text=flags_path.read_text()
for field in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(line for line in flags_text.splitlines() if line.startswith(field+' =')).split('=',1)[1])
link=shlex.split(link_path.read_text());old='CMakeFiles/redis_pool_demo.dir/examples/redis_pool_demo.cpp.o';assert link.count(old)==1;link[link.index(old)]=str(d/'probe.o');link[link.index('-o')+1]=str(binary);link[1:1]=[str(p) for p in objects]
assert 'libtinyimx_cache.a' in link and 'libtinyimx_message_grpc.a' not in link
linked={part:hashlib.sha256((build/part).read_bytes()).hexdigest() for part in link if part.startswith('libtinyimx_')}
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Deterministic original self-heal public-cache-API interleaving reconstruction','head':head,'cpp_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest(),'retained_green_object_sha256':{str(p.relative_to(b)):hashes[str(p.relative_to(b))] for p in objects},'cached_project_library_sha256':linked,'scope':'Own diagnosticfrontend only, existing proven productAPI/Connection objects, no newproduct build/deployment/source/SQL/realGateway sessions orlogin changes','keys':'codex:online-restore-interleave-probe-20261005:60001..60004 only4new keys,initialEXISTS0, TTL120/300, explicitown SetOnline competitor/originalwrites','offline_operation':'SetOfflineIfMatch onlyown initiallymissingfixture: expectedNotFound/no deletion, no directDEL/FLUSH/CONFIG/global writes','modeled_predicate':'Explicit local-current boolean in deterministic schedule; actualTCP/Gateway session notmodeled bynetwork, no live racefrequency/capacityclaim','cases':'2healthy controls and2expected originalred invariants, rawstates/sequences kept. RemoteSetbetweencheck/originalrestore overwritesnewowner; unbind+missingcleanupbetweencheck/restore createsoldrecord','resources':'Ownpool1,privateconfig onlyRAM/no credentialargv/export; compile/link180s/native30s onlyownPIDidentitycleanup, allpartial/log/failure preserved','rollback':'Closeonlyownpool/process,retainkeys naturalTTL/source/raw/Git; no reset/delete/prune/push/appstop/cleanup','runtime_before':before},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');(d/'original-heartbeat-source.txt').write_text(heartbeat);(d/'original-refresh-selfheal-source.txt').write_text(refresh);(d/'original-offline-task-source.txt').write_text(offline)
(d/'source-facts.json').write_text(json.dumps({'gateway_source_sha256':hashlib.sha256((r/'gateway/GatewayServer.cpp').read_bytes()).hexdigest(),'heartbeat_ordering_key':'nullopt','offline_ordering_key':'unset','missing_restore':'Afterlocalcurrentcheck invokes unconditionalSetUserOnline→SetOnline/SETEX','proof_limits':'PublicAPI deterministicinterleaving only, no realTCPsession/racefrequency acceptance'},indent=2)+'\n')
def interrupted(signum,frame):raise KeyboardInterrupt('Own restore-interleave probe interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def stop(child,proc,identity,args,label):
 if child.poll() is None:
  assert identity is not None and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid
  assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[str(value).encode() for value in args]
  (d/(label+'-stop-audit.json')).write_text(json.dumps({'operation':'Stoponlyownverifiedprocessgroup','pid':child.pid,'cmdline_starttime_pgid_verified':True})+'\n');os.killpg(child.pid,signal.SIGTERM)
  try:child.wait(timeout=3)
  except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
def invoke(args,label,timeout):
 (d/(label+'-command.json')).write_text(json.dumps(args,indent=2)+'\n');child=None;identity=None;proc=None
 with (d/(label+'.log')).open('w') as output:
  try:
   child=subprocess.Popen(args,cwd=build,stdout=output,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path(f'/proc/{child.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   (d/(label+'-own-process.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n');code=child.wait(timeout=timeout);assert code==0,label+' failed exit '+str(code)
  finally:
   if child is not None:stop(child,proc,identity,args,label)
phase='compile'
try:
 invoke(['/usr/bin/c++',*flags,'-c',str(cpp),'-o',str(d/'probe.o')],'compile',180);phase='link';invoke(link,'link',180);(d/'binary-sha256.txt').write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'\n');phase='native';invoke([str(binary),'/home/jackson7/.local/share/tinyimx/m21/config',ip,str(d)],'native',30)
 result=json.loads((d/'result.json').read_text());assert result['status']=='ORIGINAL_ONLINE_RESTORE_INTERLEAVING_WINDOWS_REPRODUCED' and result['healthy_controls']==result['original_red_windows']==2 and len(result['cases'])==4
 assert sum(item['expected_original_red'] for item in result['cases'])==2 and all(item['desired_safety_invariant_pass'] != item['expected_original_red'] for item in result['cases'])
except BaseException as error:
 (d/'helper-failed.json').write_text(json.dumps({'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'live_gateway_sessions_tested':False,'production_keys':False,'capacity_acceptance':False},indent=2)+'\n');raise
finally:
 after,_=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
assert subprocess.run(['pgrep','-f','^'+re.escape(str(binary))+r'( |$)'],capture_output=True).returncode==1
x={'status':'ORIGINAL_ONLINE_RESTORE_INTERLEAVING_PROBE_COMPLETE','head':head,'result':result,'all19_runtime_configs_health_preserved':True,'userapps_preserved':True,'performance_acceptance':False,'limits':'Sourcewindow actualAPI deterministic model only, no liveGateway frequency orfullcapacityclaim; next separatelyaudit atomicmissing-onlyrestore andshareduserordering/sessionfencing thenasyncbatch lifecycle integration'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
