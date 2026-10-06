#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,time,socket,re,sys,types,math,signal
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-fanout-size-analysis-20261006';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
source=json.loads((b/'group-fanout-size-source-20261006/summary.json').read_text());head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/n)==h for n,h in source['files'].items())
accepted=json.loads((b/'accepted-group-fanout-wake-20261006/summary.json').read_text());assert accepted['status']=='GROUP_FANOUT_WAKE_SELECTED_FOR_CONTINUED_OPTIMIZATION'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
def inspect():return json.loads(run(['docker','inspect',*names]))
cs=inspect();before={c['Name']:ident(c) for c in cs};assert before==accepted['runtime']
assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
gws=[next(c for c in cs if c['Name']=='/tinyimx-m21-'+role+'-1') for role in ['gateway-a','gateway-b']]
assert all(c['Image']==accepted['image'] and envmap(c).get('TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE')=='1' and not any(k.startswith('TINYIMX_FAULT_') for k in envmap(c)) for c in gws)
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
assert sha(r/'benchmark/local_capacity/cross_feature_actor.py')=='3f4327755929487d58114876cd87cbc21311a858a72a4d44a23acf8c2d3f08a9'
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:sha(p) for p in cfg.glob('*.json')}
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
ips=[next(v['IPAddress'] for v in c['NetworkSettings']['Networks'].values() if v.get('IPAddress')) for c in gws]
for c in gws:
 for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
  f=line.split()
  if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
   raw=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(raw)[::-1]) if len(raw)==8 else 'ipv6'
   assert peer in ips or peer.startswith('127.'),'Externalclient would invalidate isolatedsize analysis; preserve it'
def sql(q):
 assert q.startswith('SELECT ') and ';' not in q
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','group-size-readonly',q])
def settings():return sql('SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count').strip()
assert settings()=='1\t1\t1\t0\t0'
users=[]
for row in sql('SELECT user_id,username,status FROM im_users WHERE user_id BETWEEN 519800 AND 519950 ORDER BY user_id').splitlines():
 uid,name,status=row.split('\t');uid=int(uid)
 if status=='1' and name==f'm21b500000_{uid-500000:06d}' and uid!=519870:users.append(uid)
assert len(users)>=99;uids=[519870,*users[:99]];assert len(set(uids))==100
assert sql("SELECT COUNT(*) FROM im_groups WHERE name LIKE 'cf-gsize20261006-%'").strip()=='0'
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'IsolatedsameacceptedONruntime real2/16/65/100member ownAPIgroup sizeanalysis;13messages/size with3warm10measured; actualALLrecipient wire identity andSQLstatus3; unchanged3s ACK/deliverydeadline','runtime_before':before,'private_configs_sha256':hashes,'users':uids,'data_scope':'100verifiedselfownedsynthetic users;4newownGroups onlynormalcreate/invite,52newgroupmessages and2327logicalrecipient rows; preserveallgroups/members/rows/outbox/history. Existinggroups/privatefiles untouched. NoSQLwrites/reset/delete/DDL/cacheclear','transport':'Eachuser alternatesfixed2GatewayIPs, originalActor protocol/HB/receiverACK; lowloadserial eachmessage untilallreceived/confirmed, notofferedRPS/10000usercapacity','writes':'Ownnewstage/publictimelines/requestmetrics, privatecurrentlogs only, normalown APIrecords; no product/host/config mutation','source_mechanisms':'Originalclaims perrecipientUPDATE andcoordinator perrecipient completeRPC; ACK+peerreceive sameMID businessordering. Hypotheses to quantify, notexclusivephase attribution','public_group_limit':'Sourcemax500, internalrecipient snapshot safetyguard5000 is notpublicgroupcapacity','rollback':'Closeownclient sockets only; keepallpartialfailures/groups fornextsamefixture iteration, all19IDs/configs/durability preserved','allfeature_extreme_acceptance':False})
sys.path.insert(0,str(r/'benchmark/local_capacity'));from cross_feature_actor import Actor,Run
class CaptureRun(Run):
 def __init__(self,args,out):
  super().__init__(args,out);self.response_times={};self.first_delivery_ns={};self.duplicate_deliveries=0
 def emit(self,event,**kw):
  now=time.monotonic_ns()
  if event=='response':
   if kw['kind']==2051:
    key=(int(kw['body']['message_id']),kw['uid'])
    if key in self.first_delivery_ns:self.duplicate_deliveries+=1
    else:self.first_delivery_ns[key]=now
   else:self.response_times[(kw['uid'],kw['kind'],kw['seq'])]=now
  super().emit(event,**kw)
