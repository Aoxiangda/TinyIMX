#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-maintenance-functional-control-20261005';assert not d.exists()
deployment=json.loads((b/'online-maintenance-gateway-deployment-20261005/summary.json').read_text());assert deployment['status']=='ONLINE_MAINTENANCE_GATEWAYS_READY'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 for c in cs:
  if c['Name'] in deployment['gateway_ids']:assert c['Id']==deployment['gateway_ids'][c['Name']] and c['Image']==deployment['image']
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Real publicfunctional chain throughnginx onmaintenance candidate','users':[519880,519882,519884,519886],'scope':'Existingactor independentlyverifies exactsynthetic identities andempty socialpairs before anymutation; normalfriend/private/read/group/file APIs andownfileobjects only. No row/filedelete/reset/SQLwrite/configchanges, runtime unchanged','resources':'Ownactor300s, allpartials retained, allapps kept','limits':'Realfeaturechain samples, notallfeaturecapacity/TLS/MCP/AI/offline/fault/soak acceptance','runtime_before':before},indent=2)+'\n')
with (d/'actor.log').open('w') as f:
 code=subprocess.run(['python3','benchmark/local_capacity/cross_feature_actor.py','--run','batchfunc1','--users','519880','519882','519884','519886'],stdout=f,stderr=subprocess.STDOUT,timeout=300).returncode
after=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');result=json.loads((b/'cross-feature-batchfunc1/summary.json').read_text());x={'status':'ONLINE_MAINTENANCE_FUNCTIONAL_CONTROL_COMPLETE','exit':code,'functional':result,'all19_configs_preserved':True,'full_feature_capacity_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2));assert code==0 and result['status']=='PASS'
PY
