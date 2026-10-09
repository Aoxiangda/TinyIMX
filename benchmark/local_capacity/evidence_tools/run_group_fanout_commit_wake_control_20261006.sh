#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,socket,re,os,signal,sys,types,math
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-fanout-wake-endpoint-control-20261006';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
source=json.loads((b/'group-fanout-wake-control-source-20261006/summary.json').read_text())
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
assert before==json.loads((b/'group-fanout-delay-analysis-20261006/summary.json').read_text())['runtime_before']
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
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'TwoGateway sameELF OFF/ON/ON/OFF commitwake actual group26 alternating persistent crossGateway senders, sixty measured plus ten warm messages percase at2/s; full54op37assertion chain percase; final restore','before':before,'configs_sha256':hashes,'candidate_elf_sha256':build['gateway_elf_sha256'],'candidate_image':build['gateway_image_id'],'flag':flag,'candidate_scope':'Only2Gateway image and strictwake flag, fullotherEnv/health/Cmd/HostConfig/mounts preserved; other17 exactinstances','data_scope':'Onlynormalpubliclogin/HB and280newuniqueCID group26 messages fromexisting2verifiedsynthetic members; fourfresh owned4actor featurechains; preserveallrows/files/history. NoSQLwrites/deletes/DDL/cacheclear orhostchanges','controls':'Perrequest exactseq/CID/MID/recipient/content/SQLdelivery_status3; ACKvsactualwire timings; 3sec unchanged deadlines; failed/skipped/late samples alwaysretained; OFF/ON sameELF, originalrecovery1000/lease5000/batch64/ACKretry3000; noAI/capacity concurrently','rollback':'Privateoriginalinspect and fullEnv exactrestoreoverride; closeonlyownactors/PGIDverifiedchild. No writesoutsideauditednewstage/source/Git/normalownedAPI. Originaldurability1/1/1/0/0','limits':'Selected2memberactualdelivery microcontrol andfullfunctionalregression, notallfeatures10k50kcapacity/soak/TLS/fault acceptance','performance_acceptance':False})
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
def invoke_chain(case,index):
 stage=d/case;args=['/usr/bin/python3',str(r/'benchmark/local_capacity/group_actor_snapshot_cross_feature.py'),'--run','gfwfeat'+case,'--users',*map(str,chosen[index*4:index*4+4]),'--host','192.168.220.128']
 save(case+'-cross-audit-before.json',{'operation':'Originalfullchain plusleft/kick/disbanddeny','argv':args,'normalAPIonly':True,'noexistingpairoverwrite':True})
 with (private/(case+'-cross.log')).open('w') as f:
  p=subprocess.Popen(args,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path('/proc')/str(p.pid);ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19];save(case+'-cross-process.json',{'pid':p.pid,'start_ticks':ticks,'argv':args})
  try:code=p.wait(timeout=300)
  finally:
   if p.poll() is None:
    assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
    save(case+'-cross-stop-audit.json',{'operation':'Stoponlyverifiedownchainchildgroup','pid':p.pid});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 x=json.loads((b/('cross-feature-gfwfeat'+case)/'summary.json').read_text());assert code==0 and x['status']=='PASS' and x['operations_completed']==54 and len(x['assertions'])==37 and all(v['pass'] for v in x['assertions'])
 print(json.dumps({'status':'GROUP_CHAIN_PASS','case':case,'operations':54,'checks':37}),flush=True);return x
def stats(values):
 a=sorted(values);return {'samples':len(a),'mean_ms':sum(a)/len(a),'p50_ms':a[math.ceil(.5*len(a))-1],'p99_ms':a[math.ceil(.99*len(a))-1],'max_ms':a[-1]}
class CaptureRun(Run):
 def __init__(self,args,out):super().__init__(args,out);self.responses=[]
 def emit(self,event,**kw):
  now=time.monotonic_ns()
  if event=='response':self.responses.append({'observed_mono_ns':now,**kw})
  super().emit(event,**kw)
