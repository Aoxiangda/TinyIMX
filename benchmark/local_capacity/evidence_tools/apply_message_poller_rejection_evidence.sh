#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,base64,subprocess,tarfile,tempfile,os,datetime,ast
r=pathlib.Path.cwd(); raw=pathlib.Path('/tmp/codex-tinyimx-message-poller-rejection-evidence.json').read_bytes()
assert hashlib.sha256(raw).hexdigest()=='12b74d81d9b5551ccb9bb4ad3211f249d8c2d40b028b8660850410d6a107ceb1';m=json.loads(raw)
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==m['baseline_head']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert not subprocess.check_output(['git','diff','--cached','--name-only'],text=True).strip()
expected=set(["services/message/server/MessageServiceServer.cpp","docs/performance/ISSUES.md","docs/performance/CAPACITY_RUNBOOK.md","docs/performance/ROOT_CAUSE_REVIEW_20261005.md","benchmark/local_capacity/evidence_tools/apply_storage_wait_diagnostics.sh","benchmark/local_capacity/evidence_tools/build_storage_wait_diagnostics.sh","benchmark/local_capacity/evidence_tools/test_storage_wait_isolated_mysql.sh","benchmark/local_capacity/evidence_tools/test_storage_wait_pool_recovery.sh","benchmark/local_capacity/evidence_tools/build_storage_wait_diagnostics_image.sh","benchmark/local_capacity/evidence_tools/deploy_storage_wait_diagnostics_message.sh","benchmark/local_capacity/evidence_tools/run_storage_wait_diagnostic.sh","benchmark/local_capacity/evidence_tools/analyze_storage_wait_diagnostic.sh","benchmark/local_capacity/evidence_tools/rollback_storage_wait_diagnostics.sh","benchmark/local_capacity/evidence_tools/export_post_restart_evidence_v12.sh","benchmark/local_capacity/evidence_tools/review_storage_wait_matches_readonly.sh","benchmark/local_capacity/evidence_tools/apply_message_sync_poller_retention.sh","benchmark/local_capacity/evidence_tools/build_message_sync_poller_retention.sh","benchmark/local_capacity/evidence_tools/build_message_poller_retention_image.sh","benchmark/local_capacity/evidence_tools/deploy_message_poller_diagnostic.sh","benchmark/local_capacity/evidence_tools/run_message_poller_diagnostic.sh","benchmark/local_capacity/evidence_tools/analyze_message_poller_diagnostic.sh","benchmark/local_capacity/evidence_tools/rollback_message_poller_retention.sh","benchmark/local_capacity/evidence_tools/export_post_restart_evidence_v13.sh","benchmark/local_capacity/evidence_tools/read_storage_commit_costs.sh","benchmark/local_capacity/evidence_tools/run_storage_io_baseline.sh"])
assert {e['path'] for e in m['files']}==expected
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines(); assert len(names)==19
 cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
 for c in cs:
  if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:
   assert c['Image']=='sha256:1d8d71faa91a5a1e0a261c2a89efa1027a93515ae2b94b2c2d06f48010a7486e'
   env=dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x);assert env.get('TINYIMX_MESSAGE_WORKER_THREADS')=='16'
  if c['Name']=='/tinyimx-m21-message-service-1':assert c['Image']=='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898'
 config=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 assert json.loads((config/'message.json').read_text())['mysql']['pool_size']==16
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in config.glob('*.json')}}
before=runtime();d=r/'.local/codex/message-poller-rejection-evidence-source-20261005';assert not d.exists();d.mkdir();entries=[]
for e in m['files']:
 p=(r/e['path']).resolve();assert p.is_relative_to(r)
 if e['before_sha256'] is None:assert not p.exists()
 else:assert hashlib.sha256(p.read_bytes()).hexdigest()==e['before_sha256']
 data=base64.b64decode(e['bytes_base64'],validate=True);assert hashlib.sha256(data).hexdigest()==e['after_sha256'];entries.append((p,data))
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
 p.parent.mkdir(parents=True,exist_ok=True);fd,n=tempfile.mkstemp(prefix='.storage-wait-',dir=p.parent)
 with os.fdopen(fd,'wb') as f:f.write(data)
 os.chmod(n,p.stat().st_mode&0o777 if p.exists() else 0o644);os.replace(n,p)
 if p.suffix=='.sh':subprocess.run(['bash','-n',str(p)],check=True)
subprocess.run(['git','diff','--check'],check=True)
(d/'commit-audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Restore exact Message defaultpoller source, record225.5ms rejection and preserve completed diagnostic helpers plus guarded pending I/O baseline','files':sorted(expected),'validation':'Exact hashes, embedded Python AST, bash syntax and git whitespace; repository commit preflight follows','runtime_changes':False,'new_load_tests':False,'cpp_changed':True,'push':False,'rollback':'Parent Git and source-before archive; no reset or deletion'},indent=2)+'\n')
subprocess.run(['git','add','--',*sorted(expected)],check=True);assert set(subprocess.check_output(['git','diff','--cached','--name-only'],text=True).splitlines())==expected
subprocess.run(['git','commit','-m','perf(message): restore pollers after failed latency control'],check=True)
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();after=runtime();assert after==before
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');(d/'source-commit.txt').write_text(head+'\n');(d/'source.diff').write_bytes(subprocess.check_output(['git','show','--binary',head]))
(d/'summary.json').write_text(json.dumps({'status':'REJECTED_POLLER_SOURCE_RESTORED_AND_EVIDENCE_COMMITTED','head':head,'parent':m['baseline_head'],'files':sorted(expected),'all19_container_ids_images_started_preserved':True,'all_private_config_sha_preserved':True,'new_load_tests':False,'build':False,'deployment':False,'cpp_changed':True,'compiled_revisions':{'gateway':'33fc9bb','message':'ddc7e8e'},'business_acceptance':False},indent=2)+'\n')
print('DIAGNOSTIC_COMMIT='+head);print('RUNTIME_PRESERVED_NO_NEW_LOAD')
PY
