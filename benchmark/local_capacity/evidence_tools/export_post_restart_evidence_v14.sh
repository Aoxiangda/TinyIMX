#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,tarfile,datetime,subprocess
r=pathlib.Path.cwd();base=r/'.local/codex';archive=base/'post-restart-evidence-v14.tar.gz';assert not archive.exists()
names='''message-poller-rejection-evidence-source-20261005 storage-io-baseline-control-20261005 capacity-io150base storage-io-login-abort-review-20261005 auth-phase-diagnostics-source-20261005 auth-phase-diagnostics-build-20261005 auth-phase-diagnostics-image-20261005 auth-phase-diagnostics-user-deployment-20261005 auth-phase-diagnostic-login-control-20261005 capacity-auth10kdiag auth-phase-completed-analysis-20261005 auth-phase-diagnostics-user-restored-20261005 auth-phase-user-restoration-review-20261005 auth-init-thread-count-review-20261005 auth-phase-outcomes-source-20261005'''.split()
assert len(names)==len(set(names))
assert json.loads((base/'auth-phase-user-restoration-review-20261005/summary.json').read_text())['status']=='ORIGINAL_USER_38DCA_RESTORATION_VERIFIED'
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
  if any(x in rel.parts for x in ['runtime-private','image-context','bundle','__pycache__']) or p.suffix=='.pyc' or p.name.endswith('.before'):
   excluded.append(str(p.relative_to(base)));continue
  assert not p.is_symlink(),'No symlink export'
  assert p.stat().st_size<20*1024*1024,'Only bounded evidence files'
  data=p.read_bytes()
  if data.startswith(b'\x7fELF'):
   excluded.append(str(p.relative_to(base)));continue
  assert not any(secret in data for secret in secrets),'Credential scan failed in '+str(p.relative_to(base))
  files.append((p,hashlib.sha256(data).hexdigest()))
audit=base/'post-restart-export-audit-before-v14.json';assert not audit.exists()
audit.write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Export completed evidence including every failed rate run, tests, source iterations and deployment audit','source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'directories':names,'credential_exact_scan':'PASS: production and owned private config plus live/stopped sensitive environments, values only in RAM','files':[{'path':str(p.relative_to(base)),'sha256':h} for p,h in files],'excluded_paths':excluded,'mutations':'Fresh audit/archive only, originals immutable; Completed originalIObaseline abortedbeforewindow,36 authchecks,10khold/61440HB/804 numericphases/matchedpairs, exactoriginalUserrestorationverified, histogram/initthread/ENVorder corrections andsourcehelperpreservation. Failedrollbackstage retained withseparatelycompleted verification. No activestage/privateconfig/env/fullservicelog/binary exported.'},indent=2)+'\n')
with tarfile.open(archive,'w:gz') as t:
 t.add(audit,arcname=audit.name)
 for p,h in files:
  assert hashlib.sha256(p.read_bytes()).hexdigest()==h,'Completed stage changed';t.add(p,arcname=str(p.relative_to(base)))
print('FILES='+str(len(files)));print('ARCHIVE='+str(archive));print('SHA256='+hashlib.sha256(archive.read_bytes()).hexdigest())
PY