def close(v):
 for c in list(v.clients):c.close()
 if v in clients:clients.remove(v)
def resource(label):
 save(label,{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'monotonic_ns':time.monotonic_ns(),'meminfo':pathlib.Path('/proc/meminfo').read_text(),'guest_stat':pathlib.Path('/proc/stat').read_text(),'cpu_pressure':pathlib.Path('/proc/pressure/cpu').read_text(),'io_pressure':pathlib.Path('/proc/pressure/io').read_text()})
def measure(case):
 stage=d/case;stage.mkdir(mode=0o700)
 v=CaptureRun(types.SimpleNamespace(run='gfw'+case,users=[],host='192.168.220.128',port=9000),stage);clients.append(v)
 current={role:next(c for c in inspect() if c['Name']==original[role]['Name']) for role in roles}
 ips=[next(x['IPAddress'] for x in current[role]['NetworkSettings']['Networks'].values() if x.get('IPAddress')) for role in roles]
 rows=[];summary=None;namespace='gfw-20261006-'+case
 assert re.fullmatch(r'gfw-20261006-[AB][12]',namespace)
 assert sql(f"SELECT COUNT(*) FROM im_group_messages WHERE client_message_id LIKE '{namespace}-%'").strip()=='0'
 def write(): (stage/'requests.json').write_text(json.dumps(rows,indent=2)+'\n');(stage/'wire-responses.json').write_text(json.dumps(v.responses,indent=2)+'\n')
 try:
  actors=[Actor(v,uid,ip,9000) for uid,ip in zip([519870,519872],ips)]
  resource(case+'-resources-before.json');start=time.monotonic_ns()
  for i in range(70):
   scheduled=start+i*500000000
   while time.monotonic_ns()<scheduled:v.pump(min(.005,max(0,(scheduled-time.monotonic_ns())/1e9)))
   sender=actors[i%2];receiver=actors[1-i%2]
   cmid=namespace+f'-{i:05d}';content=namespace+f'-content-{i:05d}'
   body={'group_id':26,'client_message_id':cmid,'message_type':1,'content':content}
   sent=time.monotonic_ns();seq=sender.send(2049,body)
   row={'index':i,'measured':i>=10,'sender':sender.uid,'recipient':receiver.uid,'seq':seq,'scheduled_mono_ns':scheduled,'sent_mono_ns':sent,'client_message_id':cmid,'content':content,'body':body,'send_lag_ms':(sent-scheduled)/1e6}
   rows.append(row);v.emit('request',uid=sender.uid,name='group-wake-measured' if i>=10 else 'group-wake-warm',kind=2049,seq=seq,body=body,scheduled_mono_ns=scheduled,sent_mono_ns=sent);write()
  end=time.monotonic()+5
  while True:
   got=[x for x in v.responses if x['kind']==2050]
   if len(got)==70 and all((2051,int(x['body'].get('message_id',0)),next(rr['recipient'] for rr in rows if rr['seq']==x['seq'] and rr['sender']==x['uid'])) in v.deliveries for x in got):break
   assert time.monotonic()<end,'Group message ACK or actual delivery drain deadline';v.pump(.005)
  for row in rows:
   acknowledgments=[x for x in v.responses if x['uid']==row['sender'] and x['kind']==2050 and x['seq']==row['seq']]
   assert len(acknowledgments)==1,'ExactoneACK';ack=acknowledgments[0];reply=ack['body']
   row['ack']=reply;row['ack_mono_ns']=ack['observed_mono_ns'];mid=int(reply.get('message_id',0));row['message_id']=mid
   assert reply.get('success') is True and mid>0 and int(reply['group_id'])==26 and reply['result']=='created','Positive durable group identity'
   wires=[x for x in v.responses if x['uid']==row['recipient'] and x['kind']==2051 and int(x['body'].get('message_id',0))==mid]
   row['deliveries']=wires;assert wires,'Missing actual recipient wire'
   assert all(int(x['body']['group_id'])==26 and int(x['body']['from_user_id'])==row['sender'] and x['body']['content']==row['content'] and int(x['body']['message_type'])==1 for x in wires),'Recipient identity/content'
   delivered=min(x['observed_mono_ns'] for x in wires);row['delivery_mono_ns']=delivered;row['duplicate_deliveries']=len(wires)-1
   row['send_to_ack_ms']=(row['ack_mono_ns']-row['sent_mono_ns'])/1e6
   row['send_to_delivery_ms']=(delivered-row['sent_mono_ns'])/1e6
   row['scheduled_to_delivery_ms']=(delivered-row['scheduled_mono_ns'])/1e6
   row['scheduled_to_ack_ms']=(row['ack_mono_ns']-row['scheduled_mono_ns'])/1e6
   row['ack_to_delivery_ms']=(delivered-row['ack_mono_ns'])/1e6
   write()
  # Check all deadlines after retaining every raw sample, without retry or dropping slow samples.
  assert len({x['message_id'] for x in rows})==70
  assert all(x['send_to_ack_ms']<=3000 and x['send_to_delivery_ms']<=3000 for x in rows),'Original3s permessage deadline'
  query=f"SELECT m.message_id,m.client_message_id,m.group_id,m.from_user_id,m.message_type,HEX(m.content),d.recipient_user_id,d.delivery_status,d.attempt_count FROM im_group_messages m LEFT JOIN im_group_message_deliveries d ON d.message_id=m.message_id WHERE m.client_message_id LIKE '{namespace}-%' ORDER BY m.message_id,d.recipient_user_id"
  snapshots=[];deadline=time.monotonic()+8
  while True:
   now=time.monotonic_ns();text=sql(query);snapshots.append({'observed_mono_ns':now,'result':text})
   (stage/'confirmation-snapshots.json').write_text(json.dumps(snapshots,indent=2)+'\n')
   db=[x.split('\t') for x in text.splitlines()]
   if len(db)==70 and all(x[7]=='3' for x in db):break
   assert time.monotonic()<deadline,'Real receiverACK durableconfirmation drain'
   for i in range(40):v.pump(.005)
  bycid={x['client_message_id']:x for x in rows};assert len(db)==70
  for x in db:
   assert x[1] in bycid;row=bycid[x[1]]
   assert [int(x[0]),int(x[2]),int(x[3]),int(x[4]),bytes.fromhex(x[5]).decode(),int(x[6]),int(x[7])]==[row['message_id'],26,row['sender'],1,row['content'],row['recipient'],3],'Exact durable message and recipient confirmation identity'
   row['confirmed_snapshot_mono_ns']=snapshots[-1]['observed_mono_ns'];row['attempt_count']=int(x[8])
  # Confirmation observation is batched after wire measurement, not permessage confirmation latency.
  for a in actors:
   reply=v.request(a,'group-send-idempotence-regression',2049,rows[0 if a.uid==519870 else 1]['body'])
   assert reply['result']=='reused' and int(reply['message_id'])==rows[0 if a.uid==519870 else 1]['message_id']
  outsider=Actor(v,519950,ips[0],9000)
  for name,body in [('outsider-send',{'group_id':26,'client_message_id':namespace+'-denied','message_type':1,'content':'denied'}),('injected-actor-send',{'group_id':26,'client_message_id':namespace+'-denied-injected','message_type':1,'content':'denied','actor_user_id':519870})]:
   reply=v.request(outsider,name,2049,body,success=False,reason={'group_send_permission_denied'})
  assert sql(f"SELECT COUNT(*) FROM im_group_messages WHERE client_message_id LIKE '{namespace}-denied%'").strip()=='0'
  until=time.monotonic()+10
  while any(a.hb_sent!=a.hb_ack for a in v.clients):assert time.monotonic()<until,'RealPongdrain';v.pump(.005,heartbeats=False)
  assert all(a.hb_sent and a.hb_sent==a.hb_ack for a in v.clients)
  measured=[x for x in rows if x['measured']];assert len(measured)==60
  resource(case+'-resources-after.json');preserved()
  summary={'status':'GROUP_WAKE_ACTUAL_DELIVERY_CASE_PASS','case':case,'mode':current_mode,'measured':60,'warm':10,'offered_rate_per_second':2,'ack':stats([x['send_to_ack_ms'] for x in measured]),'delivery':stats([x['send_to_delivery_ms'] for x in measured]),'scheduled_delivery':stats([x['scheduled_to_delivery_ms'] for x in measured]),'scheduled_ack':stats([x['scheduled_to_ack_ms'] for x in measured]),'ack_to_delivery':stats([x['ack_to_delivery_ms'] for x in measured]),'send_lag':stats([x['send_lag_ms'] for x in measured]),'duplicate_wire_deliveries':sum(x['duplicate_deliveries'] for x in rows),'sent':70,'positive_ack':70,'actual_wire_received':70,'exact_durable_recipient_confirmed':70,'negative_requests':2,'denied_durable_rows':0,'idempotence_reused':2,'timeouts':0,'skipped':0,'late_ack_gt_3s':0,'late_delivery_gt_3s':0,'confirmation_snapshot':'batched after measurement; not exact permessage confirmation latency','limits':'Two persistent actual members crossGateway,60samples at2/s, notpopulationP99 or10k50kcapacity'}
  (stage/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary),flush=True);return summary
 finally:
  write();(stage/'operations.json').write_text(json.dumps(v.operations,indent=2)+'\n');close(v)
