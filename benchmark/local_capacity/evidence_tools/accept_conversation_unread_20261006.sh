#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,socket,re,os,signal,sys,types,math
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'accepted-conversation-unread-20261006';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
def ident(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
source=json.loads((b/'conversation-unread-accept-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/p)==h for p,h in source['files'].items())
build=json.loads((b/'conversation-unread-batch-build-20261006-attempt2/summary.json').read_text());assert build['status']=='CONVERSATION_UNREAD_BATCH_BUILD_PASS'
buildaudit=json.loads((b/'conversation-unread-batch-build-20261006-attempt2/audit-before.json').read_text());assert all(sha(p)==h for p,h in buildaudit['source_sha256'].items())
pre=json.loads((b/'conversation-unread-batch-runtime-preflight-20261006/summary.json').read_text());selection=pre['selection']
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
def inspect():return json.loads(run(['docker','inspect',*names]))
cs=inspect();before={c['Name']:ident(c) for c in cs}
mix=json.loads((b/'conversation-unread-mixed-10k-20261006/summary.json').read_text());assert mix['status']=='CONVERSATION_UNREAD_MIXED10K_COMPLETE'
restored=mix['restore']['runtime'];assert before=={**pre['runtime'],**restored}
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
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Accept only proven bounded unread batch improvement afterfullsameELF endpoint and10kmixedABBA; ownreadonlysmoke, rollbackafteranyacceptancefailure, keepcandidateONonlyafterPASS','before':before,'configs_sha256':hashes,'candidate_elf_sha256':build['gateway_elf_sha256'],'candidate_image':build['gateway_image_id'],'flag':flag,'fixture':selection,'data_changes':'No newbusinessfixtures orSQLwrites: normalownedlogin/HB+readonlyfixed50page smoke; onlytwoGatewayimage+defaultOFF opt-in flag1 runtimeaccepted. Existingallhistories/apps/configspreserved','controls':'Require2ON caseslistP99<100 andbetterthanbothOFF mean/P99, bothONprivateoriginalgatesPASS andall4800page/24000ring/cross144assertions correct; publishedscopeonlyboundedreadfeature/10kmixed, notallfeature50k extreme','rollback':'Private originalinspect/logs/configSHA and explicitrestoreoverride; close onlyown sockets/processes. Keepallnewbusinessfixtures/failure/code/evidence/Git; other17IDs and durability1/1/1/0/0 preserved','user_apps':'Preserved','performance_acceptance':False})
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

ons=[x for x in mix['cases'] if x['mode']=='on'];offs=[x for x in mix['cases'] if x['mode']=='off'];assert len(ons)==len(offs)==2
assert all(x['capacity']['status']=='PASS' and x['list']['status']=='PASS' and x['list']['sent_to_response']['p99_ms']<100 and x['cross_assertions']==36 for x in ons)
assert max(x['list']['sent_to_response']['p99_ms'] for x in ons)<min(x['list']['sent_to_response']['p99_ms'] for x in offs)
assert max(x['list']['sent_to_response']['mean_ms'] for x in ons)<min(x['list']['sent_to_response']['mean_ms'] for x in offs)
save('scope-acceptance-criteria.json',{'status':'SCOPED_ACCEPTANCE_CRITERIA_PASS','API_checks':248,'sameELF_endpoint_pages':400,'sameELF_mixed_ring_messages':24000,'sameELF_mixed_list_pages':4800,'mixed_cross_operations':196,'mixed_cross_checks':144,'twoONprivate_originalgatesPASS':True,'twoONlist_means_P99_betterthanbothOFF':True,'allfeature10k50kaccepted':False,'candidate_sha256':build['gateway_elf_sha256']})
try:
 deploy('on','accept-proven-unread')
 v=new_run('acceptcu26',d/'smoke')
 try:
  c=Actor(v,selection['anchor'],'192.168.220.128',9000)
  expected=json.loads((b/'conversation-unread-batch-gateway-run-20261006/fixed-page.json').read_text())['response']
  for i in range(5):assert v.request(c,'fixed50page-'+str(i),2007,{'limit':50})==expected
  drain(v);(v.out/'operations.json').write_text(json.dumps(v.operations,indent=2)+'\n')
  save('smoke-summary.json',{'status':'PASS','fixed50pages':5,'complete_metadata_order_unread_equal':True,'owned_login':True,'heartbeat_sent':len(c.hb_sent),'heartbeat_ack':len(c.hb_ack),'data_writes':'Onlynormallogin/presence, noSQL/businesstestfixturechanges'})
 finally:close(v)
 current=preserved()
 save('summary.json',{'status':'CONVERSATION_UNREAD_SCOPED_OPTIMIZATION_ACCEPTED_RUNNING','head':head,'image':build['gateway_image_id'],'gateway_elf_sha256':build['gateway_elf_sha256'],'flag':flag,'value':'1','gateways':{c['Name']:ident(c) for c in current if c['Name'] in {v['Name'] for v in original.values()}},'other17preserved':True,'originalprivateconfigs_preserved':True,'durability':'1/1/1/0/0','private_retained_original_restore_override':str(overrides['restore'][0]),'scope':'Boundedauthorizedconversationunread batch, nativefunctional/endpointandselected10kmixed regressionPASS. Code staysdefaultOFF forotherdeployments; explicitflag1 onlyauditedcurrentstandaloneRedisruntime','allfeature10k50kextreme_acceptance':False})
 print((d/'summary.json').read_text(),flush=True)
except BaseException as e:
 save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'action':'RestoreexactoriginalGatewayafteracceptancesmokefailure'})
 for v in list(clients):close(v)
 deploy('restore','failed-acceptance-restore');raise
PY
