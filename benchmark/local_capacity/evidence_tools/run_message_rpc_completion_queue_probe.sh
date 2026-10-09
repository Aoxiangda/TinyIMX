#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,subprocess,hashlib,shlex,re,time,os,signal
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'message-rpc-completion-queue-probe-20261005';assert not d.exists()
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();source=json.loads((b/'message-rpc-completion-queue-probe-source-20261005/summary.json').read_text());assert source['head']==head
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'message-rpc-completion-queue-probe-source-20261005/runtime-after.json').read_text())
assert before['containers']['/tinyimx-m21-mcp-server-1']['image']=='sha256:bc85c186271873610ff759c008d5f36d49d1a9c331e064eb7fa1121d822485a3'
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
cpp=r/'benchmark/local_capacity/message_rpc_completion_queue_probe.cpp';build=r/'build/linux-release';d.mkdir();binary=d/'message_rpc_completion_queue_probe';p=None
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Compile andrun isolated trueCompletionQueue versus sameSync20ms transport/thread mechanism, no production modifications','head':head,'source_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest(),'writes':'Fresh ownobject/binary/logs/numeric metrics only; no realSQL/Redis/fixture/users/AI','cases':['grpc-A1','cq-B1','cq-B2','grpc-A2'],'compiler':'Existingflags/generatedprotobuf/gRPCcache, one compiler; excludeallproductimplementationarchives includingcachedMAX16 MessageServer','runtime_before':before,'runtime_changes':False,'trace':'NONE; compare untracedrusage and perrequestmatchedelapsed, no waitshareCPU interpretation','cleanup':'Eachownedprocess35s max, ownsessioncmdline/starttime+PGIDguards for timeout/signals; no unrelatedPIDs','limitations':'Process CPU combines caller+server; controlledsleep+echo omitsSQLmultipleRTTs/domainwork/10kheartbeat, notproductioncapacity orsolecause proof; pairedperrequesttimings andsharedhostvariation recorded'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');(d/'guest-memory-before.txt').write_text(pathlib.Path('/proc/meminfo').read_text())
flags_text=(build/'CMakeFiles/message_service_integration_tests.dir/flags.make').read_text();flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in flags_text.splitlines() if x.startswith(key+' =')).split('=',1)[1])
(d/'cached-flags.make').write_text(flags_text);link=shlex.split((build/'CMakeFiles/message_service_integration_tests.dir/link.txt').read_text());old='CMakeFiles/message_service_integration_tests.dir/tests/message/message_service_integration_test.cpp.o';assert link.count(old)==1
link[link.index(old)]=str(d/'probe.o');link[link.index('-o')+1]=str(binary)
removed=[x for x in link if x.startswith('libtinyimx_') and x!='libtinyimx_rpc_proto.a'];link=[x for x in link if x not in removed];assert 'libtinyimx_message_grpc.a' not in link and 'libtinyimx_rpc_proto.a' in link
(d/'excluded-product-archives.json').write_text(json.dumps(removed,indent=2)+'\n')
def interrupted(signum,frame):raise KeyboardInterrupt('Own component diagnostic interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def compile_call(args,label):
 (d/(label+'-command.json')).write_text(json.dumps(args,indent=2)+'\n')
 child=None;error=None;code=None;proc=None;identity=None
 with (d/(label+'.log')).open('w') as f:
  try:
   child=subprocess.Popen(args,cwd=build,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path(f'/proc/{child.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   (d/(label+'-own-process.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n');code=child.wait(timeout=180)
  except BaseException as exc:error=exc
  finally:
   if child is not None and child.poll() is None:
    assert identity is not None and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid
    assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
    (d/(label+'-stop-audit.json')).write_text(json.dumps({'operation':'Stoponlyownverifiedcompiler/linkerprocessgroup','pid':child.pid,'cmdline_starttime_pgid_verified':True})+'\n');os.killpg(child.pid,signal.SIGTERM)
    try:child.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
 if error is not None or code!=0:
  (d/'failed.json').write_text(json.dumps({'status':'FAIL','phase':label,'exit_code':code,'type':type(error).__name__ if error else 'CompilerExit','message':str(error) if error else label+' nonzero exit'})+'\n');after=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
  if error is not None:raise error
  raise RuntimeError(label+' failed, all logs preserved')
compile_call(['/usr/bin/c++',*flags,'-c',str(cpp),'-o',str(d/'probe.o')],'compile');compile_call(link,'link')
(d/'probe-binary-sha256.txt').write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'\n')
reports=[]
try:
 for name,mode,seconds,trace in [('grpc-A1','grpc',20,False),('cq-B1','cq',20,False),('cq-B2','cq',20,False),('grpc-A2','grpc',20,False)]:
  args=[str(binary),mode,'300',str(seconds)]
  if trace:args=['/usr/bin/strace','-qq','-f','-c','-o',str(d/'callback-syscall-summary.txt'),*args]
  (d/(name+'-run-audit-before.json')).write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Runexactownedboundedcomponentprocess','argv':args,'nominal_seconds':seconds,'timeout_seconds':35,'controlled_handler_wait_ms':20,'expected_synthetic_results':300*seconds,'trace_perturbs':trace,'no_business_acceptance':True},indent=2)+'\n')
  with (d/(name+'-result.json')).open('w') as out,(d/(name+'-stderr.log')).open('w') as err:
   p=subprocess.Popen(args,stdout=out,stderr=err,start_new_session=True);proc=pathlib.Path(f'/proc/{p.pid}');start=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19];(d/(name+'-own-process.json')).write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'starttime_ticks':start,'argv':args})+'\n')
   try:code=p.wait(timeout=35)
   finally:
    if p.poll() is None:
     assert proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==start and os.getpgid(p.pid)==p.pid
     cmd=proc.joinpath('cmdline').read_bytes().split(b'\0');assert cmd[:len(args)]==[x.encode() for x in args]
     (d/(name+'-timeout-stop-audit.json')).write_text(json.dumps({'operation':'Stop onlyownverifiedsessionprocessgroup','pid':p.pid,'starttime_cmdline_pgid_verified':True})+'\n');os.killpg(p.pid,signal.SIGTERM)
     try:p.wait(timeout=3)
     except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
  assert code==0,name+' failed, preserveoutput'
  data=json.loads((d/(name+'-result.json')).read_text());assert data['status']=='SYNTHETIC_COMPONENT_COMPLETE' and data['planned']==data['synthetic_result_ok']==300*seconds and data['exceptions']==0 and data['negative_matched_extra']==0
  assert len(data['raw'])==300*seconds and len({x[0] for x in data['raw']})==300*seconds
  if mode=='cq':
   q=data['cq_executor'];assert q['submitted']==q['completed']==300*seconds and q['rejected']==q['pending']==0 and q['fixed_workers']==16 and q['peak_pending']<=512 and data['handler_distinct_tids']<=16 and q['queue']==0 and q['accepted_calls']==q['finish_completions']==300*seconds and q['live_call_objects']==0 and q['allocated_call_objects']==q['deleted_call_objects'] and q['finish_not_ok']==0 and q['cancelled_unbound_accepts']>0 and q['cq_pollers']==2
  reports.append({'case':name,'trace_perturbed':trace,**{k:v for k,v in data.items() if k not in ['raw','raw_columns']}})
  (d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n')
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e),'completed_cases':[x['case'] for x in reports]})+'\n');raise
finally:
 after=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');(d/'guest-memory-after.txt').write_text(pathlib.Path('/proc/meminfo').read_text())
assert subprocess.run(['pgrep','-f','^'+re.escape(str(binary))+r'( |$)'],capture_output=True).returncode==1
x={'status':'MESSAGE_RPC_COMPLETION_QUEUE_COMPONENT_DIAGNOSTIC_COMPLETED','head':head,'cases':reports,'all19_runtime_configs_preserved':True,'real_DB_or_business_writes':False,'performance_acceptance':False,'limits':'Normal synthetic only; server thenworker thenCQ drain andalltagsreclaimed, no proof of production cancellation/overload/SQLdeadline or fullshutdown durability. Sharedprocesscaller+server CPU andperrequestextra include CQ workerqueue. No realSQL/readwrite/multihop/10kheartbeat or originalMessageexclusiveCPU/capacity acceptance.'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
