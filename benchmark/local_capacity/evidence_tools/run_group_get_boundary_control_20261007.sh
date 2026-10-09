#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,socket,re,os,signal,sys,types,math
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-get-boundary-control-20261007';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout,stderr=subprocess.STDOUT)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
source=json.loads((b/'group-get-boundary-build-repair-source-20261007/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/p)==h for p,h in source['files'].items())
candidate=json.loads((b/'group-message-runtime-build-20261006/summary.json').read_text())
message_proof=json.loads((b/'group-completion-rpc-build-20261006-attempt6/summary.json').read_text())
assert candidate['images']['message']==message_proof['images']['message']
candidate['probe_elf_sha256']=message_proof['probe_elf_sha256']
confirm=json.loads((b/'group-confirm-coalesce-build-20261006-attempt3/summary.json').read_text())
assert confirm['status']=='GROUP_CONFIRM_COALESCE_NATIVE_AND_IMAGE_PASS' and confirm['checks']==387 and confirm['caller_checks']==49 and confirm['sql_checks']==276 and confirm['rpc_checks']==62 and confirm['only_repo_runtime_object_replaced'] and confirm['no_class_layout_or_protocol_change'] and confirm['no_new_product_threads'] and confirm['original_message_unit_pass']
confirm_audit=json.loads((b/'group-confirm-coalesce-build-20261006-attempt3/audit-before.json').read_text())
assert all(sha(r/p)==h for p,h in confirm_audit['source_sha256'].items() if not p.startswith('docs/') and p not in {'services/repository/MessageRepository.cpp','services/message/service/MessageServiceImpl.cpp','services/rpc/MessageRpcClient.cpp'}) and all(sha(p)==h for p,h in confirm_audit['borrowed_sha256'].items())
candidate['images']['message']={'image_tag':confirm['image_tag'],'image_id':confirm['image_id'],'elf_sha256':confirm['elf_sha256']}

assert candidate['status']=='GROUP_MESSAGE_RUNTIME_NATIVE_AND_IMAGE_PASS' and candidate['checks']==144 and candidate['no_new_product_threads'] and candidate['coordinator_checks_inherited']==164 and candidate['resolver_checks_inherited']==128 and candidate['primitive_checks_inherited']==720 and candidate['inherited_message_rpc_checks']==62 and candidate['original_message_unit_pass'] and candidate['protocol_exact_preservation_checks']==1
build={'gateway_image_id':candidate['images']['gateway']['image_id'],'gateway_elf_sha256':candidate['images']['gateway']['elf_sha256']}
claim_build={'image_id':candidate['images']['message']['image_id'],'message_elf_sha256':candidate['images']['message']['elf_sha256']}
buildaudit=json.loads((b/'group-message-runtime-build-20261006/audit-before.json').read_text())
assert all(sha(r/p)==h for p,h in buildaudit['source_sha256'].items() if not p.startswith('docs/') and p not in {'services/repository/MessageRepository.cpp','services/repository/MessageRepository.h','services/message/service/MessageServiceImpl.cpp','services/rpc/MessageRpcClient.cpp'})
assert all(sha(p)==h for p,h in buildaudit['borrowed_sha256'].items())
boundary=json.loads((b/'group-get-boundary-build-20261007-attempt2/summary.json').read_text())
assert boundary['status']=='GROUP_GET_BOUNDARY_NATIVE_AND_IMAGES_PASS' and boundary['head']==head and boundary['native_checks']==145 and boundary['existing_class_headers_unchanged'] and boundary['protocol_unchanged'] and boundary['gateway_only_rpc_member_changed'] and boundary['message_only_repo_and_impl_objects_changed'] and boundary['other_archive_members_and_borrowed_preserved'] and boundary['original_message_unit_pass']
boundary_audit=json.loads((b/'group-get-boundary-build-20261007-attempt2/audit-before.json').read_text());assert all(sha(r/n)==h for n,h in boundary_audit['source_sha256'].items()) and all(sha(p)==h for p,h in boundary_audit['borrowed_sha256'].items())
build={'gateway_image_id':boundary['images']['gateway']['image_id'],'gateway_elf_sha256':boundary['images']['gateway']['elf_sha256']}
claim_build={'image_id':boundary['images']['message']['image_id'],'message_elf_sha256':boundary['images']['message']['elf_sha256']}
assert message_proof['rpc_checks']==62 and message_proof['protocol_exact_preservation_checks']==1
assert sha(r/'benchmark/local_capacity/cross_feature_actor.py')=='3f4327755929487d58114876cd87cbc21311a858a72a4d44a23acf8c2d3f08a9'
assert sha(r/'benchmark/local_capacity/group_actor_snapshot_cross_feature.py')=='7548899f3a1fb5264656767253701e2a852a944bc7c884127c5bfa94ba3ef8a3'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
def inspect():return json.loads(run(['docker','inspect',*names]))
cs=inspect();before={c['Name']:ident(c) for c in cs}
assert before==json.loads((b/'group-confirm128-control-20261006/restore-summary.json').read_text())['runtime']
roles=['gateway-a','gateway-b','message-service'];gateway_roles=roles[:2];flag='TINYIMX_GROUP_GET_BOUNDARY_TRACE_UID'
original={role:next(c for c in cs if c['Name']=='/tinyimx-m21-'+role+'-1') for role in roles}
assert all(c['Image']=='sha256:5c2645b1e8512bdd3fe68d4fea229a9418a5e115c9d96d636871639dba41a405' and c['State'].get('Health',{}).get('Status')=='healthy' and flag not in envmap(c) and envmap(c).get('TINYIMX_GROUP_FANOUT_COMMIT_WAKE_ENABLE')=='1' and envmap(c).get('TINYIMX_CONVERSATION_UNREAD_BATCH_ENABLE')=='1' and envmap(c).get('TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE')=='1' and 'TINYIMX_REDIS_ACQUIRE_TRACE_ENABLE' not in envmap(c) for role,c in original.items() if role in gateway_roles)
assert original['message-service']['Image']=='sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060' and flag not in envmap(original['message-service'])


assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:sha(p) for p in cfg.glob('*.json')}
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def hostconfig(c):
 value=dict(c['HostConfig'])
 for key in ['Dns','DnsOptions','DnsSearch']:
  if value.get(key) is None:value[key]=[]
 if value.get('Binds') is not None:value['Binds']=sorted(value['Binds'])
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
  if not c['State']['Running']:continue
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
 if role in gateway_roles:assert e.get('TINYIMX_GROUP_FANOUT_ENABLE')=='1'
 for key in ['TINYIMX_GROUP_FANOUT_RECOVERY_MS','TINYIMX_GROUP_FANOUT_BATCH_SIZE','TINYIMX_GROUP_FANOUT_LEASE_MS','TINYIMX_GROUP_FANOUT_ACK_RETRY_MS','TINYIMX_GROUP_FANOUT_FAILURE_RETRY_MS']:assert key not in e
elf_paths={role:'/opt/tinyimx/bin/'+('gateway_demo' if role in gateway_roles else 'message_service_demo') for role in roles}
original_elf={role:run(['docker','exec',c['Id'],'sha256sum',elf_paths[role]]).split()[0] for role,c in original.items()}
assert {original_elf[role] for role in gateway_roles}=={'e68731562d83b3b5c1923d80ed7ad4459d2f5cbc9b8a224e32014626fac04cfc'} and original_elf['message-service']=='2548733766409d282d30f6ffbbbe47f9c12b5d4fa3cacfdcc3d3e47f72582699'
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
(private/'original-inspect.json').write_text(json.dumps(original,indent=2)+'\n')
for mode in ['off','on','restore']:
 wanted={}
 for role,c in original.items():
  if mode=='restore':wanted[role]={'image':c['Image'],'environment':envmap(c)}
  elif role in gateway_roles:wanted[role]={'image':build['gateway_image_id'],'environment':{**envmap(c),'TINYIMX_GROUP_DELIVERY_RECIPIENT_ORDER_ENABLE':'1','TINYIMX_GROUP_FANOUT_PHASE_TRACE_ENABLE':'1','TINYIMX_GROUP_FANOUT_DEFER_COMPLETION_ENABLE':'1','TINYIMX_GROUP_FANOUT_ROUTE_BATCH_ENABLE':'1','TINYIMX_GROUP_FANOUT_PARTIAL_DRAIN_ENABLE':'1','TINYIMX_GROUP_FANOUT_COMPLETION_BATCH_ENABLE':'1' ,'TINYIMX_GROUP_DELIVERY_MESSAGE_RUNTIME_ENABLE':'1','TINYIMX_GROUP_FANOUT_BATCH_SIZE':'128',flag:'519862' if mode=='on' else '0'}}
  else:wanted[role]={'image':claim_build['image_id'],'environment':{**envmap(c),'TINYIMX_GROUP_DELIVERY_CLAIM_BATCH_ENABLE':'1','TINYIMX_GROUP_CONFIRM_BATCH_TRACE_ENABLE':'1','TINYIMX_GROUP_CONFIRM_COALESCE_ENABLE':'1',flag:'519862' if mode=='on' else '0'}}
  if role=='message-service':wanted[role]['healthcheck']={'test':c['Config']['Healthcheck']['Test'] if mode=='restore' else ['CMD','/opt/tinyimx/bin/rpc_readiness_probe','127.0.0.1:50053']}
 path=private/(mode+'.override.json');path.write_text(json.dumps({'services':wanted})+'\n')
 proposed=json.loads(run(base+['-f',str(path),'config','--format','json']))['services']
 for role in roles:assert {k:str(v) for k,v in proposed[role].get('environment',{}).items()}==wanted[role]['environment'] and proposed[role]['command']==original[role]['Config']['Cmd']
 overrides[mode]=(path,wanted)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Same newdiagnosticimages, only traceUID0/519862/519862/0 allthreeServices; Gatewaybatch128/Messagecoalescing1/clientdeferred1/fullotherEnv fixed. No productperformance claim from diagnostic. NumericRID/callerHash samecall client/handler/TLSrepo; <=8/side/sec, absence/unmatched preserved','before':before,'configs_sha256':hashes,'candidate_elf_sha256':build['gateway_elf_sha256'],'candidate_image':build['gateway_image_id'],'message_candidate_image':claim_build['image_id'],'message_candidate_elf_sha256':claim_build['message_elf_sha256'],'tested_flag':flag,'native_checks':145,'original_message_unit_pass':True,'candidate_scope':'ThreeGet-only tracing TUs, defaultOFF; classes/protocol/Queries/returns/deadline/auth/FIFO/lease/durability unchanged. UID519862 ownedfixture, no raw IDs/content/secrets in trace. Log clock capturedbeforeoutput; residual includes logging/transport/admission/scheduler, not pure network','data_scope':'Onlynormalpubliclogin/HB/528uniqueCID existingownGroups57/58/59/60 and100actors;23628actualrecipientconfirmations; fourfresh54op37assert crossfeature chains/filebytes. No SQLwrite/delete/DDL/cacheclear/hostchange beyond ownnormalAPI fixtures','controls':'sameimage/allEnv except UID; batch128/coalescing1/deferred1 fixed; original3secACK/ALL andSQL8sec observation unchanged; allfail/late andunpaired logs retained. 30samplesnotP99, no capacity concurrent','rollback':'Exact original5c2645/be8 fullEnv/Health/HostConfig/Mounts restorefinally; other16/config/durability1/1/1/0/0/hostapps preserved; onlyowned sockets/verifiedchild stop','limits':'Serial diagnosticandfullfunctional regression, trace perturbation assessed OFF/ON. Differentbiasedphase means notsum; allfeature10k50k/soak/fault/TLS/AI OPEN','performance_acceptance':False})
ns={role:run(['docker','exec',c['Id'],'readlink','/proc/self/ns/time']).strip() for role,c in original.items()};ns['guest_host']=os.readlink('/proc/self/ns/time');assert len(set(ns.values()))==1;save('shared-time-namespace.json',ns)
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
from deferred_timeline import DeferredTimeline
native=json.loads((b/'group-deferred-timeline-native-20261006/summary.json').read_text());assert native['status']=='DEFERRED_TIMELINE_INTEGRITY_PASS' and native['checks']>=20 and all(z['pass'] for z in native['assertions'])
for role in roles:
 a=overrides['off'][1][role];z=overrides['on'][1][role]
 assert a['environment'][flag]=='0' and z['environment'][flag]=='519862'
 assert {k:v for k,v in a['environment'].items() if k!=flag}=={k:v for k,v in z['environment'].items() if k!=flag} and a['image']==z['image']
clients=[];current_mode=None;serial=0;error=None;cases=[];shutdown_errors=[]
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
 capture(label+'-before-')
 serial+=1;path,wanted=overrides[mode]
 previous=next(c for c in inspect() if c['Name']==original['message-service']['Name']);assert mode=='restore' or previous['State']['Running']
 save('stop-'+str(serial)+'-audit-before.json',{'operation':'Only exact MessageService SIGTERM20sec; preserve registry/data/files; no external liveclients','message':ident(previous),'expected_exit_zero':previous['Image']==claim_build['image_id']})
 if previous['State']['Running']:
  with (private/(label+'-message-stop.log')).open('w') as f:subprocess.run(['docker','stop','--time','20',previous['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
 stopped=json.loads(run(['docker','inspect',previous['Id']]))[0];assert not stopped['State']['Running']
 save('stop-'+str(serial)+'-summary.json',{'message':ident(previous),'exit_code':stopped['State']['ExitCode'],'finished':stopped['State']['FinishedAt'],'candidate':previous['Image']==claim_build['image_id']})
 if previous['Image']==claim_build['image_id'] and stopped['State']['ExitCode']!=0:
  shutdown_errors.append({'mode':mode,'message':ident(previous),'exit_code':stopped['State']['ExitCode']})
  save('candidate-shutdown-failures.json',shutdown_errors)
  if mode!='restore':raise AssertionError('Candidate Message graceful shutdown failed; restore remains required')
 save('deploy-'+str(serial)+'-audit-before.json',{'operation':'Own authorized threeService controlled recreation','mode':mode,'override_sha256':sha(path),'original_restore_available':True,'no_external_live_clients':True,'other16_unchanged':True})
 with (private/(label+'.log')).open('w') as f:subprocess.run(base+['-f',str(path),'up','-d','--no-deps','--force-recreate','--pull','never',*roles],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
 end=time.monotonic()+60
 while True:
  now=inspect();selected={role:next(c for c in now if c['Name']==original[role]['Name']) for role in roles}
  if all(c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy' for c in selected.values()):break
  assert time.monotonic()<end,'Gateway health timeout';time.sleep(1)
 for role,c in selected.items():
  orig=original[role];assert c['Image']==wanted[role]['image'] and envmap(c)==wanted[role]['environment'] and hostconfig(c)==hostconfig(orig) and c['Mounts']==orig['Mounts'] and c['RestartCount']==0
  for k in ['User','WorkingDir','Cmd','Entrypoint','StopSignal']:assert c['Config'].get(k)==orig['Config'].get(k)
  assert run(['docker','exec',c['Id'],'sha256sum',elf_paths[role]]).split()[0]==(original_elf[role] if mode=='restore' else build['gateway_elf_sha256'] if role in gateway_roles else claim_build['message_elf_sha256'])
  expected_health=orig['Config']['Healthcheck'] if role in gateway_roles else {**orig['Config']['Healthcheck'],'Test':wanted[role]['healthcheck']['test']}
  assert c['Config']['Healthcheck']==expected_health
  if role=='message-service':
   if mode!='restore':
    assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/rpc_readiness_probe']).split()[0]==candidate['probe_elf_sha256']
    assert run(['docker','exec',c['Id'],'/opt/tinyimx/bin/rpc_readiness_probe','127.0.0.1:50053']).strip()=='SERVING'
    top=run(['docker','top',c['Id'],'-eo','pid,ppid,comm']).splitlines()[1:];pid1=c['State']['Pid']
    target=[int(v.split()[0]) for v in top if int(v.split()[1])==pid1 and (pathlib.Path('/proc')/v.split()[0]/'cmdline').read_bytes().split(b'\0')[0].decode()==c['Config']['Cmd'][0]];assert len(target)==1
    masks=[]
    for task in (pathlib.Path('/proc')/str(target[0])/'task').iterdir():
     blocked=int(re.search(r'^SigBlk:\s+(\w+)',(task/'status').read_text(),re.M).group(1),16);masks.append({'tid':int(task.name),'SIGTERM_blocked':bool(blocked&(1<<14)),'SIGINT_blocked':bool(blocked&(1<<1)),'wchan':(task/'wchan').read_text().strip()})
    save(label+'-actual-message-thread-masks.json',masks);waiters=[v for v in masks if v['wchan']=='do_sigtimedwait']
    assert len(waiters)==1 and all((v['SIGTERM_blocked'] and v['SIGINT_blocked']) or v in waiters for v in masks)
   # The audited main calls SetReady(true) only after Registrar.Start succeeds.
   # INFO logs are intentionally filtered by the unchanged WARN configuration.
   service_config=json.loads((cfg/'message.json').read_text())
   assert service_config['zookeeper']['enable'] is True
   save(label+'-message-registration-ready-gate.json',{'container':ident(c),'mode':mode,'gate':'named tinyimx.ready SERVING after synchronous Registrar.Start; bothGateway typedhistory follows before measurements' if mode!='restore' else 'OriginalfullNChealth restored; bothGateway typedhistory probes follow before restore receipt','namedready_source_sha256':sha(r/'examples/message_service_demo.cpp'),'namedready_probe_sha256':candidate['probe_elf_sha256'],'log_level_preserved':service_config['logger']['level'],'not_dependent_on_suppressed_INFO_log':True,'no_business_deadline_change':True})

 current_mode=mode;preserved();save('deploy-'+str(serial)+'-summary.json',{'status':'READY','mode':mode,'containers':{role:ident(c) for role,c in selected.items()}});print(json.dumps({'status':'GATEWAY_CONTROL_READY','mode':mode}),flush=True)
def gateway_ready(case):
 stage=d/('ready-'+case);stage.mkdir(mode=0o700)
 rows=[]
 for gw in [c for c in inspect() if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']]:
  ip=next(v['IPAddress'] for v in gw['NetworkSettings']['Networks'].values() if v.get('IPAddress'))
  v=Run(types.SimpleNamespace(run='ggb1Ready'+case,users=[],host=ip,port=9000),stage);clients.append(v)
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
   anchor=Actor(v,519950,ip,9000);deadline=time.monotonic()+20;consecutive=0
   while consecutive<2:
    assert time.monotonic()<deadline,'Message registration history readiness timeout'
    body={'peer_user_id':519810,'limit':1};start=time.monotonic_ns();seq=anchor.send(2005,body)
    v.emit('request',uid=anchor.uid,name='message-history-registration-readiness-only',kind=2005,seq=seq,body=body)
    v.until(lambda:(2006,seq) in anchor.replies or (9999,seq) in anchor.replies,'message-registration-readiness',seconds=5)
    assert (9999,seq) not in anchor.replies
    reply=anchor.replies.pop((2006,seq));good=reply.get('success') is True
    row={'gateway':gw['Name'],'uid':519950,'seq':seq,'kind':2005,'success':good,'reason':reply.get('reason'),'ms':(time.monotonic_ns()-start)/1e6,'response':reply,'excluded_from_measurement':True};rows.append(row);save('readiness-'+case+'-probes.json',rows)
    if good:assert reply['user_id']==519950 and reply['peer_user_id']==519810 and isinstance(reply['messages'],list)
    else:assert reply.get('reason') in {'message_service_unavailable','message_service_timeout'},'Unexpected Message history readiness failure'
    consecutive=consecutive+1 if good else 0
    if consecutive<2:time.sleep(.2)
  finally:
   for c in list(v.clients):c.close()
   clients.remove(v)
 save('readiness-'+case+'-summary.json',{'status':'BOTH_GATEWAY_GROUP_AND_MESSAGE_HISTORY_REGISTRATION_READY','probes':rows,'startup_failures_retained':sum(not x['success'] for x in rows),'not_capacity_or_measured_requests':True})
def invoke_chain(case,index):
 stage=d/case;args=['/usr/bin/python3',str(r/'benchmark/local_capacity/group_actor_snapshot_cross_feature.py'),'--run','ggb1feat'+case,'--users',*map(str,chosen[index*4:index*4+4]),'--host','192.168.220.128']
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
 x=json.loads((b/('cross-feature-ggb1feat'+case)/'summary.json').read_text());assert code==0 and x['status']=='PASS' and x['operations_completed']==54 and len(x['assertions'])==37 and all(v['pass'] for v in x['assertions'])
 print(json.dumps({'status':'GROUP_CHAIN_PASS','case':case,'operations':54,'checks':37}),flush=True);return x
def stats(values):
 a=sorted(values);return {'samples':len(a),'mean_ms':sum(a)/len(a),'p50_ms':a[math.ceil(.5*len(a))-1],'max_ms':a[-1],'p99':'NOT_ESTIMATED_30_MESSAGES'}
class CaptureRun(Run):
 def __init__(self,args,out,deferred=False):
  super().__init__(args,out);self.response_times={};self.first_delivery_ns={};self.duplicate_deliveries=0;self.deferred=DeferredTimeline(out) if deferred else None
 def checkpoint(self):
  if self.deferred:self.deferred.flush()
 def finalize_evidence(self):
  if self.deferred:
   result=self.deferred.finalize();assert result['status']=='COMPLETE',result
   return result
  p=self.out/'timeline.jsonl';raw=p.read_bytes();return {'status':'ORIGINAL_SYNC_COMPLETE','records':raw.count(bytes([10])),'sha256':hashlib.sha256(raw).hexdigest()}
 def emit(self,event,**kw):
  now=time.monotonic_ns()
  if event=='response':
   if kw['kind']==2051:
    key=(int(kw['body']['message_id']),kw['uid'])
    if key in self.first_delivery_ns:self.duplicate_deliveries+=1
    else:self.first_delivery_ns[key]=now
   else:self.response_times[(kw['uid'],kw['kind'],kw['seq'])]=now
  if self.deferred:self.deferred.emit(event,mono_ns=now,**kw)
  else:super().emit(event,**kw)

fixture_summary=json.loads((b/'group-fanout-size-analysis-20261006/summary.json').read_text())
assert fixture_summary['status']=='GROUP_FANOUT_REAL_SIZE_ANALYSIS_COMPLETE'
fixtures=fixture_summary['groups'];assert [x['group_id'] for x in fixtures]==[57,58,59,60]
uids=fixtures[-1]['members'];assert len(uids)==100 and uids[0]==519870
for x in fixtures:
 assert x['owner']==519870 and x['members']==uids[:x['size']]
 assert sql(f"SELECT owner_user_id,status,max_members FROM im_groups WHERE group_id={x['group_id']}").strip()==f"519870\t1\t{x['size']}"
 assert list(map(int,sql(f"SELECT user_id FROM im_group_members WHERE group_id={x['group_id']} AND status=1 ORDER BY user_id").splitlines()))==sorted(x['members'])
save('unchanged-owned-group-fixtures.json',fixtures)
def interrupted(signum,frame):raise RuntimeError('Ownedclaimcontrol interrupted '+str(signum))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def diagnostic_snapshot():
 started=time.monotonic_ns()
 digest=sql("SELECT DIGEST,DIGEST_TEXT,COUNT_STAR,SUM_TIMER_WAIT,SUM_LOCK_TIME,SUM_ERRORS,SUM_ROWS_AFFECTED,SUM_ROWS_EXAMINED FROM performance_schema.events_statements_summary_by_digest WHERE SCHEMA_NAME=DATABASE() AND DIGEST_TEXT LIKE '%im_group_message_deliveries%' ORDER BY DIGEST")
 status=sql("SELECT VARIABLE_NAME,VARIABLE_VALUE FROM performance_schema.global_status WHERE VARIABLE_NAME IN ('Innodb_data_fsyncs','Innodb_os_log_fsyncs','Innodb_row_lock_time','Innodb_row_lock_waits','Innodb_buffer_pool_wait_free') ORDER BY VARIABLE_NAME")
 return {'started_mono_ns':started,'finished_mono_ns':time.monotonic_ns(),'group-digests':[v.split(chr(9)) for v in digest.splitlines()],'status':[v.split(chr(9)) for v in status.splitlines()]}

def measured_case(case,mode):
 stage=d/case;stage.mkdir(mode=0o700)
 ips=[next(v['IPAddress'] for v in next(c for c in inspect() if c['Name']==original[role]['Name'])['NetworkSettings']['Networks'].values() if v.get('IPAddress')) for role in gateway_roles]
 v=CaptureRun(types.SimpleNamespace(run='ggb1'+case,users=[],host='192.168.220.128',port=9000),stage,deferred=True)
 actors={};results=[];messages=[];started=datetime.datetime.now(datetime.timezone.utc).isoformat();clients.append(v)
 try:
  for i,uid in enumerate(uids):actors[uid]=Actor(v,uid,ips[i%2],9000)
  v.checkpoint()
  owner=actors[519870]
  for fixture in fixtures:
   size=fixture['size'];gid=fixture['group_id'];members=fixture['members'];label='s'+str(size)
   reply=v.request(owner,'same-fixture-'+label+'-members',2045,{'group_id':gid,'limit':100})
   assert {int(z['user_id']) for z in reply['members']}==set(members) and len(reply['members'])==size and not reply['has_more']
   v.checkpoint()
   stage=d/case/label;stage.mkdir(mode=0o700);rows=[]
   diagnostic_before=diagnostic_snapshot();save(case+'-'+label+'-sql-before.json',diagnostic_before)
   for index in range(33):
    cmid='ggb120261006-'+case+'-'+label+'-'+str(index);content=cmid+'-exact-content'
    body={'group_id':gid,'client_message_id':cmid,'message_type':1,'content':content}
    sent=time.monotonic_ns();seq=owner.send(2049,body);v.emit('request',uid=owner.uid,kind=2049,seq=seq,name='size-'+label+'-send-'+str(index),body=body)
    row={'size':size,'index':index,'measured':index>=3,'seq':seq,'group_id':gid,'client_message_id':cmid,'sent_mono_ns':sent,'expected_recipients':members[1:]};rows.append(row);messages.append(row)
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
    row['recipient_arrivals']=arrivals;row['send_to_ack_ms']=(row['ack_mono_ns']-sent)/1e6;row['send_to_first_delivery_ms']=min(z['send_to_delivery_ms'] for z in arrivals);row['send_to_all_delivery_ms']=max(z['send_to_delivery_ms'] for z in arrivals)
    snapshots=[];end=time.monotonic()+8
    while True:
     snapshot_start=time.monotonic_ns();text=sql(f'SELECT recipient_user_id,group_id,delivery_status,attempt_count FROM im_group_message_deliveries WHERE message_id={mid} ORDER BY recipient_user_id');observed=time.monotonic_ns()
     parsed=[list(map(int,z.split('\t'))) for z in text.splitlines()];snapshots.append({'started_mono_ns':snapshot_start,'observed_mono_ns':observed,'rows':parsed});row['confirmation_snapshots']=snapshots
     assert len(parsed)==size-1 and {z[0] for z in parsed}==set(members[1:]) and all(z[1]==gid for z in parsed),'Exactdurable recipient set'
     if all(z[2]==3 for z in parsed):break
     assert time.monotonic()<end,'Allactual recipientACK durableconfirmation deadline'
     for z in range(20):v.pump(.005)
    row['all_confirmed_snapshot_mono_ns']=observed;row['send_to_all_confirmed_observation_upper_ms']=(observed-sent)/1e6
    assert row['send_to_ack_ms']<=3000 and row['send_to_all_delivery_ms']<=3000
    v.checkpoint();save(case+'-all-requests.json',messages)
    print(json.dumps({'status':'GROUP_SIZE_MESSAGE_RECONCILED','size':size,'index':index,'recipients':size-1,'sender_ack_ms':row['send_to_ack_ms'],'first_delivery_ms':row['send_to_first_delivery_ms'],'all_delivery_ms':row['send_to_all_delivery_ms'],'confirmed_observation_upper_ms':row['send_to_all_confirmed_observation_upper_ms']}),flush=True)
   diagnostic_after=diagnostic_snapshot();save(case+'-'+label+'-sql-after.json',diagnostic_after)
   measured=[z for z in rows if z['measured']];assert len(measured)==30
   result={'status':'GROUP_GET_BOUNDARY_REAL_WIRE_AND_SQL_PASS','case':case,'mode':mode,'size':size,'group_id':gid,'sent':33,'measured':30,'recipients_per_message':size-1,'actual_wire_and_recipient_confirmations':33*(size-1),'sender_ack':stats([z['send_to_ack_ms'] for z in measured]),'first_delivery':stats([z['send_to_first_delivery_ms'] for z in measured]),'all_delivery':stats([z['send_to_all_delivery_ms'] for z in measured]),'confirmed_observation_upper':stats([z['send_to_all_confirmed_observation_upper_ms'] for z in measured]),'all_delivery_measured_max_le_100ms':max(z['send_to_all_delivery_ms'] for z in measured)<=100,'duplicate_wire':sum(z['duplicate_count'] for row in rows for z in row['recipient_arrivals']),'limits':'Serialno-capacity sizeanalysis,30message samples notP99; confirmation isboundedSQLobservation upper notexactlastACKcommit time'}
   results.append(result);(stage/'summary.json').write_text(json.dumps(result,indent=2)+'\n');save(case+'-size-results.json',results);preserved();print(json.dumps(result),flush=True)
  end=time.monotonic()+10
  while any(a.hb_sent!=a.hb_ack for a in v.clients):
   assert time.monotonic()<end,'Owned100actor originalPongdrain';v.pump(.005,heartbeats=False)
  assert all(a.hb_sent and a.hb_sent==a.hb_ack for a in v.clients)
  assert sum(x['actual_wire_and_recipient_confirmations'] for x in results)==5907
  return {'case':case,'mode':mode,'sizes':results,'messages':132,'actual_recipient_confirmations':5907,'duplicate_wire':v.duplicate_deliveries,'allfeature_extreme_acceptance':False}
 finally:
  save(case+'-all-requests.json',messages);save(case+'-operations.json',v.operations)
  save(case+'-evidence-summary.json',v.finalize_evidence())
  for actor in list(v.clients):actor.close()
  clients.remove(v);preserved()
  observations=[];boundaries=[]
  for role in gateway_roles:
   c=next(c for c in inspect() if c['Name']==original[role]['Name'])
   text=subprocess.check_output(['docker','logs','--timestamps','--since',started,c['Id']],text=True,stderr=subprocess.STDOUT,timeout=40)
   (private/(case+'-'+role+'-measured.log')).write_text(text)
   for line in text.splitlines():
    if 'group_get_boundary_phase ' in line:
     fields=re.findall(r'([a-z0-9_]+)=(-?\d+)',line.split('group_get_boundary_phase ',1)[1]);row={k:int(v) for k,v in fields};row['service']=role;row['event']='group_get_boundary_phase';row['log_prefix']=line.split('group_get_boundary_phase ',1)[0];boundaries.append(row)
    for event in ['group_fanout_phase','group_delivery_task_phase']:
     if event+' ' in line:
      pairs=re.findall(r'([a-z_]+)=(-?\d+)',line.split(event+' ',1)[1])
      row={key:int(value) for key,value in pairs};row['gateway']=role;row['event']=event;row['log_prefix']=line.split(event+' ',1)[0]
      observations.append(row)
  save(case+'-fanout-phases.json',observations)
  c=next(c for c in inspect() if c['Name']==original['message-service']['Name'])
  text=subprocess.check_output(['docker','logs','--timestamps','--since',started,c['Id']],text=True,stderr=subprocess.STDOUT,timeout=40)
  (private/(case+'-message-measured.log')).write_text(text)
  phases=[]
  for line in text.splitlines():
   if 'group_get_boundary_phase ' in line:
    fields=re.findall(r'([a-z0-9_]+)=(-?\d+)',line.split('group_get_boundary_phase ',1)[1]);row={k:int(v) for k,v in fields};row['service']='message-service';row['event']='group_get_boundary_phase';row['log_prefix']=line.split('group_get_boundary_phase ',1)[0];boundaries.append(row)
   if 'group_confirm_batch_phase ' not in line:continue
   fields=re.findall(r'([a-z_]+)=(-?\d+)',line.split('group_confirm_batch_phase ',1)[1]);row={k:int(v) for k,v in fields};row['log_prefix']=line.split('group_confirm_batch_phase ',1)[0];phases.append(row)
  save(case+'-confirm-phases.json',phases)
  save(case+'-get-boundaries.json',boundaries)
  assert all(z['recipient']==519862 and z['side'] in [1,2,3] for z in boundaries)
  if mode=='off':assert not boundaries
  else:assert boundaries, 'Enabledtrace mustprovide someboundaries'

try:
 for index,(case,mode) in enumerate([('A1','off'),('B1','on'),('B2','on'),('A2','off')]):
  if mode!=current_mode:deploy(mode,'deploy-'+case)
  gateway_ready(case)
  result=measured_case(case,mode)
  result['functional_chain']=invoke_chain(case,index)
  cases.append(result);save('completed-cases.json',cases)
  assert result['duplicate_wire']==0 and len(result['sizes'])==4
  print(json.dumps({'status':'GROUP_GET_BOUNDARY_CASE_RECONCILED','case':case,'mode':mode,'sizes':[{k:x[k] for k in ['size','sender_ack','first_delivery','all_delivery','all_delivery_measured_max_le_100ms']} for x in result['sizes']],'chain_operations':54,'chain_checks':37}),flush=True)
except BaseException as exc:
 error=exc;save('failed.json',{'status':'FAIL','type':type(exc).__name__,'message':str(exc),'completed_cases':len(cases),'allfeature_extreme_acceptance':False,'action':'Preservefailedrequests; restore originalacceptedGateway+Message images/fullEnv; no retries/skip/deadlinerelax'})
finally:
 for v in list(clients):
  for actor in list(v.clients):actor.close()
 clients.clear()
 deploy('restore','restore-accepted-wake');gateway_ready('restored')
 restored=preserved()
 for role,orig in original.items():
  restoredc=next(c for c in restored if c['Name']==orig['Name']);assert restoredc['Image']==orig['Image'] and envmap(restoredc)==envmap(orig) and hostconfig(restoredc)==hostconfig(orig) and restoredc['Mounts']==orig['Mounts'] and restoredc['Config']['Healthcheck']==orig['Config']['Healthcheck']
 save('restore-summary.json',{'status':'ACCEPTED_GATEWAY_AND_MESSAGE_FULL_ENV_RESTORED','runtime':{c['Name']:ident(c) for c in restored},'other16_unchanged':True,'configs_durability_preserved':True,'ownclients_closed':True,'hostapps_preserved':True})
if error is not None:raise error
assert not shutdown_errors,'Candidate shutdown failed after original restoration; raw retained'
assert len(cases)==4
save('summary.json',{'status':'GROUP_GET_BOUNDARY_REAL_ABBA_COMPLETE','head':head,'gateway_image':build['gateway_image_id'],'gateway_elf':build['gateway_elf_sha256'],'message_image':claim_build['image_id'],'message_elf':claim_build['message_elf_sha256'],'server_confirmation_coalescing_fixed':1,'trace_recipient_abba':[0,519862,519862,0],'client_deferred_evidence_fixed':1,'batch_size_fixed':128,'native_checks':boundary['native_checks'],'capture_native_checks_inherited':native['checks'],'cases':cases,'messages':528,'actual_recipient_confirmations':23628,'functional_operations':216,'functional_checks':148,'allfeature_extreme_acceptance':False,'limits':'TraceUID0/519862/519862/0 singlevariable, batch128/coalescing1/clientdeferred1 fixed, samenewimages/allotherEnv. 100connections,30measuredmsgs/size/case notP99 or10k50kcapacity; full54op37assert/failures unchanged. No allfeature extreme claim, DefaulttraceOFF andoriginalacceptedimages/config restored.'})
PY