v=CaptureRun(types.SimpleNamespace(run='gsize20261006',users=[],host='192.168.220.128',port=9000),d)
actors={};groups=[];results=[];messages=[];error=None;start_utc=datetime.datetime.now(datetime.timezone.utc).isoformat()
def preserved():
 assert {c['Name']:ident(c) for c in inspect()}==before and {p.name:sha(p) for p in cfg.glob('*.json')}==hashes and settings()=='1\t1\t1\t0\t0'
def stats(values):
 a=sorted(values);return {'samples':len(a),'mean_ms':sum(a)/len(a),'p50_ms':a[math.ceil(.5*len(a))-1],'max_ms':a[-1],'p99':'NOT_ESTIMATED_10_MESSAGES'}
def interrupt(signum,frame):raise RuntimeError('Ownedsize analysis interrupted '+str(signum))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupt)
try:
 for i,uid in enumerate(uids):actors[uid]=Actor(v,uid,ips[i%2],9000)
 owner=actors[519870]
 save('logged-own-actors.json',{'uids':uids,'connections':100,'routes':{str(uid):('gateway-a' if i%2==0 else 'gateway-b') for i,uid in enumerate(uids)},'no_historical_relation_or_credential_reset':True})
 for size in [2,16,65,100]:
  label='s'+str(size);members=uids[:size]
  group=v.request(owner,'size-'+label+'-create',2023,{'client_operation_id':'cf-gsize20261006-'+label+'-create','name':'cf-gsize20261006-'+label,'join_policy':'invite_only','max_members':size})
  gid=int(group['group']['group_id']);record={'size':size,'group_id':gid,'owner':519870,'members':members,'invites_completed':[]};groups.append(record);save('owned-groups.json',groups)
  for uid in members[1:]:
   v.request(owner,'size-'+label+'-invite-'+str(uid),2035,{'client_operation_id':'cf-gsize20261006-'+label+'-invite-'+str(uid),'group_id':gid,'target_user_id':uid})
   record['invites_completed'].append(uid);save('owned-groups.json',groups)
  reply=v.request(owner,'size-'+label+'-members-exact',2045,{'group_id':gid,'limit':100})
  assert {int(z['user_id']) for z in reply['members']}==set(members) and len(reply['members'])==size and not reply['has_more']
  stage=d/label;stage.mkdir(mode=0o700);rows=[]
  for index in range(13):
   cmid='gsize20261006-'+label+'-'+str(index);content=cmid+'-exact-content'
   body={'group_id':gid,'client_message_id':cmid,'message_type':1,'content':content}
   sent=time.monotonic_ns();seq=owner.send(2049,body);v.emit('request',uid=owner.uid,kind=2049,seq=seq,name='size-'+label+'-send-'+str(index),body=body)
   row={'size':size,'index':index,'measured':index>=3,'seq':seq,'group_id':gid,'client_message_id':cmid,'sent_mono_ns':sent,'expected_recipients':members[1:]};rows.append(row);messages.append(row);save('all-requests.json',messages)
   deadline=time.monotonic()+3
   while (2050,seq) not in owner.replies and (9999,seq) not in owner.replies:
    assert time.monotonic()<deadline,'Original3s senderACK deadline';v.pump(.005)
   assert (9999,seq) not in owner.replies
   ack=owner.replies.pop((2050,seq));row['ack']=ack;row['ack_mono_ns']=v.response_times[(owner.uid,2050,seq)]
   mid=int(ack.get('message_id',0));row['message_id']=mid
   assert ack.get('success') is True and ack.get('result')=='created' and mid>0 and int(ack['group_id'])==gid
   while not all((mid,uid) in v.first_delivery_ns for uid in members[1:]):
    assert time.monotonic()<deadline,'Original3s allrecipientdelivery deadline';v.pump(.005)
   arrivals=[]
   for uid in members[1:]:
    bodies=v.deliveries[(2051,mid,uid)];assert all(z['group_id']==gid and z['from_user_id']==519870 and z['message_type']==1 and z['content']==content for z in bodies),'Exactgroup actualwire identity/content'
    arrivals.append({'recipient':uid,'observed_mono_ns':v.first_delivery_ns[(mid,uid)],'send_to_delivery_ms':(v.first_delivery_ns[(mid,uid)]-sent)/1e6,'duplicate_count':len(bodies)-1})
   row['recipient_arrivals']=arrivals;row['send_to_ack_ms']=(row['ack_mono_ns']-sent)/1e6;row['send_to_first_delivery_ms']=min(z['send_to_delivery_ms'] for z in arrivals);row['send_to_all_delivery_ms']=max(z['send_to_delivery_ms'] for z in arrivals);save('all-requests.json',messages)
   snapshots=[];end=time.monotonic()+8
   while True:
    snapshot_start=time.monotonic_ns();text=sql(f'SELECT recipient_user_id,group_id,delivery_status,attempt_count FROM im_group_message_deliveries WHERE message_id={mid} ORDER BY recipient_user_id');observed=time.monotonic_ns()
    parsed=[list(map(int,z.split('\t'))) for z in text.splitlines()];snapshots.append({'started_mono_ns':snapshot_start,'observed_mono_ns':observed,'rows':parsed});row['confirmation_snapshots']=snapshots;save('all-requests.json',messages)
    assert len(parsed)==size-1 and {z[0] for z in parsed}==set(members[1:]) and all(z[1]==gid for z in parsed),'Exactdurable recipient set'
    if all(z[2]==3 for z in parsed):break
    assert time.monotonic()<end,'Allactual recipientACK durableconfirmation deadline'
    for z in range(20):v.pump(.005)
   row['all_confirmed_snapshot_mono_ns']=observed;row['send_to_all_confirmed_observation_upper_ms']=(observed-sent)/1e6
   assert row['send_to_ack_ms']<=3000 and row['send_to_all_delivery_ms']<=3000
   save('all-requests.json',messages)
   print(json.dumps({'status':'GROUP_SIZE_MESSAGE_RECONCILED','size':size,'index':index,'recipients':size-1,'sender_ack_ms':row['send_to_ack_ms'],'first_delivery_ms':row['send_to_first_delivery_ms'],'all_delivery_ms':row['send_to_all_delivery_ms'],'confirmed_observation_upper_ms':row['send_to_all_confirmed_observation_upper_ms']}),flush=True)
  measured=[z for z in rows if z['measured']];assert len(measured)==10
  result={'status':'GROUP_SIZE_REAL_WIRE_AND_SQL_PASS','size':size,'group_id':gid,'sent':13,'measured':10,'recipients_per_message':size-1,'actual_wire_and_recipient_confirmations':13*(size-1),'sender_ack':stats([z['send_to_ack_ms'] for z in measured]),'first_delivery':stats([z['send_to_first_delivery_ms'] for z in measured]),'all_delivery':stats([z['send_to_all_delivery_ms'] for z in measured]),'confirmed_observation_upper':stats([z['send_to_all_confirmed_observation_upper_ms'] for z in measured]),'all_delivery_measured_max_le_100ms':max(z['send_to_all_delivery_ms'] for z in measured)<=100,'duplicate_wire':sum(z['duplicate_count'] for row in rows for z in row['recipient_arrivals']),'limits':'Serialno-capacity sizeanalysis,10message samples notP99; confirmation isboundedSQLobservation upper notexactlastACKcommit time'}
  results.append(result);(stage/'summary.json').write_text(json.dumps(result,indent=2)+'\n');save('completed-size-cases.json',results);preserved();print(json.dumps(result),flush=True)
 end=time.monotonic()+10
 while any(a.hb_sent!=a.hb_ack for a in v.clients):assert time.monotonic()<end,'Owned100actor Pongdrain';v.pump(.005,heartbeats=False)
 assert all(a.hb_sent and a.hb_sent==a.hb_ack for a in v.clients)
