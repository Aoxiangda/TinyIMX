#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,signal,os
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-maintenance-session-control-20261005';assert not d.exists()
source=json.loads((b/'online-maintenance-live-sessions-source-20261005/summary.json').read_text());assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==source['head']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
deployment=json.loads((b/'online-maintenance-gateway-deployment-20261005/summary.json').read_text());assert deployment['status']=='ONLINE_MAINTENANCE_GATEWAYS_READY'
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 for c in cs:
  if c['Name'] in deployment['gateway_ids']:assert c['Id']==deployment['gateway_ids'][c['Name']] and c['Image']==deployment['image']
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();d.mkdir();actor=r/'benchmark/local_capacity/online_maintenance_session_actor.py';args=['/usr/bin/python3',str(actor)]
(d/'helper-audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Realowned twoGateway session actor wrapper, candidatealreadyhealthy','head':source['head'],'actor_sha256':hashlib.sha256(actor.read_bytes()).hexdigest(),'users':[519890,519892],'scope':'Actualactor checks exactUID/username/status andabsence before login. BeforeeachownTTL1 mutationCAS fullrecord guarded andsaved. No DEL/SQLwrite/config/app/resourcechanges','timeout_seconds':90,'cleanup':'Onlyownverifiedactor PID/starttime/argv/PGID onfailure, child ownTCP/Redis sockets, retainallpartial/failure','limits':'Realcontrolledsamples, notallfeaturecapacity/racefrequency/P99 proof','runtime_before':before},indent=2)+'\n')
def interrupted(signum,frame):raise KeyboardInterrupt('Ownreal session control interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
p=None;identity=None;proc=None;code=None;error=None
try:
 with (d/'actor.log').open('w') as output:
  p=subprocess.Popen(args,stdout=output,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path(f'/proc/{p.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19];(d/'own-process.json').write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'starttime_ticks':identity,'argv':args})+'\n');code=p.wait(timeout=90)
except BaseException as caught:error=caught
finally:
 if p is not None and p.poll() is None:
  assert proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(p.pid)==p.pid and proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[a.encode() for a in args]
  (d/'stop-audit-before.json').write_text(json.dumps({'operation':'Stoponlyverifiedownedactorprocessgroup','pid':p.pid,'identity_verified':True})+'\n');os.killpg(p.pid,signal.SIGTERM)
  try:p.wait(timeout=3)
  except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 after=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
if error is not None:(d/'helper-failed.json').write_text(json.dumps({'status':'FAIL','type':type(error).__name__,'message':str(error),'exit':code})+'\n');raise error
result=json.loads((d/'result.json').read_text());x={'status':'ONLINE_MAINTENANCE_SESSION_CONTROL_COMPLETE','exit':code,'result':result,'all19_configs_preserved':True,'performance_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2));assert code==0 and result['status']=='ONLINE_MAINTENANCE_REAL_SESSION_PASS'
PY
