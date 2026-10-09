#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,tarfile,io,datetime,re,subprocess
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-route-outcome-export-20261006';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
stages=['group-route-read-batch-source-20261006', 'group-route-read-batch-native-20261006', 'group-route-integration-source-20261006', 'group-route-integration-build-source-20261006', 'group-route-integration-build-20261006', 'group-route-batch-control-source-20261006', 'group-route-batch-control-20261006', 'cross-feature-groute1featA1', 'cross-feature-groute1featB1', 'cross-feature-groute1featB2', 'cross-feature-groute1featA2', 'group-route-outcome-source-20261006', 'group-route-partial-outcome-source-20261006', 'group-route-partial-drain-control-20261006', 'cross-feature-groutep1featA1', 'cross-feature-groutep1featB1', 'cross-feature-groutep1featB2', 'cross-feature-groutep1featA2']
assert json.loads((b/'group-route-partial-drain-control-20261006/summary.json').read_text())['status']=='GROUP_ROUTE_PARTIAL_DRAIN_REAL_ABBA_COMPLETE'
assert json.loads((b/'group-route-partial-drain-control-20261006/restore-summary.json').read_text())['status']=='ACCEPTED_GATEWAY_AND_MESSAGE_FULL_ENV_RESTORED'
archive=b/'group-route-outcome-evidence-20261006.tar.gz';assert not archive.exists()
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
selected=[];missing=[];excluded=[];total=0
for name in stages:
 base=b/name
 if not base.exists():missing.append(name);continue
 assert base.is_dir() and not base.is_symlink()
 for p in sorted(base.rglob('*')):
  rel=p.relative_to(b)
  if not p.is_file() or p.is_symlink():continue
  if 'runtime-private' in rel.parts or p.suffix not in {'.json','.jsonl','.log','.txt','.tsv','.make'} or p.name in {'original-inspect.json','config.json','ai-agent.json','mcp.json'}:
   excluded.append({'path':str(rel),'bytes':p.stat().st_size,'reason':'private_orbinary_orpayload_notpublicwhitelist'});continue
  assert p.stat().st_size<32*1024**2
  raw=p.read_bytes();content=bytes_safe(p,raw);total+=len(content);assert total<=audit['maximum_uncompressed_bytes']
  selected.append((str(rel),content,{'path':str(rel),'bytes':len(content),'sha256':hashlib.sha256(content).hexdigest(),'original_sha256':hashlib.sha256(raw).hexdigest(),'export_copy_changed':content!=raw}))
manifest={'status':'PUBLIC_EVIDENCE_MANIFEST','head':audit['head'],'files':[x[2] for x in selected],'files_count':len(selected),'bytes':total,'missing_stages_explicitly_retained':missing,'excluded_count':len(excluded),'raw_originals_retained':True,'runtime_private_exported':False,'source_documents':['docs/performance/ALL_FEATURE_OPTIMIZATION_20261006.md','docs/performance/GROUP_ROUTE_READ_BATCH_20261006.md','docs/performance/GROUP_FANOUT_PARTIAL_DRAIN_20261006.md']}
(d/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');(d/'excluded-index.json').write_text(json.dumps(excluded,indent=2)+'\n')
assert not missing,'Preserve missing whitelist diagnosis, never omit requested evidence'
with tarfile.open(archive,'w:gz') as tf:
 for name,content,_ in selected:
  info=tarfile.TarInfo(name);info.size=len(content);info.mode=0o600;tf.addfile(info,io.BytesIO(content))
 content=(d/'manifest.json').read_bytes();info=tarfile.TarInfo('manifest.json');info.size=len(content);info.mode=0o600;tf.addfile(info,io.BytesIO(content))
summary={'status':'PUBLIC_GROUP_ROUTE_EVIDENCE_EXPORTED','archive':str(archive),'archive_bytes':archive.stat().st_size,'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'files':len(selected),'uncompressed_public_bytes':total,'export_copies_changed':sum(x[2]['export_copy_changed'] for x in selected),'missing_stages':missing,'raw_original_guest_files_preserved':True,'full_feature_acceptance':False}
(d/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
PY
