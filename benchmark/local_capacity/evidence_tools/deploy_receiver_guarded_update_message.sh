#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time,hashlib,socket,shutil,signal
r=pathlib.Path.cwd();d=r/'.local/codex/receiver-guarded-update-message-deployment-20261005';assert not d.exists()
def run(a,timeout=20):return subprocess.check_output(a,text=True,timeout=timeout)
assert run(['git','rev-parse','HEAD']).strip()==json.loads((r/'.local/codex/receiver-guarded-update-controls-source-20261005/summary.json').read_text())['head']
assert json.loads((r/'.local/codex/receiver-guarded-update-control-guard150A1/summary.json').read_text())['status']=='PRIVATE_GUARDED_UPDATE_CONTROL_COMPLETED'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
image=json.loads((r/'.local/codex/receiver-guarded-update-image-20261005/summary.json').read_text());assert image['status']=='SEALED_IMAGE_PASS' and image['binary_build_head']=='84cb214e17bdd4590eb5763483e448d4f78b0d0b' and image['binary_sha256']=='5d24b144577e01a5bfc7049b93ebcd65f61e1008da7e8513546ad0e041c55202'
assert json.loads((r/'.local/codex/receiver-confirm-guarded-update-mysql-20261005-attempt2/summary.json').read_text())['status']=='GUARDED_CONFIRM_REAL_MYSQL_RED_GREEN_PASS'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();cs=json.loads(run(['docker','inspect',*names]));assert len(cs)==19;target=next(c for c in cs if c['Name']=='/tinyimx-m21-message-service-1');old='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898';assert target['Image']==old
before={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs};ips={v['IPAddress'] for c in cs if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1'] for v in c['NetworkSettings']['Networks'].values()}
for c in cs:
 if c['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:continue
 for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
  f=line.split()
  if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
   v=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(v)[::-1]) if len(v)==8 else 'ipv6';assert peer in ips or peer.startswith('127.'),'Business clients active'
def envmap(c):
 pairs=[x.split('=',1) for x in c['Config']['Env']];assert len(pairs)==len({x[0] for x in pairs});return dict(pairs)

env=envmap(target);assert env.get('TINYIMX_PERSIST_PHASE_TRACE_ENABLE')=='1'
p=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json');assert hashlib.sha256(p.read_bytes()).hexdigest()=='2ead45f4c44348c6fad9a5526f3a7e816d4c3db43f21bcf8a8276aab1e324736' and json.loads(p.read_text())['mysql']['pool_size']==16
hashes={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in p.parent.glob('*.json')};assert json.loads(run(['docker','image','inspect',image['image_tag']]))[0]['Id']==image['image_id']
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')];cfg=json.loads(run(base+['config','--format','json']))['services']['message-service'];assert target['Config']['Cmd']==cfg['command'] and all(env.get(k)==str(v) for k,v in cfg.get('environment',{}).items())
d.mkdir();private=d/'runtime-private';private.mkdir();(private/'original-container-inspect.json').write_text(json.dumps(target,indent=2)+'\n');shutil.copy2(p,private/'message.json.before')
override=d/'candidate.override.json';rollback=d/'rollback.override.json';override.write_text(json.dumps({'services':{'message-service':{'image':image['image_tag'],'environment':{'TINYIMX_PERSIST_PHASE_TRACE_ENABLE':'1'}}}})+'\n');rollback.write_text(json.dumps({'services':{'message-service':{'image':'tinyimx/runtime:codex-private-single-lease-v1','environment':{'TINYIMX_PERSIST_PHASE_TRACE_ENABLE':'1'}}}})+'\n')
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Replace only MessageService withUnit/RPC and real isolated SQL validated recipient-guarded UPDATE candidate','compiled_revision':image['binary_build_head'],'new_image':image['image_id'],'new_binary_sha256':image['binary_sha256'],'old_image':old,'before_containers':before,'private_config_sha256':hashes,'pool_size':16,'environment_changes':[],'scope':'Only MessageService recreated, same env/cmd/pool/config/durability, other18 exactID/image/start; no source build during test','impact':'BriefRPC reconnect outside ownload; all existing rows/outbox retained','validation':'Healthy50053, same configSHA/environment/command, other18preserved; same-offering controls with new message-image pin','rollback_command':base+['-f',str(rollback),'up','-d','--no-deps','--pull','never','message-service'],'other_apps':'Preserved'},indent=2)+'\n')
with (private/'message-service-before.log').open('w') as f:subprocess.run(['docker','logs','--timestamps',target['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=25)
def deploy(path,log):
 with (d/log).open('w') as f:subprocess.run(base+['-f',str(path),'up','-d','--no-deps','--pull','never','message-service'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
 deadline=time.monotonic()+50
 while True:
  c=json.loads(run(['docker','inspect','tinyimx-m21-message-service-1']))[0]
  if c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy':return c
  assert time.monotonic()<deadline,'MessageService unhealthy';time.sleep(2)
def interrupted(signum,frame):raise KeyboardInterrupt('Own guarded Message deployment interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)

try:
 current=deploy(override,'deployment.log');assert current['Image']==image['image_id'] and envmap(current)==envmap(target) and current['Config']['Healthcheck']==target['Config']['Healthcheck'] and current['Config']['Cmd']==target['Config']['Cmd'] and current['RestartCount']==0
 after=json.loads(run(['docker','inspect',*names]));assert all(c['Name']==target['Name'] or before[c['Name']]=={'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in after)
 for path,h in hashes.items():assert hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()==h
 assert run(['docker','exec',current['Id'],'sha256sum','/opt/tinyimx/bin/message_service_demo']).split()[0]==image['binary_sha256']
 ip=next(iter(current['NetworkSettings']['Networks'].values()))['IPAddress']
 with socket.create_connection((ip,50053),3):pass
 x={'status':'RECEIVER_GUARDED_UPDATE_MESSAGE_READY','compiled_revision':image['binary_build_head'],'image':image['image_id'],'binary_sha256':image['binary_sha256'],'message_service_id':current['Id'],'pool_size':16,'config_sha256':hashes[str(p)],'other18_preserved':True,'private_configs_preserved':True,'environment_preserved':True,'performance':'NOT_RUN'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e)},indent=2)+'\n');deploy(rollback,'rollback.log');raise
PY
