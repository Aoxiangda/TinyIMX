#!/usr/bin/env python3
"""Real two-Gateway session/TTL intersections on two verified synthetic users."""
import datetime,hashlib,json,os,pathlib,select,socket,struct,subprocess,time,traceback
R=pathlib.Path('/home/jackson7/projects/TinyIMX_publish');B=R/'.local/codex'
OUT=B/'online-maintenance-session-control-20261005';UIDS=(519890,519892)
HEADER=struct.Struct('!IHHHHII');MAGIC=0x54494D58
def command(args):return subprocess.check_output(args,text=True,timeout=20)
def emit(event,**data):
 with (OUT/'timeline.jsonl').open('a') as f:f.write(json.dumps({'event':event,'mono_ns':time.monotonic_ns(),'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),**data})+'\n')
checks=[]
def check(name,passed,**details):
 checks.append({'name':name,'pass':bool(passed),'details':details});(OUT/'checks.json').write_text(json.dumps(checks,indent=2)+'\n');assert passed,name
class Redis:
 def __init__(self,host,config):
  self.socket=socket.create_connection((host,config['port']),3);self.socket.settimeout(3);self.stream=self.socket.makefile('rb')
  if config.get('password'):assert self.call('AUTH',config['password'])==b'OK'
  if config.get('db',0):assert self.call('SELECT',config['db'])==b'OK'
 def reply(self):
  line=self.stream.readline(1024);assert line.endswith(b'\r\n'),'OwnRedisProtocol'
  if line[:1]==b'+':return line[1:-2]
  if line[:1]==b':':return int(line[1:-2])
  if line[:1]==b'$':
   n=int(line[1:-2]);assert -1<=n<=1048576
   if n==-1:return None
   value=self.stream.read(n);assert len(value)==n and self.stream.read(2)==b'\r\n';return value
  raise RuntimeError('OwnRedisReplyRejected')
 def call(self,*args):
  values=[str(a).encode() if not isinstance(a,bytes) else a for a in args];wire=b'*'+str(len(values)).encode()+b'\r\n'
  for value in values:wire+=b'$'+str(len(value)).encode()+b'\r\n'+value+b'\r\n'
  self.socket.sendall(wire);return self.reply()
 def key(self,uid):assert uid in UIDS;return 'tinyimx:online:'+str(uid)
 def state(self,uid):
  raw=self.call('GET',self.key(uid));ttl=self.call('PTTL',self.key(uid));record=json.loads(raw) if raw is not None else None
  if record is not None:assert record['user_id']==uid and record['gateway_id'] and record['connection_name']
  return {'record':record,'raw':raw.decode() if raw is not None else None,'pttl_ms':ttl}
 def expire_own(self,uid,expected):
  before=self.state(uid);check('TTL-audit-current-owner-'+str(uid),before['raw']==expected['raw'] and before['record']['user_id']==uid)
  audit=OUT/('ttl-mutation-before-'+str(uid)+'.json');assert not audit.exists()
  audit.write_text(json.dumps({'operation':'CAS EXPIRE1 on this exact own active synthetic session, no DEL','uid':uid,'key':self.key(uid),'before':before,'guard':'AtomicGET whole-record equality; preserve any competing record','rollback':'Normal heartbeat restore or own socket offline cleanup; no other records/settings/files'},indent=2)+'\n')
  lua="if redis.call('GET',KEYS[1])~=ARGV[1] then return 0 end return redis.call('EXPIRE',KEYS[1],1)"
  check('TTL-CAS-applied-'+str(uid),self.call('EVAL',lua,1,self.key(uid),expected['raw'])==1)
 def close(self):self.stream.close();self.socket.close()
class Client:
 def __init__(self,run,uid,endpoint,label):
  self.run,self.uid,self.label=run,uid,label;self.seq=100;self.buffer=bytearray();self.replies={};self.hbs={};self.hb_ack=set();self.expected_close=False;self.closed=False
  self.socket=socket.create_connection(endpoint,3);self.socket.settimeout(3);run.clients.append(self)
  seq=self.send(1001,{'username':f'm21b500000_{uid-500000:06d}','password':os.environ.get('TINYIMX_BENCH_PASSWORD','123456')})
  run.until(lambda:(1002,seq) in self.replies,'login-'+label)
  reply=self.replies[(1002,seq)];check('login-owner-'+label,reply.get('success') is True and reply.get('user_id')==uid)
 def send(self,kind,body):
  self.seq+=1;raw=json.dumps(body,separators=(',',':')).encode();self.socket.sendall(HEADER.pack(MAGIC,1,kind,0,0,self.seq,len(raw))+raw)
  emit('request',uid=self.uid,label=self.label,kind=kind,seq=self.seq,body={k:v for k,v in body.items() if k!='password'})
  if kind==9001:self.hbs[self.seq]=time.monotonic_ns()
  return self.seq
 def receive(self):
  raw=self.socket.recv(65536)
  if not raw:
   check('expected-EOF-'+self.label,self.expected_close);self.close();emit('expected-EOF',uid=self.uid,label=self.label);return
  self.buffer.extend(raw)
  while len(self.buffer)>=20:
   magic,ver,kind,flags,reserved,seq,n=HEADER.unpack(self.buffer[:20]);assert magic==MAGIC and ver==1 and flags==reserved==0 and n<=1048576
   if len(self.buffer)<20+n:return
   body=json.loads(self.buffer[20:20+n]) if n else {};del self.buffer[:20+n];emit('response',uid=self.uid,label=self.label,kind=kind,seq=seq,body=body)
   if kind==9001:
    assert seq in self.hbs and seq not in self.hb_ack;self.hb_ack.add(seq);self.run.pong_samples.append((time.monotonic_ns()-self.hbs[seq])/1e6)
   else:self.replies[(kind,seq)]=body
 def heartbeat(self,count=1):
  seqs=[self.send(9001,{}) for _ in range(count)];self.run.until(lambda:all(s in self.hb_ack for s in seqs),'heartbeat-'+self.label);return seqs
 def close(self):
  if not self.closed:self.closed=True;self.socket.close()
class Run:
 def __init__(self,redis):self.redis=redis;self.clients=[];self.pong_samples=[]
 def pump(self,seconds=.01):
  live=[c for c in self.clients if not c.closed];ready,_,_=select.select([c.socket for c in live],[],[],seconds)
  for c in live:
   if c.socket in ready:c.receive()
 def until(self,predicate,name,seconds=5):
  end=time.monotonic()+seconds
  while not predicate():assert time.monotonic()<end,'OwnTimeout:'+name;self.pump()
 def wait(self,seconds):
  end=time.monotonic()+seconds
  while time.monotonic()<end:self.pump(min(.01,max(0,end-time.monotonic())))
 def owner(self,uid):
  state=self.redis.state(uid);emit('own-presence',uid=uid,state=state);assert state['record'] is not None;return state
 def missing(self,uid):return self.redis.state(uid)['record'] is None
 def watch_missing(self,uid,name):
  self.until(lambda:self.missing(uid),name,5);samples=[]
  for _ in range(10):self.wait(.1);state=self.redis.state(uid);samples.append(state);check(name+'-sample-'+str(len(samples)),state['record'] is None)
  emit('offline-watch',uid=uid,name=name,samples=samples,duration_seconds=1.0)
def main():
 assert OUT.is_dir() and not (OUT/'audit-before.json').exists();cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 deployment=json.loads((B/'online-maintenance-gateway-deployment-20261005/summary.json').read_text());assert deployment['status']=='ONLINE_MAINTENANCE_GATEWAYS_READY'
 names=command(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(command(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 def ip(name):
  c=next(c for c in cs if c['Name']=='/'+name);ips=[v['IPAddress'] for v in c['NetworkSettings']['Networks'].values() if v.get('IPAddress')];assert len(ips)==1;return ips[0]
 for c in cs:
  if c['Name'] in deployment['gateway_ids']:
   assert c['Id']==deployment['gateway_ids'][c['Name']] and c['Image']==deployment['image'];env=dict(x.split('=',1) for x in c['Config']['Env']);assert env.get('TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE')=='1'
 endpoints=[(ip('tinyimx-m21-gateway-'+side+'-1'),json.loads((cfg/('gateway-'+side+'.json')).read_text())['server']['port']) for side in ['a','b']]
 jsoncfg=json.loads((cfg/'gateway-a.json').read_text());redis=None;run=None;failure=None
 try:
  query='SELECT user_id,username,status FROM im_users WHERE user_id IN(519890,519892)'
  rows=command(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-session-readonly',query]);assert set(rows.splitlines())=={f'{u}\tm21b500000_{u-500000:06d}\t1' for u in UIDS}
  redis=Redis(ip('tinyimx-m21-redis-1'),jsoncfg['redis']);initial={str(uid):redis.state(uid) for uid in UIDS};assert all(s['record'] is None for s in initial.values()),'PreserveExistingActorPresence'
  (OUT/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Real twoGateway synthetic session/TTL crossings','uids':UIDS,'verified_users':rows.splitlines(),'initial_own_keys':initial,'endpoints':endpoints,'script_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'source_head':command(['git','rev-parse','HEAD']).strip(),'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')},'writes':'Ownnormal logins/HBs/connectioncloses andexactCAS EXPIRE1 for twoown currentrecordfixtures; no DEL/SQLwrite/otherrecords/globalsettings','limits':'Real controlled session samples and watchduration, no deterministic live racefrequency or fullcapacity/P99 claim. Ownclosedburst clientbytes not guaranteed serveraccepted','credential_handling':'PrivateRedis/password onlyRAM, neverevidence/argv','rollback':'Closeonlyown sockets; normaloffline responsibility/naturalTTL, retainallraw andbusinesshistory'},indent=2)+'\n')
  run=Run(redis);a=Client(run,UIDS[0],endpoints[0],'A-old');old=run.owner(UIDS[0]);redis.expire_own(UIDS[0],old);run.until(lambda:run.missing(UIDS[0]),'ownTTL-expiry',3);a.heartbeat();run.until(lambda:redis.state(UIDS[0])['record'] is not None,'missing-restore',5);restored=run.owner(UIDS[0]);check('missing-restored-same-session',restored['record']['gateway_id']==old['record']['gateway_id'] and restored['record']['connection_name']==old['record']['connection_name'] and restored['pttl_ms']>110000)
  a.expected_close=True;b=Client(run,UIDS[0],endpoints[0],'A-new');new=run.owner(UIDS[0]);check('sameGW-new-connection',new['record']['connection_name']!=old['record']['connection_name'] and new['record']['gateway_id']==old['record']['gateway_id']);run.until(lambda:a.closed,'oldsameGWclose',5);b.heartbeat();run.wait(.15);check('sameGW-oldcleanup-preserves-new',redis.state(UIDS[0])['raw']==new['raw'])
  c=Client(run,UIDS[0],endpoints[1],'B-takeover');remote=run.owner(UIDS[0]);check('crossGW-takeover',remote['record']['gateway_id']!=new['record']['gateway_id']);b.heartbeat(8);run.wait(.15);check('oldGW-heartbeats-preserve-new-owner',redis.state(UIDS[0])['raw']==remote['raw']);b.close();run.wait(.25);check('oldGW-offline-preserves-new-owner',redis.state(UIDS[0])['raw']==remote['raw']);c.heartbeat(8);c.close();run.watch_missing(UIDS[0],'new-owner-offline-no-resurrection')
  d=Client(run,UIDS[1],endpoints[1],'B-expired-close');current=run.owner(UIDS[1]);redis.expire_own(UIDS[1],current);run.until(lambda:run.missing(UIDS[1]),'second-ownTTL-expiry',3)
  for _ in range(32):d.send(9001,{})
  d.close();emit('intentional-close-with-HB-burst',uid=UIDS[1],client_writes=32,server_accepted_count='Not asserted');run.watch_missing(UIDS[1],'expired-session-burst-close-no-resurrection')
  check('actual-Pong-samples',len(run.pong_samples)>=17,samples=len(run.pong_samples));emit('Pong-samples',milliseconds=run.pong_samples,limits='Idlefunctional samples only, no capacityP99')
 except BaseException as error:failure={'type':type(error).__name__,'message':str(error),'traceback':traceback.format_exc()};(OUT/'failure.json').write_text(json.dumps(failure,indent=2)+'\n')
 finally:
  if run is not None:
   for client in run.clients:client.close()
  if redis is not None:redis.close()
  summary={'status':'FAIL' if failure else 'ONLINE_MAINTENANCE_REAL_SESSION_PASS','checks':checks,'failure':failure,'uids':UIDS,'live_gateway_sessions_tested':True,'full_feature_capacity_acceptance':False,'limits':'Controlled realTTL/restoration/replacement/crossGateway/oldHB/offline samples only, no allfunctions/load50k/racefrequency/P99 claim'};(OUT/'result.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
 return 2 if failure else 0
if __name__=='__main__':raise SystemExit(main())
