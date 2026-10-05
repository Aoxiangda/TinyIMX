#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time,hashlib,socket
r=pathlib.Path.cwd();d=r/'.local/codex/auth-current-user-deployment-20261005';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
source=json.loads((r/'.local/codex/auth-current-phase-source-20261005/summary.json').read_text());assert run(['git','rev-parse','HEAD']).strip()==source['head']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
image=json.loads((r/'.local/codex/auth-phase-diagnostics-image-20261005/summary.json').read_text());assert image['status']=='SEALED_AUTH_IMAGE_PASS' and image['image_id']=='sha256:1ff078812b309e36a859c2a2057e35b4783bcc02db76a8c5d7fcb120d0427cf3'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();cs=json.loads(run(['docker','inspect',*names]));assert len(cs)==19
target=next(c for c in cs if c['Name']=='/tinyimx-m21-user-service-1');old=image['original_user_image'];assert target['Image']==old and target['State'].get('Health',{}).get('Status')=='healthy'
assert run(['docker','exec',target['Id'],'sha256sum','/opt/tinyimx/bin/user_service_demo']).split()[0]==image['original_user_binary_sha256']
before={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
ips={v['IPAddress'] for c in cs if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1'] for v in c['NetworkSettings']['Networks'].values()}
for c in cs:
 if c['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:continue
 assert c['Image']=='sha256:a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9'
 for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
  f=line.split()
  if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
   v=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(v)[::-1]) if len(v)==8 else 'ipv6';assert peer in ips or peer.startswith('127.'),'Refuse active business-client interruption'
env=dict(x.split('=',1) for x in target['Config']['Env'] if '=' in x);assert all(k not in env for k in ['TINYIMX_AUTH_PHASE_TRACE_ENABLE','TINYIMX_MYSQL_POOL_TRACE','TINYIMX_STORAGE_WAIT_TRACE_ENABLE']);expected_env={**env,'TINYIMX_AUTH_PHASE_TRACE_ENABLE':'1'}
cfgdir=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');cfg=cfgdir/'user.json';hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfgdir.glob('*.json')};assert json.loads(cfg.read_text())['mysql']['pool_size']==8
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')];scfg=json.loads(run(base+['config','--format','json']))['services']['user-service'];assert target['Config']['Cmd']==scfg['command'] and all(env.get(k)==str(v) for k,v in scfg.get('environment',{}).items())
assert json.loads(run(['docker','image','inspect',image['image_tag']]))[0]['Id']==image['image_id']
d.mkdir();private=d/'runtime-private';private.mkdir();(private/'original-container-inspect.json').write_text(json.dumps(target,indent=2)+'\n')
candidate=private/'candidate.override.json';rollback=private/'rollback.override.json';candidate.write_text(json.dumps({'services':{'user-service':{'image':image['image_tag'],'environment':expected_env}}})+'\n');rollback.write_text(json.dumps({'services':{'user-service':{'image':'tinyimx/runtime:m21-final','environment':env}}})+'\n')
for path,wanted in [(candidate,expected_env),(rollback,env)]:
 proposed=json.loads(run(base+['-f',str(path),'config','--format','json']))['services']['user-service'];assert {k:str(v) for k,v in proposed.get('environment',{}).items()}==wanted and proposed['command']==target['Config']['Cmd']
command=base+['-f',str(rollback),'up','-d','--no-deps','--pull','never','user-service']
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'User-only numeric diagnostic deployment after36 checks; addexactlyone defaultOFF flag','head':image['compiled_head'],'old_image':old,'old_binary_sha256':image['original_user_binary_sha256'],'new_image':image['image_id'],'new_binary_sha256':image['binary_sha256'],'before_containers':before,'private_config_sha256':hashes,'environment_changes':['TINYIMX_AUTH_PHASE_TRACE_ENABLE=1'],'scope':'OnlyUser recreated; samecmd/privateconfig/pool8/auth/KDF; other18 preserved; fulltargetHostConfig/mounts/command checked; flagonly numericaldiagnostic; no claimrecreation fixesoverload','impact':'BriefRPC reconnect outside ownedload; no business rows deleted','rollback_command':command,'validation':'Health50052, exactenv/binary/command/config, allother18IDs/images/started; no buildduring measurement','other_apps':'Preserved'},indent=2)+'\n')
with (private/'user-service-before.log').open('w') as f:subprocess.run(['docker','logs','--timestamps',target['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
def deploy(path,log):
 with (d/log).open('w') as f:subprocess.run(base+['-f',str(path),'up','-d','--no-deps','--pull','never','user-service'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
 end=time.monotonic()+50
 while True:
  c=json.loads(run(['docker','inspect','tinyimx-m21-user-service-1']))[0]
  if c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy':return c
  assert time.monotonic()<end,'User not healthy';time.sleep(2)
try:
 c=deploy(candidate,'deployment.log');assert c['Image']==image['image_id'] and dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)==expected_env and c['Config']['Cmd']==target['Config']['Cmd'] and c['RestartCount']==0
 assert c['HostConfig']==target['HostConfig'] and c['Mounts']==target['Mounts']
 for key in ['User','WorkingDir','Cmd','Entrypoint','Healthcheck','StopSignal']:assert c['Config'].get(key)==target['Config'].get(key)
 assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/user_service_demo']).split()[0]==image['binary_sha256']
 after=json.loads(run(['docker','inspect',*names]));assert all(x['Name']==target['Name'] or before[x['Name']]=={'id':x['Id'],'image':x['Image'],'started':x['State']['StartedAt']} for x in after)
 assert {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfgdir.glob('*.json')}==hashes
 ip=next(iter(c['NetworkSettings']['Networks'].values()))['IPAddress']
 with socket.create_connection((ip,50052),3):pass
 x={'status':'AUTH_DIAGNOSTIC_USER_READY','compiled_head':image['compiled_head'],'image':image['image_id'],'binary_sha256':image['binary_sha256'],'user_service_id':c['Id'],'user_pool_size':8,'private_config_sha256':hashes,'other18_preserved':True,'one_flag_verified':'TINYIMX_AUTH_PHASE_TRACE_ENABLE=1','rollback_command':command,'performance':'NOT_RUN_DIAGNOSTICS_ONLY'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__})+'\n');c=deploy(rollback,'rollback.log');assert c['Image']==old and dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)==env;raise
PY
