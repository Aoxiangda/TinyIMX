#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,shlex,signal,os,socket,threading,time
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'redis-command-timeout-original-probe-20261005';assert not d.exists()
source=json.loads((b/'redis-command-timeout-original-probe-source-20261005/summary.json').read_text());head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head==source['head']
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
connection_source=r/'common/cache/RedisConnection.cpp';assert 'redisSetTimeout(' not in connection_source.read_text()
assert connection_source.read_bytes()==subprocess.check_output(['git','show','33fc9bbbf7b8a8e828ce70d1ae0035d8e8c71ce8:common/cache/RedisConnection.cpp'])
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'redis-command-timeout-original-probe-source-20261005/runtime-after.json').read_text())
preflight=json.loads((b/'online-maintenance-mechanism-preflight-20261005/summary.json').read_text());build=r/'build/linux-release';cpp=r/'benchmark/local_capacity/redis_command_timeout_probe.cpp';binary=d/'redis_command_timeout_probe'
flags_path=build/'CMakeFiles/redis_pool_demo.dir/flags.make';link_path=build/'CMakeFiles/redis_pool_demo.dir/link.txt';assert hashlib.sha256(flags_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['flags.make'] and hashlib.sha256(link_path.read_bytes()).hexdigest()==preflight['cached_compile_inputs']['link.txt']
flags=[];flags_text=flags_path.read_text()
for field in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(line for line in flags_text.splitlines() if line.startswith(field+' =')).split('=',1)[1])
link=shlex.split(link_path.read_text());old='CMakeFiles/redis_pool_demo.dir/examples/redis_pool_demo.cpp.o';assert link.count(old)==1;link[link.index(old)]=str(d/'probe.o');link[link.index('-o')+1]=str(binary)
assert 'libtinyimx_cache.a' in link and 'libtinyimx_message_grpc.a' not in link
linked={part:hashlib.sha256((build/part).read_bytes()).hexdigest() for part in link if part.startswith('libtinyimx_')}
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Original cachedRedisSDK command timeout noresponse reproduction with ownloopback surrogate','head':head,'cpp_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest(),'original_connection_source_sha256':hashlib.sha256(connection_source.read_bytes()).hexdigest(),'connection_matches_running33fc_source':True,'linked_cached_project_sha256':linked,'endpoint':'Own127.0.0.1 OSephemeral loopbackTCP listener, reject6379/11434; not productionRedis','protocol':'AUTH/SELECT/PING/EVAL healthycontrol thenstalled4.25saftervalidcommandreceipt; closeonlyownpeer to releaseoriginal normally','credentials':'No configcredentials used in probe; publicdummy AUTH fixture only. Runtimeconfigs onlyhashed, not exported','writes':'Freshownstage/object/ELF/log/JSON andloopback sockets only; no Rediskeys/SQL/config/service writes','resources':'Onechild/oneserverthread/listener/peer percase, sequential8cases, compile/link180s, percase20s hardguard with5sownpeer-release cleanup. No appstop/cleanup','proof_limits':'Commandstall correctness only, not normal175ms tail cause, no performance acceptance, no productionoutage','rollback':'Closeonlyown sockets/process/thread, keepevery result/partial/failure andallsource/Git; no delete/reset/prune/push','runtime_before':before},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');(d/'original-RedisConnection.cpp').write_bytes(connection_source.read_bytes());(d/'installed-hiredis.h').write_bytes((r/'vcpkg_installed/x64-linux/include/hiredis/hiredis.h').read_bytes())
def interrupted(signum,frame):raise KeyboardInterrupt('Own timeout diagnostic interrupted')
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
def command(stream):
 line=stream.readline(64);assert line.startswith(b'*') and line.endswith(b'\r\n');count=int(line[1:-2]);assert 1<=count<=8;first=None
 for index in range(count):
  line=stream.readline(64);assert line.startswith(b'$') and line.endswith(b'\r\n');length=int(line[1:-2]);assert 0<=length<=1024;value=stream.read(length);assert len(value)==length and stream.read(2)==b'\r\n'
  if index==0:first=value.decode('ascii').upper()
 return first,count
