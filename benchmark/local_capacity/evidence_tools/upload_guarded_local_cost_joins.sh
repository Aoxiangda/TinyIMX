#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,base64,ast,datetime,subprocess
r=pathlib.Path.cwd();b=r/'.local/codex';raw=pathlib.Path('/tmp/codex-tinyimx-local-cost-join-evidence.json').read_bytes();assert hashlib.sha256(raw).hexdigest()=='3ecffd023853264e686c2e57d4e6a10d8006a812ddae2efccc1175b010d17fab';m=json.loads(raw)
names={'ack-persistence-join-20261005','gateway-repository-common-join-20261005'};files={'audit-before.json','summary.json','paired-numeric.json','executed-workspace-reporter.py'}
assert {stage['name'] for stage in m['stages']}==names and len(m['stages'])==2
d=b/'local-cost-joins-upload-20261005';assert not d.exists() and all(not (b/name).exists() for name in names)
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def identity():
 ns=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(ns)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*ns],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=identity();expected=json.loads((b/'online-shared-cost-proof-source-20261005/runtime-after.json').read_text());assert before==expected;entries=[]
for stage in m['stages']:
 assert {entry['path'] for entry in stage['files']}==files and len(stage['files'])==4
 for entry in stage['files']:
  data=base64.b64decode(entry['bytes_base64'],validate=True);assert len(data)<20*1024*1024 and hashlib.sha256(data).hexdigest()==entry['sha256']
  if entry['path'].endswith('.json'):json.loads(data)
  else:ast.parse(data.decode())
  entries.append((stage['name'],entry['path'],data,entry['sha256']))
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Preserve completed Windows numerical joins in guestownfreshstages with exactSHA/whitelist; no execute ofreporters','package_sha256':hashlib.sha256(raw).hexdigest(),'stage_names':sorted(names),'files':sorted(files),'runtime_before':before,'scope':'Own new bounded files only, no source/config/service/SQL/Redis/test mutations','rollback':'Retain all original/new records, no overwrite/delete'},indent=2)+'\n')
for name in names:(b/name).mkdir()
for name,path,data,h in entries:(b/name/path).write_bytes(data)
assert identity()==before
x={'status':'LOCAL_COST_JOINS_EXACT_COPY_PRESERVED','file_count':len(entries),'files':[{'stage':name,'path':path,'sha256':h} for name,path,data,h in entries],'runtime_configs_preserved':True,'windows_reporters':'Forensic executedsource copies, defaultroot expectsoriginalWindowsworkspace tools location; notexecutedinguest','business_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({'status':x['status'],'file_count':x['file_count'],'runtime_configs_preserved':True},indent=2))
PY
