#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,shutil,re
r=pathlib.Path.cwd();d=r/'.local/codex/message-sync-poller-retention-build-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head=='f7b5a08e47bfec08e5504001cc1d4b7edbface54'
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>1024*1024
p=r/'build/linux-release/message_service_demo';assert hashlib.sha256(p.read_bytes()).hexdigest()=='405d810283232c4b04019e13d87bfefb0a40b4ed05c3cc7d02102686a3cecd9f'
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Single-job MessageService MAX_POLLERS16 build and existing real-gRPC/application/crash/ACK/trace tests; productionb24 remains running','head':head,'before_message_binary_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'writes':'Owned build outputs and exact original MessageService binary backup; unit output only in this stage','targets':['message_service_demo','message_outbox_integration_tests','private_persistence_trace_tests','message_application_service_tests','m16_crash_window_contract_tests','unread_snapshot_aggregate_tests','private_receiver_ack_tests','private_chat_ack_boundary_tests','message_service_integration_tests'],'validation':'Unit regressions now; real fresh-schema pool1/pool4 integration separately audited next','dependencies':'ExistingSDK5.1.1 and cached dependencies, manifestinstallOFF, no downloads','other_apps':'Preserved','runtime_deployment':False,'rollback':'Before binary and runningb24 image retained; no runtime orbusiness mutation'},indent=2)+'\n');shutil.copy2(p,d/'message_service_demo.before')
PY
d=.local/codex/message-sync-poller-retention-build-20261005
cmake -S . -B build/linux-release -DVCPKG_MANIFEST_INSTALL=OFF 2>&1 | tee "$d/configure.log"
cmake --build build/linux-release --target message_service_demo message_service_integration_tests message_outbox_integration_tests private_persistence_trace_tests message_application_service_tests m16_crash_window_contract_tests unread_snapshot_aggregate_tests private_receiver_ack_tests private_chat_ack_boundary_tests --parallel 1 2>&1 | tee "$d/build.log"
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime
r=pathlib.Path.cwd();d=r/'.local/codex/message-sync-poller-retention-build-20261005';checks={}
for name in ['private_persistence_trace_tests','message_application_service_tests','m16_crash_window_contract_tests','private_receiver_ack_tests','private_chat_ack_boundary_tests','message_service_integration_tests']:
 with (d/(name+'.log')).open('w') as f:p=subprocess.run([str(r/'build/linux-release'/name)],stdout=f,stderr=subprocess.STDOUT,timeout=150)
 text=(d/(name+'.log')).read_text();checks[name]={'exit_code':p.returncode,'pass':text.count('[PASS]')+sum(line.startswith('PASS ') for line in text.splitlines()),'fail':text.count('[FAIL]')+sum(line.startswith('FAIL ') for line in text.splitlines())}
x={'status':'BUILD_UNIT_PASS' if all(z['exit_code']==0 and z['pass']>0 and z['fail']==0 for z in checks.values()) else 'FAIL','compiled_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'message_binary_sha256':hashlib.sha256((r/'build/linux-release/message_service_demo').read_bytes()).hexdigest(),'checks':checks,'integration':'NOT_RUN','runtime_deployment':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2));assert x['status']=='BUILD_UNIT_PASS'
PY
