#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,subprocess,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';old=b/'message-rpc-kernel-probe-20261005';d=b/'message-rpc-kernel-probe-compile-failure-review-20261005';assert not d.exists()
log=(old/'compile.log').read_text();assert 'pb' in log and 'ambiguous' in log and not (old/'link.log').exists() and not (old/'summary.json').exists()
assert subprocess.run(['pgrep','-f','^'+str(old/'message_rpc_kernel_probe')],capture_output=True).returncode==1
names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
runtime={'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}};assert runtime==json.loads((old/'runtime-before.json').read_text())
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Read-only classify preserved firstprobe compilerfailure andverifyall19/config/no newprobeworker','writes':'Fresh review only, originalfailurestage unchanged','cause':'pb namespacealias conflicts withglobalProtobuf pb namespace fromextension_set.h; no diagnosticload started','fix':'Rename onlyownprobealias; improvefuturecompilefailure recording andownedprocesscleanup; no productionimplementation change'},indent=2)+'\n')
x={'status':'RPC_PROBE_COMPILE_FAILURE_CLASSIFIED_NO_LOAD','failed_source_head':'5cd8b4b20826c8eec4d80fdae8c5b590b19f917f','compile_log_sha256':hashlib.sha256((old/'compile.log').read_bytes()).hexdigest(),'measurement_cases':'NOT_RUN_compilefailed','all19_runtime_configs_preserved':True,'owned_probe_running':False,'original_raw_failure_unchanged':True};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
