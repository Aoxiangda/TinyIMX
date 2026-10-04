#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,collections,hashlib,csv
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'rate-curve-first-failure-diagnostic-20261005';assert not d.exists();d.mkdir()
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Completed300/s run failure response and immutable reconciliation/digest only','runtime_changes':[],'limits':'Response logs may be bounded; never count sampled reasons as full negative population'})+'\n')
run='acrate300a';p=b/('capacity-'+run);assert (p/'container-identity-after.json').exists();v=json.loads((p/'reconciliation.json').read_text());summary=json.loads((p/'summary.json').read_text());counts=collections.Counter()
for q in p.glob('worker-*/failure-responses.jsonl'):
 for line in q.read_text().splitlines():
  a=json.loads(line);counts[a.get('kind','')+':'+a.get('reason','')]+=1
recon={k:(len(x) if isinstance(x,list) else x) for k,x in v.items()}
parent=b/'unread-atomic-counts-rate-curve-20261005'/run
def digest(q):
 with q.open() as f:return {x[0]:x for x in list(csv.reader(f,delimiter='\t'))[1:]}
before=digest(parent/'digest-before.tsv');after=digest(parent/'digest-after.tsv');rows=[]
for k,x in after.items():
 old=before.get(k,[k,'','0','0','0','0']);calls=int(x[2])-int(old[2]);timer=int(x[3])-int(old[3]);assert calls>=0 and timer>=0
 if calls:rows.append({'digest':x[1],'calls':calls,'total_ms':timer/1e9,'mean_ms':timer/calls/1e9})
rows.sort(key=lambda x:x['total_ms'],reverse=True)
out={'run':run,'summary':summary,'saved_response_reason_counts':dict(counts),'reconciliation_counts':recon,'digest_deltas':rows,'source_sha256':{q.name:hashlib.sha256(q.read_bytes()).hexdigest() for q in [p/'summary.json',p/'reconciliation.json',parent/'digest-before.tsv',parent/'digest-after.tsv']}}
(d/'summary.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({k:v for k,v in out.items() if k not in ['digest_deltas','summary','source_sha256']},indent=2));print('TOP_SQL='+json.dumps(rows[:12]))
PY
