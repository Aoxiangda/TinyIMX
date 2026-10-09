#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,time,os,signal,re,socket,sys,types,math
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-actor-snapshot-endpoint-control-20261006-attempt4';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def envmap(c):return dict(v.split('=',1) for v in c['Config']['Env'] if '=' in v)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
source=json.loads((b/'group-readiness-sigwait-control-source-20261006/summary.json').read_text());head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/n)==h for n,h in source['files'].items())
build=json.loads((b/'rpc-readiness-build-20261006-attempt2/summary.json').read_text());assert build['status']=='RPC_READINESS_FIVE_SERVICE_NATIVE_AND_GROUP_IMAGE_PASS' and build['native_checks']==97
core=json.loads((b/'group-actor-snapshot-source-20261006/summary.json').read_text())
assert all(sha(r/n)==h for n,h in core['files'].items() if n.startswith('services/') or n.endswith('readonly_test.cpp'))
pre=json.loads((b/'group-actor-snapshot-preflight-20261006-attempt2/summary.json').read_text())
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
def inspect():return json.loads(run(['docker','inspect',*names]))
cs=inspect();before={c['Name']:ident(c) for c in cs}
expected_runtime=json.loads((b/'group-endpoint-unavailable-analysis-20261006/summary.json').read_text())['whole_runtime_identity']
previous_control=json.loads((b/'group-actor-snapshot-endpoint-control-20261006-attempt3/restore-summary.json').read_text())
assert previous_control['other18_preserved'] and previous_control['configs_fullEnv_mounts_HostConfig_originalHealthcheck_preserved']
expected_runtime['/tinyimx-m21-group-service-1']=previous_control['group'];assert before==expected_runtime
def hostconfig(c):
 value=dict(c['HostConfig'])
 # Normalize only optional DNS array representation, not actual DNS values.
 for key in ['Dns','DnsOptions','DnsSearch']:
  if value.get(key) is None:value[key]=[]
 return value

role='group-service';original=next(c for c in cs if c['Name']=='/tinyimx-m21-group-service-1')
flag='TINYIMX_GROUP_ACTOR_SNAPSHOT_ENABLE';assert flag not in envmap(original) and original['Image']==pre['group_image']
assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:sha(p) for p in cfg.glob('*.json')}
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
def sql(q):
 assert q.startswith('SELECT ') and ';' not in q
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','group-control-readonly',q])
def settings():return sql('SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count').strip()
assert settings()=='1\t1\t1\t0\t0'
assert sql('SELECT owner_user_id,status FROM im_groups WHERE group_id=26').strip()=='519870\t1'
assert sql('SELECT COUNT(*) FROM im_group_members WHERE group_id=26 AND user_id=519950').strip()=='0'
def no_live():
 gws=[c for c in inspect() if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']]
 ips={v['IPAddress'] for c in gws for v in c['NetworkSettings']['Networks'].values()}
 for c in gws:
  for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
   f=line.split()
   if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
    v=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(v)[::-1]) if len(v)==8 else 'ipv6'
    if peer not in ips and not peer.startswith('127.'):return False
 return True
