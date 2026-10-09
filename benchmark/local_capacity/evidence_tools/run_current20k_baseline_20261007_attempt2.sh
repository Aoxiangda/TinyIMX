#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,socket,re,os,signal,urllib.request,threading
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'current20k-baseline-20261007-attempt2';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
accepted={'image':'sha256:5c2645b1e8512bdd3fe68d4fea229a9418a5e115c9d96d636871639dba41a405','gateway_elf_sha256':'e68731562d83b3b5c1923d80ed7ad4459d2f5cbc9b8a224e32014626fac04cfc','flag':'TINYIMX_CONVERSATION_UNREAD_BATCH_ENABLE'}
previous=json.loads((b/'resume-capacity-plan-source-20261007-attempt2/audit-before.json').read_text())
accepted['gateways']={k:v for k,v in previous['runtime_before'].items() if k in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']}
source=json.loads((b/'insert-first-source-20261007/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/p)==h for p,h in source['files'].items())
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
def inspect():return json.loads(run(['docker','inspect',*names]))
cs=inspect();before={c['Name']:ident(c) for c in cs};gw=[c for c in cs if c['Name'] in accepted['gateways']]
assert len(gw)==2 and {c['Name']:ident(c) for c in gw}==accepted['gateways']
assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' and c['State']['Running'] for c in cs)
assert all(c['Image']==accepted['image'] and envmap(c).get(accepted['flag'])=='1' for c in gw)
for c in gw:assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/gateway_demo']).split()[0]==accepted['gateway_elf_sha256']
message=next(c for c in cs if c['Name']=='/tinyimx-m21-message-service-1')
assert message['Image']=='sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
available=int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1));assert available>2*1024*1024
with urllib.request.urlopen('http://127.0.0.1:11434/api/ps',timeout=4) as response:modelps=json.load(response)
assert not modelps.get('models'),'Model remains resident; preserve service and analyze resource scope'
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:sha(p) for p in cfg.glob('*.json')}
def settings():return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-20k-settings','SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count']).strip()
assert settings()=='1\t1\t1\t0\t0'
ips={v['IPAddress'] for c in gw for v in c['NetworkSettings']['Networks'].values()}
for c in gw:
 for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
  f=line.split()
  if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
   raw=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(raw)[::-1]) if len(raw)==8 else 'ipv6'
   assert peer in ips or peer.startswith('127.'),'External live clients; preserve and do not launch conflicting fixture'
coordinator=r/'benchmark/local_capacity/capacity_run_safe_abort_20261007.py';worker=r/'build/linux-release/tinyimx_capacity_worker'
actor=r/'benchmark/local_capacity/cross_feature_actor.py';listclient=r/'benchmark/local_capacity/conversation_list_openloop_20k.py'
assert sha(r/'benchmark/local_capacity/capacity_run.py')=='1ef1c527335a998809296d487378776715ab0fe8c73ccda90d8fa4392431d8ba'
assert sha(coordinator)=='42680d42a4c47c3d0e095ea66719457ad92171a7dbca74df4ff66fa9048f04e7'
assert sha(worker)=='5d6bd183ef7119783497f60cda99566ad3d7f7bfcb96192fbdc4b46b8c1f40d3'
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
runid='cur20kR';case='mixed20k-ON';actorid='curfeat20kR';bg=b/('capacity-'+runid);assert not bg.exists()
folder=d/case;folder.mkdir(mode=0o700)
pairs=json.loads(run(['bash',str(r/'benchmark/local_capacity/evidence_tools/select_online_maintenance_actor_pairs_readonly.sh')]))['users'];assert len(pairs)==4 and 519950 not in pairs
args=['python3',str(coordinator),'--run',runid,'--users','20000','--rate','135','--duration','60','--mode','private','--user-id-base','700000','--username-prefix','codex50k_20261004_','--gateway-image',accepted['image'],'--message-image',message['Image'],'--host','192.168.220.128','--source-ips','192.168.220.128,192.168.220.129,192.168.58.129,172.18.0.1,172.17.0.1']
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Current accepted Gateway5c/Messagebe8 baseline; original20k135/s contract, readonly2sec resources and existing sameMIDlogs; no deployment/config/observer-method changes','runtime_before':before,'config_sha256':hashes,'coordinator_sha256':sha(coordinator),'worker_sha256':sha(worker),'listclient_sha256':sha(listclient),'actor_sha256':sha(actor),'args':args,'fresh_cross_users':pairs,'mem_available_kib':available,'ollama_ps':modelps,'planned_private':8100,'planned_list':1200,'list_rate':20,'login_total_ramp_per_sec':100,'data_changes':'Original coordinator audits onlymissing own700001..720000 ringfriendedges beforeinsert and retains; normalprivate writes/ACKs, fourfreshactors49ops, fixed519950listreadonly. No reset/deletion/pruning','unchanged':'All19instances/images/configs, nativeworker/coordinator/deadlines, durability1/1/1/0/0, hostapps, modelservice','rollback':'Only audited ownprocessgroups/sockets abort; no productrestart/data delete. Preserve allpartial evidence','limits':'SingleON point notABBA/all50k/extreme acceptance'})

