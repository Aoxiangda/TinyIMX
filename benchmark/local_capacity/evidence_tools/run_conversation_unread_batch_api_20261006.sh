#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,shlex,signal,os
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'conversation-unread-batch-api-20261006';assert not d.exists()
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def run(a):return subprocess.check_output(a,text=True,timeout=30)
pre=json.loads((b/'all-feature-optimization-preflight-20261006-attempt2/summary.json').read_text());source=json.loads((b/'conversation-unread-batch-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and pre['redis_cluster_enabled']==0
for p,h in source['files'].items():assert sha(r/p)==h
assert not run(['git','diff','--cached','--name-only']).strip()
assert set(run(['git','diff','--name-only']).splitlines())<=set(source['preserved_dirty'])
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['containers']==pre['runtime'] and before['config_sha256']==pre['private_config_sha256']
build=r/'build/linux-release';cpp=r/'benchmark/local_capacity/conversation_unread_batch_test.cpp';api=r/'services/cache/UnreadCountCache.cpp';binary=d/'conversation_unread_batch_test'
for p,h in pre['borrowed_sha256'].items():assert sha(p)==h
flags_path=build/'CMakeFiles/redis_pool_demo.dir/flags.make';link_path=build/'CMakeFiles/redis_pool_demo.dir/link.txt'
flags_text=flags_path.read_text();flags=[]
for field in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(line for line in flags_text.splitlines() if line.startswith(field+' =')).split('=',1)[1])
link=shlex.split(link_path.read_text());old='CMakeFiles/redis_pool_demo.dir/examples/redis_pool_demo.cpp.o';assert link.count(old)==1
link[link.index(old)]=str(d/'test.o');link[link.index('-o')+1]=str(binary);link.insert(1,str(d/'api.o'));link.append('-Wl,-Map,'+str(d/'link-map.txt'))
assert 'libtinyimx_cache.a' in link and 'libtinyimx_message_grpc.a' not in link
linked={p:sha(build/p) for p in link if p.startswith('libtinyimx_')}
d.mkdir(mode=0o700)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Own bounded UnreadCountCache API/test objects native Redis conformance and closed-loop component ABBA, pool1/4','source':source['files'],'linked_libraries':linked,'namespace':'codex:conversation-unread-batch-20261006:{p1|p4}:','data_scope':'Only own absent fixture keys63+2 percase TTL300, bounded own atomic pair writer; no SQL/production unread/write/delete/FLUSH/restart/config/network fault','semantics':'Keep original healthy Acquire/PING and exact single status/count/error including wrongtype, int64 and invalid input. Max50 read-only Lua atomic snapshot. StandaloneRedis only','test_limits':'Actual native 100 pages perABBAcase closed-loop, not openloop requests/users/Gateway or mixed50k acceptance','build_scope':'Own twoobjects/ELF/map, cacheflags/staticlibraries borrowed by exact hash; no productbinary/CMake cache overwritten','watchdog':'Each compile/link180s, eachcase120s; only own verified PID/startticks/cmdline/PGID can signal. Preserve all failures','rollback':'No deployment or cleanup/reset/prune/push/appsstop; ownkeys naturallyexpire; all source/Git/testdata retained','runtime_before':before},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
def interrupted(signum,frame):raise KeyboardInterrupt('Own API diagnostic interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def stop(child,identity,args,label):
 if child.poll() is not None:return
 proc=pathlib.Path(f'/proc/{child.pid}')
 assert identity and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid
 assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[str(v).encode() for v in args]
 (d/(label+'-stop-audit.json')).write_text(json.dumps({'operation':'Only own verified process group','pid':child.pid,'starttime_ticks':identity,'cmdline_and_pgid_verified':True})+'\n')
 os.killpg(child.pid,signal.SIGTERM)
 try:child.wait(timeout=3)
 except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
def invoke(args,label,cwd,timeout):
 (d/(label+'-command.json')).write_text(json.dumps(args,indent=2)+'\n');child=None;identity=None
 with (d/(label+'.log')).open('w') as output:
  try:
   child=subprocess.Popen(args,cwd=cwd,stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
   try:identity=pathlib.Path(f'/proc/{child.pid}/stat').read_text().rsplit(')',1)[1].split()[19]
   except FileNotFoundError:assert child.poll() is not None
   (d/(label+'-own-process.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n')
   code=child.wait(timeout=timeout);assert code==0,label+' exit '+str(code)
  finally:
   if child is not None:stop(child,identity,args,label)
reports=[];phase='compile-api'
try:
 invoke(['/usr/bin/c++',*flags,'-c',str(api),'-o',str(d/'api.o')],'compile-api',build,180)
 phase='compile-test';invoke(['/usr/bin/c++',*flags,'-c',str(cpp),'-o',str(d/'test.o')],'compile-test',build,180)
 phase='link';invoke(link,'link',build,180)
 assert 'tinyimx::UnreadCountCache::GetPrivateUnreadBatch(' in run(['nm','-C',str(binary)])
 (d/'compiled-sha256.json').write_text(json.dumps({p.name:sha(p) for p in [d/'api.o',d/'test.o',binary]},indent=2)+'\n')
 for mode,pool in [('p1',1),('p4',4)]:
  phase=mode;case=d/mode;case.mkdir(mode=0o700);args=[str(binary),mode,'/home/jackson7/.local/share/tinyimx/m21/config',pre['native_redis_ip'],str(case)]
  (case/'audit-before.json').write_text(json.dumps({'operation':'Own real Redis mixed statuses, 50page, concurrent caller/writer and ABBA component','argv':args,'pool_size':pool,'namespace':'codex:conversation-unread-batch-20261006:'+mode+':','timeout_seconds':120,'production_keys_changed':False,'capacity_acceptance':False},indent=2)+'\n')
  invoke(args,mode,r,120);result=json.loads((case/'result.json').read_text());checks=json.loads((case/'checks.json').read_text())
  assert result['status']=='CONVERSATION_UNREAD_BATCH_API_PASS' and result['pool_size']==pool and result['checks']==len(checks)>100 and all(x['pass'] for x in checks)
  perf=json.loads((case/'component-performance.json').read_text());assert len(perf)==4 and all(x['pages']==100 and x['counts_checked']==5000 and x['incorrect']==0 and len(x['page_wall_ms'])==100 for x in perf)
  reports.append(result);(d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n')
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','phase':phase,'type':type(e).__name__,'message':str(e),'completed':reports,'capacity_acceptance':False},indent=2)+'\n');raise
finally:
 after=runtime();(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');assert after==before
 (d/'guest-memory-after.txt').write_text(pathlib.Path('/proc/meminfo').read_text())
x={'status':'CONVERSATION_UNREAD_BATCH_API_COMPLETE','head':head,'cases':reports,'checks':sum(x['checks'] for x in reports),'runtime_configs_preserved':True,'production_keys_changed':False,'performance_acceptance':False,'limits':'Native component/fault conformance only; Gateway not integrated/deployed and allfeature10k50k not accepted'}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
