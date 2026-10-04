#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time,hashlib,threading,sys,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'storage-wait-diagnostic-control-20261005';assert not d.exists()
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()=='a315d5d2edfa29daccdc9ae1c39466d164f8fd9a'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
deploy=json.loads((b/'storage-wait-diagnostics-message-deployment-20261005/summary.json').read_text());assert deploy['status']=='STORAGE_WAIT_DIAGNOSTIC_MESSAGE_READY'
sys.path.insert(0,str(r/'benchmark/local_capacity'))
from apply_pending_recipient_index import definitions,EXPECTED,NAME
indexes={**EXPECTED,NAME:[('delivery_status','1','YES'),('to_user_id','1','YES')]}
prior=json.loads((b/'stripe-fair-handoff-rejected-rollback-20261005/summary.json').read_text())
config=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json')
def identity():
 for name,cid in prior['gateway_ids'].items():
  c=json.loads(subprocess.check_output(['docker','inspect',cid],text=True))[0]
  assert c['Name']==name and c['Image']==prior['image'] and c['State']['Running']
 c=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-message-service-1'],text=True))[0]
 assert c['Id']==deploy['message_service_id'] and c['Image']==deploy['image'] and c['State']['Running']
 env=dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
 assert all(env.get(k)=='1' for k in ['TINYIMX_STORAGE_WAIT_TRACE_ENABLE','TINYIMX_MYSQL_POOL_TRACE','TINYIMX_PERSIST_PHASE_TRACE_ENABLE'])
 assert definitions()==indexes and hashlib.sha256(config.read_bytes()).hexdigest()==deploy['config_sha256'] and json.loads(config.read_text())['mysql']['pool_size']==16
 return {'gateway_ids':prior['gateway_ids'],'gateway_image':prior['image'],'message_id':c['Id'],'message_image':c['Image'],'config_sha256':deploy['config_sha256'],'pool_size':16,'indexes':indexes}
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>=2*1024*1024
before=identity();d.mkdir();run='sw150a';raw=b/('capacity-'+run);assert not raw.exists()
names=['tinyimx-m21-'+n+'-1' for n in ['message-service','gateway-a','gateway-b','mysql','redis','user-service','social-service']]
cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));pids={c['Name'].lstrip('/'):c['State']['Pid'] for c in cs}
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'One fixed10k/150messages/s60s diagnostic window with numeric handler/repository/pool CPU+wait correlation','head':'a315d5d2edfa29daccdc9ae1c39466d164f8fd9a','runtime_before':before,'writes':'Normal owned private messages/ACK and own evidence only; original strict100ms gates retained, diagnostics do not constitute capacity acceptance','observer':'Read-only numeric /proc task schedstat/status/stat and allowlisted cgroupcpu/stat pressure at5s intervals, no perf permissions or sysctl changes; record denied reads explicitly','pid_allowlist':pids,'no_changes':['Other applications','Memory/cache/swap','Configuration/pool/index','Deadlines','Durability','Gateway image/threads'],'rollback':'Ownclientsdrain; diagnostic image later restored after exact logs collected; all results retained'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
def scheduler_snapshot():
 out={'monotonic_ns':time.monotonic_ns(),'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'schedstats_enabled':pathlib.Path('/proc/sys/kernel/sched_schedstats').read_text().strip(),'processes':{}}
 for name,pid in pids.items():
  proc=pathlib.Path('/proc')/str(pid);row={'host_pid':pid,'threads':[]}
  try:
   for t in sorted((proc/'task').iterdir(),key=lambda x:int(x.name)):
    try:
     status=t.joinpath('status').read_text();sched=list(map(int,t.joinpath('schedstat').read_text().split()));stat=t.joinpath('stat').read_text().rsplit(')',1)[1].split()
     def field(key):
      m=re.search(r'^'+key+r':\s+([\d\s]+)$',status,re.M);return list(map(int,m.group(1).split())) if m else []
     row['threads'].append({'host_tid':int(t.name),'ns_tid':field('NSpid')[-1] if field('NSpid') else 0,'state':stat[0],'runtime_ns':sched[0],'runqueue_wait_ns':sched[1],'slices':sched[2],'user_ticks':int(stat[11]),'system_ticks':int(stat[12]),'voluntary_context_switches':field('voluntary_ctxt_switches'),'involuntary_context_switches':field('nonvoluntary_ctxt_switches')})
    except (OSError,ValueError,IndexError) as e:row['threads'].append({'host_tid':int(t.name),'error':type(e).__name__})
   group=proc.joinpath('cgroup').read_text().splitlines();unified=[x.split(':',2)[2] for x in group if x.startswith('0::')]
   if unified:
    path=(pathlib.Path('/sys/fs/cgroup')/unified[0].lstrip('/')).resolve();assert path.is_relative_to(pathlib.Path('/sys/fs/cgroup'))
    row['cpu_stat']={k:int(v) for k,v in [x.split() for x in (path/'cpu.stat').read_text().splitlines()]}
    row['cpu_max']=(path/'cpu.max').read_text().strip()
    row['cpu_pressure']=(path/'cpu.pressure').read_text().strip()
  except (OSError,ValueError) as e:row['error']=type(e).__name__
  out['processes'][name]=row
 return out
def observe(p):
 try:
  deadline=time.monotonic()+350
  while not (raw/'control/start_ns').exists() and p.poll() is None and time.monotonic()<deadline:time.sleep(.5)
  if not (raw/'control/start_ns').exists():return
  start=int((raw/'control/start_ns').read_text())
  while time.monotonic_ns()<start and p.poll() is None:time.sleep(.1)
  with (d/'scheduler-numeric.jsonl').open('w') as f:
   while p.poll() is None and time.monotonic_ns()<start+60_000_000_000:
    tick=time.monotonic();s=scheduler_snapshot();s['snapshot_elapsed_ms']=(time.monotonic()-tick)*1000;f.write(json.dumps(s)+'\n');f.flush();time.sleep(max(.1,5-(time.monotonic()-tick)))
 except BaseException as e:(d/'observer-error.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__})+'\n')
args=['python3','benchmark/local_capacity/capacity_run.py','--user-id-base','700000','--username-prefix','codex50k_20261004_','--gateway-image',prior['image'],'--message-image',deploy['image'],'--host','192.168.220.128','--source-ips','192.168.220.128,192.168.220.129,192.168.58.129,172.18.0.1,172.17.0.1','--run',run,'--users','10000','--rate','150','--duration','60']
with (d/'capacity.log').open('w') as f:
 p=subprocess.Popen(args,stdout=f,stderr=subprocess.STDOUT);observer=threading.Thread(target=observe,args=(p,));observer.start();code=p.wait();observer.join()
after=identity();assert before==after
(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
x={'status':'DIAGNOSTIC_CONTROL_COMPLETED','run':run,'private_exit':code,'private':json.loads((raw/'summary.json').read_text()),'observer_error':(d/'observer-error.json').exists(),'performance_acceptance':False,'all_feature_acceptance':False}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
