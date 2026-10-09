#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'redis-post-analysis-runtime-20261006';assert not d.exists()
names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
rest=json.loads((b/'redis-acquire-mixed20k-control-20261006/restore-summary.json').read_text())
gw=[c for c in cs if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']]
assert len(gw)==2 and all(c['Image']=='sha256:a2bb65215bfc85246b946a8c0850e134cd3144d078e5b56c376676c2cd8d1cfa' for c in gw)
d.mkdir(mode=0o700)
(d/'audit-before.json').write_text(json.dumps({'operation':'Readonly exact current19 identities and narrowly allowed public maintenance flags','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'writes':'Own evidence only','source':'docker inspect held onlyinRAM; no fullenv/config export'})+'\n')
allowed={'TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE','TINYIMX_CONVERSATION_UNREAD_BATCH_ENABLE','TINYIMX_REDIS_ACQUIRE_TRACE_ENABLE','TINYIMX_PRESENCE_WORKERS','TINYIMX_GATEWAY_PRESENCE_WORKERS','TINYIMX_GATEWAY_PRESENCE_MAINTENANCE_ENABLE'}
x={'status':'READONLY_CURRENT_RUNTIME_REVIEW','gateways':[{'name':c['Name'],'id':c['Id'],'started':c['State']['StartedAt'],'flags':{e.split('=',1)[0]:e.split('=',1)[1] for e in c['Config']['Env'] if e.split('=',1)[0] in allowed}} for c in gw],'all19':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({'status':x['status'],'gateways':x['gateways'],'source_head':x['source_head']},indent=2))
PY
