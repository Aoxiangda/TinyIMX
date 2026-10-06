#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';inp=b/'group-confirm128-control-20261006';d=b/'group-confirm128-sql-analysis-20261006';assert not d.exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
s=json.loads((inp/'summary.json').read_text());assert s['status']=='GROUP_CONFIRM128_REAL_ABBA_COMPLETE'
names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));assert {c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}==json.loads((inp/'restore-summary.json').read_text())['runtime']
d.mkdir(mode=0o700);(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Only readonly saved Performance Schema snapshots, no SQL/pressure/runtime/config changes. New exactcounterdelta receipt, retain all normalized digesttext and timings','input_sha256':sha(inp/'summary.json')},indent=2)+'\n')
cases=[]
for case in ['A1','B1','B2','A2']:
 sizes=[]
 for size in [2,16,65,100]:
  paths=[inp/(case+'-s'+str(size)+'-sql-'+side+'.json') for side in ['before','after']];a,z=[json.loads(p.read_text()) for p in paths];before={v[0]:v for v in a['group-digests']};rows=[]
  for v in z['group-digests']:
   old=before.get(v[0],[v[0],v[1],*['0']*6]);assert old[1]==v[1]
   delta=[int(v[i])-int(old[i]) for i in range(2,8)];assert all(x>=0 for x in delta),'Digest reset detected: preserve failure'
   if delta[0]:rows.append({'digest':v[0],'normalized_sql':v[1],'count':delta[0],'statement_total_ms':delta[1]/1e9,'lock_total_ms':delta[2]/1e9,'errors':delta[3],'affected':delta[4],'examined':delta[5],'statement_mean_ms':delta[1]/1e9/delta[0]})
  st={v[0]:int(v[1]) for v in a['status']};status={v[0]:int(v[1])-st[v[0]] for v in z['status']};assert all(x>=0 for x in status.values())
  sizes.append({'size':size,'snapshots_sha256':{p.name:sha(p) for p in paths},'window_started_ns':a['started_mono_ns'],'window_finished_ns':z['finished_mono_ns'],'digests':rows,'status_delta':status})
 cases.append({'case':case,'confirmation_coalescing':int(case.startswith('B')),'sizes':sizes})
x={'status':'GROUP_CONFIRM128_SAVED_SQL_DELTAS_PASS','cases':cases,'limits':'Entire serialwindow33messages including3warmup, all global cumulative digest deltas in same database; includes claim/completion/observer and recovery, not exactoneoperation criticalpath. Statementtime includes engine wait, cannot infer perrequestfsync from count; no perrequest P99 inferred.'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n')
for c in cases:
 for size in [65,100]:
  z=next(v for v in c['sizes'] if v['size']==size);print(json.dumps({'case':c['case'],'size':size,'updates':[v for v in z['digests'] if v['normalized_sql'].startswith('UPDATE')],'status':z['status_delta']}))
PY
