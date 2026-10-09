#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,subprocess,json,datetime,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-maintenance-api-baseline-20261005';assert not d.exists()
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()=='c87b9b5ca7586af3796399849267e78c34ae889e'
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
runtime={'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
assert runtime==json.loads((b/'online-maintenance-component-probe-20261005/runtime-after.json').read_text())
paths=['gateway/business/BusinessExecutor.cpp','services/cache/OnlineStatusCache.cpp','services/cache/OnlineStatusCache.h'];diff=subprocess.check_output(['git','diff','--name-only','33fc9bbbf7b8a8e828ce70d1ae0035d8e8c71ce8','--','gateway','examples/gateway_demo.cpp','services/cache/OnlineStatusCache.cpp','services/cache/OnlineStatusCache.h'],text=True).splitlines();assert diff==['gateway/business/BusinessExecutor.cpp']
original=subprocess.check_output(['git','show','33fc9bbbf7b8a8e828ce70d1ae0035d8e8c71ce8:gateway/business/BusinessExecutor.cpp'])
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly source baseline snapshot for next audited batchAPI iteration','paths':paths,'scope':'Save original running33fc executor exact bytes plus hashes of current3paths; no runtime/product writes/build/load/config/keys','reason':'Only executor fair continuation differs from running Gateway baseline; rejected6521 source must be restored before future GW compilation, retainitsGit/preimage','runtime_preserved':True},indent=2)+'\n')
(d/'original-BusinessExecutor.cpp').write_bytes(original)
x={'status':'ONLINE_MAINTENANCE_API_BASELINE_SNAPSHOT_COMPLETE','head':'c87b9b5ca7586af3796399849267e78c34ae889e','current_source_sha256':{p:hashlib.sha256((r/p).read_bytes()).hexdigest() for p in paths},'running_gateway_reference':'33fc9bbbf7b8a8e828ce70d1ae0035d8e8c71ce8','original_executor_sha256':hashlib.sha256(original).hexdigest(),'only_gateway_diff_from_running':diff,'all19_configs_preserved':True};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