cgroup_paths={}
for c in cs:
 text=pathlib.Path('/proc/'+str(c['State']['Pid'])+'/cgroup').read_text()
 path=next(x.split(':',2)[2] for x in text.splitlines() if x.startswith('0::'))
 folder=pathlib.Path('/sys/fs/cgroup')/path.lstrip('/');assert (folder/'cpu.stat').is_file()
 cgroup_paths[c['Name']]=folder
sampler_stop=threading.Event();sample_errors=[]
def sample_resources():
 try:
  with (d/'resource-samples.jsonl').open('w') as f:
   while not sampler_stop.is_set():
    now=time.monotonic_ns();memory=pathlib.Path('/proc/meminfo').read_text()
    x={'mono_ns':now,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'mem_available_kib':int(re.search(r'MemAvailable:\s+(\d+)',memory).group(1)),'loadavg':pathlib.Path('/proc/loadavg').read_text().strip(),'cpu_stat':pathlib.Path('/proc/stat').read_text().splitlines()[0],'vmstat':{k:int(v) for k,v in (z.split() for z in pathlib.Path('/proc/vmstat').read_text().splitlines()) if k in ['pswpin','pswpout','pgmajfault']},'pressure':{k:pathlib.Path('/proc/pressure/'+k).read_text() for k in ['cpu','memory','io']},'cgroups':{}}
    for name,folder in cgroup_paths.items():
     x['cgroups'][name]={'cpu':{k:int(v) for k,v in (z.split() for z in (folder/'cpu.stat').read_text().splitlines())},'pids_current':int((folder/'pids.current').read_text()),'memory_current':int((folder/'memory.current').read_text())}
    f.write(json.dumps(x)+chr(10));f.flush();sampler_stop.wait(2)
 except BaseException as exc:sample_errors.append({'type':type(exc).__name__,'message':str(exc)})
sampler=threading.Thread(target=sample_resources,name='own-readonly-resources',daemon=True)

owned=[];error=None;result=None
def launch(argv,path):
 f=path.open('w');p=subprocess.Popen(argv,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
 ticks=pathlib.Path(f'/proc/{p.pid}/stat').read_text().rsplit(')',1)[1].split()[19]
 owned.append({'p':p,'f':f,'ticks':ticks,'argv':argv})
 (path.parent/(path.name+'.process.json')).write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'start_ticks':ticks,'argv':argv})+'\n');return p
def stop_owned():
 for item in owned:
  p=item['p']
  if p.poll() is None:
   proc=pathlib.Path(f'/proc/{p.pid}');assert proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==item['ticks'] and os.getpgid(p.pid)==p.pid and proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(item['argv'])]==[x.encode() for x in item['argv']]
   save('stop-'+str(p.pid)+'-audit-before.json',{'operation':'Only ownfailedmixedchildgroup PID/start/argv/pgid verified','pid':p.pid,'start_ticks':item['ticks']})
   os.killpg(p.pid,signal.SIGINT)
   try:p.wait(timeout=15)
   except subprocess.TimeoutExpired:
    os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=4)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=4)
  item['f'].close()
def preserved():
 assert {c['Name']:ident(c) for c in inspect()}==before and {p.name:sha(p) for p in cfg.glob('*.json')}==hashes and settings()=='1\t1\t1\t0\t0'
 assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in inspect())
