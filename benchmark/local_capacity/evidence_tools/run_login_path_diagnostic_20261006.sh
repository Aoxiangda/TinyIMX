#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,socket,re,os,signal,threading,collections
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'login-path-diagnostic-run-20261006';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
def inspect():return json.loads(run(['docker','inspect',*names]))
head=run(['git','rev-parse','HEAD']).strip();assert head=='8d6eb0a4618f4b68af5109f194b78c0ea4fd7179'
build=json.loads((b/'login-path-diagnostic-build-20261006/summary.json').read_text());assert build['status']=='LOGIN_PATH_DIAGNOSTIC_BUILD_PASS' and build['head']==head and build['trace_checks']==16
userimage=json.loads((b/'auth-phase-diagnostics-image-20261005/summary.json').read_text());assert userimage['status']=='SEALED_AUTH_IMAGE_PASS' and userimage['image_id']=='sha256:1ff078812b309e36a859c2a2057e35b4783bcc02db76a8c5d7fcb120d0427cf3'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
cs=inspect();before={c['Name']:ident(c) for c in cs}
roles={'gateway-a':{'image':build['gateway_image_id'],'old':'sha256:a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9','binary':'gateway_demo','sha':build['gateway_elf_sha256'],'flag':'TINYIMX_LOGIN_PHASE_TRACE_ENABLE'},'gateway-b':{'image':build['gateway_image_id'],'old':'sha256:a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9','binary':'gateway_demo','sha':build['gateway_elf_sha256'],'flag':'TINYIMX_LOGIN_PHASE_TRACE_ENABLE'},'user-service':{'image':userimage['image_id'],'old':userimage['original_user_image'],'binary':'user_service_demo','sha':userimage['binary_sha256'],'flag':'TINYIMX_AUTH_PHASE_TRACE_ENABLE'}}
original={role:next(c for c in cs if c['Name']=='/tinyimx-m21-'+role+'-1') for role in roles}
assert all(c['Image']==roles[role]['old'] and c['State'].get('Health',{}).get('Status')=='healthy' for role,c in original.items())
assert next(c for c in cs if c['Name']=='/tinyimx-m21-message-service-1')['Image']=='sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
ips={v['IPAddress'] for role,c in original.items() if role.startswith('gateway') for v in c['NetworkSettings']['Networks'].values()}
for role,c in original.items():
 if not role.startswith('gateway'):continue
 for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
  f=line.split()
  if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
   v=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(v)[::-1]) if len(v)==8 else 'ipv6'
   assert peer in ips or peer.startswith('127.'),'Refuse live business-client interruption'
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:sha(p) for p in cfg.glob('*.json')}
worker=pathlib.Path(build['worker_path']);assert sha(worker)==build['worker_sha256']
coordinator=r/'benchmark/local_capacity/capacity_run.py';coordinator_sha=sha(coordinator);assert coordinator_sha=='1ef1c527335a998809296d487378776715ab0fe8c73ccda90d8fa4392431d8ba'
text=coordinator.read_text();old="    binary = ROOT / 'build/linux-release/tinyimx_capacity_worker'";assert text.count(old)==1
replacement="    binary = pathlib.Path("+repr(str(worker))+")"
owntext=text.replace(old,replacement);assert owntext.replace(replacement,old)==text;compile(owntext,'own-coordinator','exec')
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def settings():return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-loginpath-settings','SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count']).strip()
assert settings()=='1\t1\t1\t0\t0'
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')]
basecfg=json.loads(run(base+['config','--format','json']))['services']
cands={};rolls={}
for role,c in original.items():
 e=envmap(c);assert roles[role]['flag'] not in e and not any(k.startswith('TINYIMX_FAULT_') for k in e)
 assert c['Config']['Cmd']==basecfg[role]['command'] and all(e.get(k)==str(v) for k,v in basecfg[role].get('environment',{}).items())
 assert json.loads(run(['docker','image','inspect',roles[role]['image']]))[0]['Id']==roles[role]['image']
 roles[role]['original_binary_sha256']=run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/'+roles[role]['binary']]).split()[0]
 cands[role]={'image':roles[role]['image'],'environment':{**e,roles[role]['flag']:'1'}}
 rolls[role]={'image':c['Image'],'environment':e}
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
(private/'original-inspect.json').write_text(json.dumps(original,indent=2)+'\n')
candidate=private/'candidate.override.json';rollback=private/'rollback.override.json'
candidate.write_text(json.dumps({'services':cands})+'\n');rollback.write_text(json.dumps({'services':rolls})+'\n')
for path,expected in [(candidate,cands),(rollback,rolls)]:
 proposed=json.loads(run(base+['-f',str(path),'config','--format','json']))['services']
 for role in roles:
  assert {k:str(v) for k,v in proposed[role].get('environment',{}).items()}==expected[role]['environment'] and proposed[role]['command']==original[role]['Config']['Cmd']
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Temporary exact three-service defaultOFF numeric diagnostics; one10000login100/s hold30, original3sdeadline; automatic original-image/environment restore after any result','before_containers':before,'private_config_sha256':hashes,'roles':roles,'coordinator_original_sha256':coordinator_sha,'coordinator_only_edit':'Own worker absolute path, original source unchanged','worker_sha256':sha(worker),'durability':'1/1/1/0/0','all_other16':'IDs/image/start preserved throughout','original_logs':'Preserved private before recreation','other_apps':'Preserved','no_cleanup_or_deadline_changes':True,'rollback_override_sha256':sha(rollback)},indent=2)+'\n')
for role,c in original.items():
 with (private/(role+'-original.log')).open('w') as f:subprocess.run(['docker','logs','--timestamps',c['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=35)
def verify(c,role,diagnostic):
 originalc=original[role];wanted=cands[role] if diagnostic else rolls[role]
 assert c['Image']==wanted['image'] and envmap(c)==wanted['environment'] and c['HostConfig']==originalc['HostConfig'] and c['Mounts']==originalc['Mounts'] and c['RestartCount']==0
 for k in ['User','WorkingDir','Cmd','Entrypoint','Healthcheck','StopSignal']:assert c['Config'].get(k)==originalc['Config'].get(k)
 expected=roles[role]['sha'] if diagnostic else roles[role]['original_binary_sha256']
 assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/'+roles[role]['binary']]).split()[0]==expected
def preserved():
 assert {p.name:sha(p) for p in cfg.glob('*.json')}==hashes and settings()=='1\t1\t1\t0\t0'
 current=inspect();targetnames={c['Name'] for c in original.values()}
 assert all(c['Name'] in targetnames or ident(c)==before[c['Name']] for c in current)
 return current
def deploy(path,label,diagnostic):
 with (private/(label+'.log')).open('w') as f:subprocess.run(base+['-f',str(path),'up','-d','--no-deps','--pull','never',*roles],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
 end=time.monotonic()+60
 while True:
  current=inspect();chosen={role:next(c for c in current if c['Name']==original[role]['Name']) for role in roles}
  if all(c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy' for c in chosen.values()):break
  assert time.monotonic()<end,'Diagnostic/restore health deadline';time.sleep(2)
 for role,c in chosen.items():verify(c,role,diagnostic)
 preserved();return chosen
stop=threading.Event();observer=None;child=None;childticks=None;code=None;error=None;restore=None
def observe(chosen):
 paths={}
 for role,c in chosen.items():
  init=c['State']['Pid'];rows=run(['docker','top',c['Id'],'-eo','pid,ppid,comm']).splitlines()[1:]
  children=[int(x.split()[0]) for x in rows if len(x.split())==3 and int(x.split()[1])==init];assert len(children)==1;pid=children[0]
  cg=next(x.split(':',2)[2] for x in pathlib.Path(f'/proc/{pid}/cgroup').read_text().splitlines() if x.startswith('0::'))
  paths[role]=(pid,pathlib.Path('/sys/fs/cgroup')/cg.lstrip('/'))
 try:
  with (d/'numeric-resources.jsonl').open('w') as f:
   while not stop.is_set():
    t=time.monotonic_ns();x={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'monotonic_ns':t,'guest':{k:pathlib.Path(v).read_text() for k,v in [('stat','/proc/stat'),('cpu_pressure','/proc/pressure/cpu'),('io_pressure','/proc/pressure/io'),('memory_pressure','/proc/pressure/memory')]},'containers':{},'owned_processes':[]}
    for role,(pid,p) in paths.items():
     x['containers'][role]={'pid':pid,'cpu_stat':(p/'cpu.stat').read_text(),'cpu_pressure':(p/'cpu.pressure').read_text(),'proc_stat':pathlib.Path(f'/proc/{pid}/stat').read_text(),'threads':len(list(pathlib.Path(f'/proc/{pid}/task').iterdir()))}
    for pidpath in pathlib.Path('/proc').iterdir():
     if not pidpath.name.isdigit():continue
     try:
      cmd=pidpath.joinpath('cmdline').read_bytes().split(b'\0');stat=pidpath.joinpath('stat').read_text()
      if cmd and (cmd[0]==str(worker).encode() or (child is not None and int(pidpath.name)==child.pid)):
       x['owned_processes'].append({'pid':int(pidpath.name),'kind':'native_worker' if cmd[0]==str(worker).encode() else 'coordinator','stat':stat})
     except (FileNotFoundError,ProcessLookupError,PermissionError):pass
    x['mem_available_kib']=int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1));x['capture_elapsed_ms']=(time.monotonic_ns()-t)/1e6
    f.write(json.dumps(x)+'\n');f.flush();stop.wait(5)
 except BaseException as e:(d/'observer-error.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__})+'\n')
def stopchild():
 if child is not None and child.poll() is None:
  proc=pathlib.Path('/proc')/str(child.pid)
  assert childticks is not None and (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==childticks and os.getpgid(child.pid)==child.pid
  assert (proc/'cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
  (d/'stop-owned-audit-before.json').write_text(json.dumps({'operation':'Stop only own verified PID/startticks/argv/PGID group','pid':child.pid,'start_ticks':childticks})+'\n')
  os.killpg(child.pid,signal.SIGINT)
  try:child.wait(timeout=15)
  except subprocess.TimeoutExpired:
   os.killpg(child.pid,signal.SIGTERM)
   try:child.wait(timeout=4)
   except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=4)
def interrupted(signum,frame):raise RuntimeError('Owned diagnostic interrupted '+str(signum))
signal.signal(signal.SIGTERM,interrupted);signal.signal(signal.SIGHUP,interrupted)
since=None;until=None;chosen=None;records=collections.Counter()
try:
 chosen=deploy(candidate,'deployment',True)
 (d/'deployment-summary.json').write_text(json.dumps({'status':'LOGIN_PATH_DIAGNOSTIC_READY','containers':{k:ident(c) for k,c in chosen.items()},'head':head,'performance_acceptance':False},indent=2)+'\n')
 print(json.dumps({'status':'LOGIN_PATH_DIAGNOSTIC_READY','all_other16_preserved':True}),flush=True)
 owncoordinator=private/'capacity_run_login_path.py';owncoordinator.write_text(owntext)
 (d/'coordinator-copy-audit.json').write_text(json.dumps({'original_sha256':coordinator_sha,'own_sha256':sha(owncoordinator),'only_edit':'binary absolute path','original_unchanged':sha(coordinator)==coordinator_sha})+'\n')
 runid='loginpath10k1';raw=b/('capacity-'+runid);assert not raw.exists()
 since=datetime.datetime.now(datetime.timezone.utc).isoformat()
 args=['python3',str(owncoordinator),'--run',runid,'--users','10000','--rate','100','--duration','30','--mode','hold','--user-id-base','700000','--username-prefix','codex50k_20261004_','--gateway-image',build['gateway_image_id'],'--message-image','sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060','--host','192.168.220.128','--source-ips','192.168.220.128,192.168.220.129,192.168.58.129,172.18.0.1,172.17.0.1']
 observer=threading.Thread(target=observe,args=(chosen,));observer.start()
 with (private/'capacity.log').open('w') as f:
  child=subprocess.Popen(args,stdout=f,stderr=subprocess.STDOUT,start_new_session=True,env={**os.environ,'TINYIMX_LOGIN_CLIENT_TRACE_ENABLE':'1'})
  childticks=pathlib.Path(f'/proc/{child.pid}/stat').read_text().rsplit(')',1)[1].split()[19]
  (d/'own-process.json').write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'start_ticks':childticks,'argv':args,'only_child_env_change':'TINYIMX_LOGIN_CLIENT_TRACE_ENABLE=1'})+'\n')
  code=child.wait(timeout=600)
 until=datetime.datetime.now(datetime.timezone.utc).isoformat()
 stop.set();observer.join(timeout=15);assert not observer.is_alive()
 for role,c in chosen.items():
  logs=subprocess.run(['docker','logs','--since',since,'--until',until,c['Id']],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=True,timeout=35).stdout
  (private/(role+'-diagnostic.log')).write_text(logs)
  kind='auth_phase' if role=='user-service' else 'login_path'
  with (d/(role+'-numeric.jsonl')).open('w') as f:
   for line in logs.splitlines():
    if kind+' ' not in line:continue
    fields={k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line.split(kind+' ',1)[1])}
    fields['service']=role;f.write(json.dumps(fields)+'\n');records[role]+=1
 preserved()
 (d/'capacity-result.json').write_text((raw/'summary.json').read_text())
except BaseException as e:
 error=e;(d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e),'capacity_exit':code},indent=2)+'\n')
