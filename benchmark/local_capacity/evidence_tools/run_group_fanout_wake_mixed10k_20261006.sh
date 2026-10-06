#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,socket,re,os,signal,sys,types,math
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-fanout-wake-mixed10k-20261006';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
source=json.loads((b/'group-fanout-wake-mixed-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/p)==h for p,h in source['files'].items())
build=json.loads((b/'group-fanout-wake-build-20261006/summary.json').read_text());assert build['status']=='GROUP_FANOUT_COMMIT_WAKE_NATIVE_AND_IMAGE_PASS' and build['native_tests_total']==18
buildaudit=json.loads((b/'group-fanout-wake-build-20261006/audit-before.json').read_text())
assert all(sha(r/p)==h for p,h in buildaudit['source_sha256'].items() if not p.startswith('docs/'))
assert all(sha(p)==h for p,h in buildaudit['borrowed_sha256'].items())
assert sha(r/'benchmark/local_capacity/cross_feature_actor.py')=='3f4327755929487d58114876cd87cbc21311a858a72a4d44a23acf8c2d3f08a9'
assert sha(r/'benchmark/local_capacity/group_actor_snapshot_cross_feature.py')=='7548899f3a1fb5264656767253701e2a852a944bc7c884127c5bfa94ba3ef8a3'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
def inspect():return json.loads(run(['docker','inspect',*names]))
cs=inspect();before={c['Name']:ident(c) for c in cs}
assert before==json.loads((b/'group-fanout-wake-endpoint-control-20261006/restore-summary.json').read_text())['runtime']
roles=['gateway-a','gateway-b'];flag='TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE'
original={role:next(c for c in cs if c['Name']=='/tinyimx-m21-'+role+'-1') for role in roles}
assert all(c['Image']=='sha256:a2bb65215bfc85246b946a8c0850e134cd3144d078e5b56c376676c2cd8d1cfa' and c['State'].get('Health',{}).get('Status')=='healthy' and flag not in envmap(c) and envmap(c).get('TINYIMX_CONVERSATION_UNREAD_BATCH_ENABLE')=='1' and envmap(c).get('TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE')=='1' and 'TINYIMX_REDIS_ACQUIRE_TRACE_ENABLE' not in envmap(c) for c in original.values())
assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:sha(p) for p in cfg.glob('*.json')}
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def hostconfig(c):
 value=dict(c['HostConfig'])
 for key in ['Dns','DnsOptions','DnsSearch']:
  if value.get(key) is None:value[key]=[]
 return value
def sql(q):
 assert q.startswith('SELECT ') and ';' not in q
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','group-wake-readonly',q])
def settings():return sql('SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count').strip()
assert settings()=='1\t1\t1\t0\t0'
assert sql('SELECT owner_user_id,status FROM im_groups WHERE group_id=26').strip()=='519870\t1'
assert sql('SELECT COUNT(*) FROM im_group_members WHERE group_id=26 AND user_id=519950').strip()=='0'

assert sql('SELECT user_id,role,status,IFNULL(muted_until,\'NULL\') FROM im_group_members WHERE group_id=26 ORDER BY user_id').strip()=='519870\t1\t1\tNULL\n519872\t3\t1\tNULL'
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

base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')]
basecfg=json.loads(run(base+['config','--format','json']))['services'];overrides={}
for role,c in original.items():
 e=envmap(c);assert not any(k.startswith('TINYIMX_FAULT_') for k in e)
 assert c['Config']['Cmd']==basecfg[role]['command'] and all(e.get(k)==str(v) for k,v in basecfg[role].get('environment',{}).items())
 assert e.get('TINYIMX_GROUP_FANOUT_ENABLE')=='1'
 for key in ['TINYIMX_GROUP_FANOUT_RECOVERY_MS','TINYIMX_GROUP_FANOUT_BATCH_SIZE','TINYIMX_GROUP_FANOUT_LEASE_MS','TINYIMX_GROUP_FANOUT_ACK_RETRY_MS','TINYIMX_GROUP_FANOUT_FAILURE_RETRY_MS']:assert key not in e
original_elf={role:run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/gateway_demo']).split()[0] for role,c in original.items()}
assert set(original_elf.values())=={'ca01b00011b29eff29fe776fe3ac2dbaa0b4bb6e7a4ddce76412b82e583579ce'}
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
(private/'original-inspect.json').write_text(json.dumps(original,indent=2)+'\n')
for mode in ['off','on','restore']:
 wanted={role:{'image':c['Image'] if mode=='restore' else build['gateway_image_id'],'environment':envmap(c) if mode=='restore' else {**envmap(c),flag:'1' if mode=='on' else '0'}} for role,c in original.items()}
 path=private/(mode+'.override.json');path.write_text(json.dumps({'services':wanted})+'\n')
 proposed=json.loads(run(base+['-f',str(path),'config','--format','json']))['services']
 for role in roles:assert {k:str(v) for k,v in proposed[role].get('environment',{}).items()}==wanted[role]['environment'] and proposed[role]['command']==original[role]['Config']['Cmd']
 overrides[mode]=(path,wanted)

import urllib.request
with urllib.request.urlopen('http://127.0.0.1:11434/api/ps',timeout=4) as response:modelps=json.load(response)
assert not modelps.get('models'),'Preserve externalmodel service and diagnose instead of forcedunload'
coordinator=r/'benchmark/local_capacity/capacity_run.py';worker=r/'build/linux-release/tinyimx_capacity_worker'
listclient=r/'benchmark/local_capacity/conversation_list_openloop_group_wake_10k.py';groupclient=r/'benchmark/local_capacity/group_delivery_openloop_wake_10k.py'
actor_path=r/'benchmark/local_capacity/group_actor_snapshot_cross_feature.py'
assert sha(coordinator)=='1ef1c527335a998809296d487378776715ab0fe8c73ccda90d8fa4392431d8ba'
assert sha(worker)=='5d6bd183ef7119783497f60cda99566ad3d7f7bfcb96192fbdc4b46b8c1f40d3'
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'TwoGW sameELF wake OFF/ON/ON/OFF original10000native private100/s60sec +fixed50page20/s +twoactualcrossGatewaygroup2/s +54operationfullchain allinside steady; restoreafteranyresult','runtime_before':before,'configs_sha256':hashes,'candidate_image':build['gateway_image_id'],'candidate_elf':build['gateway_elf_sha256'],'source_sha256':source['files'],'load_inputs':{str(p):sha(p) for p in [coordinator,worker,listclient,groupclient,actor_path]},'original_ramp_per_second':100,'data_scope':'Ownexistingring700000..709999 normal6000privatemessages/case, normal280newuniqueprefix group26messages, fourfreshreserved4actor friend/read/group/filechains; onlymissingringedges audited by unchangedcoordinator. Retainallrows/files/evidence. NoDBreset/delete/DDL/prune/cacheclear/forceunload/hostapps modification','runtime_scope':'Only2GWimage+strictwake flag; preserveacceptedconversation/maintenance1, fullEnv/health/Cmd/HostConfig/mounts andother17 exactIDs; restoreoriginala2bbafteranyfail','deadlines':'Originalnative andcompanion3sec; no hiddenretry/skips/delayedbarrier. Candidate100ms gates independentlyprivate/list/actualgroup; stop andanalyze firstcandidatefailure','rollback':'Originalprivateinspect/fullrestore override andSHA; shutdownonlyPID/startticks/PGID/argvverifiedownchildren; preserveeveryfailedsample','limits':'Selected10000mixedrates/2membergroup andlowfrequency28publictypechains; notallfunctions50k/largegroup/TLS/offline/fault/soak acceptance','allfeature_extreme_acceptance':False})
# Select sixteen distinct, offline reserved actors without touching any relation.
occupied=set()
for q in ['SELECT user_id,peer_user_id FROM im_user_relations WHERE user_id BETWEEN 519800 AND 519950 AND peer_user_id BETWEEN 519800 AND 519950','SELECT from_user_id,to_user_id FROM im_friend_requests WHERE from_user_id BETWEEN 519800 AND 519950 AND to_user_id BETWEEN 519800 AND 519950']:
 for row in sql(q).splitlines():occupied.add(tuple(sorted(map(int,row.split('\t')))))
