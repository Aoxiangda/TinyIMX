#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,socket,re,os,signal,sys,types,math
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'conversation-unread-mixed-10k-20261006';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
source=json.loads((b/'conversation-unread-mixed-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/p)==h for p,h in source['files'].items())
build=json.loads((b/'conversation-unread-batch-build-20261006-attempt2/summary.json').read_text());assert build['status']=='CONVERSATION_UNREAD_BATCH_BUILD_PASS'
buildaudit=json.loads((b/'conversation-unread-batch-build-20261006-attempt2/audit-before.json').read_text());assert all(sha(p)==h for p,h in buildaudit['source_sha256'].items())
pre=json.loads((b/'conversation-unread-batch-runtime-preflight-20261006/summary.json').read_text());selection=pre['selection']
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
def inspect():return json.loads(run(['docker','inspect',*names]))
cs=inspect();before={c['Name']:ident(c) for c in cs}
restored=json.loads((b/'conversation-unread-batch-gateway-run-20261006/restore-summary.json').read_text())['runtime']
assert before=={**pre['runtime'],**restored}
roles=['gateway-a','gateway-b'];flag='TINYIMX_CONVERSATION_UNREAD_BATCH_ENABLE'
original={role:next(c for c in cs if c['Name']=='/tinyimx-m21-'+role+'-1') for role in roles}
assert all(c['Image']=='sha256:a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9' and c['State'].get('Health',{}).get('Status')=='healthy' for c in original.values())
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:sha(p) for p in cfg.glob('*.json')};assert hashes==pre['config_sha256']
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def no_live():
 ips={v['IPAddress'] for c in original.values() for v in c['NetworkSettings']['Networks'].values()}
 for c in inspect():
  if c['Name'] not in {v['Name'] for v in original.values()}:continue
  for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
   f=line.split()
   if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
    v=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(v)[::-1]) if len(v)==8 else 'ipv6'
    if peer not in ips and not peer.startswith('127.'):return False
 return True
assert no_live(),'Refuse live client interruption'
def settings():return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-unread-settings','SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count']).strip()
assert settings()=='1\t1\t1\t0\t0'
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')]
basecfg=json.loads(run(base+['config','--format','json']))['services'];overrides={}
for role,c in original.items():
 e=envmap(c);assert flag not in e and not any(k.startswith('TINYIMX_FAULT_') for k in e)
 assert c['Config']['Cmd']==basecfg[role]['command'] and all(e.get(k)==str(v) for k,v in basecfg[role].get('environment',{}).items())
original_elf={role:run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/gateway_demo']).split()[0] for role,c in original.items()}
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
(private/'original-inspect.json').write_text(json.dumps(original,indent=2)+'\n')
for mode in ['off','on','restore']:
 wanted={role:{'image':c['Image'] if mode=='restore' else build['gateway_image_id'],'environment':envmap(c) if mode=='restore' else {**envmap(c),flag:'1' if mode=='on' else '0'}} for role,c in original.items()}
 path=private/(mode+'.override.json');path.write_text(json.dumps({'services':wanted})+'\n')
 proposed=json.loads(run(base+['-f',str(path),'config','--format','json']))['services']
 for role in roles:assert {k:str(v) for k,v in proposed[role].get('environment',{}).items()}==wanted[role]['environment'] and proposed[role]['command']==original[role]['Config']['Cmd']
 overrides[mode]=(path,wanted)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'TwoGateway sameELF OFF/ON/ON/OFF 10000private100/s60s +one50page-list20/s +fresh49opcrossfeature inside eachsteady window; exactoriginal restoration afterany failure','before':before,'configs_sha256':hashes,'candidate_elf_sha256':build['gateway_elf_sha256'],'candidate_image':build['gateway_image_id'],'flag':flag,'fixture':selection,'data_changes':'Use existing verified fixed50page anchor519950 readonly; original10000ring synthetic700000..709999 normalprivate sends/ACKs, coordinator audited onlymissingringrelations. Per-case4freshownedactors normalfriend/private/read/group/file. Keepallbusiness/evidence, no reset/deletion','controls':'Originalcoordinator/nativeworker hashes and100/sloginramp/deadlines; offeredprivate100/s60s andlist20/s60s exactlysamebarrier; preserveall1200list scheduled/send/response/failures andeachcapacity6000planned withSQL/wire/reconciliation/Pong. 49crossops mustinsidewindow; noAI/cleanup/durability/security/hostchanges.','rollback':'Private originalinspect/logs/configSHA and explicitrestoreoverride; close onlyown sockets/processes. Keepallnewbusinessfixtures/failure/code/evidence/Git; other17IDs and durability1/1/1/0/0 preserved','user_apps':'Preserved','performance_acceptance':False})