finally:
 try:stopchild()
 finally:
  stop.set()
  if observer is not None:observer.join(timeout=15)
  (d/'restore-audit-before.json').write_text(json.dumps({'operation':'Restore exact three original image/env/cmd/HostConfig/mounts; other16IDs/config/durability retained','rollback_sha256':sha(rollback),'diagnostic_capacity_exit':code})+'\n')
  restored=deploy(rollback,'restore',False)
  restore={'status':'ORIGINAL_THREE_SERVICES_RESTORED','containers':{role:ident(c) for role,c in restored.items()},'other16_preserved':True,'config_env_cmd_hostconfig_mounts_and_original_binary_verified':True,'durability':'1/1/1/0/0'}
  (d/'restore-summary.json').write_text(json.dumps(restore,indent=2)+'\n');print(json.dumps(restore,indent=2),flush=True)
 if error is not None:raise error
summary={'status':'LOGIN_PATH_DIAGNOSTIC_COMPLETED','head':head,'capacity_exit':code,'capacity':json.loads((d/'capacity-result.json').read_text()),'numeric_counts':dict(records),'observer_error':(d/'observer-error.json').exists(),'restore':restore,'performance_acceptance':False,'purpose':'Same-request gap attribution; separate sampled distributions from complete success histogram and abort population'}
(d/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2),flush=True)
PY
