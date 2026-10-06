#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,socket,re,os,signal,sys,types,math
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'accepted-group-fanout-wake-20261006';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
source=json.loads((b/'group-fanout-wake-accept-source-20261006/summary.json').read_text())
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
assert before==json.loads((b/'group-fanout-wake-mixed10k-20261006/restore-summary.json').read_text())['runtime']
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

mixed=json.loads((b/'group-fanout-wake-mixed10k-20261006/summary.json').read_text());endpoint=json.loads((b/'group-fanout-wake-endpoint-control-20261006/summary.json').read_text())
assert mixed['status']=='GROUP_FANOUT_WAKE_MIXED10K_ABBA_COMPLETE' and len(mixed['cases'])==4 and mixed['private_messages']==24000 and mixed['full50pages']==4800 and mixed['group_messages_confirmed']==280 and mixed['cross_operations']==216 and mixed['cross_assertions']==152
assert all(all(c['performance_gates'].values()) for c in mixed['cases'] if c['mode']=='on')
assert all(c['cross_operations']==54 and c['cross_assertions']==38 and c['reconciliation']['sent']==c['reconciliation']['positive_ack']==c['reconciliation']['db_rows']==c['reconciliation']['confirmed']==6000 and c['group_actual_delivery']['exact_durable_recipient_confirmed']==70 for c in mixed['cases'])
assert endpoint['allON_delivery_p99_better_than_allOFF'] and endpoint['allON_delivery_means_better_than_allOFF']
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Select sameELF commitwakeON validated18native/280endpointmessages+10kmixed24000private/4800fullpages/280actualgroup/216ops152checks, verifypostrecreation registration/profile/fixedpage/oneownactualgroup; keepselectedrunning aftersuccess, restoreafteranyfailure','runtime_before':before,'configs_sha256':hashes,'candidate_image':build['gateway_image_id'],'candidate_elf':build['gateway_elf_sha256'],'original_restore_override_sha256':sha(overrides['restore'][0]),'source_sha256':source['files'],'data_scope':'Read only existinggroup26/fixedpage/profile; oneunique normalgroup26message fromowner519870 toverifiedmember519872 andrealACK; noSQLwrites/deletes/DDL/cleanup. Retainallhistory/evidence','runtime_scope':'Only2GWimage andstrictwake1; fullotherEnv/Health/Cmd/HostConfig/mounts/other17 exactinstances andMySQLdouble1 unchanged','rollback':'Privateoriginalinspect/fullEnvrestore override, sourcefixed5c2645 buildmanifest andoriginala2bbactualELF retained. Ownsocketsclose andrestore ifsmoke fails','allfeature_extreme_acceptance':False,'limits':'Selected10kmixedrates+2membergroup, no20k50klargegroup/AI/filecapacity/TLS/fault/soak claim'})
sys.path.insert(0,str(r/'benchmark/local_capacity'));from cross_feature_actor import Actor,Run
clients=[];current_mode=None;serial=0;error=None
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