assert no_live(),'Refuse interrupt active Gateway users'
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')]
basecfg=json.loads(run(base+['config','--format','json']))['services'][role]
assert original['Config']['Cmd']==basecfg['command'] and all(envmap(original).get(k)==str(v) for k,v in basecfg.get('environment',{}).items())
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
(private/'original-inspect.json').write_text(json.dumps(original,indent=2)+'\n')
overrides={}
for mode in ['off','on','restore']:
 wanted={'image':original['Image'] if mode=='restore' else build['group_image_id'],'environment':envmap(original) if mode=='restore' else {**envmap(original),flag:'1' if mode=='on' else '0'}}
 wanted['healthcheck']={'test':original['Config']['Healthcheck']['Test'] if mode=='restore' else ['CMD','/opt/tinyimx/bin/rpc_readiness_probe','127.0.0.1:50054']}
 path=private/(mode+'.override.json');path.write_text(json.dumps({'services':{role:wanted}})+'\n')
 proposed=json.loads(run(base+['-f',str(path),'config','--format','json']))['services'][role]
 assert {k:str(v) for k,v in proposed.get('environment',{}).items()}==wanted['environment'] and proposed['command']==original['Config']['Cmd']
 overrides[mode]=(path,wanted)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'OneGroup sameELF OFF/ON/ON/OFF actualfixedGetGroup/members read800measured+80warm and54op37assertion fullfeaturechains percase; restoreoriginalGroup afteranyresult','head':head,'runtime_before':before,'private_configs_sha256':hashes,'original_group_elf_sha256':pre['group_elf_sha256'],'candidate_group_image':build['group_image_id'],'candidate_group_elf_sha256':build['group_elf_sha256'],'nativechecks':3856,'readiness_nativechecks':97,'cross_original_sha256':sha(r/'benchmark/local_capacity/cross_feature_actor.py'),'cross_wrapper_sha256':sha(r/'benchmark/local_capacity/group_actor_snapshot_cross_feature.py'),'candidate_scope':'OnlyGroup readiness image/strictsnapshotflag/namedhealthcheck; candidate threads inherit SIGTERM, stop onlyexactGroup then verifiedreadyGate; preserve allotherEnv/Cmd/HostConfig/mounts andother18 exactinstances','data_scope':'Existingfixedgid26/owner519870 andnonmember519950 readonly requests; fournewselfownedsynthetic4actor sets only normalpublicfriend/private/read/group/file/disband/cancel; keepallrows/files/history andfailedsamples; no DDL/DML/delete/reset','rollback':'Exact private originalinspect/restoreoverride includingoriginalTCPhealthcheck; ownclientsclose, capture beforeandafter scopedGroupStop, restore38dca/noSnapshotflag; other18/config/durabilityunchanged','performance_acceptance':False,'limits':'Serial endpointmicrocontrol andfunctionalchains, not20k50kallfeature/P99capacityproof'})
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
 assert len(chosen)==16 and all(redis_call('EXISTS','tinyimx:online:'+str(uid))==0 for uid in [519870,519950])
finally:f.close();ss.close()
save('actor-selection-readonly.json',{'users':chosen,'sets':[chosen[i:i+4] for i in range(0,16,4)],'offline_and_username_status_pair_empty':True,'SQL_writes':False})
sys.path.insert(0,str(r/'benchmark/local_capacity'));from cross_feature_actor import Actor,Run
clients=[];current_mode=None;serial=0;attempted=False;error=None;results=[];expected={};negative_expected={}
def preserved():
 now=inspect();assert all(c['Name']==original['Name'] or ident(c)==before[c['Name']] for c in now)
 assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in now)
 assert {p.name:sha(p) for p in cfg.glob('*.json')}==hashes and settings()=='1\t1\t1\t0\t0';return now