for role,c in original.items():
 with (private/(role+'-original.log')).open('w') as f:subprocess.run(['docker','logs','--timestamps',c['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=40)
sys.path.insert(0,str(r/'benchmark/local_capacity'));from cross_feature_actor import Actor,Run,sql
actor_path=r/'benchmark/local_capacity/cross_feature_actor.py';actor_sha=sha(actor_path)
save('actor-inputs.json',{'script_sha256':actor_sha,'file_e2e_binary_sha256':sha(r/'build/linux-release/file_transfer_release_e2e_client')})
clients=[];child=None;ticks=None;args=None;current_mode=None;serial=0;error=None;cases=[]
def preserved():
 assert {p.name:sha(p) for p in cfg.glob('*.json')}==hashes and settings()=='1\t1\t1\t0\t0'
 now=inspect();target={c['Name'] for c in original.values()};assert all(c['Name'] in target or ident(c)==before[c['Name']] for c in now)
 assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in now)
 return now
def capture(label):
 for c in inspect():
  if c['Name'] in {v['Name'] for v in original.values()}:
   with (private/(label+c['Name'].replace('/','')+'.log')).open('w') as f:subprocess.run(['docker','logs','--timestamps',c['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=35)
def deploy(mode,label):
 global current_mode,serial
 end=time.monotonic()+15
 while not no_live():assert time.monotonic()<end,'Refuse active clients';time.sleep(.5)
 if current_mode is not None:capture(label+'-before-')
 serial+=1;path,wanted=overrides[mode]
 save('deploy-'+str(serial)+'-audit-before.json',{'operation':'Own authorized twoGateway controlled recreation','mode':mode,'override_sha256':sha(path),'original_restore_available':True,'no_external_live_clients':True,'other17_unchanged':True})
 with (private/(label+'.log')).open('w') as f:subprocess.run(base+['-f',str(path),'up','-d','--no-deps','--pull','never',*roles],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
 end=time.monotonic()+60
 while True:
  now=inspect();selected={role:next(c for c in now if c['Name']==original[role]['Name']) for role in roles}
  if all(c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy' for c in selected.values()):break
  assert time.monotonic()<end,'Gateway health timeout';time.sleep(1)
 for role,c in selected.items():
  orig=original[role];assert c['Image']==wanted[role]['image'] and envmap(c)==wanted[role]['environment'] and c['HostConfig']==orig['HostConfig'] and c['Mounts']==orig['Mounts'] and c['RestartCount']==0
  for k in ['User','WorkingDir','Cmd','Entrypoint','Healthcheck','StopSignal']:assert c['Config'].get(k)==orig['Config'].get(k)
  assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/gateway_demo']).split()[0]==(original_elf[role] if mode=='restore' else build['gateway_elf_sha256'])
 current_mode=mode;preserved();save('deploy-'+str(serial)+'-summary.json',{'status':'READY','mode':mode,'containers':{role:ident(c) for role,c in selected.items()}});print(json.dumps({'status':'GATEWAY_CONTROL_READY','mode':mode}),flush=True)
def new_run(name,out):
 out.mkdir(mode=0o700);v=Run(types.SimpleNamespace(run=name,host='192.168.220.128',port=9000,users=[]),out);clients.append(v);return v
def close(v):
 for c in list(v.clients):c.close()
 if v in clients:clients.remove(v)
def drain(v):
 end=time.monotonic()+10
 while any(c.hb_sent!=c.hb_ack for c in v.clients):assert time.monotonic()<end,'Own HB drain';v.pump(.02,heartbeats=False)
 assert all(c.hb_sent and c.hb_sent==c.hb_ack for c in v.clients)

coordinator=r/'benchmark/local_capacity/capacity_run.py';worker=r/'build/linux-release/tinyimx_capacity_worker';listclient=r/'benchmark/local_capacity/conversation_list_openloop.py'
assert sha(coordinator)=='1ef1c527335a998809296d487378776715ab0fe8c73ccda90d8fa4392431d8ba'
assert sha(worker)=='5d6bd183ef7119783497f60cda99566ad3d7f7bfcb96192fbdc4b46b8c1f40d3'
save('load-input-audit-before.json',{'coordinator_sha256':sha(coordinator),'worker_sha256':sha(worker),'listclient_sha256':sha(listclient),'operation':'Original ring load harness/nativeworker, ownreadonlycompanion andpublicfeaturechain; no binary/harness modification','ramp_per_sec':100,'private_rate':100,'private_duration':60,'list_rate':20,'list_planned':1200,'users':10000,'fixture':'Owned700000..709999 usernamescodex50k_20261004_, distinct519xxx companion/chain'})
owned=[]
def launch(argv,path):
 path.parent.mkdir(parents=True,exist_ok=True);f=path.open('w');p=subprocess.Popen(argv,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);ticks=pathlib.Path(f'/proc/{p.pid}/stat').read_text().rsplit(')',1)[1].split()[19]
 record={'p':p,'f':f,'ticks':ticks,'argv':argv};owned.append(record)
 (path.parent/(path.name+'.process.json')).write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'start_ticks':ticks,'argv':argv})+'\n');return p
def stop_owned():
 for item in owned:
  p=item['p']
  if p.poll() is None:
   proc=pathlib.Path(f'/proc/{p.pid}');assert proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==item['ticks'] and os.getpgid(p.pid)==p.pid and proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(item['argv'])]==[x.encode() for x in item['argv']]
   save('stop-'+str(p.pid)+'-audit-before.json',{'operation':'Only own timed-out childgroup','pid':p.pid,'start_ticks':item['ticks'],'argv_and_pgid_verified':True})
   os.killpg(p.pid,signal.SIGINT)
   try:p.wait(timeout=15)
   except subprocess.TimeoutExpired:
    os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=4)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=4)
  item['f'].close()
def interrupt(signum,frame):raise RuntimeError('Own mixed run interrupted '+str(signum))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupt)
try:
 for case,mode,runid,actorid in [('mixed-A1','off','cumixA1','cumixfeatA1'),('mixed-B1','on','cumixB1','cumixfeatB1'),('mixed-B2','on','cumixB2','cumixfeatB2'),('mixed-A2','off','cumixA2','cumixfeatA2')]:
  deploy(mode,'deploy-'+case);folder=d/case;folder.mkdir(mode=0o700);bg=b/('capacity-'+runid);assert not bg.exists()
  pairs=json.loads(run(['bash',str(r/'benchmark/local_capacity/evidence_tools/select_online_maintenance_actor_pairs_readonly.sh')]))['users'];assert selection['anchor'] not in pairs
  args=['python3',str(coordinator),'--run',runid,'--users','10000','--rate','100','--duration','60','--mode','private','--user-id-base','700000','--username-prefix','codex50k_20261004_','--gateway-image',build['gateway_image_id'],'--message-image','sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060','--host','192.168.220.128','--source-ips','192.168.220.128,192.168.220.129,192.168.58.129,172.18.0.1,172.17.0.1']
  (folder/'audit-before.json').write_text(json.dumps({'operation':'One mixed10000private/list/crossfeature case','mode':mode,'args':args,'fresh_cross_users':pairs,'ring_users_distinct':True,'listfixture_existing_readonly':True,'all_partialdata_retained':True,'timeout_seconds':600,'other17_instances_and_durability_preserved':True},indent=2)+'\n')
  cap=launch(args,private/(case+'-capacity.log'));deadline=time.monotonic()+350
  while not (bg/'control/start_ns').exists():
   assert cap.poll() is None,'Original ramp failed beforebarrier; analyze instead ofrepeat'
   assert time.monotonic()<deadline,'Loginramp barrier timeout';time.sleep(.2)
  start=int((bg/'control/start_ns').read_text());end=start+60_000_000_000;assert time.monotonic_ns()<start
  ls=launch(['python3',str(listclient),case,str(bg),str(folder/'list-openloop')],private/(case+'-list.log'))
  while time.monotonic_ns()<start+5_000_000_000:
   assert cap.poll() is None and ls.poll() is None,'Mixed companion/ring early failure';time.sleep(.05)
  cross=launch(['python3',str(actor_path),'--run',actorid,'--users',*map(str,pairs),'--host','192.168.220.128','--background-run',runid],private/(case+'-feature.log'))
  crosscode=cross.wait(timeout=50);assert crosscode==0,'Crossfeature failed inload; stop andanalyze'
  cf=json.loads((b/('cross-feature-'+actorid)/'summary.json').read_text());assert cf['status']=='PASS' and cf['operations_completed']==49 and len(cf['assertions'])==36 and all(x['pass'] for x in cf['assertions'])
  assert time.monotonic_ns()<end,'Featurechain mustfit original steadywindow'
  listcode=ls.wait(timeout=75);assert listcode==0,'List companion failed; stop andanalyze'
  capcode=cap.wait(timeout=180)
  capresult=json.loads((bg/'summary.json').read_text());listresult=json.loads((folder/'list-openloop/summary.json').read_text())
  quality=json.loads((bg/'reconciliation.json').read_text());gates=capresult.get('gates',{})
  assert listresult['status']=='PASS' and listresult['sent']==listresult['recorded']==1200 and listresult['invalid']==listresult['timeouts']==listresult['skipped']==0
  assert quality['sent']==quality['positive_ack']==quality['db_rows']==quality['confirmed']==6000 and quality['negative_ack']==quality['pending']==0 and not quality['positive_ack_identity_or_confirmation_or_wire_mismatches'] and not quality['sent_not_durable_at_snapshot'] and not quality['durable_without_positive_ack'] and not quality['db_without_send'],'Ring correctness failed'
  assert gates and all(v for k,v in gates.items() if k not in ['positive_ack_p99_le_100ms','scheduled_to_ack_p99_le_100ms']),'Non-latency ring gates failed; analyze beforeanother ramp'
  assert capcode in [0,2] and capresult['status'] in ['PASS','FAIL'],'Unexpected originalharness state'
  row={'case':case,'mode':mode,'capacity_exit':capcode,'capacity':capresult,'list':listresult,'cross_operations':49,'cross_assertions':36,'all_cross_operations_inside_steady_window':True,'reconciliation':quality}
  (folder/'summary.json').write_text(json.dumps(row,indent=2)+'\n');cases.append(row);save('completed-cases.json',cases);preserved();stop_owned();owned.clear();print(json.dumps({'status':'MIXED_CASE_COMPLETE','case':case,'capacity':capresult['status'],'list_p99_ms':listresult['sent_to_response']['p99_ms'],'feature_checks':36}),flush=True)