try:
 deploy('on','select-validated-on');gateway_ready('accepted')
 stage=d/'functional-smoke';stage.mkdir(mode=0o700)
 v=Run(types.SimpleNamespace(run='gfwaccepted26',users=[],host='192.168.220.128',port=9000),stage);clients.append(v)
 active={role:next(cc for cc in inspect() if cc['Name']==original[role]['Name']) for role in roles}
 ips=[next(z['IPAddress'] for z in active[role]['NetworkSettings']['Networks'].values() if z.get('IPAddress')) for role in roles]
 a=Actor(v,519870,ips[0],9000);bb=Actor(v,519872,ips[1],9000);listed=Actor(v,519950,ips[0],9000)
 for actor in [a,bb,listed]:
  reply=v.request(actor,'profile-after-selected',2021,{})
  assert reply['profile']['user_id']==actor.uid
 expected=json.loads((b/'conversation-unread-batch-gateway-run-20261006/fixed-page.json').read_text())['response']
 assert v.request(listed,'exact50page-after-selected',2007,{'limit':50})==expected
 cmid='gfwaccepted20261006';assert sql(f"SELECT COUNT(*) FROM im_group_messages WHERE from_user_id=519870 AND client_message_id='{cmid}'").strip()=='0'
 body={'group_id':26,'client_message_id':cmid,'message_type':1,'content':'gfwaccepted20261006-real-content'}
 ack=v.request(a,'actual-crossgateway-group-after-selected',2049,body);mid=int(ack['message_id']);assert ack['result']=='created' and mid>0
 v.until(lambda:(2051,mid,bb.uid) in v.deliveries,'Actualaccepted group recipient',seconds=3)
 assert v.deliveries[(2051,mid,bb.uid)][0]['content']==body['content'] and v.deliveries[(2051,mid,bb.uid)][0]['from_user_id']==a.uid
 v.authoritative('accepted-actual-receiver-ACK',f'SELECT delivery_status FROM im_group_message_deliveries WHERE message_id={mid} AND recipient_user_id=519872 AND group_id=26','3',seconds=8)
 end=time.monotonic()+10
 while any(actor.hb_sent!=actor.hb_ack for actor in v.clients):assert time.monotonic()<end,'Postselect Pongdrain';v.pump(.005,heartbeats=False)
 assert all(actor.hb_sent and actor.hb_sent==actor.hb_ack for actor in v.clients)
 save('postselect-smoke.json',{'status':'PASS','whole_fixed50page_equal':True,'profiles_exact':3,'one_crossGateway_group_message':mid,'actual_wire_and_recipient_SQL_status3':True,'allPongs_drained':True,'performance_sample_claim':False})
except BaseException as e:
 error=e;save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'selected_acceptance':False})
finally:
 for v in list(clients):
  (v.out/'operations.json').write_text(json.dumps(v.operations,indent=2)+'\n')
  for actor in list(v.clients):actor.close()
 if error is not None:
  save('restore-audit-before.json',{'operation':'Exacta2bb/fullEnvrollback afterfailed selection','override_sha256':sha(overrides['restore'][0])})
  deploy('restore','restore-after-failure');save('restore-summary.json',{'runtime':{cc['Name']:ident(cc) for cc in inspect()},'other17_fullconfigs_anddurability_preserved':True})
if error is not None:raise error
now=preserved();target={cc['Name']:ident(cc) for cc in now if cc['Name'] in {o['Name'] for o in original.values()}}
assert len(target)==2 and all(envmap(cc).get(flag)=='1' and envmap(cc).get('TINYIMX_CONVERSATION_UNREAD_BATCH_ENABLE')=='1' and envmap(cc).get('TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE')=='1' for cc in now if cc['Name'] in target)
result={'status':'GROUP_FANOUT_WAKE_SELECTED_FOR_CONTINUED_OPTIMIZATION','head':head,'image':build['gateway_image_id'],'image_tag':build['gateway_image_tag'],'gateway_elf_sha256':build['gateway_elf_sha256'],'flag':flag,'flag_value':'1','runtime':{cc['Name']:ident(cc) for cc in now},'gateways':target,'other17_exact_preserved':True,'configs_fullEnv_health_Cmd_HostConfig_mounts_anddurability_preserved':True,'accepted_scope':'SameELF selected10000mixedprivate100/s+full50page20/s+twoactualgroupmembers2/s+complete54opchains','previous_image':'sha256:a2bb65215bfc85246b946a8c0850e134cd3144d078e5b56c376676c2cd8d1cfa','rollback_override_sha256':sha(overrides['restore'][0]),'allfeature_extreme_acceptance':False,'next':'Quantifylargegroupclaim/dispatch/complete/receiverACK andremaining20kfailures, AI/filedomaincapacities'}
save('summary.json',result);print(json.dumps(result,indent=2),flush=True)
PY
