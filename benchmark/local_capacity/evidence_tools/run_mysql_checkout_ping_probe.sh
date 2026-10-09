#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,subprocess,hashlib,shlex,re,time,os,signal
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'mysql-checkout-ping-probe-20261005';assert not d.exists()
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();source=json.loads((b/'mysql-checkout-ping-probe-source-20261005/summary.json').read_text());assert source['head']==head
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'mysql-checkout-ping-probe-source-20261005/runtime-after.json').read_text())
assert before['containers']['/tinyimx-m21-mcp-server-1']['image']=='sha256:bc85c186271873610ff759c008d5f36d49d1a9c331e064eb7fa1121d822485a3'
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
cpp=r/'benchmark/local_capacity/mysql_checkout_ping_probe.cpp';build=r/'build/linux-release';d.mkdir();binary=d/'mysql_checkout_ping_probe';p=None
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Bounded readonly connection network roundtrip ABBA diagnosis','head':head,'source_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest(),'SQL':'SELECT 1 AS probe_value only, existing private credentials read in RAM, no tables or writes','connections':'16fresh exclusive nativeworker connections; native-to-Docker differs from production route','cases':['ping-query-A1','query-B1','query-B2','ping-query-A2'],'compiler':'Existing mysql_pool_demo flags/link/cache, no SDK install or product build','runtime_before':before,'runtime_changes':False,'cleanup':'Own180s compiler/35s cases newprocessgroups with cmdline/starttime/PGID guard only','acceptance':False,'limits':'Healthy query-only control is not product health policy or failure recovery proof; no real commit/domain/10kload; MySQL cgroup includes background and setup whereas probe rusage only measured loop'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');(d/'guest-memory-before.txt').write_text(pathlib.Path('/proc/meminfo').read_text())
flags_text=(build/'CMakeFiles/mysql_pool_demo.dir/flags.make').read_text();flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in flags_text.splitlines() if x.startswith(key+' =')).split('=',1)[1])
(d/'cached-flags.make').write_text(flags_text);link=shlex.split((build/'CMakeFiles/mysql_pool_demo.dir/link.txt').read_text());old='CMakeFiles/mysql_pool_demo.dir/examples/mysql_pool_demo.cpp.o';assert link.count(old)==1
link[link.index(old)]=str(d/'probe.o');link[link.index('-o')+1]=str(binary)
assert 'libtinyimx_db.a' in link and 'libtinyimx_message_grpc.a' not in link
(d/'linked-project-libraries.json').write_text(json.dumps([x for x in link if x.startswith('libtinyimx_')],indent=2)+'\n')
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
mysql=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-mysql-1'],text=True))[0]
ips={x['IPAddress'] for x in mysql['NetworkSettings']['Networks'].values() if x.get('IPAddress')};assert len(ips)==1;ip=next(iter(ips))
config=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json')
fields=json.loads(config.read_text())['mysql'];assert set(['host','port','database','user','password']).issubset(fields);assert fields['pool_size']==16;del fields
cg=next(line.split(':',2)[2] for line in pathlib.Path('/proc/'+str(mysql['State']['Pid'])+'/cgroup').read_text().splitlines() if line.startswith('0::'))
cpu=pathlib.Path('/sys/fs/cgroup')/cg.lstrip('/')/'cpu.stat';assert cpu.is_file()
def snapshot():
 started=time.monotonic_ns();values={k:int(v) for k,v in (line.split() for line in cpu.read_text().splitlines())};ended=time.monotonic_ns()
 return {'start_monotonic_ns':started,'end_monotonic_ns':ended,'cpu_stat':values}
reports=[]
try:
 for name,mode in [('ping-query-A1','ping-query'),('query-B1','query'),('query-B2','query'),('ping-query-A2','ping-query')]:
  result=d/(name+'-result.json');args=[str(binary),mode,'300','20',ip,str(config),str(result)]
  (d/(name+'-run-audit-before.json')).write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Bounded ownreadonly SELECT1 process','argv':args,'timeout_seconds':35,'real_table_writes':False,'expected_results':6000,'initial_connections':16,'no_business_acceptance':True},indent=2)+'\n')
  first=snapshot();(d/(name+'-mysql-cpu-before.json')).write_text(json.dumps(first,indent=2)+'\n')
  with (d/(name+'-stdout.log')).open('w') as out,(d/(name+'-stderr.log')).open('w') as err:
   p=subprocess.Popen(args,stdout=out,stderr=err,start_new_session=True);proc=pathlib.Path(f'/proc/{p.pid}');start=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   (d/(name+'-own-process.json')).write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'starttime_ticks':start,'argv':args})+'\n')
   try:code=p.wait(timeout=35)
   finally:
    if p.poll() is None:
     assert proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==start and os.getpgid(p.pid)==p.pid
     assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
     (d/(name+'-stop-audit.json')).write_text(json.dumps({'operation':'Stop onlyownverifiedconnectionprobe session','pid':p.pid,'identity_cmdline_pgid_verified':True})+'\n');os.killpg(p.pid,signal.SIGTERM)
     try:p.wait(timeout=3)
     except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
  last=snapshot();(d/(name+'-mysql-cpu-after.json')).write_text(json.dumps(last,indent=2)+'\n');assert code==0,name+' nonzero, preserve logs'
  data=json.loads(result.read_text());assert data['status']=='READONLY_CONNECTION_COMPONENT_COMPLETE' and data['planned']==data['select_one_correct']==6000 and data['exceptions']==0
  assert len(data['raw'])==6000 and len({x[0] for x in data['raw']})==6000 and data['thread_cpu']['count']==6000
  assert data['ping_calls']==(6000 if mode=='ping-query' else 0)
  delta={k:last['cpu_stat'][k]-v for k,v in first['cpu_stat'].items()};assert all(v>=0 for v in delta.values())
  elapsed=(last['end_monotonic_ns']-first['end_monotonic_ns'])/1e9
  report={'case':name,**{k:v for k,v in data.items() if k not in ['raw','raw_columns']},'mysql_cgroup':{'elapsed_seconds':elapsed,'delta':delta,'mean_cpu_cores':delta['usage_usec']/1e6/elapsed,'limits':'Whole MySQL cgroup including ownstartup/close and existingbackground work, notexclusive probe attribution'}}
  reports.append(report);(d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n')
except BaseException as error:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(error).__name__,'message':str(error),'completed_cases':[x['case'] for x in reports]})+'\n');raise
finally:
 after=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');(d/'guest-memory-after.txt').write_text(pathlib.Path('/proc/meminfo').read_text())
assert subprocess.run(['pgrep','-f','^'+re.escape(str(binary))+r'( |$)'],capture_output=True).returncode==1
sql=subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-readonly-check','SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'],text=True,timeout=10).strip();assert sql=='1\t1\t1\t0\t0'
(d/'sql-durability-after.txt').write_text(sql+'\n')
x={'status':'MYSQL_CHECKOUT_PING_READONLY_COMPONENT_DIAGNOSTIC_COMPLETED','head':head,'cases':reports,'all19_runtime_configs_preserved':True,'real_table_writes':False,'performance_acceptance':False,'SQL_durability':'1/1/1/0/0 unchanged','limits':'Healthy SELECT1 costcontrol, no product checkoutpolicy/deployment/fault/realmessage/10kcapacity claim'}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
