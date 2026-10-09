#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,subprocess,shutil,hashlib
r=pathlib.Path.cwd();d=r/'.local/codex/storage-wait-pool-recovery-build-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()=='a315d5d2edfa29daccdc9ae1c39466d164f8fd9a'
p=r/'build/linux-release/pool_recovery_probe';d.mkdir()
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Archive owned probe, rebuild one job, run MySQL and Redis recovery through own relays with diagnostics enabled','before_binary_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'writes':'Own build/probe outputs, configs0600, empty rollback transaction only; all stages and failed results preserved','production_runtime_changes':False,'other_apps':'Preserved','validation':'Three outage/reconnect cycles, slot return, transaction cleanup and SELECT1/PING checks','rollback':'Terminate only owned test process and private relay; no row writes, shared restart, global cleanup or deletion'},indent=2)+'\n')
shutil.copy2(p,d/'pool_recovery_probe.before')
PY
d=.local/codex/storage-wait-pool-recovery-build-20261005
cmake --build build/linux-release --target pool_recovery_probe --parallel 1 2>&1 | tee "$d/build.log"
sha256sum build/linux-release/pool_recovery_probe | tee "$d/binary.sha256"
TINYIMX_STORAGE_WAIT_TRACE_ENABLE=1 TINYIMX_MYSQL_POOL_TRACE=1 TINYIMX_PERSIST_PHASE_TRACE_ENABLE=1 python3 benchmark/local_capacity/pool_recovery_probe.py --run storagewait1 2>&1 | tee "$d/recovery.log"
python3 - <<'PY'
import pathlib,json,hashlib
r=pathlib.Path.cwd();d=r/'.local/codex/storage-wait-pool-recovery-build-20261005';s=json.loads((r/'.local/codex/pool-recovery-storagewait1/summary.json').read_text());assert s['status']=='PASS'
(d/'summary.json').write_text(json.dumps({'status':'RECOVERY_BUILD_AND_PROBE_PASS','compiled_head':'a315d5d2edfa29daccdc9ae1c39466d164f8fd9a','probe_binary_sha256':hashlib.sha256((r/'build/linux-release/pool_recovery_probe').read_bytes()).hexdigest(),'recovery_run':'storagewait1','probe_summary_sha256':hashlib.sha256((r/'.local/codex/pool-recovery-storagewait1/summary.json').read_bytes()).hexdigest(),'performance_acceptance':False},indent=2)+'\n')
PY