def interrupt(sig,frame):raise RuntimeError('Own20k run interrupted '+str(sig))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupt)
try:
 sampler.start()
 cap=launch(args,private/'capacity.log');deadline=time.monotonic()+560;last=0
 while not (bg/'control/start_ns').exists():
  assert cap.poll() is None,'Original20kramp failed beforebarrier; analyze firstfailedstage'
  assert time.monotonic()<deadline,'Original20kramp barrier timeout'
  if time.monotonic()-last>30:
   last=time.monotonic();progress=[]
   for w in sorted(bg.glob('worker-*')):
    p=w/'live.json'
    if p.exists():progress.append({k:v for k,v in json.loads(p.read_text()).items() if k in ['worker_id','online_now','status','metrics']})
   print(json.dumps({'status':'20K_LOGIN_RAMP','elapsed_guard_seconds':560-(deadline-time.monotonic()),'worker_progress':progress}),flush=True)
  time.sleep(.2)
 start=int((bg/'control/start_ns').read_text());end=start+60_000_000_000;assert time.monotonic_ns()<start
 ls=launch(['python3',str(listclient),case,str(bg),str(folder/'list-openloop')],private/'list.log')
 while time.monotonic_ns()<start+5_000_000_000:
  assert cap.poll() is None and ls.poll() is None,'Companion/ring earlyfailure';time.sleep(.05)
 cross=launch(['python3',str(actor),'--run',actorid,'--users',*map(str,pairs),'--host','192.168.220.128','--background-run',runid],private/'feature.log')
 assert cross.wait(timeout=50)==0,'Crossfeature failed; analyze beforenewramp'
 cf=json.loads((b/('cross-feature-'+actorid)/'summary.json').read_text());assert cf['status']=='PASS' and cf['operations_completed']==49 and len(cf['assertions'])==36 and all(x['pass'] for x in cf['assertions'])
 assert time.monotonic_ns()<end,'Featurechain outside originalsteadywindow'
 print(json.dumps({'status':'20K_FEATURE_CHAIN_PASS','operations':49,'checks':36}),flush=True)
 assert ls.wait(timeout=75)==0,'List correctness failed; analyze beforenewramp'
 capcode=cap.wait(timeout=180);cr=json.loads((bg/'summary.json').read_text());lr=json.loads((folder/'list-openloop/summary.json').read_text());quality=json.loads((bg/'reconciliation.json').read_text())
 assert lr['status']=='PASS' and lr['sent']==lr['recorded']==1200 and lr['invalid']==lr['timeouts']==lr['skipped']==0
 assert quality['sent']==quality['positive_ack']==quality['db_rows']==quality['confirmed']==8100 and quality['negative_ack']==quality['pending']==0 and not quality['positive_ack_identity_or_confirmation_or_wire_mismatches'] and not quality['sent_not_durable_at_snapshot'] and not quality['durable_without_positive_ack'] and not quality['db_without_send'],'20kcore correctness failed'
 gates=cr['gates'];assert gates and all(v for k,v in gates.items() if k not in ['positive_ack_p99_le_100ms','scheduled_to_ack_p99_le_100ms'])
 assert capcode in [0,2] and cr['status'] in ['PASS','FAIL']
 result={'status':'MIXED20K_POINT_COMPLETE','capacity':cr,'list':lr,'reconciliation':quality,'cross_operations':49,'cross_assertions':36,'all_cross_inside_steady':True,'list_p99_le_100ms':lr['sent_to_response']['p99_ms']<=100,'list_scheduled_p99_le_100ms':lr['scheduled_to_response']['p99_ms']<=100,'head':head,'allfeature_extreme_acceptance':False,'limits':'20000online135/sprivate+one50pageactor20/s+fourfunctionalactors; singleON, notfullfeature throughput/TLS/fault/soak'}
except BaseException as e:
 error=e;save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'performance_acceptance':False,'next':'Analyze firstfailedwindow; no unchangedramp repeat'})
finally:
 try:stop_owned()
 finally:
  sampler_stop.set();sampler.join(timeout=5);assert not sampler.is_alive();save('resource-sampler-summary.json',{'sample_errors':sample_errors,'closed':True});assert not sample_errors
  for c in cs:
   if c['Name'] in accepted['gateways'] or c['Name']=='/tinyimx-m21-message-service-1':
    with (private/(c['Name'].replace('/','')+'-since-start.log')).open('w') as f:subprocess.run(['docker','logs','--timestamps','--since',json.loads((d/'audit-before.json').read_text())['utc'],c['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=40)
  preserved();save('preservation-summary.json',{'status':'ALL19_INSTANCES_CONFIGS_AND_DURABILITY_PRESERVED','runtime':before,'pressure_children_closed':True,'host_apps_preserved':True,'no_product_restart_or_data_deletion':True})
if error is not None:raise error
save('summary.json',result)
print(json.dumps({'status':result['status'],'capacity':result['capacity']['status'],'private_p99_ms':result['capacity']['positive_ack_p99_ms_upper_bin'],'list_p99_ms':result['list']['sent_to_response']['p99_ms'],'allfeature_extreme_acceptance':False}),flush=True)
PY
