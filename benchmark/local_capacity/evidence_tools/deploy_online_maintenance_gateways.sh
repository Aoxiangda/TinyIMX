#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time,hashlib,socket,shutil
r=pathlib.Path.cwd();d=r/'.local/codex/online-maintenance-gateway-deployment-20261005';assert not d.exists()
def run(a,timeout=20):return subprocess.check_output(a,text=True,timeout=timeout)
assert json.loads((r/'.local/codex/online-maintenance-control-batch150A1/summary.json').read_text())['status']=='ONLINE_MAINTENANCE_MATCHED_CONTROL_COMPLETED'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
image=json.loads((r/'.local/codex/online-maintenance-gateway-image-20261005/summary.json').read_text());assert image['status']=='SEALED_IMAGE_PASS'
assert json.loads((r/'.local/codex/unread-atomic-counts-isolated-redis-20261004/summary.json').read_text())['status']=='REAL_ISOLATED_REDIS_PASS'
assert image['image_tag']=='tinyimx/runtime:codex-online-maintenance-gateway-v1'
assert image['binary_build_head']=='38f41a9663603d959331340926e26ca6c29d08ac' and image['binary_sha256']=='c2894a21cf308ad35ef665c603d17b1a2577c9216aa9e74856772befa99d640f'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();cs=json.loads(run(['docker','inspect',*names]));assert len(cs)==19
gw=[c for c in cs if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']];assert len(gw)==2
old='sha256:1d8d71faa91a5a1e0a261c2a89efa1027a93515ae2b94b2c2d06f48010a7486e';assert all(c['Image']==old and c['State'].get('Health',{}).get('Status')=='healthy' for c in gw)
before={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
ips={v['IPAddress'] for c in gw for v in c['NetworkSettings']['Networks'].values()}
for c in gw:
 for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
  f=line.split()
  if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
   remote=f[2].split(':')[0];ip=socket.inet_ntoa(bytes.fromhex(remote)[::-1]) if len(remote)==8 else 'ipv6';assert ip in ips or ip.startswith('127.'),'Refuse interrupt active business client'
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml'),'-f',str(r/'.local/codex/candidate-deployment-1836cf5-20261004-attempt3/candidate.override.yml'),'-f',str(r/'.local/codex/message-workers16-deployment-20261004/workers16.override.yml')]
config=json.loads(run(base+['config','--format','json']))['services'];envs={}
for c in gw:
 s=c['Name'].split('tinyimx-m21-')[1][:-2];cfg=config[s];assert c['Config']['Cmd']==cfg['command']
 env=dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x);envs[c['Name']]=env
 for k,v in cfg.get('environment',{}).items():assert env.get(k)==str(v),'Resolved Gateway environment drift'
 assert 'TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE' not in env
 assert env.get('TINYIMX_MESSAGE_WORKER_THREADS')=='16' and env.get('TINYIMX_DURABLE_PRIVATE_RECOVERY_ENABLE')=='1' and env.get('TINYIMX_GROUP_FANOUT_ENABLE')=='1'
assert json.loads(run(['docker','image','inspect',image['image_tag']]))[0]['Id']==image['image_id']
d.mkdir();private=d/'runtime-private';private.mkdir();hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config').glob('*.json')}
override=d/'candidate.override.json';override.write_text(json.dumps({'services':{s:{'image':image['image_tag'],'environment':{'TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE':'1'}} for s in ['gateway-a','gateway-b']}})+'\n')
rollback_override=d/'rollback.override.json';rollback_override.write_text(json.dumps({'services':{s:{'image':'tinyimx/runtime:codex-unread-atomic-counts-v1'} for s in ['gateway-a','gateway-b']}})+'\n')
rollback=d/'rollback-command.json';rollback.write_text(json.dumps(base+['-f',str(rollback_override),'up','-d','--no-deps','--pull','never','gateway-b','gateway-a'],indent=2)+'\n')
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Replace only twoGWs with224-check/fullcompiled optionalmaintenance candidate, same16messageworkers','compiled_revision':image['binary_build_head'],'binary_sha256':image['binary_sha256'],'new_image':image['image_id'],'old_image':old,'before_containers':before,'private_config_sha256':hashes,'environment_changes':['Only TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE=1 on twoGWs'],'scope':'Same16messageworkers/64stripes/queues/3sdeadlines, recoveryON/groupfanoutON, existing pools8; Onlymaintenancebatch flag pluscompiledcache idle andmissingrestore/userordering changes; allACKdurabilityvalidation preserved, no Message84 build','impact':'OnlytwoGatewaybriefrecreation outside workload; originalGWlogs/image retained, same aliases/listeners; other17 unchanged including Messageb24/index013/pool16/nginx131072/DB/Redis/MQ','validation':'Healthy/no restart, exactenvironment/command/privateconfig, other17 IDs/image/start unchanged, realACK boundary and sameoffering controls afterwards','rollback':json.loads(rollback.read_text()),'other_apps':'Preserved'},indent=2)+'\n')
for c in gw:
 (private/(c['Name'].split('/')[-1]+'.inspect.json')).write_text(json.dumps(c,indent=2)+'\n')
 with (private/(c['Name'].split('/')[-1]+'.before.log')).open('w') as f:subprocess.run(['docker','logs','--timestamps',c['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
changed=[]
try:
 for s in ['gateway-b','gateway-a']:
  changed.append(s)
  with (d/(s+'-deploy.log')).open('w') as f:subprocess.run(base+['-f',str(override),'up','-d','--no-deps','--pull','never',s],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
  deadline=time.monotonic()+90
  while True:
   c=json.loads(run(['docker','inspect','tinyimx-m21-'+s+'-1']))[0]
   if c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy':break
   assert time.monotonic()<deadline,'Gateway never healthy';time.sleep(2)
  assert c['Image']==image['image_id'] and c['RestartCount']==0
  assert dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)=={**envs[c['Name']],'TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE':'1'}
  assert c['Config']['Cmd']==config[s]['command'];print('ATOMIC_COUNTS_HEALTHY='+s,flush=True)
 after=json.loads(run(['docker','inspect',*names]));assert all(c['Name'] in envs or before[c['Name']]=={'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in after)
 for p,h in hashes.items():assert hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h
 with socket.create_connection(('127.0.0.1',9000),3):pass
 x={'status':'ONLINE_MAINTENANCE_GATEWAYS_READY','image':image['image_id'],'compiled_revision':image['binary_build_head'],'binary_sha256':image['binary_sha256'],'other17_preserved':True,'private_configs_preserved':True,'environment_preserved_except_batch_enable':True,'gateway_ids':{c['Name']:c['Id'] for c in after if c['Name'] in envs},'performance':'NOT_RUN'}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e),'targets_changed':changed},indent=2)+'\n')
 with (d/'rollback.log').open('w') as f:subprocess.run(base+['-f',str(rollback_override),'up','-d','--no-deps','--pull','never',*changed],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=90)
 raise
PY
