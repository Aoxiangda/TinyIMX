#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export CODEX_LOGGING_PHASE="$1" CODEX_LOGGING_RUN="$2"
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time,hashlib,threading,re,sys,os,signal
r=pathlib.Path.cwd();b=r/'.local/codex';phase=os.environ['CODEX_LOGGING_PHASE'];name=os.environ['CODEX_LOGGING_RUN']
assert (phase,name)==('original','logging150O1')
d=b/('logging-original-endpoint-control-'+name);raw=b/('capacity-'+name);assert not d.exists() and not raw.exists()
source=json.loads((b/'lazy-logging-failure-review-source-20261005/summary.json').read_text());assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==source['head']
original=json.loads((b/'lazy-logging-failure-review-source-20261005/runtime-after.json').read_text())
assert hashlib.sha256((r/'build/linux-release/tinyimx_capacity_worker').read_bytes()).hexdigest()=='5d6bd183ef7119783497f60cda99566ad3d7f7bfcb96192fbdc4b46b8c1f40d3'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
sys.path.insert(0,str(r/'benchmark/local_capacity'))
from apply_pending_recipient_index import definitions,EXPECTED,NAME
indexes={**EXPECTED,NAME:[('delivery_status','1','YES'),('to_user_id','1','YES')]};assert definitions()==indexes
def sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-readonly-guard-control',q],text=True,timeout=20)
durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'
assert sql(durability).strip()=='1\t1\t1\t0\t0'
entry={'image_id':'sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060','elf_sha256':'2548733766409d282d30f6ffbbbe47f9c12b5d4fa3cacfdcc3d3e47f72582699','binary_build_revision':'d37dcfe03c849d55582d6da577ff1b94617638c0'}
gateway={'image_id':'sha256:a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9','elf_sha256':'c2894a21cf308ad35ef665c603d17b1a2577c9216aa9e74856772befa99d640f','binary_build_revision':'38f41a9663603d959331340926e26ca6c29d08ac'}
image={'status':'PRIVATE_BATCH_FULL_MESSAGE_SEALED_IMAGE_PASS','image_id':entry['image_id'],'binary_sha256':entry['elf_sha256'],'head':entry['binary_build_revision']}
expected_image=image['image_id'];expected_binary=image['binary_sha256'];gateway_image=gateway['image_id']

