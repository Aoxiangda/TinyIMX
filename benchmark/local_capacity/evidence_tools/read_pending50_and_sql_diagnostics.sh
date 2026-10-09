#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,collections,hashlib
r=pathlib.Path.cwd(); b=r/'.local/codex';d=b/'pending50-login-and-sql-readonly-20261005';assert not d.exists();d.mkdir()
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Read completed failure responses and saved normalized SQL digest deltas only','writes':'Fresh evidence only; no runtime query or changes','limits':'Digest interval includes ramp/drain; not full latency distribution or causal proof'})+'\n')
p=b/'capacity-pending50k1'; reasons=[]; hashes={}
for q in sorted(p.glob('worker-*/failure-responses.jsonl')):
 hashes[str(q.relative_to(b))]=hashlib.sha256(q.read_bytes()).hexdigest()
 for line in q.read_text().splitlines():
  v=json.loads(line);reasons.append({k:v[k] for k in ['kind','success','reason'] if k in v})
out={'failure':json.loads((p/'failure.json').read_text()),'responses':reasons,'reason_counts':dict(collections.Counter(str(v.get('reason')) for v in reasons)),'original_sha256':hashes,'sql':[]}
for run in ['pending1k1','pending10k1','pending20k1','pending30k1','pending50k1']:
 vals=json.loads((b/'pending-index-completed-phase-diagnostic-20261004'/run/'digest-deltas.json').read_text())
 rows=[v for v in vals if 'DISTINCTROW' in v['digest'] or 'pending_recipient' in v['digest']]
 out['sql'].append({'run':run,'pending_recipient':rows,'top_total_time':vals[:6]})
(d/'summary.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
PY
