#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,os,signal,socket,time,re,datetime,types,sys,shutil
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'permission20k-runtime-control-20261007-attempt2';assert not d.exists();sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest();run=lambda a:subprocess.check_output(a,text=True,timeout=30);ident=lambda c:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']};envmap=lambda c:dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
source=json.loads((b/'permission20k-control-mount-repair-source-20261007/summary.json').read_text());head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/n)==h for n,h in source['files'].items())
product=json.loads((b/'permission-boundary-source-20261007/summary.json').read_text());hist=json.loads((b/'duration-histogram-source-20261007/summary.json').read_text());assert all(sha(r/n)==h for n,h in {**product['files'],**hist['files']}.items());assert subprocess.run(['git','merge-base','--is-ancestor',product['head'],head]).returncode==0
built=json.loads((b/'permission-boundary-build-20261007/summary.json').read_text());assert built['status']=='PERMISSION_BOUNDARY_NATIVE_RPC_AND_IMAGES_PASS' and built['head']==product['head'] and built['native_checks']==135 and len(built['real_grpc_regressions'])==6 and all(x['exit']==0 for x in built['real_grpc_regressions'].values())
assert json.loads((b/'duration-histogram-native-20261007/summary.json').read_text())['green_checks']==23
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
inspect=lambda:json.loads(run(['docker','inspect',*names]));cs=inspect();before={c['Name']:ident(c) for c in cs};roles=['gateway-a','gateway-b','social-service'];targets={'/tinyimx-m21-'+s+'-1' for s in roles};original={s:next(c for c in cs if c['Name']=='/tinyimx-m21-'+s+'-1') for s in roles};flag='TINYIMX_PERMISSION_BOUNDARY_TRACE_ENABLE';assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
assert all(original[s]['Image']=='sha256:5c2645b1e8512bdd3fe68d4fea229a9418a5e115c9d96d636871639dba41a405' for s in roles[:2]);assert original['social-service']['Image']=='sha256:38dca459e0a88141c1385503b28cbd6e29a1eed18d253f808dc1dfa89e0a0316';assert all(flag not in envmap(c) and not any(k.startswith('TINYIMX_FAULT_') for k in envmap(c)) for c in original.values());assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');cfgsha={p.name:sha(p) for p in cfg.glob('*.json')};assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text())[1])>2*1024*1024 and shutil.disk_usage(r).free>2*1024**3
os.environ['TINYIMX_M21_STATE_DIR']=str(cfg.parent)
sys.path.insert(0,str(r/'benchmark/local_capacity'));from permission_runtime_mount_preflight import validate_mounts
assert cfg.is_dir() and all(len(c['Mounts'])==1 and pathlib.Path(c['Mounts'][0]['Source']).resolve()==cfg.resolve() for c in original.values())
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')];basecfg=json.loads(run(base+['config','--format','json']))['services']
for role,c in original.items():assert c['Config']['Cmd']==basecfg[role]['command'] and all(envmap(c).get(k)==str(v) for k,v in basecfg[role].get('environment',{}).items())
elf_paths={role:'/opt/tinyimx/bin/'+('gateway_demo' if role in roles[:2] else 'social_service_demo') for role in roles};original_elf={role:run(['docker','exec',c['Id'],'sha256sum',elf_paths[role]]).split()[0] for role,c in original.items()};assert all(original_elf[s]=='e68731562d83b3b5c1923d80ed7ad4459d2f5cbc9b8a224e32014626fac04cfc' for s in roles[:2])
def hostconfig(c):
 x=dict(c['HostConfig'])
 for k in ['Dns','DnsOptions','DnsSearch']:
  if x.get(k) is None:x[k]=[]
 if x.get('Binds') is not None:x['Binds']=sorted(x['Binds'])
 return x
def settings():return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-permission-control','SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count']).strip()
assert settings()=='1\t1\t1\t0\t0'
def no_live():
 ips={v['IPAddress'] for c in inspect() if c['Name'] in targets for v in c['NetworkSettings']['Networks'].values()}
 for c in inspect():
  if c['Name'] not in targets or not c['State']['Running'] or c['State'].get('Restarting') or c['State']['Status']!='running':continue
  for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
   f=line.split()
   if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
    raw=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(raw)[::-1]) if len(raw)==8 else 'ipv6'
    if peer not in ips and not peer.startswith('127.'):return False
 return True