reports=[];phase='compile'
try:
 invoke(['/usr/bin/c++',*flags,'-c',str(cpp),'-o',str(d/'probe.o')],'compile',180);phase='link';invoke(link,'link',180);(d/'binary-sha256.txt').write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'\n')
 for mode,expected,reply in [('ping','PING',b'+PONG\r\n'),('auth','AUTH',b'+OK\r\n'),('select','SELECT',b'+OK\r\n'),('eval','EVAL',b':1\r\n')]:
  for kind in ['healthy','stalled']:
   label=mode+'-'+kind;phase=label;case=d/label;case.mkdir();listener=socket.socket();listener.bind(('127.0.0.1',0));port=listener.getsockname()[1];assert port>=1024 and port not in [6379,11434];listener.listen(1);listener.settimeout(1)
   ready=threading.Event();release=threading.Event();public={};server_error=[]
   (case/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Ownsurrogate positivecontrol' if kind=='healthy' else 'Ownpeer noresponseuntil4.25s, thencloseonlyownpeer','mode':mode,'kind':kind,'loopback_port':port,'expected_command':expected,'production_access':False,'timeout_seconds':20,'credential':'Publiccodedfixture only, no realcredentials/configreading in client'},indent=2)+'\n')
   def serve():
    peer=None
    try:
     deadline=time.monotonic()+8
     while not release.is_set():
      try:peer,address=listener.accept();break
      except socket.timeout:assert time.monotonic()<deadline
     if peer is None:return
     peer.settimeout(5)
     with peer.makefile('rb') as stream:
      cmd,count=command(stream);assert cmd==expected;public.update({'command':cmd,'argument_count':count,'received_monotonic_ns':time.monotonic_ns(),'peer_loopback':address[0]=='127.0.0.1'});ready.set()
      if kind=='healthy':peer.sendall(reply)
      assert release.wait(12),'Ownsurrogate releasetimeout'
    except BaseException as error:server_error.append(type(error).__name__);ready.set()
    finally:
     if peer is not None:
      try:peer.shutdown(socket.SHUT_RDWR)
      except OSError:pass
      peer.close()
   thread=threading.Thread(target=serve,name='own-surrogate-'+label);child=None;identity=None;proc=None;args=[str(binary),mode,str(port),str(case)]
   with (case/'stdout.log').open('w') as output,(case/'stderr.log').open('w') as error:
    try:
     thread.start();child=subprocess.Popen(args,stdout=output,stderr=error,start_new_session=True);proc=pathlib.Path(f'/proc/{child.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
     (case/'own-process.json').write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n');assert ready.wait(8) and not server_error and public['peer_loopback']
     completed_before_release=True
     try:child.wait(timeout=4.25 if kind=='stalled' else 5)
     except subprocess.TimeoutExpired:completed_before_release=False
     public['observed_before_release_monotonic_ns']=time.monotonic_ns();public['completed_before_peer_release']=completed_before_release;public['own_peer_release_monotonic_ns']=time.monotonic_ns();(case/'surrogate-observation.json').write_text(json.dumps(public,indent=2)+'\n');release.set();code=child.wait(timeout=5);assert code==0 and not server_error
    finally:
     release.set();listener.close()
     if child is not None:stop(child,proc,identity,args,label)
     if thread.ident is not None:thread.join(timeout=6);assert not thread.is_alive(),'Ownsurrogatethreadmustclose'
   data=json.loads((case/'result.json').read_text());assert data['status']=='OWN_SURROGATE_CASE_COMPLETE' and data['installed_hiredis']=='1.3.0'
   if kind=='healthy':assert completed_before_release and data['operation_succeeded']
   else:assert not completed_before_release and not data['operation_succeeded'] and data['elapsed_seconds']>4.0
   report={'case':label,'expected_red_bound_violation':kind=='stalled','surrogate':public,'native':data};reports.append(report);(d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n')
except BaseException as error:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'completed_cases':[row['case'] for row in reports],'production_access':False,'capacity_acceptance':False},indent=2)+'\n');raise
finally:
 after=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
assert subprocess.run(['pgrep','-f','^'+re.escape(str(binary))+r'( |$)'],capture_output=True).returncode==1
x={'status':'ORIGINAL_REDIS_COMMAND_TIMEOUT_MISSING_REPRODUCED','head':head,'healthy_controls':4,'stalled_bound_violations':4,'cases':reports,'all19_runtime_configs_health_preserved':True,'production_redis_access':False,'userapps_preserved':True,'performance_acceptance':False,'limits':'OriginalcachedSDK no-response command bound violation, onlyownsurrogate, not rootcause proof for normal175ms endpointlatency. Next separatelyaudit finite commandtimeout andoriginalAPI regression before asyncbatch lifecycle implementation'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