except BaseException as e:
 error=e;save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'completed_cases':len(cases),'performance_acceptance':False,'next':'Analyze earliestfailedwindow beforeanynewramp'})
finally:
 try:stop_owned()
 finally:
  for v in list(clients):close(v)
  save('restore-audit-before.json',{'operation':'ExactoriginaltwoGateway restoreafteranymixedresult; preserveother17/config/durability/apps/allpartialhistories','override_sha256':sha(overrides['restore'][0])})
  deploy('restore','restore-original')
  restore={'status':'ORIGINAL_GATEWAYS_RESTORED','runtime':{c['Name']:ident(c) for c in inspect() if c['Name'] in {v['Name'] for v in original.values()}},'other17_preserved':True,'configs_preserved':True,'durability':'1/1/1/0/0'}
  save('restore-summary.json',restore);print(json.dumps(restore),flush=True)
if error is not None:raise error
save('summary.json',{'status':'CONVERSATION_UNREAD_MIXED10K_COMPLETE','head':head,'cases':cases,'restore':restore,'performance_acceptance':False,'limits':'10000ring users100/sprivate plusone50page listactor20/s andfourfreshfunctionalactors eachcase; mixed10k selecteddomainproof, notallfunctions50000users/TLS/offline/fault/soak acceptance'})
print(json.dumps({'status':'CONVERSATION_UNREAD_MIXED10K_COMPLETE','cases':len(cases),'performance_acceptance':False}),flush=True)
PY
