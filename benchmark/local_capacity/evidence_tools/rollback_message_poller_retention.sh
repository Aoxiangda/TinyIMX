#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,time,socket
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'message-poller-rejected-rollback-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert json.loads((b/'message-poller-diagnostic-control-20261005/summary.json').read_text())['status']=='DIAGNOSTIC_CONTROL_COMPLETED'
assert json.loads((b/'message-poller-completed-analysis-20261005/summary.json').read_text())['status']=='COMPLETED_NUMERIC_DIAGNOSTIC_ANALYSIS'
deployment=json.loads((b/'message-poller-diagnostic-deployment-20261005/summary.json').read_text())
def run(argv):return subprocess.check_output(argv,text=True,timeout=30)
names=run(['docker','ps','--format','{{.Names}}']).splitlines();cs=json.loads(run(['docker','inspect',*names]));assert len(cs)==19
target=next(c for c in cs if c['Name']=='/tinyimx-m21-message-service-1');assert target['Id']==deployment['message_service_id'] and target['Image']==deployment['image']
old='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898'
assert json.loads(run(['docker','image','inspect','tinyimx/runtime:codex-private-single-lease-v1']))[0]['Id']==old
ips={v['IPAddress'] for c in cs if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1'] for v in c['NetworkSettings']['Networks'].values()}
for c in cs:
 if c['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:continue
 for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
  f=line.split()
  if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
   v=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(v)[::-1]) if len(v)==8 else 'ipv6';assert peer in ips or peer.startswith('127.'),'Refuse active business client interruption'
before={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
config=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in config.glob('*.json')}
assert hashes['message.json']==deployment['config_sha256'];assert json.loads((config/'message.json').read_text())['mysql']['pool_size']==16
command=json.loads((b/'message-poller-diagnostic-deployment-20261005/audit-before.json').read_text())['rollback_command']
d.mkdir();private=d/'runtime-private';private.mkdir()
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Reject MAX_POLLERS16 candidate and restore original Messageb24/environment after completed numeric comparison','before':before,'private_config_sha256':hashes,'command':command,'reason':'205 checks passed;150/sP99225.5FAIL compared179.0baseline diagnostic. No demonstrated gain, threadIDs still107/113 and sampledstorage waits higher; preserve failedcandidate, no claimsolecause','writes':'Only Message container brief recreation outside ownedload; private inspect/log preserved; other18/env/config/pool/index unchanged','other_apps':'Preserved','rollback':'Original/candidate immutable images and allsource/evidence retained; no deletion or Git reset'},indent=2)+'\n')
(private/'rejected-message.inspect.json').write_text(json.dumps(target,indent=2)+'\n')
with (private/'rejected-message.before.log').open('w') as f:subprocess.run(['docker','logs','--timestamps',target['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
with (d/'rollback.log').open('w') as f:subprocess.run(command,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
deadline=time.monotonic()+60
while True:
 c=json.loads(run(['docker','inspect',target['Name']]))[0]
 if c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy':break
 assert time.monotonic()<deadline,'Original Message never healthy';time.sleep(2)
assert c['Image']==old and c['Config']['Env']==json.loads((b/'message-poller-diagnostic-deployment-20261005/runtime-private/original-container-inspect.json').read_text())['Config']['Env'] and c['Config']['Cmd']==target['Config']['Cmd']
after=json.loads(run(['docker','inspect',*names]));assert all(x['Name']==target['Name'] or before[x['Name']]=={'id':x['Id'],'image':x['Image'],'started':x['State']['StartedAt']} for x in after)
assert {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in config.glob('*.json')}==hashes
x={'status':'RESTORED_PREVIOUS_B24_POOL16','image':old,'message_service_id':c['Id'],'gateway_image':'1d8','message_workers':16,'pool_size':16,'private_config_sha256':hashes,'other18_preserved':True,'original_environment_restored':True,'removed_flags':['TINYIMX_STORAGE_WAIT_TRACE_ENABLE','TINYIMX_MYSQL_POOL_TRACE'],'candidate_performance_accepted':False}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
