#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,tarfile,io,datetime,re,subprocess
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-get-boundary-pollers-export-20261007';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
stages=['group-get-boundary-audit-20261007', 'group-get-boundary-source-20261007', 'group-get-boundary-build-20261007', 'group-get-boundary-build-repair-source-20261007', 'group-get-boundary-build-20261007-attempt2', 'group-get-boundary-control-20261007', 'group-get-time-namespace-review-20261007', 'group-get-boundary-control-repair-source-20261007', 'group-get-boundary-control-20261007-attempt2', 'group-get-boundary-analysis-20261007', 'message-rpc-pollers-source-20261007', 'message-rpc-pollers-build-20261007', 'message-rpc-pollers-build-repair-source-20261007', 'message-rpc-pollers-provenance-source-20261007', 'message-rpc-pollers-build-20261007-attempt2', 'message-rpc-pollers-control-20261007', 'message-rpc-pollers-analysis-20261007', 'message-rpc-pollers-outcome-source-20261007', 'cross-feature-ggb2featA1', 'cross-feature-ggb2featB1', 'cross-feature-ggb2featB2', 'cross-feature-ggb2featA2', 'cross-feature-mrp1featA1', 'cross-feature-mrp1featB1', 'cross-feature-mrp1featB2', 'cross-feature-mrp1featA2']
for stage,status in [('group-get-boundary-control-20261007-attempt2','GROUP_GET_BOUNDARY_REAL_ABBA_COMPLETE'),('message-rpc-pollers-control-20261007','MESSAGE_RPC_POLLERS_REAL_ABBA_COMPLETE')]:
 assert json.loads((b/stage/'summary.json').read_text())['status']==status
 assert json.loads((b/stage/'restore-summary.json').read_text())['status']=='ACCEPTED_GATEWAY_AND_MESSAGE_FULL_ENV_RESTORED'
archive=b/'group-get-boundary-pollers-evidence-20261007.tar.gz';assert not archive.exists()
d.mkdir(mode=0o700)
audit={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'operation':'Export onlyexactnewroundstage publicJSON/log/text/TSV with perfileSHA; preserve raw originalguest. No loads/runtime/config changes','stages':stages,'exclusions':'runtime-private, configs/Env/inspect, binaries/object/archive/payload andsymlinks. Sensitive JSON/JSONL stringkeys andBearer/logsecretfields redactedin exportcopies only; originalsha retained','maximum_uncompressed_bytes':128*1024**2,'destination':str(archive),'rollback':'Keep original+export/newmanifest, no deletion/prune'}
(d/'audit-before.json').write_text(json.dumps(audit,indent=2)+'\n')
sensitive={'password','passwd','password_hash','password_salt','api_key','authorization','bearer_token','access_token','refresh_token','session_token','upload_token','token','secret','private_key'}
def marker(value):return '[REDACTED_SHA256:'+hashlib.sha256(str(value).encode()).hexdigest()+']'
def scrub(x):
 if isinstance(x,dict):return {k:marker(v) if k.lower() in sensitive and isinstance(v,str) and v else scrub(v) for k,v in x.items()}
 if isinstance(x,list):return [scrub(v) for v in x]
 return x
def bytes_safe(p,raw):
 suffix=p.suffix
 if suffix=='.json':
  x=json.loads(raw.decode());z=scrub(x);return (json.dumps(z,indent=2)+'\n').encode() if z!=x else raw
 if suffix=='.jsonl':
  rows=[]
  for line in raw.decode().splitlines():
   x=json.loads(line);z=scrub(x);rows.append(json.dumps(z) if z!=x else line)
  return ('\n'.join(rows)+('\n' if rows else '')).encode()
 text=raw.decode('utf-8')
 text=re.sub(r'(?i)(Bearer\s+)([A-Za-z0-9._~-]{12,})',lambda m:m[1]+marker(m[2]),text)
 text=re.sub(r'(?i)("(?:password|passwd|api_key|authorization|token|upload_token|session_token|access_token|refresh_token)"\s*:\s*")([^"]*)(")',lambda m:m[1]+marker(m[2])+m[3],text)
 return text.encode()
