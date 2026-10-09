#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,threading,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'auth-phase-diagnostic-login-control-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
deployment=json.loads((b/'auth-phase-diagnostics-user-deployment-20261005/summary.json').read_text());assert deployment['status']=='AUTH_DIAGNOSTIC_USER_READY'
def run(a,timeout=25):return subprocess.check_output(a,text=True,timeout=timeout)
def identity():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));user=next(c for c in cs if c['Name']=='/tinyimx-m21-user-service-1');assert user['Id']==deployment['user_service_id'] and user['Image']==deployment['image'] and user['State'].get('Health',{}).get('Status')=='healthy'
 env=dict(x.split('=',1) for x in user['Config']['Env'] if '=' in x);assert env.get('TINYIMX_AUTH_PHASE_TRACE_ENABLE')=='1' and all(not k.startswith('TINYIMX_FAULT_') for k in env)
 for c in cs:
  if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:assert c['Image']=='sha256:1d8d71faa91a5a1e0a261c2a89efa1027a93515ae2b94b2c2d06f48010a7486e'
  if c['Name']=='/tinyimx-m21-message-service-1':assert c['Image']=='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898'
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');assert {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}==deployment['private_config_sha256']
 return cs,{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
cs,before=identity();assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def settings():return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-auth-settings','SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count']).strip()
assert settings()=='1\t1\t1\t0\t0'
d.mkdir();run_id='auth10kdiag';raw=b/('capacity-'+run_id);assert not raw.exists();since=datetime.datetime.now(datetime.timezone.utc).isoformat()
(d/'audit-before.json').write_text(json.dumps({'utc':since,'operation':'One diagnostic10klogin-only hold30s with original100users/s ramp and3s deadline; numeric lookup/password/handler wallCPU evidence','head':run(['git','rev-parse','HEAD']).strip(),'before_containers':before,'user_diagnostic':deployment,'writes':'Normal ownedfixture login/session/presence/last_login metadata and own bounded evidence, no privatechat/group/file business traffic','strict_gates':'Originalauthidentity/deadline/ramp, all10konline, completedhold/HB/disconnect gates; no missingplan omission ordeadline extension','observer':'User/Gateway cgroupCPU/stat/pressure andguestCPU/io/memory every5s; captureelapsed retained, no schedstats runqueue claim','sql_settings':'Unchanged1/1/1/0/0; noreset/instrument/globalparameter change','other_apps':'Preserved; ownWindowsCPU observer synchronized','rollback':'Ownedclients drain; separately restore exact originalUser38dca/env; preserve allFAILevidence'},indent=2)+'\n')
paths={}
for c in cs:
 if c['Name'] not in ['/tinyimx-m21-user-service-1','/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:continue
 pid=c['State']['Pid'];cg=next(line.split(':',2)[2] for line in pathlib.Path(f'/proc/{pid}/cgroup').read_text().splitlines() if line.startswith('0::'));paths[c['Name']]=(pid,pathlib.Path('/sys/fs/cgroup')/cg.lstrip('/'))
stop=threading.Event()
def observe():
 try:
  with (d/'numeric-resources.jsonl').open('w') as f:
   while not stop.is_set():
    started=time.monotonic_ns();x={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'monotonic_ns':started,'guest':{name:pathlib.Path(path).read_text() for name,path in [('stat','/proc/stat'),('cpu_pressure','/proc/pressure/cpu'),('io_pressure','/proc/pressure/io'),('memory_pressure','/proc/pressure/memory')]},'containers':{}}
    for name,(pid,p) in paths.items():
     x['containers'][name]={'pid':pid,'cpu_stat':(p/'cpu.stat').read_text(),'cpu_pressure':(p/'cpu.pressure').read_text(),'threads':len(list(pathlib.Path(f'/proc/{pid}/task').iterdir()))}
    x['mem_available_kib']=int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1));x['capture_elapsed_ms']=(time.monotonic_ns()-started)/1e6;f.write(json.dumps(x)+'\n');f.flush();stop.wait(5)
 except BaseException as e:(d/'observer-error.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__})+'\n')
args=['python3','benchmark/local_capacity/capacity_run.py','--run',run_id,'--users','10000','--rate','100','--duration','30','--mode','hold','--user-id-base','700000','--username-prefix','codex50k_20261004_','--gateway-image','sha256:1d8d71faa91a5a1e0a261c2a89efa1027a93515ae2b94b2c2d06f48010a7486e','--message-image','sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898','--host','192.168.220.128','--source-ips','192.168.220.128,192.168.220.129,192.168.58.129,172.18.0.1,172.17.0.1']
o=threading.Thread(target=observe);o.start()
try:
 with (d/'capacity.log').open('w') as f:p=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=650)
 code=p.returncode
finally:stop.set();o.join(timeout=15)
until=datetime.datetime.now(datetime.timezone.utc).isoformat();logs=subprocess.run(['docker','logs','--since',since,'--until',until,deployment['user_service_id']],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=True,timeout=25).stdout
records=[]
for line in logs.splitlines():
 if 'auth_phase kind=' not in line:continue
 fields={k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line.split('auth_phase ',1)[1])};assert set(fields)=={'kind','uid','tid','started_us','total_us','cpu_us','lookup_us','lookup_cpu_us','password_us','password_cpu_us','status','outcome','threw'};records.append(fields)
del logs
(d/'auth-phase-numeric.jsonl').write_text(''.join(json.dumps(x)+'\n' for x in records));_,after=identity();assert before==after and settings()=='1\t1\t1\t0\t0'
x={'status':'AUTH_DIAGNOSTIC_CONTROL_COMPLETED','run':run_id,'mode':'hold','capacity_exit':code,'capacity':json.loads((raw/'summary.json').read_text()),'numeric_auth_records':len(records),'resource_observer_error':(d/'observer-error.json').exists(),'runtime_preserved':True,'sql_settings_changed':False,'performance_acceptance':False,'ended_utc':until};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