def capture(label):
 c=next(c for c in inspect() if c['Name']==original['Name'])
 with (private/(label+'.log')).open('w') as f:subprocess.run(['docker','logs','--timestamps',c['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=35)
def gateway_ready(case):
 stage=d/('ready-'+case);stage.mkdir(mode=0o700)
 rows=[]
 for gw in [c for c in inspect() if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']]:
  ip=next(v['IPAddress'] for v in gw['NetworkSettings']['Networks'].values() if v.get('IPAddress'))
  v=Run(types.SimpleNamespace(run='gsready'+case,users=[],host=ip,port=9000),stage);clients.append(v)
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

def deploy(mode,label):
 global current_mode,serial
 end=time.monotonic()+20
 while not no_live():assert time.monotonic()<end,'Refuse liveuser interruption';time.sleep(.5)
 capture(label+'-before');path,wanted=overrides[mode];serial+=1
 previous=next(c for c in inspect() if c['Name']==original['Name'])
 assert previous['State']['Running']
 save('stop-'+str(serial)+'-audit-before.json',{'operation':'Only exact ownGroup process SIGTERM Docker20s grace, preserve allfiles/data; no externalconnectedclients','group':ident(previous),'grace_seconds':20,'expected_exit_zero':previous['Image']==build['group_image_id']})
 with (private/(label+'-stop.log')).open('w') as f:subprocess.run(['docker','stop','--time','20',previous['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
 stopped=json.loads(run(['docker','inspect',previous['Id']]))[0];assert not stopped['State']['Running'];capture(label+'-after-stop')
 stop={'group':ident(previous),'exit_code':stopped['State']['ExitCode'],'finished':stopped['State']['FinishedAt'],'candidate':previous['Image']==build['group_image_id']}
 save('stop-'+str(serial)+'-summary.json',stop)
 if stop['candidate']:assert stop['exit_code']==0,'Candidate graceful shutdown failed'
 save('deploy-'+str(serial)+'-audit-before.json',{'mode':mode,'operation':'OnlyGroup scopedrecreation withreadyexactrestore','override_sha256':sha(path),'no_live_external_clients':True})
 with (private/(label+'.log')).open('w') as f:subprocess.run(base+['-f',str(path),'up','-d','--no-deps','--force-recreate','--pull','never',role],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
 end=time.monotonic()+60
 while True:
  c=next(c for c in inspect() if c['Name']==original['Name'])
  if c['State']['Running'] and c['State'].get('Health',{}).get('Status')=='healthy':break
  assert time.monotonic()<end,'Grouphealth timeout';time.sleep(1)
 assert c['Image']==wanted['image'] and envmap(c)==wanted['environment'] and hostconfig(c)==hostconfig(original) and c['Mounts']==original['Mounts'] and c['RestartCount']==0
 for k in ['User','WorkingDir','Cmd','Entrypoint','StopSignal']:assert c['Config'].get(k)==original['Config'].get(k)
 assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/group_service_demo']).split()[0]==(pre['group_elf_sha256'] if mode=='restore' else build['group_elf_sha256'])
 expected_health={**original['Config']['Healthcheck'],'Test':wanted['healthcheck']['test']};assert c['Config']['Healthcheck']==expected_health
 if mode!='restore':
  assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/rpc_readiness_probe']).split()[0]==build['probe_elf_sha256']
  assert run(['docker','exec',c['Id'],'/opt/tinyimx/bin/rpc_readiness_probe','127.0.0.1:50054']).strip()=='SERVING'
  top=run(['docker','top',c['Id'],'-eo','pid,ppid,comm']).splitlines()[1:];pid1=c['State']['Pid']
  target=[int(v.split()[0]) for v in top if int(v.split()[1])==pid1 and (pathlib.Path('/proc')/v.split()[0]/'cmdline').read_bytes().split(b'\0')[0].decode()==c['Config']['Cmd'][0]];assert len(target)==1
  masks=[]
  for p in (pathlib.Path('/proc')/str(target[0])/'task').iterdir():
   blocked=int(re.search(r'^SigBlk:\s+(\w+)',(p/'status').read_text(),re.M).group(1),16);masks.append({'tid':int(p.name),'SIGTERM_blocked':bool(blocked&(1<<14)),'SIGINT_blocked':bool(blocked&(1<<1)),'wchan':(p/'wchan').read_text().strip()})
  save(label+'-actual-thread-masks.json',masks)
  waiters=[v for v in masks if v['wchan']=='do_sigtimedwait']
  assert len(waiters)==1,'Exactly one dedicated sigwait thread'
  assert all((v['SIGTERM_blocked'] and v['SIGINT_blocked']) or v in waiters for v in masks),'Ordinary workers must block signals; only actual sigwait waiter may unblock while sleeping'
  save(label+'-thread-mask-summary.json',{'ordinary_workers':len(masks)-1,'ordinary_workers_all_blocked':True,'dedicated_sigwait_threads':1,'waiter_kernel_wchan':'do_sigtimedwait'})
 current_mode=mode;preserved();save('deploy-'+str(serial)+'-summary.json',{'status':'READY','mode':mode,'group':ident(c)});print(json.dumps({'status':'GROUP_CONTROL_READY','mode':mode}),flush=True)
def invoke_chain(case,index):
 stage=d/case;args=['/usr/bin/python3',str(r/'benchmark/local_capacity/group_actor_snapshot_cross_feature.py'),'--run','gsv4feat'+case,'--users',*map(str,chosen[index*4:index*4+4]),'--host','192.168.220.128']
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
 x=json.loads((b/('cross-feature-gsv4feat'+case)/'summary.json').read_text());assert code==0 and x['status']=='PASS' and x['operations_completed']==54 and len(x['assertions'])==37 and all(v['pass'] for v in x['assertions'])
 print(json.dumps({'status':'GROUP_CHAIN_PASS','case':case,'operations':54,'checks':37}),flush=True);return x
def stats(values):
 a=sorted(values);return {'samples':len(a),'mean_ms':sum(a)/len(a),'p50_ms':a[math.ceil(.5*len(a))-1],'p99_ms':a[math.ceil(.99*len(a))-1],'max_ms':a[-1]}
def endpoint(case):
 stage=d/case;stage.mkdir(mode=0o700);v=Run(types.SimpleNamespace(run='gsread'+case,users=[],host='192.168.220.128',port=9000),stage);clients.append(v)
 own=Actor(v,519870,'192.168.220.128',9000);negative=Actor(v,519950,'192.168.220.128',9000);rows=[]
 for name,kind,body in [('group-get',2025,{'group_id':26}),('group-members',2045,{'group_id':26,'limit':50})]:
  initial=v.request(own,name+'-baseline',kind,body)
  if name not in expected:expected[name]=initial;save('fixed-'+name+'.json',initial)
  assert initial==expected[name],'Fixedwholepage changed'
  for i in range(10):assert v.request(own,name+'-warm',kind,body)==expected[name]
  begin=len(v.operations)
  for i in range(100):assert v.request(own,name+'-measured',kind,body)==expected[name]
  values=[x['ms'] for x in v.operations[begin:]];assert len(values)==100
  rows.append({'name':name,'type':kind,'metrics':stats(values),'full_response_equal':True,'raw_ms':values})
 for name,kind,body in [('outsider-group-get',2025,{'group_id':26}),('outsider-group-members',2045,{'group_id':26,'limit':50}),('injected-group-get',2025,{'group_id':26,'actor_user_id':519870}),('injected-group-members',2045,{'group_id':26,'limit':50,'actor_user_id':519870})]:
  response=v.request(negative,name,kind,body,success=False)
  if name not in negative_expected:negative_expected[name]=response
  assert response==negative_expected[name]
 end=time.monotonic()+10
 while any(c.hb_sent!=c.hb_ack for c in v.clients):assert time.monotonic()<end,'OwnPongdrain';v.pump(.01,heartbeats=False)
 hbs=[{'uid':c.uid,'sent':len(c.hb_sent),'ack':len(c.hb_ack)} for c in v.clients];assert all(x['sent']>0 and x['sent']==x['ack'] for x in hbs)
 for c in list(v.clients):c.close()
 clients.remove(v)
 x={'status':'PASS','case':case,'metrics':rows,'negative_complete_responses':negative_expected,'heartbeats':hbs,'operations':v.operations,'all_fixed_whole_responses_equal':True,'scope':'200measuredread/20warm/4negatives percase, persistentTCP singleauthorizedactor; notpopulationcapacity'}
 (stage/'summary.json').write_text(json.dumps(x,indent=2)+'\n');return x
def interrupted(n,f):raise RuntimeError('OwnGroupControlInterrupted '+str(n))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
try:
 for index,(case,mode) in enumerate([('A1','off'),('B1','on'),('B2','on'),('A2','off')]):
  attempted=True;deploy(mode,'deploy-'+case);gateway_ready(case);ep=endpoint(case);chain=invoke_chain(case,index);preserved()
  results.append({'case':case,'mode':mode,'endpoint':ep,'cross':chain});save('completed-cases.json',results)
except BaseException as e:error=e;save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'completed_cases':[x['case'] for x in results],'allpartialretained':True})
finally:
 for v in clients:
  for c in list(v.clients):c.close()
 if attempted:deploy('restore','restore-original-group')
 now=preserved();restore={'status':'ORIGINAL_GROUP_RESTORED_SNAPSHOT_CANDIDATE_STOPPED','group':ident(next(c for c in now if c['Name']==original['Name'])),'other18_preserved':True,'configs_fullEnv_mounts_HostConfig_originalHealthcheck_preserved':True,'HostConfig_comparison':'OnlyoptionalDNSnull/emptyarraynormalized; allotherfieldsandactualDNSvaluesexact','durability':'1/1/1/0/0','allpartialfixtureandevidence_retained':True}
 save('restore-summary.json',restore);print(json.dumps(restore),flush=True)
if error is not None:raise error
x={'status':'GROUP_ACTOR_ENDPOINT_ABBA_AND_FULLCHAIN_COMPLETE','head':head,'cases':results,'nativechecks':3856,'readiness_nativechecks':97,'fixed_measured_requests':800,'fixed_warm_requests':80,'negative_operations':16,'cross_operations':216,'cross_assertions':148,'restore':restore,'performance_acceptance':False,'limits':'Actualserialendpoint ABBA and4real chains only, no10k–50k allfunction/scalability/soak acceptance'}
save('summary.json',x);print(json.dumps({'status':x['status'],'fixed_measured_requests':800,'cross_operations':216,'cross_assertions':148,'metrics':[{ 'case':a['case'],'byfunction':[{'name':v['name'],'metrics':v['metrics']} for v in a['endpoint']['metrics']]} for a in results],'performance_acceptance':False},indent=2))
PY
