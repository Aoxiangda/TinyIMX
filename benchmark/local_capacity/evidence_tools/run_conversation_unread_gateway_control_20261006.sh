#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,socket,re,os,signal,sys,types,math
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'conversation-unread-batch-gateway-run-20261006';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
source=json.loads((b/'conversation-unread-batch-run-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/p)==h for p,h in source['files'].items())
build=json.loads((b/'conversation-unread-batch-build-20261006-attempt2/summary.json').read_text());assert build['status']=='CONVERSATION_UNREAD_BATCH_BUILD_PASS'
buildaudit=json.loads((b/'conversation-unread-batch-build-20261006-attempt2/audit-before.json').read_text());assert all(sha(p)==h for p,h in buildaudit['source_sha256'].items())
pre=json.loads((b/'conversation-unread-batch-runtime-preflight-20261006/summary.json').read_text());selection=pre['selection']
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
def inspect():return json.loads(run(['docker','inspect',*names]))
cs=inspect();before={c['Name']:ident(c) for c in cs};assert before==pre['runtime']
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
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Two Gateway sameELF OFF/ON/ON/OFF real50conversation closed-loop controls then ON crossfeature chain; exact original image/env/HostConfig/mounts restore after any result','before':before,'configs_sha256':hashes,'candidate_elf_sha256':build['gateway_elf_sha256'],'candidate_image':build['gateway_image_id'],'flag':flag,'fixture':selection,'data_changes':'Only existing verified offline synthetic users,50absent bidirectional friend/request/message pairs: normal friend create/accept then50private sends with realreceiverACK; mark one own dialog read; persist all test history. Four separate fresh owned actors normal friend/read/group/file chains. No SQLwrites/reset/delete/cleanup','controls':'100measured50itempages percase after10warmups; fullsameJSON fingerprint/order/unread validation, all rawtimes saved, original3s socket deadline. Serialclosedloop notcapacity/RPS/50k acceptance. No AI inference duringtiming','rollback':'Private originalinspect/logs/configSHA and explicitrestoreoverride; close onlyown sockets/processes. Keepallnewbusinessfixtures/failure/code/evidence/Git; other17IDs and durability1/1/1/0/0 preserved','user_apps':'Preserved','performance_acceptance':False})
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
def fixture():
 v=new_run('culistfixture26',d/'fixture');u=selection['anchor'];anchor=Actor(v,u,'192.168.220.128',9000);messages=[]
 try:
  for p in selection['peers']:
   peer=Actor(v,p,'192.168.220.128',9000)
   made=v.request(anchor,'own-friend-create-'+str(p),2011,{'to_user_id':p,'request_message':v.prefix})
   v.request(peer,'own-friend-accept-'+str(p),2015,{'request_id':int(made['request_id'])})
   text=v.prefix+'-content-'+str(p);cmid=v.prefix+'-'+str(p)
   ack=v.request(peer,'own-private-'+str(p),2001,{'to':u,'text':text,'client_message_id':cmid})
   mid=int(ack['message_id']);assert ack['stored_persistent'] is True and ack['from']==p and ack['to']==u and mid>0
   v.until(lambda:(2019,mid,u) in v.deliveries,'Own actual delivered '+str(p))
   assert v.deliveries[(2019,mid,u)][0]['text']==text
   messages.append({'peer':p,'message_id':mid,'text':text,'client_message_id':cmid})
   (v.out/'messages-created.json').write_text(json.dumps(messages,indent=2)+'\n')
   drain(v);peer.close()
  uids=','.join(str(x['message_id']) for x in messages)
  v.authoritative('all50-actual-receiverACKs',f'SELECT COUNT(*) FROM im_private_messages WHERE message_id IN({uids}) AND to_user_id={u} AND delivery_status=1','50')
  p=messages[0]['peer'];v.request(anchor,'mark-one-own-dialog-read',2003,{'peer_user_id':p})
  v.authoritative('own-read-durable',f"SELECT delivery_status FROM im_private_messages WHERE message_id={messages[0]['message_id']}",'2')
  reply=v.request(anchor,'50dialog-ready',2007,{'limit':50})
  expected_peers=[x['peer'] for x in sorted(messages,key=lambda x:x['message_id'],reverse=True)]
  assert [x['peer_user_id'] for x in reply['conversations']]==expected_peers and reply['limit']==50 and reply['user_id']==u
  assert all(x['unread_count']==(0 if x['peer_user_id']==p else 1) for x in reply['conversations'])
  drain(v);expected=reply
  save('fixed-page.json',{'response':expected,'sha256':hashlib.sha256(json.dumps(expected,sort_keys=True,separators=(',',':')).encode()).hexdigest(),'new_messages':messages,'all50_real_ACKs':True,'one_read_other49unread':True})
  return expected
 finally:
  (v.out/'operations.json').write_text(json.dumps(v.operations,indent=2)+'\n');close(v)
def resource(label):
 save(label,{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'monotonic_ns':time.monotonic_ns(),'meminfo':pathlib.Path('/proc/meminfo').read_text(),'guest_stat':pathlib.Path('/proc/stat').read_text(),'cpu_pressure':pathlib.Path('/proc/pressure/cpu').read_text(),'io_pressure':pathlib.Path('/proc/pressure/io').read_text()})
def measure(name,expected):
 v=new_run(name,d/name);raw=[];u=selection['anchor'];start=time.monotonic_ns();c=Actor(v,u,'192.168.220.128',9000)
 try:
  resource(name+'-resources-before.json')
  for i in range(110):
   response=v.request(c,'conversation50-'+str(i),2007,{'limit':50})
   assert response==expected,'Full fixed conversation metadata/order/unread changed'
   if i>=10:raw.append(v.operations[-1]['ms']);(v.out/'latencies-ms.json').write_text(json.dumps(raw)+'\n')
  # Same wire entry covers cap/default/invalid limit and actor fencing.
  for limit,count in [(1,1),(20,20),(0,20),(100,50)]:
   reply=v.request(c,'limit-'+str(limit),2007,{'limit':limit,'user_id':selection['peers'][0]})
   assert reply['user_id']==u and reply['limit']==count and reply['conversations']==expected['conversations'][:count]
  for limit in [-1,"50",1.5]:v.request(c,'invalid-limit-'+str(limit),2007,{'limit':limit},success=False,reason={'invalid_limit'})
  drain(v);resource(name+'-resources-after.json');preserved()
  vals=sorted(raw);assert len(raw)==100
  x={'case':name,'mode':current_mode,'pages':100,'page_size':50,'warmup_pages':10,'mean_ms':sum(raw)/len(raw),'p50_ms':vals[49],'p99_ms':vals[98],'max_ms':vals[-1],'incorrect_pages':0,'timeouts':0,'wall_seconds':(time.monotonic_ns()-start)/1e9,'full_response_equal':True,'case_folder':name,'limits':'Single persistent TCP actor serialclosedloop unloaded endpoint; 100samples. Not capacity/openloop/users/soak acceptance'}
  (v.out/'summary.json').write_text(json.dumps(x,indent=2)+'\n');cases.append(x);save('completed-cases.json',cases);print(json.dumps(x),flush=True)
 finally:
  (v.out/'operations.json').write_text(json.dumps(v.operations,indent=2)+'\n');close(v)
def stopchild():
 if child is not None and child.poll() is None:
  proc=pathlib.Path(f'/proc/{child.pid}')
  assert proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(child.pid)==child.pid and proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
  save('stop-owned-actor-audit-before.json',{'pid':child.pid,'start_ticks':ticks,'cmdline_pgid_verified':True,'operation':'Stop only own timed-out crossfeature child group'})
  os.killpg(child.pid,signal.SIGTERM)
  try:child.wait(timeout=5)
  except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=5)
def interrupt(signum,frame):raise RuntimeError('Own run interrupted '+str(signum))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupt)
try:
 deploy('off','deploy-A1');expected=fixture();measure('single-A1',expected)
 deploy('on','deploy-B1');measure('batch-B1',expected);measure('batch-B2',expected)
 deploy('off','deploy-A2');measure('single-A2',expected)
 deploy('on','deploy-functional')
 args=['python3',str(actor_path),'--run','cuall20261006','--users',*map(str,selection['cross_users']),'--host','192.168.220.128','--port','9000'];assert not (b/'cross-feature-cuall20261006').exists()
 save('functional-audit-before.json',{'operation':'Four fresh owned actors actual all existing actor chains, candidateON; no backgroundcapacity/AI inference','argv':args,'source_sha256':actor_sha,'expected_scope':'49operations/35assertions coversfriend/private/history/read/conversation/profile,all13groupcontrols+real3receiverfanout+idempotence/mute/auth,3filecontrols+actualstreamchecksumresume,HB; notall33/scale/TLS/offline/fault'})
 with (private/'functional.log').open('w') as f:
  child=subprocess.Popen(args,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);ticks=pathlib.Path(f'/proc/{child.pid}/stat').read_text().rsplit(')',1)[1].split()[19]
  save('functional-own-process.json',{'pid':child.pid,'pgid':child.pid,'start_ticks':ticks,'argv':args})
  code=child.wait(timeout=240);assert code==0,'Own crossfeature actor failed'
 functional=json.loads((b/'cross-feature-cuall20261006/summary.json').read_text());assert functional['status']=='PASS' and functional['operations_completed']==49 and len(functional['assertions'])==35 and all(x['pass'] for x in functional['assertions'])
 save('functional-result.json',functional);preserved()
