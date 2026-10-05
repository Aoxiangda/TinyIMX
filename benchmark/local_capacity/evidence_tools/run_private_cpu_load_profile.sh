#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export CODEX_SQL_BATCH_PHASE="on" CODEX_SQL_BATCH_RUN="cpuprofile150"
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time,hashlib,threading,re,sys,os,signal
r=pathlib.Path.cwd();b=r/'.local/codex';phase=os.environ['CODEX_SQL_BATCH_PHASE'];name=os.environ['CODEX_SQL_BATCH_RUN']
assert (phase,name)==('on','cpuprofile150')
d=b/'private-cpu-load-profile-20261005';raw=b/('capacity-'+name);assert not d.exists() and not raw.exists()
source=json.loads((b/'private-cpu-load-profile-source-20261005/summary.json').read_text());assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==source['head']
original=json.loads((b/'private-batch-message-deployment-retained-on-20261005/runtime-after.json').read_text())
assert hashlib.sha256((r/'build/linux-release/tinyimx_capacity_worker').read_bytes()).hexdigest()=='5d6bd183ef7119783497f60cda99566ad3d7f7bfcb96192fbdc4b46b8c1f40d3'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
sys.path.insert(0,str(r/'benchmark/local_capacity'))
from apply_pending_recipient_index import definitions,EXPECTED,NAME
indexes={**EXPECTED,NAME:[('delivery_status','1','YES'),('to_user_id','1','YES')]};assert definitions()==indexes
def sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-readonly-guard-control',q],text=True,timeout=20)
durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'
assert sql(durability).strip()=='1\t1\t1\t0\t0'
image=json.loads((b/'private-batch-message-build-image-20261005/summary.json').read_text());assert image['status']=='PRIVATE_BATCH_FULL_MESSAGE_SEALED_IMAGE_PASS'
expected_image=image['image_id'];expected_binary=image['binary_sha256'];gateway_image='sha256:a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9'