except BaseException as e:
 error=e;save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'completed_sizes':len(results),'partial_messages':len(messages),'allfeature_extreme_acceptance':False,'next':'Analyze exactfirstfailure, do notskip/retry sameCID orrelax originaldeadline'})
finally:
 save('all-requests.json',messages);save('operations.json',v.operations)
 for actor in list(v.clients):actor.close()
 preserved()
 for c in gws:
  with (private/(c['Name'].replace('/','')+'-since-analysis.log')).open('w') as f:subprocess.run(['docker','logs','--timestamps','--since',start_utc,c['Id']],stdout=f,stderr=subprocess.STDOUT,timeout=40,check=True)
 save('preservation-summary.json',{'status':'ALL19_CURRENT_ACCEPTED_INSTANCES_AND_CONFIGS_PRESERVED','runtime':before,'ownclients_closed':True,'all_created_groups_and_history_retained':True,'host_apps_preserved':True,'no_runtime_restart_or_cleanup':True})
if error is not None:raise error
save('summary.json',{'status':'GROUP_FANOUT_REAL_SIZE_ANALYSIS_COMPLETE','head':head,'sizes':results,'groups':groups,'messages':52,'actual_recipient_confirmations':2327,'allfeature_extreme_acceptance':False,'limits':'Selected2/16/65/100members onacceptedON, isolated100ownedconnections,10samples/size; groups keptfornextsamefixture controlled optimization. Publicmax500, no5000group claim'})
PY
