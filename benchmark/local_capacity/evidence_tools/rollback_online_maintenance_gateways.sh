#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time,hashlib,socket
r=pathlib.Path.cwd();b=r/'.local/codex';p=b/'online-maintenance-gateway-deployment-20261005';d=b/'online-maintenance-gateway-rejected-rollback-20261005';assert not d.exists()
assert (p/'summary.json').exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def run(a):return subprocess.check_output(a,text=True,timeout=30)
deployment=json.loads((p/'summary.json').read_text());old=json.loads((p/'audit-before.json').read_text());names=run(['docker','ps','--format','{{.Names}}']).splitlines();cs=json.loads(run(['docker','inspect',*names]));assert len(cs)==19
before={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs};gw=[c for c in cs if c['Name'] in deployment['gateway_ids']]
ips={x['IPAddress'] for c in gw for x in c['NetworkSettings']['Networks'].values()};envs={}
for c in gw:
 assert c['Id']==deployment['gateway_ids'][c['Name']] and c['Image']==deployment['image']
 envs[c['Name']]=dict(v.split('=',1) for v in c['Config']['Env'] if '=' in v);assert envs[c['Name']]['TINYIMX_MESSAGE_WORKER_THREADS']=='16'
 for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
  f=line.split()
  if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
   addr=f[2].split(':')[0];ip=socket.inet_ntoa(bytes.fromhex(addr)[::-1]) if len(addr)==8 else 'ipv6';assert ip in ips or ip.startswith('127.'),'Refuse active client interruption'
for path,h in old['private_config_sha256'].items():assert hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()==h
cmd=json.loads((p/'rollback-command.json').read_text());assert cmd[-2:]==['gateway-b','gateway-a']
d.mkdir();private=d/'runtime-private';private.mkdir()
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Restore original twoGateway images/flags after maintenance evaluation andcapturecandidate drain logs','reason':'Returntooriginalruntime after candidate evaluation or fault; outcome classified separately from rollback','targets':['gateway-b','gateway-a'],'before':before,'private_config_sha256':old['private_config_sha256'],'environment_changes':['Remove only TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE candidateflag'], 'image_changes':['maintenancecandidate->1d8original'],'rollback_command':cmd,'other_apps':'Preserved','data':'All original results, pending/history and productiondata retained'},indent=2)+'\n')
capture_errors=[]
for c in gw:
 try:
  with (private/(c['Name'].split('/')[-1]+'.before.log')).open('w') as f:subprocess.run(['docker','logs','--timestamps',c['Id']],stdout=f,stderr=subprocess.STDOUT,timeout=30,check=True)
 except BaseException as error:capture_errors.append({'container':c['Name'],'type':type(error).__name__})
(private/'capture-errors.json').write_text(json.dumps(capture_errors)+'\n')
for s in cmd[-2:]:
 with (d/(s+'-rollback.log')).open('w') as f:subprocess.run(cmd[:-2]+[s],stdout=f,stderr=subprocess.STDOUT,timeout=60,check=True)
 end=time.monotonic()+90
 while True:
  c=json.loads(run(['docker','inspect','tinyimx-m21-'+s+'-1']))[0]
  if c['State'].get('Health',{}).get('Status')=='healthy':break
  assert time.monotonic()<end;time.sleep(2)
 assert c['Image']==old['old_image'] and dict(v.split('=',1) for v in c['Config']['Env'] if '=' in v)=={key:value for key,value in envs[c['Name']].items() if key!='TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE'}
 print('RESTORED16='+s,flush=True)
after=json.loads(run(['docker','inspect',*names]))
for c in after:
 if c['Name'] not in envs:assert before[c['Name']]=={'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
for path,h in old['private_config_sha256'].items():assert hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()==h
x={'status':'RESTORED_PREVIOUS_1D8_MESSAGE_WORKERS16','image':old['old_image'],'gateway_ids':{c['Name']:c['Id'] for c in after if c['Name'] in envs},'other17_preserved':True,'config_preserved':True,'candidate_performance':'Evaluate retainedcontrols separately','capacity_full_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