def identity():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 current={'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
 assert current['config_sha256']==original['config_sha256'];assert all(name in ['/tinyimx-m21-message-service-1','/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1'] or info==original['containers'][name] for name,info in current['containers'].items())
 target=next(c for c in cs if c['Name']=='/tinyimx-m21-message-service-1');assert target['Image']==expected_image
 assert target['State'].get('Health',{}).get('Status')=='healthy'
 env=dict(pair.split('=',1) for pair in target['Config']['Env']);assert env.get('TINYIMX_PERSIST_PHASE_TRACE_ENABLE')=='1' and 'TINYIMX_STORAGE_WAIT_TRACE_ENABLE' not in env and 'TINYIMX_MYSQL_POOL_TRACE' not in env
 assert env.get('TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE')=='1'
 for gw in cs:
  if gw['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:continue
  assert gw['Image']==gateway_image and dict(x.split('=',1) for x in gw['Config']['Env']).get('TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE')=='1'
  assert subprocess.check_output(['docker','exec',gw['Id'],'sha256sum','/opt/tinyimx/bin/gateway_demo'],text=True).split()[0]==gateway['elf_sha256']
 assert subprocess.check_output(['docker','exec',target['Id'],'sha256sum','/opt/tinyimx/bin/message_service_demo'],text=True).split()[0]==expected_binary
 assert json.loads((cfg/'message.json').read_text())['mysql']['pool_size']==16
 return current,cs
before,containers=identity();d.mkdir()
baseline_env=json.loads((b/'lazy-logging-deployment-eager-initial-20261005/runtime-private/original-containers-inspect.json').read_text())
for c in containers:
 role={'/tinyimx-m21-gateway-a-1':'gateway-a','/tinyimx-m21-gateway-b-1':'gateway-b','/tinyimx-m21-message-service-1':'message-service'}.get(c['Name'])
 if role:assert dict(x.split('=',1) for x in c['Config']['Env'])==dict(x.split('=',1) for x in baseline_env[role]['Config']['Env'])

(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Original sealed runtime fixed10k/150persecond/60second private control after both rebuilt variants login abort','phase':phase,'run':name,'head':source['head'],'compiled_message':image['head'],'same_ELF':False,'batch_flag':'1','logging_variant':phase,'compiled_gateway':gateway['binary_build_revision'],'message_image':expected_image,'message_binary_sha256':expected_binary,'worker_compiled':'2bb6b32','worker_sha256':'5d6bd183ef7119783497f60cda99566ad3d7f7bfcb96192fbdc4b46b8c1f40d3','runtime_before':before,'users':10000,'rate':150,'duration':60,'plan':9000,'ramp_users_per_second':100,'original_deadlines_unchanged':True,'other_apps':'Preserved','writes':'Normal owned benchmark700000base messages/ACK and own logs only, no configs/settings/reset/cleanup','observer':'2readonly SQLdigest+cgroup/guest/public taskID snapshots in activewindow, duration/overhead retained, no product instrumentation changes; two taskID sets only, not an exact thread creation counter','gates':'Allauth/all9000attempts+positive+wire+SQLconfirmed/no skip/negative/late/disconnect/HBdrain andbothP99<=100; retain every FAIL; no all-feature acceptance','limits':'Sequential sharedhost, private-only150 workload. Original versus rebuilt images differ beyond macro; one control cannot prove logging gain or isolate all build inputs.  No source build duringwindow; no production CPU attribution from syscall wait shares','timeout':'Own capacityprocessgroup600s guarded identity; signals finally stop onlyown group','rollback':'Ownclientsdrain; Exact retained3service rollback helper available; preserve every DBrow and raw result'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');(d/'guest-memory-before.txt').write_text(pathlib.Path('/proc/meminfo').read_text())
cgroups={}
for c in containers:
 cg=next(line.split(':',2)[2] for line in pathlib.Path('/proc/'+str(c['State']['Pid'])+'/cgroup').read_text().splitlines() if line.startswith('0::'));p=pathlib.Path('/sys/fs/cgroup')/cg.lstrip('/')/'cpu.stat'
 if p.is_file():cgroups[c['Name']]=p
digest='SELECT DIGEST,DIGEST_TEXT,COUNT_STAR,SUM_TIMER_WAIT,SUM_LOCK_TIME,SUM_ROWS_EXAMINED FROM performance_schema.events_statements_summary_by_digest WHERE SCHEMA_NAME=DATABASE()'
business_pids={}
for c in containers:
 if c['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1','/tinyimx-m21-message-service-1']:continue
 lines=subprocess.check_output(['docker','top',c['Id'],'-eo','pid,ppid,comm'],text=True,timeout=15).splitlines()[1:]
 children=[int(x.split()[0]) for x in lines if len(x.split())==3 and int(x.split()[1])==c['State']['Pid']]
 if len(children)==1:business_pids[c['Name']]=children[0]
def thread_snapshot(label):
 result={}
 for name,pid in business_pids.items():
  try:
   p=pathlib.Path('/proc')/str(pid);tids=sorted(int(x.name) for x in (p/'task').iterdir());result[name]={'status':'PUBLIC_TASK_IDS_OBSERVED','pid':pid,'tids':tids,'count':len(tids),'no_memory_stack_or_env':True}
  except (OSError,ValueError) as error:result[name]={'status':'UNAVAILABLE','type':type(error).__name__,'no_memory_stack_or_env':True}
 (d/(label+'-public-task-ids.json')).write_text(json.dumps(result,indent=2)+'\n')
def snapshot(label):
 started=time.monotonic_ns();cpu={key:{k:int(v) for k,v in (line.split() for line in path.read_text().splitlines())} for key,path in cgroups.items()}
 (d/(label+'-cgroup-cpu.json')).write_text(json.dumps({'monotonic_ns':started,'cpu':cpu},indent=2)+'\n');(d/(label+'-digest.tsv')).write_text(sql(digest))
 thread_snapshot(label)
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
 except BaseException as error:(d/'observer-error.json').write_text(json.dumps({'status':'FAIL','type':type(error).__name__,'message':str(error)})+'\n')
def interrupted(signum,frame):raise KeyboardInterrupt('Own paired logging capacity control interrupted')
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
private=json.loads((raw/'summary.json').read_text());metrics=private.get('metrics',{});hb_equal=metrics.get('heartbeat_sent')==metrics.get('heartbeat_ack') and metrics.get('heartbeat_sent',0)>0
x={'status':'ORIGINAL_LOGGING_RUNTIME_CONTROL_COMPLETED','run':name,'phase':phase,'private_exit':code,'private':private,'heartbeat_exact_equality':hb_equal,'observer_error':(d/'observer-error.json').exists(),'all19_runtime_configs_preserved':True,'full_feature_acceptance':False,'source_head':source['head'],'message_image':expected_image,'message_binary_sha256':expected_binary}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n')
print(json.dumps({'status':x['status'],'run':name,'phase':phase,'private_exit':code,'heartbeat_exact_equality':hb_equal,'private':{key:value for key,value in private.items() if key!='workers'}},indent=2))
PY