malformed_native_fixtures={}
selected=[];missing=[];excluded=[];total=0
for name in stages:
 base=b/name
 if not base.exists():missing.append(name);continue
 assert base.is_dir() and not base.is_symlink()
 for p in sorted(base.rglob('*')):
  rel=p.relative_to(b)
  if not p.is_file() or p.is_symlink():continue
  if 'runtime-private' in rel.parts or p.suffix not in {'.json','.jsonl','.log','.txt','.tsv','.make'} or p.name in {'original-inspect.json','config.json','ai-agent.json','mcp.json'} or 'scratch' in p.name:
   excluded.append({'path':str(rel),'bytes':p.stat().st_size,'reason':'private_orbinary_orpayload_notpublicwhitelist'});continue
  assert p.stat().st_size<32*1024**2
  raw=p.read_bytes()
  if str(rel) in malformed_native_fixtures:
   assert hashlib.sha256(raw).hexdigest()==malformed_native_fixtures[str(rel)]
   # These exact hash-pinned deliberately malformed synthetic fixtures test FAIL visibility.
   # Preserve raw bytes; never accept arbitrary malformed production evidence silently.
   content=raw
  else:content=bytes_safe(p,raw)
  total+=len(content);assert total<=audit['maximum_uncompressed_bytes']
  selected.append((str(rel),content,{'path':str(rel),'bytes':len(content),'sha256':hashlib.sha256(content).hexdigest(),'original_sha256':hashlib.sha256(raw).hexdigest(),'export_copy_changed':content!=raw}))
failure=b/'message-rpc-pollers-build-20261007/runtime-private/compile-pollers-native.o.log';raw=failure.read_bytes();assert len(raw)<8192 and b'message.grpc.pb.h' in raw and b'fatal error' in raw;content=bytes_safe(failure,raw);name='message-rpc-pollers-build-20261007/native-compile-failure.log';total+=len(content);selected.append((name,content,{'path':name,'bytes':len(content),'sha256':hashlib.sha256(content).hexdigest(),'original_sha256':hashlib.sha256(raw).hexdigest(),'export_copy_changed':content!=raw,'original_private_path':str(failure.relative_to(b))}))
manifest={'status':'PUBLIC_EVIDENCE_MANIFEST','head':audit['head'],'files':[x[2] for x in selected],'files_count':len(selected),'bytes':total,'missing_stages_explicitly_retained':missing,'excluded_count':len(excluded),'raw_originals_retained':True,'runtime_private_exported':False,'intentional_malformed_native_fixtures_sha256':malformed_native_fixtures,'source_documents':['docs/performance/GROUP_GET_RPC_BOUNDARY_20261007.md','docs/performance/MESSAGE_RPC_POLLERS_20261007.md']}
(d/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');(d/'excluded-index.json').write_text(json.dumps(excluded,indent=2)+'\n')
assert not missing,'Preserve missing whitelist diagnosis, never omit requested evidence'
with tarfile.open(archive,'w:gz') as tf:
 for name,content,_ in selected:
  info=tarfile.TarInfo(name);info.size=len(content);info.mode=0o600;tf.addfile(info,io.BytesIO(content))
 content=(d/'manifest.json').read_bytes();info=tarfile.TarInfo('manifest.json');info.size=len(content);info.mode=0o600;tf.addfile(info,io.BytesIO(content))
summary={'status':'PUBLIC_GROUP_GET_BOUNDARY_POLLERS_EVIDENCE_EXPORTED','archive':str(archive),'archive_bytes':archive.stat().st_size,'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'files':len(selected),'uncompressed_public_bytes':total,'export_copies_changed':sum(x[2]['export_copy_changed'] for x in selected),'missing_stages':missing,'raw_original_guest_files_preserved':True,'full_feature_acceptance':False}
(d/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
PY