def identity():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 current={'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
 assert current['config_sha256']==original['config_sha256'];assert current['containers']==original['containers']
 target=next(c for c in cs if c['Name']=='/tinyimx-m21-message-service-1');assert target['Image']==expected_image
 assert target['State'].get('Health',{}).get('Status')=='healthy'
 env=dict(pair.split('=',1) for pair in target['Config']['Env']);assert env.get('TINYIMX_PERSIST_PHASE_TRACE_ENABLE')=='1' and 'TINYIMX_STORAGE_WAIT_TRACE_ENABLE' not in env and 'TINYIMX_MYSQL_POOL_TRACE' not in env
 assert env.get('TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE')==('1' if phase=='on' else '0')
 for gw in cs:
  if gw['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:continue
  assert gw['Image']==gateway_image and dict(x.split('=',1) for x in gw['Config']['Env']).get('TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE')=='1'
 assert subprocess.check_output(['docker','exec',target['Id'],'sha256sum','/opt/tinyimx/bin/message_service_demo'],text=True).split()[0]==expected_binary
 assert json.loads((cfg/'message.json').read_text())['mysql']['pool_size']==16
 return current,cs
before,containers=identity();d.mkdir()
def preserve_helper_failure(kind,error,traceback):
 (d/'helper-failed.json').write_text(json.dumps({'status':'FAIL','type':kind.__name__,'message':str(error),'run':name,'head':source['head'],'limits':'Retain actual partialrun and all owned diagnostic evidence, no acceptance'})+'\n')
 sys.__excepthook__(kind,error,traceback)
sys.excepthook=preserve_helper_failure
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Diagnostic49Hz flatCPU profile of retainedMessage andbothGW at fixed10k150, notacceptance','phase':phase,'run':name,'head':source['head'],'compiled_message':image['head'],'same_ELF':True,'batch_flag':phase,'compiled_gateway':'38f41a9','message_image':expected_image,'message_binary_sha256':expected_binary,'worker_compiled':'2bb6b32','worker_sha256':'5d6bd183ef7119783497f60cda99566ad3d7f7bfcb96192fbdc4b46b8c1f40d3','runtime_before':before,'users':10000,'rate':150,'duration':60,'plan':9000,'ramp_users_per_second':100,'original_deadlines_unchanged':True,'other_apps':'Preserved','writes':'Normal owned benchmark700000base messages/ACK and own logs only, no configs/settings/reset/cleanup','observer':'2readonly counter snapshots plus three45sec userCPU samplers on actualservicechildPID; originalrawlatency retained but diagnosticONLY, no acceptance','gates':'Allauth/all9000attempts+positive+wire+SQLconfirmed/no skip/negative/late/disconnect/HBdrain andbothP99<=100; retain every FAIL; no all-feature acceptance','limits':'Sequential sharedhost, private-only150 workload. No source build duringwindow; no production CPU attribution from syscall wait shares','timeout':'Own capacityprocessgroup600s guarded identity; signals finally stop onlyown group','rollback':'Ownclientsdrain; Exact originalMessage rollback helper available; preserve every DBrow and raw result'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');(d/'guest-memory-before.txt').write_text(pathlib.Path('/proc/meminfo').read_text())

private_dir=d/'runtime-private';private_dir.mkdir(mode=0o700)
bootstrap=b/'private-cpu-profiler-bootstrap-20261005-attempt2';approved=json.loads((bootstrap/'summary.json').read_text());assert approved['status']=='PRIVATE_CPU_PROFILER_SAMEUID_BOOTSTRAP_PASS'
launcher=bootstrap/'perf_uid_launcher';assert launcher.stat().st_mode&0o777==0o555 and hashlib.sha256(launcher.read_bytes()).hexdigest()==json.loads((bootstrap/'own-elf-permissions-audit-before.json').read_text())['sha256']
native=pathlib.Path('/usr/lib/linux-tools/6.8.0-138-generic/perf');assert subprocess.check_output(['uname','-r'],text=True).strip()=='6.8.0-138-generic'
ldd=subprocess.check_output(['ldd',str(native)],text=True);assert 'not found' not in ldd;libs={x for x in re.findall(r'(/[^\s]+)',ldd) if pathlib.Path(x).is_file()}
mounts=[(launcher,'/opt/codex-perf/launcher'),(native,'/opt/codex-perf/perf'),(pathlib.Path('/usr/bin/sleep'),'/opt/codex-perf/sleep')]+[(pathlib.Path(p),p) for p in sorted(libs)]
tool_hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p,_ in mounts};settings={k:pathlib.Path('/proc/sys/kernel/'+k).read_text().strip() for k in ['perf_event_paranoid','kptr_restrict']};assert settings=={'perf_event_paranoid':'4','kptr_restrict':'1'}
base=['docker','create','--pull','never','--read-only','--network','none','--cap-drop','ALL','--security-opt','no-new-privileges:true','--pids-limit','64','--memory','128m','--cpus','1','--ulimit','memlock=8388608:8388608','--log-driver','none','--tmpfs','/tmp:rw,noexec,nosuid,size=16m']
for p,dst in mounts:base+=['--mount','type=bind,src='+str(p.resolve())+',dst='+dst+',readonly']
profile_image=expected_image;targets=[];profile_jobs=[];profile_results=[]
def discover(c,path,expected_sha):
 script='for p in /proc/[0-9]*; do e=$(readlink "$p/exe" 2>/dev/null || true); if [ "$e" = "$1" ]; then printf "PID %s\\n" "${p##*/}"; cat "$p/stat"; grep -E "^(Uid|Gid):" "$p/status"; test -r "$p/maps" && echo MAPS_OK; test -r "$p/root$1" && echo ELF_OK; fi; done'
 text=subprocess.check_output(['docker','exec','--user','1000',c['Id'],'sh','-c',script,'own-service-child',path],text=True,timeout=15)
 lines=text.splitlines();assert len(lines)==6 and lines[0].startswith('PID ') and lines[-2:]==['MAPS_OK','ELF_OK'];pid=int(lines[0].split()[1]);assert pid>1
 stat=lines[1];assert int(stat.split()[0])==pid;start_ticks=stat.rsplit(')',1)[1].split()[19]
 assert lines[2].split()==['Uid:','1000','1000','1000','1000'] and lines[3].split()==['Gid:','1000','1000','1000','1000']
 binary=subprocess.check_output(['docker','exec','--user','1000',c['Id'],'sha256sum',path],text=True,timeout=15).split()[0];assert binary==expected_sha
 return {'container_id':c['Id'],'container_name':c['Name'],'container_image':c['Image'],'container_started':c['State']['StartedAt'],'host_init_pid':c['State']['Pid'],'namespace_service_pid':pid,'service_start_ticks':start_ticks,'exe':path,'binary_sha256':binary,'uid':1000,'gid':1000,'maps_and_actual_root_ELF_readable':True,'pid1_is_init_not_profiled':True}
