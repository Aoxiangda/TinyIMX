#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,base64,subprocess,tarfile,tempfile,os,datetime,ast
r=pathlib.Path.cwd(); raw=pathlib.Path('/tmp/codex-tinyimx-mysql-safe-roundtrip-probe.json').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='5fcd6613530d45d066a791820c05f1394f65e3c03c61890d2032f97bc69c94c0';m=json.loads(raw)
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==m['baseline_head']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert not subprocess.check_output(['git','diff','--cached','--name-only'],text=True).strip()
expected=set(['benchmark/local_capacity/mysql_safe_roundtrip_probe.cpp', 'benchmark/local_capacity/evidence_tools/run_mysql_safe_roundtrip_probe.sh', 'benchmark/local_capacity/evidence_tools/apply_mysql_transaction_roundtrip_probe.sh', 'benchmark/local_capacity/evidence_tools/export_post_restart_evidence_v38.sh', 'docs/performance/ISSUES.md', 'docs/performance/CAPACITY_RUNBOOK.md', 'docs/performance/ROOT_CAUSE_REVIEW_20261005.md'])
assert {entry["path"] for entry in m["files"]}==expected and len(m["files"])==len(expected)

def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines(); assert len(names)==19
 cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
 for c in cs:
  if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:
   assert c['Image']=='sha256:a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9'
   env=dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x);assert env.get('TINYIMX_MESSAGE_WORKER_THREADS')=='16' and env.get('TINYIMX_ONLINE_MAINTENANCE_BATCH_ENABLE')=='1'
  if c['Name']=='/tinyimx-m21-message-service-1':assert c['Image']=='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898'
  if c['Name']=='/tinyimx-m21-user-service-1':
   assert c['Image']=='sha256:38dca459e0a88141c1385503b28cbd6e29a1eed18d253f808dc1dfa89e0a0316'
   env=dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x);assert 'TINYIMX_AUTH_PHASE_TRACE_ENABLE' not in env
 config=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 assert json.loads((config/'message.json').read_text())['mysql']['pool_size']==16
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in config.glob('*.json')}}
before=runtime();d=r/'.local/codex/mysql-safe-roundtrip-probe-source-20261005';assert not d.exists();d.mkdir();entries=[]
for e in m['files']:
 p=(r/e['path']).resolve();assert p.is_relative_to(r)
 if e['before_sha256'] is None:assert not p.exists()
 else:assert hashlib.sha256(p.read_bytes()).hexdigest()==e['before_sha256']
 data=base64.b64decode(e['bytes_base64'],validate=True);assert hashlib.sha256(data).hexdigest()==e['after_sha256'];entries.append((p,data))
 if p.suffix=='.py':ast.parse(data.decode())
 if p.suffix=='.sh':
  code=[];opened=False
  for line in data.decode().splitlines():
   if line=="python3 - <<'PY'":opened=True;continue
   if opened and line=='PY':ast.parse('\n'.join(code));opened=False;code=[];continue
   if opened:code.append(line)
  assert not opened
(d/'apply-audit-before.json').write_text(json.dumps(m['audit'],indent=2)+'\n');(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
with tarfile.open(d/'source-before.tar.gz','w:gz') as t:
 for p,data in entries:
  if p.exists():t.add(p,arcname=str(p.relative_to(r)))
for p,data in entries:
 p.parent.mkdir(parents=True,exist_ok=True);fd,n=tempfile.mkstemp(prefix='.online-maintenance-',dir=p.parent)
 with os.fdopen(fd,'wb') as f:f.write(data)
 os.chmod(n,p.stat().st_mode&0o777 if p.exists() else 0o644);os.replace(n,p)
 if p.suffix=='.py':ast.parse(data.decode())
 if p.suffix=='.sh':subprocess.run(['bash','-n',str(p)],check=True)
subprocess.run(['git','diff','--check'],check=True)
(d/'commit-audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Save actual SQL roundtrip benefit and reject postCOMMIT identity validation; test safe BEGIN INSERT SELECT batching before C++ validation and separate outbox COMMIT using existing own tables, no productionCPP/DDL/config/runtime change','files':sorted(expected),'validation':'Exact hashes, embedded Python AST, bash syntax and git whitespace; repository commit preflight follows','runtime_changes':False,'new_load_tests':False,'cpp_changed':True,'production_cpp_changed':False,'push':False,'rollback':'Parent Git and source-before archive; no reset or deletion'},indent=2)+'\n')
subprocess.run(['git','add','--',*sorted(expected)],check=True);assert set(subprocess.check_output(['git','diff','--cached','--name-only'],text=True).splitlines())==expected
subprocess.run(['git','commit','-m','test(sql): validate transaction batching before identity and commit'],check=True)
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();after=runtime();assert after==before
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');(d/'source-commit.txt').write_text(head+'\n');(d/'source.diff').write_bytes(subprocess.check_output(['git','show','--binary',head]))
(d/'summary.json').write_text(json.dumps({'status':'MYSQL_SAFE_ROUNDTRIP_PROBE_SOURCE_COMMITTED','head':head,'parent':m['baseline_head'],'files':sorted(expected),'all19_container_ids_images_started_preserved':True,'all_private_config_sha_preserved':True,'new_load_tests':False,'build':False,'deployment':False,'cpp_changed':True,'production_cpp_changed':False,'compiled_revisions':{'gateway':'38f41a9','message':'ddc7e8e','user':'original38dca-unlabeled'},'business_acceptance':False},indent=2)+'\n')
print('DIAGNOSTIC_COMMIT='+head);print('RUNTIME_PRESERVED_NO_NEW_LOAD')
PY
