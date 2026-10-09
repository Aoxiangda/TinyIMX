#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,shutil,re
r=pathlib.Path.cwd();d=r/'.local/codex/auth-phase-diagnostics-build-20261005';assert not d.exists()
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head=='374f2807e9e48312faa3653c9dde9b919d0e7300'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>1024*1024
p=r/'build/linux-release/user_service_demo';old='ae1b6f677eefe25e3d0857330fe4dab2d0a271c8ce4fe3eb35d463440a30ced1';assert hashlib.sha256(p.read_bytes()).hexdigest()==old
assert subprocess.check_output(['docker','exec','tinyimx-m21-user-service-1','sha256sum','/opt/tinyimx/bin/user_service_demo'],text=True).split()[0]==old
names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));before={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Single-job cached build of defaultOFF auth diagnostics and existing User application/realgRPC regression; no runtime deployment or load','head':head,'before_user_binary_sha256':old,'before_containers':before,'private_config_sha256':hashes,'writes':'Own build outputs, exact originalUser binary backup and fresh bounded testlogs','targets':['user_service_demo','auth_phase_trace_tests','user_application_service_tests','user_service_integration_tests'],'dependencies':'Existingcached SDK/VCPKG_MANIFEST_INSTALL OFF, no download/package install','other_apps':'Preserved','rollback':'Original binary andrunning38dca image retained; no config/SQL/deadline/source changes'},indent=2)+'\n');shutil.copy2(p,d/'user_service_demo.before')
PY
d=.local/codex/auth-phase-diagnostics-build-20261005
cmake -S . -B build/linux-release -DVCPKG_MANIFEST_INSTALL=OFF 2>&1 | tee "$d/configure.log"
cmake --build build/linux-release --target user_service_demo auth_phase_trace_tests user_application_service_tests user_service_integration_tests --parallel 1 2>&1 | tee "$d/build.log"
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,os
r=pathlib.Path.cwd();d=r/'.local/codex/auth-phase-diagnostics-build-20261005';a=json.loads((d/'audit-before.json').read_text());checks={}
env=dict(os.environ);env.pop('TINYIMX_AUTH_PHASE_TRACE_ENABLE',None)
for name in ['auth_phase_trace_tests','user_application_service_tests','user_service_integration_tests']:
 with (d/(name+'.log')).open('w') as f:p=subprocess.run([str(r/'build/linux-release'/name)],stdout=f,stderr=subprocess.STDOUT,env=env,timeout=150)
 t=(d/(name+'.log')).read_text();checks[name]={'exit_code':p.returncode,'pass':t.count('[PASS]'),'fail':t.count('[FAIL]')}
# A separate actual gRPC regression invocation with diagnosticON checks that
# auth/profile/wrong-password/storage/unavailable contracts still hold.
env['TINYIMX_AUTH_PHASE_TRACE_ENABLE']='1'
with (d/'user_service_integration_trace_on.log').open('w') as f:p=subprocess.run([str(r/'build/linux-release/user_service_integration_tests')],stdout=f,stderr=subprocess.STDOUT,env=env,timeout=150)
t=(d/'user_service_integration_trace_on.log').read_text();checks['user_service_integration_trace_on']={'exit_code':p.returncode,'pass':t.count('[PASS]'),'fail':t.count('[FAIL]')}
names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));after={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs};assert after==a['before_containers']
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');assert {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}==a['private_config_sha256']
x={'status':'AUTH_BUILD_REGRESSIONS_PASS' if all(v['exit_code']==0 and v['pass']>0 and v['fail']==0 for v in checks.values()) else 'FAIL','compiled_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'user_binary_sha256':hashlib.sha256((r/'build/linux-release/user_service_demo').read_bytes()).hexdigest(),'original_user_binary_sha256':a['before_user_binary_sha256'],'checks':checks,'all19_runtime_andconfigs_preserved':True,'deployment':False,'performance_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2));assert x['status']=='AUTH_BUILD_REGRESSIONS_PASS'
PY