except BaseException as e:
 error=e;save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'completed_cases':cases,'capacity_acceptance':False})
finally:
 try:stopchild()
 finally:
  for v in list(clients):close(v)
  save('restore-audit-before.json',{'operation':'Exact original twoGateway image/env/cmd/HostConfig/mounts restore, other17preserved','override_sha256':sha(overrides['restore'][0]),'keepallhistory_and_failures':True})
  deploy('restore','restore-original')
  restore={'status':'ORIGINAL_GATEWAYS_RESTORED','runtime':{c['Name']:ident(c) for c in inspect() if c['Name'] in {v['Name'] for v in original.values()}},'other17_ids_images_started_preserved':True,'config_hashes_preserved':True,'original_ENV_CMD_HostConfig_mounts_binary_verified':True,'durability':'1/1/1/0/0'}
  save('restore-summary.json',restore);print(json.dumps(restore),flush=True)
if error is not None:raise error
assert len(cases)==4
offs=[x for x in cases if x['mode']=='off'];ons=[x for x in cases if x['mode']=='on']
x={'status':'CONVERSATION_UNREAD_GATEWAY_CONTROL_COMPLETE','head':head,'same_elf_ABBA_cases':cases,'allON_means_better_than_allOFF':max(x['mean_ms'] for x in ons)<min(x['mean_ms'] for x in offs),'allON_p99_better_than_allOFF':max(x['p99_ms'] for x in ons)<min(x['p99_ms'] for x in offs),'functional_operations':functional['operations_completed'],'functional_checks':len(functional['assertions']),'restore':restore,'performance_acceptance':False,'limits':'Endpoint bounded50page and crossfeature functional proof; no10k50kmixed/TLS/overload/offline/soak allfeature capacity acceptance'}
save('summary.json',x);print(json.dumps(x,indent=2))
PY