for c in containers:
 if c['Name']=='/tinyimx-m21-message-service-1':label='message';path='/opt/tinyimx/bin/message_service_demo';binary=expected_binary
 elif c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:label=c['Name'].split('-')[-2];label='gateway-'+label;path='/opt/tinyimx/bin/gateway_demo';binary='c2894a21cf308ad35ef665c603d17b1a2577c9216aa9e74856772befa99d640f'
 else:continue
 target=discover(c,path,binary);target['label']=label;targets.append(target)
assert len(targets)==3 and len({x['label'] for x in targets})==3
(d/'profile-audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':source['head'],'operation':'Attach narrow userCPU samplers to actualthreeownedservicechildprocesses in respectivecontainerPIDnamespace, no hostPID','targets':targets,'inputs_sha256':tool_hashes,'settings_before':settings,'startup_caps':'PERFMON/SETUID/SETGID/SETPCAP onlylauncher, all bootstrap capabilities removed E/P/I/B/A beforeexec to1000/onlyPERFMON','record':'cpu-clock:u49Hz45s flatIP no stack/regs/memory/kernel; record -o - private0600/0700/lognone; targetpids notPID1init; syscallcaps no SYS_PTRACE/SYS_ADMIN/unconfined','report':'Only AFTER rawcapacity process/clients fullydrain; UID1000/capNONE/targetPIDnamespace, symfsactual /proc/<servicePID>/root fixes productDSO mapping despite nativeperf libcbindings','window':'Allclientsready/start+4s, threeparallel45sec recorders expectedfinishedbefore58.5ssnapshot and60srunner completion','limits':'Instrumentedrunlatencydiagnostic only; userCPU percent not wallwait/kernelCPU, sampling overhead real; no allfeatureacceptance','preserve':'All19IDs/configs/worker/SQLdurability/index/toolsha/apps unchanged; preserve allnew/failed ownstages/probes/rows'},indent=2)+'\n')
def make_probe(target,report=False):
 label=target['label'];probe_name='tinyimx-codex-real-cpu-'+label+('-report' if report else '')+'-20261005';assert subprocess.run(['docker','inspect',probe_name],capture_output=True).returncode!=0
 pid=target['namespace_service_pid']
 args=['report','-i','-','--stdio','--no-children','--sort','dso,symbol','--show-nr-samples','--percent-limit','0.5','--symfs','/proc/'+str(pid)+'/root'] if report else ['1000','1000','/opt/codex-perf/perf','record','-B','-N','-e','cpu-clock:u','-F','49','-m','8','-p',str(pid),'-o','-','--','/opt/codex-perf/sleep','45']
 entry='/opt/codex-perf/perf' if report else '/opt/codex-perf/launcher';cmd=base+['--name',probe_name,'--user','1000' if report else '0','--pid','container:'+target['container_id']]
 if report:cmd+=['-i']
 else:cmd+=['--cap-add','PERFMON','--cap-add','SETUID','--cap-add','SETGID','--cap-add','SETPCAP']
 cmd+=['--entrypoint',entry,profile_image,*args];key=label+('-report' if report else '-record')
 (d/(key+'-create-audit-before.json')).write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'argv':cmd,'target':target,'initial_caps':[] if report else ['PERFMON','SETUID','SETGID','SETPCAP'],'sampler_final_caps':[] if report else ['PERFMON'],'writes':'Nohostwrite mounts; rawstdout only or reportstdin fromownraw'},indent=2)+'\n')
 cid=subprocess.check_output(cmd,text=True,timeout=20).strip();c=json.loads(subprocess.check_output(['docker','inspect',cid],text=True))[0];h=c['HostConfig']
 assert c['Id']==cid and c['Name']=='/'+probe_name and c['Image']==profile_image and c['State']['Status']=='created' and c['Config']['Entrypoint']==[entry] and c['Config']['Cmd']==args
 assert h['ReadonlyRootfs'] and h['NetworkMode']=='none' and h['PidMode']=='container:'+target['container_id'] and set(h['CapDrop'])=={'ALL'} and set(h['CapAdd'] or [])==(set() if report else {'CAP_PERFMON','CAP_SETUID','CAP_SETGID','CAP_SETPCAP'}) and not h['Privileged'] and h['SecurityOpt']==['no-new-privileges:true'] and h['LogConfig']['Type']=='none' and h['Memory']==128*1024*1024 and not any(x.get('RW') for x in c['Mounts'])
 return {'id':cid,'name':probe_name,'created':c['Created'],'args':args,'entry':entry,'target':target,'key':key,'report':report}
