#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export CODEX_LOGGING_PHASE="$1" CODEX_LOGGING_STEP="$2"
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time,hashlib,socket,os,re,sys
r=pathlib.Path.cwd();b=r/'.local/codex';phase=os.environ['CODEX_LOGGING_PHASE'];step=os.environ['CODEX_LOGGING_STEP'];assert (phase,step) in [('eager','eager-initial'),('lazy','lazy-before-B1'),('eager','eager-before-A2'),('original','original-rollback'),('lazy','lazy-retained')]
d=b/('lazy-logging-deployment-'+step+'-20261005');assert not d.exists()
def run(a,timeout=20):return subprocess.check_output(a,text=True,timeout=timeout)
head=run(['git','rev-parse','HEAD']).strip();source=json.loads((b/'lazy-logging-endpoint-controls-source-20261005/summary.json').read_text());assert head==source['head'] and not run(['git','diff','--name-only']).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1 and int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
build=json.loads((b/'lazy-logging-service-build-20261005-attempt3/summary.json').read_text());assert build['status']=='LAZY_LOGGING_PAIRED_SERVICE_IMAGES_PASS' and build['unit_checks_per_variant']==130 and build['all19_runtime_configs_preserved']
images={(x['variant'],x['target']):x for x in build['images']};assert len(images)==4
roles={'gateway-a':'gateway_demo','gateway-b':'gateway_demo','message-service':'message_service_demo'};names={'/tinyimx-m21-'+s+'-1':s for s in roles};cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');original=json.loads((b/'private-batch-message-deployment-retained-on-20261005/runtime-after.json').read_text())
def runtime():
 cs=json.loads(run(['docker','inspect',*run(['docker','ps','--format','{{.Names}}']).splitlines()]));assert len(cs)==19
 state={'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
 assert state['config_sha256']==original['config_sha256'] and all(n in names or v==original['containers'][n] for n,v in state['containers'].items());return state,cs
before,cs=runtime();current={names[c['Name']]:c for c in cs if c['Name'] in names};assert len(current)==3 and all(c['State'].get('Health',{}).get('Status')=='healthy' for c in current.values())
initial=b/'lazy-logging-deployment-eager-initial-20261005/runtime-private/original-containers-inspect.json'
baseline=current if step=='eager-initial' else json.loads(initial.read_text());assert set(baseline)==set(roles)
if step=='eager-initial':assert before==original
for role,c in current.items():
 assert c['Image'] in [baseline[role]['Image'],images[('eager',roles[role])]['image_id'],images[('lazy',roles[role])]['image_id']]
 assert dict(x.split('=',1) for x in c['Config']['Env'])==dict(x.split('=',1) for x in baseline[role]['Config']['Env'])
ips={v['IPAddress'] for s,c in current.items() if s.startswith('gateway') for v in c['NetworkSettings']['Networks'].values() if v.get('IPAddress')}
for role,c in current.items():
 if not role.startswith('gateway'):continue
 for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
  f=line.split()
  if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
   a=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(a)[::-1]) if len(a)==8 else 'ipv6';assert peer in ips or peer.startswith('127.'),'Existing business session active; no restart'
def sql(q):return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-logging-deploy-readonly',q])
q='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count';assert sql(q).strip()=='1\t1\t1\t0\t0' and json.loads((cfg/'message.json').read_text())['mysql']['pool_size']==16
orig={'gateway_demo':{'image_tag':'tinyimx/runtime:codex-online-maintenance-gateway-v1','image_id':'sha256:a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9','elf_sha256':'c2894a21cf308ad35ef665c603d17b1a2577c9216aa9e74856772befa99d640f'},'message_service_demo':{'image_tag':'tinyimx/runtime:codex-private-begin-insert-read-batch-v1','image_id':'sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060','elf_sha256':'2548733766409d282d30f6ffbbbe47f9c12b5d4fa3cacfdcc3d3e47f72582699'}}
os.environ['TINYIMX_M21_STATE_DIR']=str(cfg.parent);os.environ['TINYIMX_RUNTIME_UID']=str(os.getuid());os.environ['TINYIMX_RUNTIME_GID']=str(os.getgid());base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')]
d.mkdir(mode=0o700)
def fail_marker(k,e,t):
 if not (d/'failed.json').exists():(d/'failed.json').write_text(json.dumps({'status':'FAIL','type':k.__name__,'message':str(e),'deployment_may_not_have_started':True})+'\n')
 sys.__excepthook__(k,e,t)