def interrupt(signum,frame):raise RuntimeError('Own run interrupted '+str(signum))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupt)
try:
 for index,(case,mode) in enumerate([('A1','off'),('B1','on'),('B2','on'),('A2','off')]):
  deploy(mode,'deploy-'+case);gateway_ready(case);row=measure(case);row['cross']=invoke_chain(case,index);cases.append(row);save('completed-cases.json',cases);capture(case+'-completed-')
except BaseException as e:
 error=e;save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'completed_cases':cases,'performance_acceptance':False})
finally:
 for v in list(clients):close(v)
 save('restore-audit-before.json',{'operation':'Exactoriginal2Gateway image/fullEnv/health/Cmd/HostConfig/mounts/binary restore; keepallhistory/failures','override_sha256':sha(overrides['restore'][0])})
 deploy('restore','restore-original');restored={c['Name']:ident(c) for c in inspect()}
 restore={'status':'ORIGINAL_GATEWAYS_RESTORED','runtime':restored,'other17_exact_identities_preserved':True,'config_fullEnv_health_Cmd_HostConfig_mounts_and_durability_preserved':True,'keep_all_evidence_and_history':True}
 save('restore-summary.json',restore);print(json.dumps(restore),flush=True)
if error is not None:raise error
offs=[x for x in cases if x['mode']=='off'];ons=[x for x in cases if x['mode']=='on'];assert len(cases)==4
x={'status':'GROUP_FANOUT_COMMIT_WAKE_ABBA_ACTUAL_DELIVERY_COMPLETE','head':head,'same_elf_ABBA_cases':cases,'allON_delivery_means_better_than_allOFF':max(x['delivery']['mean_ms'] for x in ons)<min(x['delivery']['mean_ms'] for x in offs),'allON_delivery_p99_better_than_allOFF':max(x['delivery']['p99_ms'] for x in ons)<min(x['delivery']['p99_ms'] for x in offs),'total_messages_confirmed':280,'measured_messages':240,'functional_operations':216,'functional_assertions':148,'restore':restore,'allfeature_extreme_acceptance':False,'limits':'Proves selectedcommitwake effect versusfixedpoll on2membergroup actualdelivery andfullchains; next mixedload/fanouthotspot coverage needed'}
save('summary.json',x);print(json.dumps({k:v for k,v in x.items() if k not in ['same_elf_ABBA_cases','restore']},indent=2))
PY
