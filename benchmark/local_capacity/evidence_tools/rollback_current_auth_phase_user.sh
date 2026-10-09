#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time,hashlib,socket
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'auth-current-user-restored-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert (b/'auth-current-user-deployment-20261005/summary.json').exists()  # Restore even if sampling/analysis failed
dep=b/'auth-current-user-deployment-20261005';s=json.loads((dep/'summary.json').read_text());original=json.loads((dep/'runtime-private/original-container-inspect.json').read_text())
def run(a):return subprocess.check_output(a,text=True,timeout=30)
names=run(['docker','ps','--format','{{.Names}}']).splitlines();cs=json.loads(run(['docker','inspect',*names]));assert len(cs)==19;target=next(c for c in cs if c['Name']=='/tinyimx-m21-user-service-1');assert target['Id']==s['user_service_id'] and target['Image']==s['image']
before={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs};cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');assert {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}==s['private_config_sha256']
ips={v['IPAddress'] for c in cs if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1'] for v in c['NetworkSettings']['Networks'].values()}
for c in cs:
 if c['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:continue
 for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
  f=line.split()
  if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
   v=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(v)[::-1]) if len(v)==8 else 'ipv6';assert peer in ips or peer.startswith('127.'),'Refuse activeclient interruption'
old=original['Image'];assert json.loads(run(['docker','image','inspect','tinyimx/runtime:m21-final']))[0]['Id']==old
d.mkdir();private=d/'runtime-private';private.mkdir();(private/'diagnostic-inspect.json').write_text(json.dumps(target,indent=2)+'\n')
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Restore exact originalUser38dca/environment after diagnostic-only10khold/phasecollection','reason':'Measurement not performancecandidate; priorabortnotreproduced/causenotproved; preserve allresults','before_containers':before,'old_image':old,'original_binary_sha256':'ae1b6f677eefe25e3d0857330fe4dab2d0a271c8ce4fe3eb35d463440a30ced1','command':s['rollback_command'],'private_config_sha256':s['private_config_sha256'],'writes':'OnlyUserbriefrecreation outside ownload; originalenvflagabsent; other18 preserved','rollback':'Keep original/diagnosticimages,Git,raw; no deletion/reset/otherapp termination'},indent=2)+'\n')
with (private/'diagnostic-user-before.log').open('w') as f:subprocess.run(['docker','logs','--timestamps',target['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
with (d/'rollback.log').open('w') as f:subprocess.run(s['rollback_command'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
end=time.monotonic()+60
while True:
 c=json.loads(run(['docker','inspect',target['Name']]))[0]
 if c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy':break
 assert time.monotonic()<end;time.sleep(2)
assert c['HostConfig']==original['HostConfig'] and c['Mounts']==original['Mounts']
assert c['Image']==old and dict(x.split('=',1) for x in c['Config']['Env'])==dict(x.split('=',1) for x in original['Config']['Env']) and c['Config']['Cmd']==original['Config']['Cmd']
sha=run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/user_service_demo']).split()[0];assert sha=='ae1b6f677eefe25e3d0857330fe4dab2d0a271c8ce4fe3eb35d463440a30ced1'
after=json.loads(run(['docker','inspect',*names]));assert all(x['Name']==target['Name'] or before[x['Name']]=={'id':x['Id'],'image':x['Image'],'started':x['State']['StartedAt']} for x in after)
assert {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}==s['private_config_sha256']
x={'status':'ORIGINAL_USER_38DCA_RESTORED','image':old,'binary_sha256':sha,'user_service_id':c['Id'],'other18_preserved':True,'all_private_configs_preserved':True,'original_environment_restored':True,'removed_flag':'TINYIMX_AUTH_PHASE_TRACE_ENABLE','performance_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