for target in targets:profile_jobs.append(make_probe(target))
def stop_own(job,reason):
 c=json.loads(subprocess.check_output(['docker','inspect',job['id']],text=True))[0]
 assert c['Id']==job['id'] and c['Name']=='/'+job['name'] and c['Image']==profile_image and c['Created']==job['created'] and c['Config']['Cmd']==job['args'] and c['Config']['Entrypoint']==[job['entry']]
 if c['State']['Status']=='running':
  (d/(job['key']+'-stop-audit.json')).write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'action':'Stop only exact owned diagnostic probe','id':job['id'],'created':job['created'],'pid':c['State']['Pid'],'started':c['State']['StartedAt'],'reason':reason},indent=2)+'\n');subprocess.run(['docker','stop','-t','2',job['id']],check=True,timeout=10)
def begin_profiles(active_start,capacity_process):
 while time.monotonic_ns()<active_start+4_000_000_000 and capacity_process.poll() is None:time.sleep(.05)
 assert capacity_process.poll() is None
 for job in profile_jobs:
  target=job['target'];c=json.loads(subprocess.check_output(['docker','inspect',target['container_id']],text=True))[0];assert discover(c,target['exe'],target['binary_sha256'])=={k:v for k,v in target.items() if k!='label'}
  output=(private_dir/(job['key']+'.perf.pipe')).open('wb');errors=(private_dir/(job['key']+'.stderr')).open('wb');job['output']=output;job['errors']=errors;job['started_monotonic_ns']=time.monotonic_ns();job['process']=subprocess.Popen(['docker','start','-a',job['id']],stdout=output,stderr=errors)
def end_profiles():
 for job in profile_jobs:
  process=job.get('process')
  if process is None:continue
  try:process.wait(timeout=max(.1,90-(time.monotonic_ns()-job['started_monotonic_ns'])/1e9))
  except subprocess.TimeoutExpired:stop_own(job,'90s owned recorder bound');process.wait(timeout=10);raise
  finally:job['output'].close();job['errors'].close()
  c=json.loads(subprocess.check_output(['docker','inspect',job['id']],text=True))[0];assert c['State']['Status']=='exited' and c['State']['ExitCode']==0
  rawpath=private_dir/(job['key']+'.perf.pipe');err=private_dir/(job['key']+'.stderr');proof='PROFILER_FINAL uid=1000 gid=1000 effective=0000004000000000 permitted=0000004000000000 inheritable=0000004000000000 bounding=0000004000000000 ambient=0000004000000000 nnp=1 seccomp=2'
  assert err.read_text().count(proof)==1 and rawpath.stat().st_size>1000 and rawpath.read_bytes()[:8]==b'PERFILE2'
  profile_results.append({'target':job['target'],'recorder_id':job['id'],'started_utc':c['State']['StartedAt'],'finished_utc':c['State']['FinishedAt'],'raw_bytes':rawpath.stat().st_size,'raw_sha256':hashlib.sha256(rawpath.read_bytes()).hexdigest(),'stderr_sha256':hashlib.sha256(err.read_bytes()).hexdigest(),'final_capability_proof':proof,'report_complete':False})