sys.excepthook=fail_marker
private=d/'runtime-private';private.mkdir(mode=0o700);(private/'original-containers-inspect.json').write_text(json.dumps(baseline,indent=2)+'\n');(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
def override(variant,path):
 chosen={role:(orig[target] if variant=='original' else images[(variant,target)]) for role,target in roles.items()}
 for e in chosen.values():assert json.loads(run(['docker','image','inspect',e['image_tag']]))[0]['Id']==e['image_id']
 path.write_text(json.dumps({'services':{role:{'image':e['image_tag'],'environment':dict(x.split('=',1) for x in baseline[role]['Config']['Env'])} for role,e in chosen.items()}},indent=2)+'\n');proposed=json.loads(run(base+['-f',str(path),'config','--format','json']));(private/(path.stem+'-compose-config.json')).write_text(json.dumps(proposed,indent=2)+'\n')
 for role,e in chosen.items():assert proposed['services'][role]['image']==e['image_tag'] and proposed['services'][role]['command']==baseline[role]['Config']['Cmd']
 for role in roles:assert proposed['services'][role].get('environment',{})==dict(x.split('=',1) for x in baseline[role]['Config']['Env']),'Resolved environment differs before deployment: '+role
 return chosen
candidate=private/'candidate.override.json';rollback=private/'original.rollback.override.json';chosen=override(phase,candidate);rollback_chosen=override('original',rollback)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Controlled recreation of only idle GatewayA/B andMessage for paired logging control or exact retained original restore','head':head,'phase':phase,'step':step,'selected_images':{s:{k:e[k] for k in ['image_id','elf_sha256']} for s,e in chosen.items()},'all_environment_values_fixed_private':True,'before':before,'preserve':'Other16 containerIDs/images/start, allconfigSHA, durability1/1/1/0/0, worker16/batchON/deadlines/current safety andalluserapps','container_lifecycle':'Compose recreates only3 named targets after no external9000sessions andno load. Original configs/env/logs preservedprivate, originalimages retained. No filesystem/data cleanup','rollback_command':base+['-f',str(rollback),'up','-d','--no-deps','--pull','never','--force-recreate',*roles],'impact':'Brief3service reconnect; no owned activeclients during deployment','performance':'NOT_RUN'},indent=2)+'\n')
for role,c in current.items():
 with (private/(role+'-before.log')).open('w') as out:subprocess.run(['docker','logs','--timestamps',c['Id']],stdout=out,stderr=subprocess.STDOUT,check=True,timeout=25)
def deploy(path,label,expected):
 with (private/(label+'.log')).open('w') as out:subprocess.run(base+['-f',str(path),'up','-d','--no-deps','--pull','never','--force-recreate',*roles],stdout=out,stderr=subprocess.STDOUT,check=True,timeout=80)
 end=time.monotonic()+60
 while True:
  targets=json.loads(run(['docker','inspect',*names]));
  if all(c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy' for c in targets):break
  assert time.monotonic()<end,'Targets did not become healthy';time.sleep(1)
 for c in targets:
  role=names[c['Name']];e=expected[role];old=baseline[role];assert c['Image']==e['image_id'] and c['RestartCount']==0
  for key in ['Cmd','User','WorkingDir']:assert c['Config'][key]==old['Config'][key]
  assert dict(x.split('=',1) for x in c['Config']['Env'])==dict(x.split('=',1) for x in old['Config']['Env'])
  for key in ['Memory','MemorySwap','NanoCpus','CpuQuota','CpuPeriod','CpuShares','CpusetCpus','PidsLimit','ReadonlyRootfs','Privileged','SecurityOpt','CapAdd','CapDrop','NetworkMode','ExtraHosts','LogConfig']:assert c['HostConfig'][key]==old['HostConfig'][key],key
  mounts=lambda obj:sorted((m['Type'],m['Source'],m['Destination'],m['RW']) for m in obj['Mounts']);assert mounts(c)==mounts(old)
  assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/'+roles[role]]).split()[0]==e['elf_sha256']
 after,_=runtime();assert sql(q).strip()=='1\t1\t1\t0\t0';return after
try:
 after=deploy(candidate,'deployment',chosen);(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');x={'status':'LAZY_LOGGING_CONTROL_DEPLOYMENT_READY','head':head,'phase':phase,'step':step,'images':{s:e['image_id'] for s,e in chosen.items()},'other16_configs_env_mounts_resources_preserved':True,'performance':'NOT_RUN'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e)})+'\n')
 try:
  after=deploy(rollback,'automatic-original-rollback',rollback_chosen);(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');(d/'rollback-outcome.json').write_text(json.dumps({'status':'RETAINED_ORIGINAL3_RESTORED','other16_preserved':True})+'\n')
 except BaseException as failure:(d/'rollback-failed.json').write_text(json.dumps({'status':'FAIL','type':type(failure).__name__,'message':str(failure)})+'\n');raise
 raise
PY
