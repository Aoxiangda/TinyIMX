#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';stage=b/'auth-phase-diagnostics-user-restored-20261005';assert stage.exists() and not (stage/'summary.json').exists()
d=b/'auth-phase-user-restoration-review-20261005';assert not d.exists();d.mkdir()
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly post-rollback audit after orderedENV-array equality failed; verify exact ENVkeys/values instead oflistorder','scope':'OriginalUser alreadyrecreated, do not recreateagain orrewritefailedstage','reads':'CurrentDockerID/image/cmd/health/ENV mapping privately; frozen originalinspect/rollbackaudit/configSHA','writes':'Freshboolean/hash verification only; no ENVvalues orsecrets exported','rollback':'No runtime/source/SQL/app changes; preserveoriginalAssertionError andalllogs'},indent=2)+'\n')
orig=json.loads((b/'auth-phase-diagnostics-user-deployment-20261005/runtime-private/original-container-inspect.json').read_text());a=json.loads((stage/'audit-before.json').read_text());names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));assert len(cs)==19;c=next(x for x in cs if x['Name']==orig['Name'])
def env(v):
 data=[x.split('=',1) for x in v];assert all(len(x)==2 for x in data);out=dict(data);assert len(out)==len(data);return out
ordered_equal=c['Config']['Env']==orig['Config']['Env'];assert env(c['Config']['Env'])==env(orig['Config']['Env']) and not ordered_equal
assert c['Image']==orig['Image'] and c['Config']['Cmd']==orig['Config']['Cmd'] and c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy'
assert 'TINYIMX_AUTH_PHASE_TRACE_ENABLE' not in env(c['Config']['Env'])
sha=subprocess.check_output(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/user_service_demo'],text=True).split()[0];assert sha==a['original_binary_sha256']
assert all(x['Name']==c['Name'] or a['before_containers'][x['Name']]=={'id':x['Id'],'image':x['Image'],'started':x['State']['StartedAt']} for x in cs)
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');assert {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}==a['private_config_sha256']
x={'status':'ORIGINAL_USER_38DCA_RESTORATION_VERIFIED','image':c['Image'],'binary_sha256':sha,'user_service_id':c['Id'],'original_env_order_equal':ordered_equal,'original_env_keys_and_values_equal':True,'original_command_health_verified':True,'auth_flag_absent':True,'other18_preserved':True,'all_private_configs_preserved':True,'new_recreation':False,'original_failed_validation_preserved':True,'problem_and_solution':'ENVarray order is not semantic. Originalexactkeys/values match; auditconfirmed no extra/missing/duplicatekey. Future rollbackvalidator comparesmaps, retainsfailure, no repeatedservicechange.'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