rcfg=json.loads((cfg/'gateway-a.json').read_text())['redis'];redis=next(c for c in cs if c['Name']=='/tinyimx-m21-redis-1');ip=next(v['IPAddress'] for v in redis['NetworkSettings']['Networks'].values() if v.get('IPAddress'))
ss=socket.create_connection((ip,rcfg['port']),3);ss.settimeout(3);f=ss.makefile('rb')
def redis_call(*args):
 wire=b'*'+str(len(args)).encode()+b'\r\n'
 for arg in args:
  arg=str(arg).encode();wire+=b'$'+str(len(arg)).encode()+b'\r\n'+arg+b'\r\n'
 ss.sendall(wire);line=f.readline(1024);assert line.endswith(b'\r\n')
 if line[:1]==b'+':return line[1:-2]
 if line[:1]==b':':return int(line[1:-2])
 raise RuntimeError('OwnReadonlyRedisReplyRejected')
try:
 if rcfg.get('password'):assert redis_call('AUTH',rcfg['password'])==b'OK'
 if rcfg.get('db',0):assert redis_call('SELECT',rcfg['db'])==b'OK'
 available=[]
 for row in sql('SELECT user_id,username,status FROM im_users WHERE user_id BETWEEN 519800 AND 519950 ORDER BY user_id').splitlines():
  uid,username,status=row.split('\t');uid=int(uid)
  if uid in {519870,519872,519950} or 519810<=uid<=519859:continue
  if status=='1' and username==f'm21b500000_{uid-500000:06d}' and redis_call('EXISTS','tinyimx:online:'+str(uid))==0:available.append(uid)
 chosen=[]
 for a in available:
  if a in chosen:continue
  for bb in available:
   if bb<=a or bb in chosen or bb-a==1 or (a,bb) in occupied:continue
   chosen.extend([a,bb]);break
  if len(chosen)==16:break
 assert len(chosen)==16 and all(redis_call('EXISTS','tinyimx:online:'+str(uid))==0 for uid in [519870,519872,519950])