assert no_live(),'Refuse interruption of external live clients'
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700);save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10));(private/'original-inspect.json').write_text(json.dumps(original,indent=2)+chr(10));os.chmod(private/'original-inspect.json',0o600);overrides={}
for mode in ['diagnostic','restore']:
 wanted={}
 for role,c in original.items():
  proof=built['images']['gateway' if role in roles[:2] else 'social'];wanted[role]={'image':c['Image'] if mode=='restore' else proof['image_id'],'environment':envmap(c) if mode=='restore' else {**envmap(c),flag:'1'},'volumes':[{'type':'bind','source':str(cfg),'target':m['Destination'],'read_only':not m['RW'],'bind':{'create_host_path':True}} for m in c['Mounts']]}
  assert json.loads(run(['docker','image','inspect',wanted[role]['image']]))[0]['Id']==wanted[role]['image']
 path=private/(mode+'.override.json');path.write_text(json.dumps({'services':wanted})+chr(10));os.chmod(path,0o600);proposed=json.loads(run(base+['-f',str(path),'config','--format','json']))['services']
 for role in roles:assert {k:str(v) for k,v in proposed[role]['environment'].items()}==wanted[role]['environment'] and proposed[role]['command']==original[role]['Config']['Cmd']
 validate_mounts(proposed,original)
 overrides[mode]=(path,wanted)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'image_source_head':built['head'],'operation':'Only audited three task service containers temporary controlled recreation into native-validated permission diagnostic images; exact original images/fullEnv/Health/HostConfig/Mounts restored in finally, retain16other identities/config/durability. Before any deployment validate exact existing binds on actual positive/negative Compose render and run actual list three strict startup bindings with fake Actor and no sockets. One original20000authenticated TCP135/sprivate60sec+list20/s+49op36assert crossfeature diagnostic; no sameELF performance-gain claim','runtime_before':before,'config_sha256':cfgsha,'original_elf_sha256':original_elf,'native_image_proof':built['images'],'candidate_scope':'DefaultOFF numeric client/server/repo marks; corrected seconds metrics buckets; validated native Social readiness main/server reused. No auth/cache/query/PING/pool/poller/deadline/wire change. New private INSERT-first candidate never used; accepted Message image/flags untouched.','data_scope':'Only original audited own existing benchmarkring relations ifmissing and ordinary messages/deliveries/receiverACK and four fresh reserved cross actors/file fixture via normal APIs; retain each result/row; no deletion, cacheclear, VM/security/hostapp change','mutation_targets':sorted(targets),'stop_scope':'Only currently verified exact3task containers and own subprocess PGID/startticks/argv; no external TCPclients. Original stop exits recorded; candidate shutdown failure retained.','rollback':'All original image IDs, env, commands, health, resources, mounts retained private0600; restorefinally; other16unchanged; retain every artifact/no reset/drop/truncate','limits':'Single traceON diagnostic with rate-limited biased phase intersection; SDKhistogram changes and log perturbation; no allfeature/50k/extreme acceptance'})
phase='initial';active=None;changed=False;error=None
# Own guarded subprocesses cannot outlive cleanup. Original capacity helper itself
# owns and closes its native workers, list and actors before returning.
def invoke(argv,label,seconds=700):
 global phase,active
 phase=label;save(label+'-process-audit-before.json',{'argv':argv,'seconds':seconds})
 with (private/(label+'.log')).open('w') as f:
  child=subprocess.Popen(argv,cwd=r,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);active=child;proc=pathlib.Path('/proc')/str(child.pid)
  try:ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
  except FileNotFoundError:assert child.poll() is not None;ticks=None
  save(label+'-process.json',{'pid':child.pid,'start_ticks':ticks,'argv':argv})
  try:code=child.wait(timeout=seconds)
  finally:
   if child.poll() is None:
    assert ticks and (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(child.pid)==child.pid and (proc/'cmdline').read_bytes().split(bytes([0]))[:len(argv)]==[str(x).encode() for x in argv];save(label+'-stop-audit.json',{'operation':'Only verified own childgroup graceful SIGINT then TERM','pid':child.pid,'start_ticks':ticks});os.killpg(child.pid,signal.SIGINT)
    try:child.wait(timeout=30)
    except subprocess.TimeoutExpired:
     os.killpg(child.pid,signal.SIGTERM)
     try:child.wait(timeout=8)
     except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
   active=None
 assert code==0,label+' failed; raw retained';print(json.dumps({'completed':label,'exit':code}),flush=True)
def interrupted(sig,frame):raise RuntimeError('Own permission diagnostic interrupted '+str(sig))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def preserved():
 now=inspect();assert len(now)==19 and all(c['Name'] in targets or ident(c)==before[c['Name']] for c in now);assert {p.name:sha(p) for p in cfg.glob('*.json')}==cfgsha and settings()=='1\t1\t1\t0\t0';assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in now);return now
