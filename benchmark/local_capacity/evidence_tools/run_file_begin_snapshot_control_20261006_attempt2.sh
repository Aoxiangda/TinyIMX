#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,socket,re,os,signal,sys,types,math
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'file-begin-snapshot-control-20261006-attempt2';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
source=json.loads((b/'file-begin-snapshot-control2-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/p)==h for p,h in source['files'].items())
build=json.loads((b/'file-begin-snapshot-build-test-20261006-attempt2/summary.json').read_text());assert build['status']=='FILE_SNAPSHOT_REAL_SQL_AND_IMAGE_PASS' and build['native_total_checks']==150
buildaudit=json.loads((b/'file-begin-snapshot-build-test-20261006-attempt2/audit-before.json').read_text())
assert all(sha(r/p)==h for p,h in buildaudit['source_sha256'].items() if not p.startswith('docs/'))
assert all(sha(p)==h for p,h in buildaudit['borrowed_sha256'].items())
assert sha(r/'benchmark/local_capacity/cross_feature_actor.py')=='3f4327755929487d58114876cd87cbc21311a858a72a4d44a23acf8c2d3f08a9'
assert sha(r/'benchmark/local_capacity/group_actor_snapshot_cross_feature.py')=='7548899f3a1fb5264656767253701e2a852a944bc7c884127c5bfa94ba3ef8a3'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
def inspect():return json.loads(run(['docker','inspect',*names]))
cs=inspect();before={c['Name']:ident(c) for c in cs}
assert before==json.loads((b/'file-begin-snapshot-control-20261006/restore-summary.json').read_text())['runtime']
roles=['file-service'];gateway_roles=[];flag='TINYIMX_FILE_BEGIN_UPLOAD_SNAPSHOT_ENABLE'
original={role:next(c for c in cs if c['Name']=='/tinyimx-m21-'+role+'-1') for role in roles}
assert original['file-service']['Image']==buildaudit['base_image'] and flag not in envmap(original['file-service'])
assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:sha(p) for p in cfg.glob('*.json')}
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def hostconfig(c):
 value=dict(c['HostConfig'])
 if isinstance(value.get('Binds'),list):value['Binds']=sorted(value['Binds'])
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
 ips={v['IPAddress'] for c in cs for v in c['NetworkSettings']['Networks'].values() if v.get('IPAddress')}
 for c in inspect():
  if c['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:continue
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
elf_paths={'file-service':'/opt/tinyimx/bin/file_service_demo'}
original_elf={role:run(['docker','exec',c['Id'],'sha256sum',elf_paths[role]]).split()[0] for role,c in original.items()}
assert original_elf['file-service']=='f1b9f84fc297789dc5fa462129c01fc9d252994b499fbd682e3e2db61ca5c1c1'
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
(private/'original-inspect.json').write_text(json.dumps(original,indent=2)+'\n')
for mode in ['off','on','restore']:
 wanted={}
 for role,c in original.items():
  if mode=='restore':wanted[role]={'image':c['Image'],'environment':envmap(c)}
  else:wanted[role]={'image':build['image_id'],'environment':{**envmap(c),flag:'1' if mode=='on' else '0'}}
 path=private/(mode+'.override.json');path.write_text(json.dumps({'services':wanted})+'\n')
 proposed=json.loads(run(base+['-f',str(path),'config','--format','json']))['services']
 for role in roles:assert {k:str(v) for k,v in proposed[role].get('environment',{}).items()}==wanted[role]['environment'] and proposed[role]['command']==original[role]['Config']['Cmd']
 overrides[mode]=(path,wanted)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Same File ELF OFF/ON/ON/OFF snapshot flag only; eachcase30warm480measured existing Begin across4actors+120parallel reuses+cancel/conflict/ownerchecks; full54op37assert chain real1.8MiBtransfer; exactrestore','before':before,'configs_sha256':hashes,'candidate_elf_sha256':build['file_elf_sha256'],'candidate_image':build['image_id'],'flag':flag,'scope':'OnlyFileService recreation, exactfullotherEnv/health/Cmd/HostConfig/mounts; other18same; noSQLwrites/cleanup/DDL/privatecfg/hostchanges','data':'Normal API newunique ownkeys/uploads/cancel, allrows/files preserved; nooldsocialpair overwrite','deadlines':'Original Run30sec/header/seq/owner checks unchanged; original100ms gate retained; allfailures/startup probes saved; closedloop480not10k50kcapacitypopulationP99','rollback':'Originalinspect/fullEnv override; closeonlyownedactors/verifiedownPGID; nofile deletion/reset/prune','performance_acceptance':False})
ready_identity=sql('SELECT owner_user_id,upload_id FROM im_file_upload_sessions WHERE owner_user_id BETWEEN 519800 AND 519950 ORDER BY upload_id LIMIT 1').strip().split('\t')
assert len(ready_identity)==2
file_ready_uid,file_ready_upload=map(int,ready_identity);assert file_ready_upload>0
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
  if uid in {519870,519872,519950,file_ready_uid} or 519810<=uid<=519859:continue
  if status=='1' and username==f'm21b500000_{uid-500000:06d}' and redis_call('EXISTS','tinyimx:online:'+str(uid))==0:available.append(uid)
 chosen=[]
 for a in available:
  if a in chosen:continue
  for bb in available:
   if bb<=a or bb in chosen or bb-a==1 or (a,bb) in occupied:continue
   chosen.extend([a,bb]);break
  if len(chosen)==16:break
 assert len(chosen)==16 and all(redis_call('EXISTS','tinyimx:online:'+str(uid))==0 for uid in [519870,519872,519950,file_ready_uid])
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
 save('deploy-'+str(serial)+'-audit-before.json',{'operation':'Own authorized oneFileService controlled recreation','mode':mode,'override_sha256':sha(path),'original_restore_available':True,'no_external_live_clients':True,'other18_unchanged':True})
 with (private/(label+'.log')).open('w') as f:subprocess.run(base+['-f',str(path),'up','-d','--no-deps','--force-recreate','--pull','never',*roles],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
 end=time.monotonic()+60
 while True:
  now=inspect();selected={role:next(c for c in now if c['Name']==original[role]['Name']) for role in roles}
  if all(c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy' for c in selected.values()):break
  assert time.monotonic()<end,'Gateway health timeout';time.sleep(1)
 for role,c in selected.items():
  orig=original[role];assert c['Image']==wanted[role]['image'] and envmap(c)==wanted[role]['environment'] and hostconfig(c)==hostconfig(orig) and c['Mounts']==orig['Mounts'] and c['RestartCount']==0
  for k in ['User','WorkingDir','Cmd','Entrypoint','Healthcheck','StopSignal']:assert c['Config'].get(k)==orig['Config'].get(k)
  assert run(['docker','exec',c['Id'],'sha256sum',elf_paths[role]]).split()[0]==(original_elf[role] if mode=='restore' else build['file_elf_sha256'])
 current_mode=mode;preserved();save('deploy-'+str(serial)+'-summary.json',{'status':'READY','mode':mode,'containers':{role:ident(c) for role,c in selected.items()}});print(json.dumps({'status':'FILE_CONTROL_READY','mode':mode}),flush=True)
def gateway_ready(case):
 stage=d/('ready-'+case);stage.mkdir(mode=0o700);rows=[]
 for gw in [c for c in inspect() if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']]:
  ip=next(v['IPAddress'] for v in gw['NetworkSettings']['Networks'].values() if v.get('IPAddress'))
  v=Run(types.SimpleNamespace(run='fsnap2Ready'+case,users=[],host=ip,port=9000),stage);clients.append(v)
  try:
   actor=Actor(v,file_ready_uid,ip,9000);deadline=time.monotonic()+20;consecutive=0
   while consecutive<2:
    assert time.monotonic()<deadline,'File registration readiness timeout'
    start=time.monotonic_ns();seq=actor.send(2055,{'upload_id':file_ready_upload})
    v.emit('request',uid=actor.uid,name='file-readiness-only',kind=2055,seq=seq,body={'upload_id':file_ready_upload})
    v.until(lambda:(2056,seq) in actor.replies or (9999,seq) in actor.replies,'file-registration-readiness',seconds=5)
    assert (9999,seq) not in actor.replies;reply=actor.replies.pop((2056,seq));good=reply.get('success') is True
    rows.append({'gateway':gw['Name'],'uid':actor.uid,'seq':seq,'success':good,'reason':reply.get('reason'),'ms':(time.monotonic_ns()-start)/1e6,'response':reply,'excluded_from_measurement':True});save('readiness-'+case+'-probes.json',rows)
    if good:assert int(reply['session']['upload_id'])==file_ready_upload and int(reply['session']['owner_user_id'])==file_ready_uid
    else:assert reply.get('reason') in {'file_service_unavailable','file_service_timeout'},'Unexpected File readiness failure'
    consecutive=consecutive+1 if good else 0
    if consecutive<2:time.sleep(.2)
  finally:
   for c in list(v.clients):c.close()
   clients.remove(v)
 save('readiness-'+case+'-summary.json',{'status':'BOTH_GATEWAY_FILE_REGISTRATION_READY','probes':rows,'startup_failures_retained':sum(not x['success'] for x in rows),'not_capacity_or_measured_requests':True})
def invoke_chain(case,index):
 stage=d/case;args=['/usr/bin/python3',str(r/'benchmark/local_capacity/group_actor_snapshot_cross_feature.py'),'--run','fsnap2feat'+case,'--users',*map(str,chosen[index*4:index*4+4]),'--host','192.168.220.128']
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
 x=json.loads((b/('cross-feature-gpartialfeat'+case)/'summary.json').read_text());assert code==0 and x['status']=='PASS' and x['operations_completed']==54 and len(x['assertions'])==37 and all(v['pass'] for v in x['assertions'])
 print(json.dumps({'status':'FILE_CHAIN_PASS','case':case,'operations':54,'checks':37}),flush=True);return x
def stats(values):
 a=sorted(values);assert a
 return {'samples':len(a),'mean_ms':sum(a)/len(a),'p50_ms':a[math.ceil(.5*len(a))-1],'sample_p99_ms':a[math.ceil(.99*len(a))-1],'max_ms':a[-1],'limits':'480closedloop4actors, not10k50kcapacitypopulationP99'}
def measure(case,index,mode):
 stage=d/case;stage.mkdir(mode=0o700)
 gateways=[next(v['IPAddress'] for v in c['NetworkSettings']['Networks'].values() if v.get('IPAddress')) for c in inspect() if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']]
 v=Run(types.SimpleNamespace(run='fsnap2'+case,users=chosen[index*4:index*4+4],host=gateways[0],port=9000),stage);clients.append(v)
 rows=[];groups=[];fixtures=[]
 try:
  actors=[Actor(v,uid,gateways[i%2],9000) for i,uid in enumerate(v.args.users)]
  for i,a in enumerate(actors):
   payload=('file-snapshot-control-'+case+str(i)).encode()
   fb={'client_upload_id':'fsnap2-'+case+'-'+str(a.uid),'file_name':'fsnap2-'+case+'.bin','content_type':'application/octet-stream','total_size':len(payload),'checksum_algorithm':'sha256','expected_checksum':hashlib.sha256(payload).hexdigest()}
   made=v.request(a,'new-upload',2053,fb);upload=int(made['session']['upload_id']);fid=int(made['file']['file_id'])
   assert made['result']=='created' and made['file']['owner_user_id']==a.uid and made['session']['owner_user_id']==a.uid
   fixtures.append((a,fb,upload,fid));v.check('fixture-owner-'+str(a.uid),True)
  save(case+'-fixtures.json',[{'uid':a.uid,'request':fb,'upload_id':upload,'file_id':fid} for a,fb,upload,fid in fixtures])
  for i in range(510):
   a,fb,upload,fid=fixtures[i%4];reply=v.request(a,'existing-begin',2053,fb);op=v.operations[-1]
   v.check('exact-reused-'+str(i),reply['result']=='reused' and reply['file']['file_id']==fid and reply['session']['upload_id']==upload and reply['session']['owner_user_id']==a.uid and reply['file']['owner_user_id']==a.uid and reply['session']['status']=='active')
   rows.append({**op,'index':i,'measured':i>=30,'upload_id':upload,'file_id':fid,'response':reply});save(case+'-all-requests.json',rows)
  for a,fb,upload,fid in fixtures:
   conflict=v.request(a,'fingerprint-conflict',2053,{**fb,'file_name':'conflicting-name.bin'},success=False,reason={'idempotency_conflict'})
   v.check('conflict-no-metadata-'+str(a.uid),'file' not in conflict and 'session' not in conflict)
   other=next(x for x in actors if x.uid!=a.uid);hidden=v.request(other,'cross-owner-get',2055,{'upload_id':upload},success=False,reason={'file_upload_not_found'})
   v.check('other-owner-no-metadata-'+str(a.uid),'file' not in hidden and 'session' not in hidden)
  for wave in range(30):
   sent=[]
   for a,fb,upload,fid in fixtures:
    start=time.monotonic_ns();seq=a.send(2053,fb);sent.append((a,seq,start,upload,fid));v.emit('request',uid=a.uid,name='four-parallel-reuse',kind=2053,seq=seq,body=fb)
   v.until(lambda:all((2054,seq) in a.replies or (9999,seq) in a.replies for a,seq,_,_,_ in sent),'parallel-existing-begin')
   for a,seq,start,upload,fid in sent:
    assert (9999,seq) not in a.replies;reply=a.replies.pop((2054,seq))
    v.check('parallel-exact-'+str(wave)+'-'+str(a.uid),reply.get('success') is True and reply['result']=='reused' and reply['session']['upload_id']==upload and reply['file']['file_id']==fid)
    groups.append({'wave':wave,'uid':a.uid,'seq':seq,'observed_upper_ms':(time.monotonic_ns()-start)/1e6,'response':reply});save(case+'-parallel-requests.json',groups)
  for a,fb,upload,fid in fixtures:
   canceled=v.request(a,'cancel-own',2057,{'upload_id':upload});v.check('cancel-states-'+str(a.uid),canceled['session']['status']=='canceled' and canceled['file']['status']=='canceled')
   repeat=v.request(a,'repeat-canceled',2053,fb);v.check('repeat-canceled-'+str(a.uid),repeat['result']=='reused' and repeat['session']['upload_id']==upload and repeat['file']['file_id']==fid and repeat['file']['status']=='canceled' and repeat['session']['status']=='canceled')
   durable=v.request(a,'get-canceled',2055,{'upload_id':upload});v.check('get-canceled-'+str(a.uid),durable['session']['status']=='canceled' and durable['file']['status']=='canceled')
   query='SELECT f.file_id,f.owner_user_id,f.status,u.upload_id,u.owner_user_id,u.status FROM im_files f JOIN im_file_upload_sessions u ON u.file_id=f.file_id WHERE u.upload_id='+str(upload)
   observed=sql(query).strip();(stage/('durable-'+str(upload)+'.tsv')).write_text(observed+'\n');v.check('durable-owner-states-'+str(a.uid),observed==f'{fid}\t{a.uid}\t5\t{upload}\t{a.uid}\t4')
  deadline=time.monotonic()+10
  while any(x.hb_sent!=x.hb_ack for x in v.clients):assert time.monotonic()<deadline;v.pump(.02,heartbeats=False)
  v.check('heartbeats-drained',all(x.hb_sent and x.hb_sent==x.hb_ack for x in v.clients))
  measured=stats([z['ms'] for z in rows if z['measured']])
  result={'status':'FILE_SNAPSHOT_REAL_ENTRY_PASS','case':case,'mode':mode,'existing_begin':measured,'original100ms_sample_p99_gate':measured['sample_p99_ms']<=100,'warm_samples':30,'parallel_samples':len(groups),'parallel_observation_upper':stats([z['observed_upper_ms'] for z in groups]),'assertions':len(v.assertions),'requests':len(v.operations)+len(groups),'allfeature_extreme_acceptance':False}
  save(case+'-measure-summary.json',result);print(json.dumps(result),flush=True);return result
 finally:
  save(case+'-all-requests.json',rows);save(case+'-parallel-requests.json',groups);save(case+'-operations.json',v.operations);save(case+'-assertions.json',v.assertions)
  for c in list(v.clients):c.close()
  clients.remove(v)
def signal_abort(sig,frame):raise RuntimeError('Ownfile control interrupted '+str(sig))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,signal_abort)
try:
 for index,(case,mode) in enumerate([('A1','off'),('B1','on'),('B2','on'),('A2','off')]):
  if mode!=current_mode:deploy(mode,case+'-deploy')
  gateway_ready(case);x=measure(case,index,mode);x['functional_chain']=invoke_chain(case,index);cases.append(x);save('completed-cases.json',cases);preserved()
except BaseException as e:
 error=e;save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'completed_cases':len(cases),'current_mode':current_mode});raise
finally:
 for v in list(clients):
  for c in list(v.clients):c.close()
 clients.clear()
 deploy('restore','restore');gateway_ready('restored')
 restored=preserved()
 for role,c in original.items():
  restoredc=next(z for z in restored if z['Name']==c['Name']);assert restoredc['Image']==c['Image'] and envmap(restoredc)==envmap(c) and hostconfig(restoredc)==hostconfig(c) and restoredc['Mounts']==c['Mounts']
 save('restore-summary.json',{'status':'ORIGINAL_FILE_SERVICE_FULL_ENV_RESTORED','runtime':{c['Name']:ident(c) for c in restored},'other18_unchanged':True,'configs_durability_preserved':True,'ownclients_closed':True,'hostapps_preserved':True})
if error is None:
 assert len(cases)==4
 save('summary.json',{'status':'FILE_SNAPSHOT_REAL_ABBA_COMPLETE','head':head,'file_image':build['image_id'],'file_elf':build['file_elf_sha256'],'cases':cases,'measured_existing_requests':1920,'parallel_reuses':480,'functional_operations':216,'functional_checks':148,'allfeature_extreme_acceptance':False,'limits':'SameFileELF onlysnapshotflag;480measuredclosedloop/case+4concurrent120/case+fullchains not10k50kcapacity'})
 print('FILE_SNAPSHOT_REAL_ABBA_COMPLETE',flush=True)
PY
