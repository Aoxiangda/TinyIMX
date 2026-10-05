#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,tarfile,datetime,subprocess
r=pathlib.Path.cwd();base=r/'.local/codex';archive=base/'post-restart-evidence-v68.tar.gz';assert not archive.exists()
required=['online-maintenance-image-controls-source-20261005','online-maintenance-gateway-image-20261005','online-maintenance-control-batch150A1','capacity-batch150A1','online-maintenance-gateway-deployment-20261005','online-maintenance-live-sessions-source-20261005','online-maintenance-session-control-20261005']
optional=['lazy-logging-service-build-attempt3-source-20261005','lazy-logging-service-build-20261005-attempt3','lazy-logging-service-build-attempt2-source-20261005','lazy-logging-service-build-20261005-attempt2','lazy-logging-service-build-source-20261005','lazy-logging-service-build-20261005','lazy-logging-source-20261005','lazy-logging-regression-20261005','local-transport-component-probe-source-20261005','local-transport-component-probe-20261005','mysql-unix-transport-probe-attempt4-source-20261005','mysql-unix-transport-probe-20261005-attempt4','mysql-unix-transport-probe-attempt3-source-20261005','mysql-unix-transport-probe-20261005-attempt3','mysql-unix-transport-probe-attempt2-source-20261005','mysql-unix-transport-probe-20261005-attempt2','mysql-unix-transport-probe-source-20261005','mysql-unix-transport-probe-20261005','private-clock-callsite-probe-source-20261005','private-clock-callsite-probe-20261005','private-clock-instruction-probe-source-20261005','private-clock-instruction-probe-20261005','private-clock-cost-probe-source-20261005-attempt2','private-clock-cost-probe-source-20261005','private-clock-cost-probe-20261005','private-cpu-symbol-parser-source-20261005','private-cpu-symbol-analysis-20261005','private-cpu-symbol-snapshot-attempt2-source-20261005','private-cpu-symbol-snapshot-20261005-attempt2','private-cpu-symbol-snapshot-source-20261005','private-cpu-symbol-snapshot-20261005','private-cpu-profile-recovery-source-20261005','private-cpu-profile-recovery-20261005','private-cpu-load-profile-source-20261005','private-cpu-load-profile-20261005','capacity-cpuprofile150','private-cpu-profiler-bootstrap-permissions-source-20261005','private-cpu-profiler-bootstrap-20261005-attempt2','private-cpu-profiler-bootstrap-source-20261005','private-cpu-profiler-bootstrap-20261005','private-cpu-profiler-preflight-source-20261005','private-cpu-profiler-preflight-20261005','private-batch-abba-outcome-source-20261005','private-batch-abba-analysis-20261005','private-batch-message-deployment-retained-on-20261005','private-batch-endpoint-controls-source-20261005','private-batch-message-deployment-off-initial-20261005','private-batch-message-deployment-on-before-B1-20261005','private-batch-message-deployment-off-before-A2-20261005','private-batch-message-deployment-original-rollback-20261005','private-batch-endpoint-control-sqlbatch150A1','capacity-sqlbatch150A1','private-batch-endpoint-control-sqlbatch150B1','capacity-sqlbatch150B1','private-batch-endpoint-control-sqlbatch150B2','capacity-sqlbatch150B2','private-batch-endpoint-control-sqlbatch150A2','capacity-sqlbatch150A2','private-batch-functional-control-off-20261005','private-batch-functional-control-on-20261005','cross-feature-sqlbatchfuncoff','cross-feature-sqlbatchfuncon','private-batch-message-image-source-20261005','private-batch-message-build-image-20261005','private-batch-postcommit-fault-source-20261005','private-batch-postcommit-fault-20261005','private-batch-fault-fixture-v3-source-20261005','private-batch-regression-build-20261005-sql-v3','private-batch-isolated-mysql-20261005-v3','private-batch-sql-controls-v2-source-20261005','private-batch-regression-build-20261005-sql-v2','private-batch-isolated-mysql-20261005-v2','private-batch-confirm-state-fix-source-20261005','private-batch-regression-build-20261005-confirm-fix','private-batch-isolated-mysql-20261005-confirm-fix','private-batch-regression-finalize-source-20261005','private-batch-regression-build-20261005-final','private-batch-isolated-mysql-20261005-final','private-batch-regression-attempt3-source-20261005','private-batch-regression-build-20261005-attempt3','private-batch-isolated-mysql-20261005-attempt3','private-batch-regression-attempt2-source-20261005','private-batch-regression-build-20261005-attempt2','private-batch-isolated-mysql-20261005-attempt2','private-batch-regression-tools-source-20261005','private-batch-regression-build-20261005','private-batch-isolated-mysql-20261005','private-begin-insert-read-batch-source-20261005','private-batch-source-baseline-20261005','mysql-safe-roundtrip-probe-source-20261005','mysql-safe-roundtrip-probe-20261005','mysql-transaction-roundtrip-probe-source-20261005','mysql-transaction-roundtrip-probe-20261005','mysql-roundtrip-readonly-preflight-20261005','message-rpc-completion-queue-probe-source-20261005','message-rpc-completion-queue-probe-20261005','online-maintenance-abba-outcome-source-20261005','online-maintenance-gateway-retained-deployment-20261005','online-maintenance-joined-phases-20261005','online-maintenance-endpoint-review-source-20261005','online-maintenance-endpoint-review-20261005','online-maintenance-endpoint-review-abba-20261005','online-maintenance-phase-subset-20261005','online-maintenance-gateway-drain-rollback-20261005','online-maintenance-functional-attempt2-source-20261005','online-maintenance-functional-control-20261005-attempt2','cross-feature-batchfunc2','online-maintenance-functional-control-20261005','cross-feature-batchfunc1','online-maintenance-control-batch150B1','capacity-batch150B1','online-maintenance-control-batch150B2','capacity-batch150B2','online-maintenance-control-batch150A2','capacity-batch150A2','online-maintenance-gateway-rejected-rollback-20261005']
for name in required:assert (base/name).is_dir(),name
names=required+[name for name in optional if (base/name).is_dir()];assert len(names)==len(set(names))
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
for name in names:
 d=base/name
 assert (d/'summary.json').exists() or any((d/p).exists() for p in ['failed.json','failure.json','helper-failed.json']),name+' is not completed or failed'