def capture(label):
 for c in inspect():
  if c['Name'] in targets and c['State']['Running']:
   with (private/(label+c['Name'].replace('/','')+'.log')).open('w') as f:subprocess.run(['docker','logs','--timestamps',c['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
def clocks(label):
 out={'host':{'boot':pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),'offset':pathlib.Path('/proc/self/timens_offsets').read_text()}}
 for c in inspect():
  if c['Name'] in targets:out[c['Name']]={'id':c['Id'],'boot':run(['docker','exec',c['Id'],'cat','/proc/sys/kernel/random/boot_id']).strip(),'offset':run(['docker','exec',c['Id'],'cat','/proc/self/timens_offsets'])}
 for v in out.values():v['monotonic_offset_ns']=next(int(x.split()[1])*1000000000+int(x.split()[2]) for x in v['offset'].splitlines() if x.split()[0]=='monotonic')
 save(label+'-clock-domain.json',out);assert len({x['boot'] for x in out.values()})==1 and {x['monotonic_offset_ns'] for x in out.values()}=={0}
def deploy(mode):
 global changed,phase
 phase='deploy-'+mode;deadline=time.monotonic()+20
 while not no_live():assert time.monotonic()<deadline,'Live clients prevent recreation';time.sleep(.5)
 current={s:next(c for c in inspect() if c['Name']==original[s]['Name']) for s in roles};capture(mode+'-before-');path,wanted=overrides[mode]
 save(mode+'-deploy-audit-before.json',{'operation':'Controlled recreation exact3 auditedtask containers only, originalimage/env/health/host/mount restore available; no external clients; preserveother16','current':{s:ident(c) for s,c in current.items()},'override_sha256':sha(path),'target_names':sorted(targets)})
 changed=True
 for role,c in current.items():
  if c['State']['Running']:
   with (private/(mode+'-'+role+'-stop.log')).open('w') as f:subprocess.run(['docker','stop','--time','20',c['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
  z=json.loads(run(['docker','inspect',c['Id']]))[0];assert not z['State']['Running'];save(mode+'-'+role+'-stop-summary.json',{'container':ident(c),'exit':z['State']['ExitCode'],'finished':z['State']['FinishedAt']})
 invoke(base+['-f',str(path),'up','-d','--no-deps','--force-recreate','--pull','never',*roles],mode+'-compose',120)
 deadline=time.monotonic()+90
 while True:
  now=inspect();chosen={s:next(c for c in now if c['Name']==original[s]['Name']) for s in roles}
  if all(c['State']['Running'] and c['State'].get('Health',{}).get('Status')=='healthy' for c in chosen.values()):break
  assert time.monotonic()<deadline,'Health timeout';time.sleep(1)
 for role,c in chosen.items():
  o=original[role];expected=built['images']['gateway' if role in roles[:2] else 'social'];assert c['Image']==wanted[role]['image'] and envmap(c)==wanted[role]['environment'] and hostconfig(c)==hostconfig(o) and c['Mounts']==o['Mounts'] and c['RestartCount']==0 and c['Config']['Healthcheck']==o['Config']['Healthcheck']
  for k in ['User','WorkingDir','Cmd','Entrypoint','StopSignal']:assert c['Config'].get(k)==o['Config'].get(k)
  assert run(['docker','exec',c['Id'],'sha256sum',elf_paths[role]]).split()[0]==(original_elf[role] if mode=='restore' else expected['elf_sha256'])
 preserved();clocks(mode);save(mode+'-deploy-summary.json',{'status':'HEALTHY_AND_IDENTITIES_BOUND','runtime':{c['Name']:ident(c) for c in now}});print(json.dumps({'status':'PERMISSION_CONTROL_READY','mode':mode}),flush=True)
sys.path.insert(0,str(r/'benchmark/local_capacity'));from cross_feature_actor import Run,Actor
# Read-only friend-list public calls on each actual gateway wait for current Social
# endpoint registration. Original business timeout remains untouched, probes excluded.
def gateway_ready(label):
 stage=d/('ready-'+label);assert not stage.exists();stage.mkdir(mode=0o700);results=[]
 for c in [x for x in inspect() if x['Name'] in {original[s]['Name'] for s in roles[:2]}]:
  ip=next(v['IPAddress'] for v in c['NetworkSettings']['Networks'].values() if v.get('IPAddress'));v=Run(types.SimpleNamespace(run='perReady'+label,users=[],host=ip,port=9000),stage)
  try:
   actor=Actor(v,519950,ip,9000);deadline=time.monotonic()+25;successes=0
   while successes<2:
    assert time.monotonic()<deadline,'Social endpoint readiness timeout';seq=actor.send(2009,{'limit':1});v.until(lambda:(2010,seq) in actor.replies or (9999,seq) in actor.replies,'friend-list-readiness',5);assert (9999,seq) not in actor.replies;reply=actor.replies.pop((2010,seq));good=reply.get('success') is True
    results.append({'gateway':c['Name'],'success':good,'reason':reply.get('reason'),'excluded_from_measurement':True});save(label+'-ready-probes.json',results)
    if good:assert isinstance(reply['friends'],list) and len(reply['friends'])<=1
    else:assert reply.get('reason') in ['social_service_unavailable','social_service_timeout'],'Unexpected readiness failure'
    successes=successes+1 if good else 0
    if successes<2:time.sleep(.2)
  finally:
   for actor in list(v.clients):actor.close()
 save(label+'-ready-summary.json',{'status':'BOTH_GATEWAY_SOCIAL_PUBLIC_READINESS_PASS','calls':len(results),'no_SQL_write_or_private_send':True})
try:
 invoke(['python3',str(r/'benchmark/local_capacity/permission_runtime_mount_preflight_test.py')],'actual-compose-mount-preflight-no-mutation',60);assert json.loads((b/'permission20k-mount-preflight-20261007/summary.json').read_text())['status']=='REAL_COMPOSE_MOUNT_PREFLIGHT_POSITIVE_NEGATIVE_PASS'
 invoke(['python3',str(r/'benchmark/local_capacity/permission20k_list_scope_fixed_startup_test.py'),str(r/'benchmark/local_capacity/conversation_list_permission20k_scope_fixed_20261007.py'),str(b/'permission20k-list-startup-scope-fixed-20261007-attempt2')],'actual-list-startup-no-network',30);assert json.loads((b/'permission20k-list-startup-scope-fixed-20261007-attempt2/summary.json').read_text())['status']=='ALL_LIST_CLIENT_STARTUP_BINDINGS_PASS_BEFORE_NETWORK'
 deploy('diagnostic');gateway_ready('diagnostic');save('diagnostic-ready.json',{'status':'DIAGNOSTIC_IMAGES_AND_BOTH_PUBLIC_SOCIAL_ENDPOINTS_READY','runtime':{c['Name']:ident(c) for c in inspect()}});print(json.dumps({'status':'PERMISSION20K_DIAGNOSTIC_LOAD_START','original_contract':True,'allfeature_acceptance':False}),flush=True)
 invoke(['bash',str(r/'benchmark/local_capacity/evidence_tools/run_permission20k_diagnostic_20261007_attempt2.sh')],'original20k-diagnostic',700)
 result=json.loads((b/'permission20k-diagnostic-20261007/summary.json').read_text());assert result['status']=='MIXED20K_POINT_COMPLETE';save('diagnostic-point.json',result);capture('diagnostic-after-')
except BaseException as e:error=e;save('failed.json',{'status':'FAIL','phase':phase,'type':type(e).__name__,'message':str(e),'acceptance':False})
finally:
 if changed:
  try:deploy('restore');gateway_ready('restore');capture('restored-');now=preserved();save('restore-summary.json',{'status':'ORIGINAL3IMAGES_FULLENV_HEALTH_HOSTCONFIG_MOUNTS_AND_OTHER16_RESTORED','runtime':{c['Name']:ident(c) for c in now},'source_changes_retained':True,'all_load_children_closed':True,'config_durability_hostapps_preserved':True,'container_recreation_scope':sorted(targets),'original_container_ids_replaced_only_for_audited3':True});changed=False
  except BaseException as e:save('restore-failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e)});raise
if error is not None:raise error
save('summary.json',{'status':'PERMISSION20K_DIAGNOSTIC_COMPLETE_AND_ACCEPTED_RUNTIME_RESTORED','head':head,'native_image_source_head':built['head'],'diagnostic_point':result,'allfeature_extreme_acceptance':False,'performance_gain':'NOT_CLAIMED','limits':'One traceON point with new histogram shape and logging; not sameELF ABBA optimisation, fullfeature capacity,50k,TLS,soak,or fault'});print(json.dumps({'status':'PERMISSION20K_DIAGNOSTIC_COMPLETE_AND_ACCEPTED_RUNTIME_RESTORED','private_p99_ms':result['capacity']['positive_ack_p99_ms_upper_bin'],'list_p99_ms':result['list']['sent_to_response']['p99_ms'],'allfeature_extreme_acceptance':False}),flush=True)
PY
