#!/usr/bin/env python3
"""SHA-bound actual group delivery companion inside unchanged original10k steady."""

import pathlib,json,subprocess,time,datetime,hashlib,sys,types,math,re,os
from cross_feature_actor import Actor,Run
os.umask(0o077)
r=pathlib.Path('/home/jackson7/projects/TinyIMX_publish');b=r/'.local/codex'
assert len(sys.argv)==4
case_arg,bg_arg,out_arg=sys.argv[1:]
assert case_arg in ['mixed-A1','mixed-B1','mixed-B2','mixed-A2']
case=case_arg.split('-')[1];d=b/'group-fanout-wake-mixed10k-20261006';out=pathlib.Path(out_arg)
assert out==d/case_arg/'group-openloop' and not out.exists()
bg=pathlib.Path(bg_arg);assert bg==b/('capacity-gfwm10'+case)
audit=json.loads((bg/'audit-before.json').read_text());assert audit['scenario']['users']==10000 and audit['scenario']['duration']==60 and audit['scenario']['rate']==100
barrier_start=int((bg/'control/start_ns').read_text());barrier_end=barrier_start+60_000_000_000
assert not (bg/'control/abort').exists() and time.monotonic_ns()<barrier_start
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
def inspect():return json.loads(run(['docker','inspect',*names]))
cs=inspect();before={c['Name']:ident(c) for c in cs}
roles=['gateway-a','gateway-b'];flag='TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE'
original={role:next(c for c in cs if c['Name']=='/tinyimx-m21-'+role+'-1') for role in roles}
current_mode='on' if case.startswith('B') else 'off'
assert all(c['Image']=='sha256:5c2645b1e8512bdd3fe68d4fea229a9418a5e115c9d96d636871639dba41a405' and envmap(c).get(flag)==('1' if current_mode=='on' else '0') for c in original.values())
clients=[]
def save(n,x):(out/n).write_text(json.dumps(x,indent=2)+'\n')
def sql(q):
 assert q.startswith('SELECT ') and ';' not in q
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','group-wake-mixed-readonly',q])
def preserved():
 assert {c['Name']:ident(c) for c in inspect()}==before and not (bg/'control/abort').exists()
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
 stage=out;stage.mkdir(mode=0o700)
 v=CaptureRun(types.SimpleNamespace(run='gfwm10'+case,users=[],host='192.168.220.128',port=9000),stage);clients.append(v)
 current={role:next(c for c in inspect() if c['Name']==original[role]['Name']) for role in roles}
 ips=[next(x['IPAddress'] for x in current[role]['NetworkSettings']['Networks'].values() if x.get('IPAddress')) for role in roles]
 rows=[];summary=None;namespace='gfwm10-20261006-'+case
 assert re.fullmatch(r'gfwm10-20261006-[AB][12]',namespace)
 assert sql(f"SELECT COUNT(*) FROM im_group_messages WHERE client_message_id LIKE '{namespace}-%'").strip()=='0'
 def write(): (stage/'requests.json').write_text(json.dumps(rows,indent=2)+'\n');(stage/'wire-responses.json').write_text(json.dumps(v.responses,indent=2)+'\n')
 try:
  actors=[Actor(v,uid,ip,9000) for uid,ip in zip([519870,519872],ips)]
  resource(case+'-resources-before.json');start=barrier_start+5_000_000_000
  assert time.monotonic_ns()<start,'Group companion login missed original window; never shift schedule'
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
  until=time.monotonic()+10
  while any(a.hb_sent!=a.hb_ack for a in v.clients):assert time.monotonic()<until,'RealPongdrain';v.pump(.005,heartbeats=False)
  assert all(a.hb_sent and a.hb_sent==a.hb_ack for a in v.clients)
  measured=[x for x in rows if x['measured']];assert len(measured)==60
  assert all(barrier_start<=x['sent_mono_ns']<=x['ack_mono_ns']<barrier_end and barrier_start<=x['delivery_mono_ns']<barrier_end for x in rows),'Each actualsend/ACK/delivery must remain inside original steady'
  resource(case+'-resources-after.json');preserved()
  summary={'status':'GROUP_WAKE_ACTUAL_DELIVERY_CASE_PASS','case':case,'mode':current_mode,'measured':60,'warm':10,'offered_rate_per_second':2,'ack':stats([x['send_to_ack_ms'] for x in measured]),'delivery':stats([x['send_to_delivery_ms'] for x in measured]),'scheduled_delivery':stats([x['scheduled_to_delivery_ms'] for x in measured]),'scheduled_ack':stats([x['scheduled_to_ack_ms'] for x in measured]),'ack_to_delivery':stats([x['ack_to_delivery_ms'] for x in measured]),'send_lag':stats([x['send_lag_ms'] for x in measured]),'duplicate_wire_deliveries':sum(x['duplicate_deliveries'] for x in rows),'sent':70,'positive_ack':70,'actual_wire_received':70,'exact_durable_recipient_confirmed':70,'negative_requests':0,'permission_negatives':'Covered by simultaneous full54operation chain; listactor519950 never evicted','idempotence_reused':2,'timeouts':0,'skipped':0,'late_ack_gt_3s':0,'late_delivery_gt_3s':0,'confirmation_snapshot':'batched after measurement; not exact permessage confirmation latency','background_users':10000,'every_send_ack_delivery_inside_original_steady':True,'background_private_rate':100,'background_list_rate':20,'limits':'Two actualgroup members fixedto separateGatewayIPs within unchanged10000private ring100/s +publicNGINX50page20/s+fullfeaturechain;60samples at2/s, notallgroup sizes/50kcapacity'}
  (stage/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary),flush=True);return summary
 finally:
  write();(stage/'operations.json').write_text(json.dumps(v.operations,indent=2)+'\n');close(v)

try:
 result=measure(case);save('companion-result.json',{'status':'GROUP_WAKE_MIXED10K_COMPANION_COMPLETE','case':case,'background':str(bg),'window_start_ns':barrier_start,'window_end_ns':barrier_end,'result':result})
except BaseException as e:
 if out.exists():save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'background':str(bg)})
 raise