secrets=set()
def sensitive(k):return any(t in k.lower() for t in ['password','token','api_key','secret']) and not k.lower().endswith('_env')
def walk(v,key=''):
 if isinstance(v,dict):
  for k,x in v.items():walk(x,k)
 elif isinstance(v,list):
  for x in v:walk(x,key)
 elif isinstance(v,str) and len(v)>=8 and sensitive(key):secrets.add(v.encode())
for p in pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config').glob('*.json'):walk(json.loads(p.read_text()))
for p in base.glob('*/runtime-private/*.json'):walk(json.loads(p.read_text()))
for p in base.glob('*/runtime-private/*.env'):
 for pair in p.read_text().splitlines():
  if '=' in pair:
   k,v=pair.split('=',1)
   if sensitive(k) and len(v)>=8:secrets.add(v.encode())
containers=subprocess.check_output(['docker','ps','-a','--format','{{.Names}}'],text=True).splitlines()
for c in json.loads(subprocess.check_output(['docker','inspect',*containers],text=True)):
 for pair in c['Config'].get('Env') or []:
  if '=' not in pair:continue
  k,v=pair.split('=',1)
  if len(v)>=8 and sensitive(k):secrets.add(v.encode())
files=[];excluded=[]
for name in names:
 d=base/name;assert d.is_dir(),name
 for p in sorted(d.rglob('*')):
  if not p.is_file():continue
  rel=p.relative_to(d)
  if any(x in rel.parts for x in ['runtime-private','image-context','bundle','__pycache__']) or p.suffix=='.pyc' or p.name.endswith('.before') or p.name=='online-path-source-review.tar.gz':
   excluded.append(str(p.relative_to(base)));continue
  assert not p.is_symlink(),'No symlink export'
  with p.open('rb') as stream:magic=stream.read(4)
  if magic==b'\x7fELF':
   excluded.append(str(p.relative_to(base)));continue
  limit=64*1024*1024 if str(p.relative_to(base)) in ['online-maintenance-gateway-candidate-build-20261005/link.map','private-batch-message-build-image-20261005/link.map'] else 20*1024*1024
  assert p.stat().st_size<limit,'Onlybounded evidence, fullknownownlinkmap64MiB guard'
  data=p.read_bytes()
  assert not any(secret in data for secret in secrets),'Credential scan failed in '+str(p.relative_to(base))
  files.append((p,hashlib.sha256(data).hexdigest()))
audit=base/'post-restart-export-audit-before-v68.json';assert not audit.exists()
audit.write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Export completed evidence including every failed rate run, tests, source iterations and deployment audit','source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'directories':names,'credential_exact_scan':'PASS: production and owned private config plus live/stopped sensitive environments, values only in RAM','files':[{'path':str(p.relative_to(base)),'sha256':h} for p,h in files],'excluded_paths':excluded,'mutations':'Fresharchive/audit only; sealedGW image/deployment/source/realowned sessionTTL/functionalchain/matched10k150 pressure rawresults andanyfailures/rollback present. Preserveallstages, no deletion. Candidate only2GWs, other17/config/apps fixed; explicitownrecord CAS TTL1 andnormalfixtureAPI SQL history preserved. No fullfeature/extreme50k acceptance inferred. Privateconfig/env/log/ELF excluded.'},indent=2)+'\n')
with tarfile.open(archive,'w:gz') as t:
 t.add(audit,arcname=audit.name)
 for p,h in files:
  assert hashlib.sha256(p.read_bytes()).hexdigest()==h,'Completed stage changed';t.add(p,arcname=str(p.relative_to(base)))
print('FILES='+str(len(files)));print('ARCHIVE='+str(archive));print('SHA256='+hashlib.sha256(archive.read_bytes()).hexdigest())
PY