finally:f.close();ss.close()
save('actor-selection-readonly.json',{'users':chosen,'sets':[chosen[i:i+4] for i in range(0,16,4)],'offline_and_username_status_pair_empty':True,'SQL_writes':False})

sys.path.insert(0,str(r/'benchmark/local_capacity'));from cross_feature_actor import Actor,Run
clients=[];current_mode=None;serial=0;error=None;cases=[]
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
 with (private/(label+'.log')).open('w') as f:subprocess.run(base+['-f',str(path),'up','-d','--no-deps','--force-recreate','--pull','never',*roles],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
 end=time.monotonic()+60
 while True:
  now=inspect();selected={role:next(c for c in now if c['Name']==original[role]['Name']) for role in roles}
  if all(c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy' for c in selected.values()):break
  assert time.monotonic()<end,'Gateway health timeout';time.sleep(1)
 for role,c in selected.items():
  orig=original[role];assert c['Image']==wanted[role]['image'] and envmap(c)==wanted[role]['environment'] and hostconfig(c)==hostconfig(orig) and c['Mounts']==orig['Mounts'] and c['RestartCount']==0
  for k in ['User','WorkingDir','Cmd','Entrypoint','Healthcheck','StopSignal']:assert c['Config'].get(k)==orig['Config'].get(k)
  assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/gateway_demo']).split()[0]==(original_elf[role] if mode=='restore' else build['gateway_elf_sha256'])
 current_mode=mode;preserved();save('deploy-'+str(serial)+'-summary.json',{'status':'READY','mode':mode,'containers':{role:ident(c) for role,c in selected.items()}});print(json.dumps({'status':'GATEWAY_CONTROL_READY','mode':mode}),flush=True)
def gateway_ready(case):
 stage=d/('ready-'+case);stage.mkdir(mode=0o700)
 rows=[]
 for gw in [c for c in inspect() if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']]:
  ip=next(v['IPAddress'] for v in gw['NetworkSettings']['Networks'].values() if v.get('IPAddress'))
  v=Run(types.SimpleNamespace(run='gfwready'+case,users=[],host=ip,port=9000),stage);clients.append(v)
  try:
   actor=Actor(v,519870,ip,9000);deadline=time.monotonic()+20;consecutive=0
   while consecutive<2:
    assert time.monotonic()<deadline,'Gateway registration snapshot readiness timeout'
    start=time.monotonic_ns();seq=actor.send(2025,{'group_id':26})
    v.emit('request',uid=actor.uid,name='registration-readiness-only',kind=2025,seq=seq,body={'group_id':26})
    v.until(lambda:(2026,seq) in actor.replies or (9999,seq) in actor.replies,'registration-readiness',seconds=5)
    assert (9999,seq) not in actor.replies
    reply=actor.replies.pop((2026,seq));good=reply.get('success') is True
    row={'gateway':gw['Name'],'uid':519870,'seq':seq,'success':good,'reason':reply.get('reason'),'ms':(time.monotonic_ns()-start)/1e6,'response':reply,'excluded_from_measurement':True};rows.append(row)
    save('readiness-'+case+'-probes.json',rows)
    if not good:assert reply.get('reason')=='group_service_unavailable','Unexpected failure in startup gate'
    else:assert int(reply['group']['group_id'])==26 and int(reply['group']['owner_user_id'])==519870
    consecutive=consecutive+1 if good else 0
    if consecutive<2:time.sleep(.2)
  finally:
   for c in list(v.clients):c.close()
   clients.remove(v)
 save('readiness-'+case+'-summary.json',{'status':'BOTH_GATEWAY_GROUP_REGISTRATION_READY','probes':rows,'startup_failures_retained':sum(not x['success'] for x in rows),'not_capacity_or_measured_requests':True})
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
 for index,(case,mode,runid,actorid) in enumerate([('mixed-A1','off','gfwm10A1','gfwmfeatA1'),('mixed-B1','on','gfwm10B1','gfwmfeatB1'),('mixed-B2','on','gfwm10B2','gfwmfeatB2'),('mixed-A2','off','gfwm10A2','gfwmfeatA2')]):
  deploy(mode,'deploy-'+case);gateway_ready(case.replace('mixed-',''))
  folder=d/case;folder.mkdir(mode=0o700);bg=b/('capacity-'+runid);assert not bg.exists()
  pairs=chosen[index*4:index*4+4];assert len(pairs)==4 and not set(pairs)&{519870,519872,519950}
  args=['python3',str(coordinator),'--run',runid,'--users','10000','--rate','100','--duration','60','--mode','private','--user-id-base','700000','--username-prefix','codex50k_20261004_','--gateway-image',build['gateway_image_id'],'--message-image','sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060','--host','192.168.220.128','--source-ips','192.168.220.128,192.168.220.129,192.168.58.129,172.18.0.1,172.17.0.1']
  (folder/'audit-before.json').write_text(json.dumps({'operation':'Originalsamewindow10kprivate/list +truegroupdelivery/fullfeaturechain','mode':mode,'args':args,'cross_users':pairs,'group26_actors':[519870,519872],'listactor':519950,'all_actors_distinct_from_ring_and_eachother':True,'timeout_seconds':600,'allpartialdata_retained':True},indent=2)+'\n')
  cap=launch(args,private/(case+'-capacity.log'));deadline=time.monotonic()+350
  while not (bg/'control/start_ns').exists():
   assert cap.poll() is None,'Originalramp failed beforebarrier; diagnose beforeanother ramp'
   assert time.monotonic()<deadline,'Login ramp timeout';time.sleep(.2)
  start=int((bg/'control/start_ns').read_text());end=start+60_000_000_000;assert time.monotonic_ns()<start
  ls=launch(['python3',str(listclient),case,str(bg),str(folder/'list-openloop')],private/(case+'-list.log'))
  gs=launch(['python3',str(groupclient),case,str(bg),str(folder/'group-openloop')],private/(case+'-group.log'))
  while time.monotonic_ns()<start+5_000_000_000:
   assert cap.poll() is None and ls.poll() is None and gs.poll() is None,'Companion failed before window; no shiftedretry';time.sleep(.05)
  cross=launch(['python3',str(actor_path),'--run',actorid,'--users',*map(str,pairs),'--host','192.168.220.128','--background-run',runid],private/(case+'-feature.log'))
  crosscode=cross.wait(timeout=50);assert crosscode==0,'Crossfeature failed inload; stop andanalyze'
  cf=json.loads((b/('cross-feature-'+actorid)/'summary.json').read_text());assert cf['status']=='PASS' and cf['operations_completed']==54 and len(cf['assertions'])==38 and all(x['pass'] for x in cf['assertions'])
  assert time.monotonic_ns()<end,'Full54operation chain must fit unchangedsteadywindow'
  groupcode=gs.wait(timeout=55);assert groupcode==0,'Actual groupcompanion failed; stop andanalyze'
  gr=json.loads((folder/'group-openloop/summary.json').read_text());assert gr['status']=='GROUP_WAKE_ACTUAL_DELIVERY_CASE_PASS' and gr['sent']==gr['positive_ack']==gr['actual_wire_received']==gr['exact_durable_recipient_confirmed']==70 and gr['measured']==60 and gr['timeouts']==gr['skipped']==gr['duplicate_wire_deliveries']==0 and gr['every_send_ack_delivery_inside_original_steady']
  listcode=ls.wait(timeout=75);assert listcode==0,'Listcompanion failed'
  capcode=cap.wait(timeout=180);cr=json.loads((bg/'summary.json').read_text());lr=json.loads((folder/'list-openloop/summary.json').read_text());quality=json.loads((bg/'reconciliation.json').read_text());gates=cr.get('gates',{})
  assert lr['status']=='PASS' and lr['sent']==lr['recorded']==1200 and lr['invalid']==lr['timeouts']==lr['skipped']==0
  assert quality['sent']==quality['positive_ack']==quality['db_rows']==quality['confirmed']==6000 and quality['negative_ack']==quality['pending']==0 and not quality['positive_ack_identity_or_confirmation_or_wire_mismatches'] and not quality['sent_not_durable_at_snapshot'] and not quality['durable_without_positive_ack'] and not quality['db_without_send'],'Private correctness failed'
  assert gates and all(v for k,v in gates.items() if k not in ['positive_ack_p99_le_100ms','scheduled_to_ack_p99_le_100ms']),'Non-latency nativegates failed'
  assert capcode in [0,2] and cr['status'] in ['PASS','FAIL']
  performance={'private_all_original_gates':all(gates.values()),'list_sent_p99_le_100ms':lr['sent_to_response']['p99_ms']<=100,'list_scheduled_p99_le_100ms':lr['scheduled_to_response']['p99_ms']<=100,'group_actual_delivery_sample_p99_le_100ms':gr['delivery']['p99_ms']<=100,'group_scheduled_delivery_sample_p99_le_100ms':gr['scheduled_delivery']['p99_ms']<=100}
  row={'case':case,'mode':mode,'capacity_exit':capcode,'capacity':cr,'list':lr,'group_actual_delivery':gr,'performance_gates':performance,'cross_operations':54,'cross_assertions':38,'all_cross_operations_inside_steady':True,'reconciliation':quality}
  (folder/'summary.json').write_text(json.dumps(row,indent=2)+'\n');cases.append(row);save('completed-cases.json',cases);preserved();stop_owned();owned.clear()
  capture(case+'-completed-');print(json.dumps({'status':'GROUP_WAKE_MIXED10K_CASE_COMPLETE','case':case,'private_p99_ms':cr['positive_ack_p99_ms_upper_bin'],'list_p99_ms':lr['sent_to_response']['p99_ms'],'group_actual_delivery_sample_p99_ms':gr['delivery']['p99_ms'],'performance_gates':performance}),flush=True)
  if mode=='on':assert all(performance.values()),'Candidate mixed latency failed; retain andanalyze beforeanynext ramp'
except BaseException as e:
 error=e;save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'completed_cases':len(cases),'next':'Analyze firstfailedwindow, neverrepeat unchangedramp','allfeature_extreme_acceptance':False})
finally:
 try:stop_owned()
 finally:
  for v in list(clients):
   for actor in list(v.clients):actor.close()
  save('restore-audit-before.json',{'operation':'ExactoriginaltwoGWrestoreafteranymixedresult; keepallfiles/history/failedsamples andhostapps','override_sha256':sha(overrides['restore'][0])})
  deploy('restore','restore-original');restored={c['Name']:ident(c) for c in inspect()}
  restore={'status':'ORIGINAL_GATEWAYS_RESTORED','runtime':restored,'other17_exact_preserved':True,'fullEnv_health_Cmd_HostConfig_mounts_config_durability_preserved':True,'all_pressure_children_closed':True}
  save('restore-summary.json',restore);print(json.dumps(restore),flush=True)
if error is not None:raise error
assert len(cases)==4
save('summary.json',{'status':'GROUP_FANOUT_WAKE_MIXED10K_ABBA_COMPLETE','head':head,'cases':cases,'restore':restore,'private_messages':24000,'full50pages':4800,'group_messages_confirmed':280,'group_measured':240,'cross_operations':216,'cross_assertions':152,'allfeature_extreme_acceptance':False,'limits':'Selected10kprivate100/s+one50page20/s+two groupmembers2/s+54operationchains; eachcapacity gate retained, notallfunctions50k/largegroup/TLS/fault/soak'})
print(json.dumps({'status':'GROUP_FANOUT_WAKE_MIXED10K_ABBA_COMPLETE','allfeature_extreme_acceptance':False}),flush=True)
PY