def reports_after_load():
 for target in targets:
  job=make_probe(target,True);inp=(private_dir/(target['label']+'-record.perf.pipe')).open('rb');report=d/(target['label']+'-cpu-symbols.txt');out=report.open('wb');err=(private_dir/(target['label']+'-report.stderr')).open('wb')
  try:
   p=subprocess.Popen(['docker','start','-ai',job['id']],stdin=inp,stdout=out,stderr=err)
   try:p.wait(timeout=90)
   except subprocess.TimeoutExpired:stop_own(job,'90s ownreport bound');p.wait(timeout=10);raise
  finally:inp.close();out.close();err.close()
  c=json.loads(subprocess.check_output(['docker','inspect',job['id']],text=True))[0];assert c['State']['Status']=='exited' and c['State']['ExitCode']==0 and report.stat().st_size>200
  result=next(x for x in profile_results if x['target']['label']==target['label']);result.update(report_complete=True,report_id=job['id'],report_sha256=hashlib.sha256(report.read_bytes()).hexdigest())
 assert len(profile_results)==3 and all(x['report_complete'] for x in profile_results)
 assert tool_hashes=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p,_ in mounts} and settings=={k:pathlib.Path('/proc/sys/kernel/'+k).read_text().strip() for k in settings}
 (d/'cpu-profile-summary.json').write_text(json.dumps({'status':'PRIVATE_REAL_CPU_PROFILE_COMPLETE','targets':profile_results,'flat_user_CPU_only':True,'callstack_or_memory_collected':False,'actual_product_DSO_via_target_symfs':True,'performance_acceptance':False},indent=2)+'\n')

cgroups={}
for c in containers:
 cg=next(line.split(':',2)[2] for line in pathlib.Path('/proc/'+str(c['State']['Pid'])+'/cgroup').read_text().splitlines() if line.startswith('0::'));p=pathlib.Path('/sys/fs/cgroup')/cg.lstrip('/')/'cpu.stat'
 if p.is_file():cgroups[c['Name']]=p
digest='SELECT DIGEST,DIGEST_TEXT,COUNT_STAR,SUM_TIMER_WAIT,SUM_LOCK_TIME,SUM_ROWS_EXAMINED FROM performance_schema.events_statements_summary_by_digest WHERE SCHEMA_NAME=DATABASE()'
def snapshot(label):
 started=time.monotonic_ns();cpu={key:{k:int(v) for k,v in (line.split() for line in path.read_text().splitlines())} for key,path in cgroups.items()}
 (d/(label+'-cgroup-cpu.json')).write_text(json.dumps({'monotonic_ns':started,'cpu':cpu},indent=2)+'\n');(d/(label+'-digest.tsv')).write_text(sql(digest))
 for key,path in [('stat','/proc/stat'),('cpu-pressure','/proc/pressure/cpu'),('io-pressure','/proc/pressure/io'),('memory-pressure','/proc/pressure/memory')]: (d/(label+'-'+key+'.txt')).write_text(pathlib.Path(path).read_text())
 ended=time.monotonic_ns();(d/(label+'-snapshot.json')).write_text(json.dumps({'started_monotonic_ns':started,'ended_monotonic_ns':ended,'capture_elapsed_ms':(ended-started)/1e6},indent=2)+'\n')
def observe(process):
 try:
  end=time.monotonic()+350
  while not (raw/'control/start_ns').exists() and process.poll() is None and time.monotonic()<end:time.sleep(.5)
  assert (raw/'control/start_ns').exists(),'No activewindow, preserve partialrun'
  start=int((raw/'control/start_ns').read_text());(d/'observed-active-start-ns.txt').write_text(str(start)+'\n')
  for label,offset in [('before',100_000_000),('after',58_500_000_000)]:
   while time.monotonic_ns()<start+offset and process.poll() is None:time.sleep(.05)
   assert process.poll() is None,'Control ended before counterwindow';snapshot(label)
   if label=='before':begin_profiles(start,process)
  end_profiles()
 except BaseException as error:
  for job in profile_jobs:stop_own(job,'Observer failure, stoponlyownsampler')
  (d/'observer-error.json').write_text(json.dumps({'status':'FAIL','type':type(error).__name__,'message':str(error)})+'\n')
def interrupted(signum,frame):raise KeyboardInterrupt('Own same-ELF batchcapacity control interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
args=['/usr/bin/python3',str(r/'benchmark/local_capacity/capacity_run.py'),'--user-id-base','700000','--username-prefix','codex50k_20261004_','--gateway-image',gateway_image,'--message-image',expected_image,'--host','192.168.220.128','--source-ips','192.168.220.128,192.168.220.129,192.168.58.129,172.18.0.1,172.17.0.1','--run',name,'--users','10000','--rate','150','--duration','60']
p=None;observer=None;start=None;code=None;error=None
try:
 with (d/'capacity.log').open('w') as out:
  p=subprocess.Popen(args,stdout=out,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path(f'/proc/{p.pid}');start=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
  (d/'own-process.json').write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'starttime_ticks':start,'argv':args})+'\n');observer=threading.Thread(target=observe,args=(p,));observer.start();code=p.wait(timeout=600)
except BaseException as caught:error=caught
finally:
 if p is not None and p.poll() is None:
  assert start is not None and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==start and os.getpgid(p.pid)==p.pid
  assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
  (d/'own-stop-audit.json').write_text(json.dumps({'operation':'Stoponlyverifiedowncapacityprocessgroup','pid':p.pid,'identity_starttime_cmdline_pgid_verified':True})+'\n');os.killpg(p.pid,signal.SIGINT)
  try:p.wait(timeout=10)
  except subprocess.TimeoutExpired:
   os.killpg(p.pid,signal.SIGTERM)
   try:p.wait(timeout=3)
   except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 if observer is not None:
  observer.join(timeout=25);assert not observer.is_alive(),'Own bounded observer did not finish'
 after,_=identity();assert after==before;assert definitions()==indexes and sql(durability).strip()=='1\t1\t1\t0\t0'
 (d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');(d/'guest-memory-after.txt').write_text(pathlib.Path('/proc/meminfo').read_text())
if error is not None:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(error).__name__,'message':str(error),'run':name,'exit_code':code})+'\n');raise error
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert not (d/'observer-error.json').exists(),'Profile observer failed; preserve allraw'
reports_after_load();final_identity,_=identity();assert final_identity==before
private=json.loads((raw/'summary.json').read_text());metrics=private.get('metrics',{});hb_equal=metrics.get('heartbeat_sent')==metrics.get('heartbeat_ack') and metrics.get('heartbeat_sent',0)>0
x={'status':'PRIVATE_CPU_LOAD_PROFILE_COMPLETED','profile_results':profile_results,'instrumented_latency_diagnostic_only':True,'run':name,'phase':phase,'private_exit':code,'private':private,'heartbeat_exact_equality':hb_equal,'observer_error':(d/'observer-error.json').exists(),'all19_runtime_configs_preserved':True,'full_feature_acceptance':False,'source_head':source['head'],'message_image':expected_image,'message_binary_sha256':expected_binary}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n')
print(json.dumps({'status':x['status'],'run':name,'phase':phase,'private_exit':code,'heartbeat_exact_equality':hb_equal,'private':{key:value for key,value in private.items() if key!='workers'}},indent=2))
PY
