#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re,statistics,csv
r=pathlib.Path.cwd();base=r/'.local/codex';d=base/'single-lease-completed-phase-diagnostic-20261004';assert not d.exists()
assert (base/'private-single-lease-capacity-controls-20261004/summary.json').exists(),'Do not analyze while original scale matrix active'
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly bounded numeric phase extraction and saved normalized SQL digest delta analysis after matrix finishes','reads':'Existing container logs between each completed parent audit UTC and its summary mtime; allowlisted numeric fields only; saved digest counts only','writes':'Fresh diagnostic evidence; no raw log, SQLliteral, environment or credentials','limits':'Slow rate-limited samples include ramp/drain; NOT population P99 or unique cause; SUM_LOCK_TIME is not InnoDB row-wait proof','changes':[],'other_apps':'Preserved','rollback':'None'},indent=2)+'\n')
def metrics(rows,keys):
 out={}
 for k in keys:
  values=[x[k]/1000 for x in rows if x.get(k,-1)>=0]
  if values:out[k]={'samples':len(values),'median_ms':statistics.median(values),'mean_ms':statistics.mean(values),'max_ms':max(values)}
 return out
def digest(path):
 with path.open() as f:return {x[0]:x for x in list(csv.reader(f,delimiter='\t'))[1:]}
results=[]
for run,n,parent in [('slease1k1',1000,'private-single-lease-capacity-controls-20261004'),('slease10k1',10000,'private-single-lease-capacity-controls-20261004')]:
 p=base/parent/run;out=d/run;out.mkdir();since=json.loads((p/'audit-before.json').read_text())['utc'];until=datetime.datetime.fromtimestamp((p/'summary.json').stat().st_mtime,datetime.timezone.utc).isoformat();repo=[];gateway=[]
 for name in ['tinyimx-m21-message-service-1','tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1']:
  logs=subprocess.run(['docker','logs','--since',since,'--until',until,name],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30,check=True).stdout
  for line in logs.splitlines():
   if 'private_persist_phase ' in line:
    x={k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line.split('private_persist_phase ',1)[1])}
    if 700001<=x.get('from',0)<=700000+n:repo.append(x)
   elif 'gateway private chat phase sample, ' in line:
    x={}
    for item in line.split('gateway private chat phase sample, ',1)[1].strip().split(', '):
     k,sep,v=item.partition('=')
     if k=='path' and v in ['chat','peer']:x[k]=v
     elif sep and re.fullmatch('[a-z_]+',k) and re.fullmatch('-?[0-9]+',v):x[k]=int(v)
    if 700001<=x.get('user_id',0)<=700000+n:gateway.append({'gateway':name,**x})
  del logs
 byid={x['mid']:x for x in repo if x.get('mid',0)>0};matched=[{'mid':x['message_id'],'outside_repository_us':x['persist_us']-byid[x['message_id']]['total_us']} for x in gateway if x.get('path')=='chat' and x.get('message_id',0) in byid and x.get('persist_us',-1)>=0]
 for name,rows in [('repository',repo),('gateway',gateway),('matched',matched)]: (out/(name+'-numeric.json')).write_text(json.dumps(rows,indent=2)+'\n')
 before=digest(p/'digest-before.tsv');after=digest(p/'digest-after.tsv');deltas=[]
 for k,x in after.items():
  prev=before.get(k,[k,'','0','0','0','0']);calls=int(x[2])-int(prev[2]);timer=int(x[3])-int(prev[3]);lock=int(x[4])-int(prev[4]);exam=int(x[5])-int(prev[5])
  assert min(calls,timer,lock,exam)>=0,'Counter reset or eviction; no invented delta'
  if calls:deltas.append({'digest':x[1],'calls':calls,'total_ms':timer/1e9,'mean_ms':timer/calls/1e9,'statement_table_lock_ms':lock/1e9,'rows_examined':exam})
 deltas.sort(key=lambda x:x['total_ms'],reverse=True);(out/'digest-deltas.json').write_text(json.dumps(deltas,indent=2)+'\n')
 x={'run':run,'since':since,'until':until,'repository':metrics(repo,['total_us','precheck_us','acquire_us','begin_us','insert_us','identity_read_us','outbox_insert_us','commit_us']),'gateway_chat':metrics([x for x in gateway if x.get('path')=='chat'],['dispatch_age_us','work_us','permission_us','persist_us','route_us','unread_us']),'matched':metrics(matched,['outside_repository_us']),'sql_top':deltas[:15]};(out/'summary.json').write_text(json.dumps(x,indent=2)+'\n');results.append(x)
 print(json.dumps({'run':run,'repository_total':x['repository'].get('total_us'),'precheck':x['repository'].get('precheck_us'),'acquire':x['repository'].get('acquire_us'),'dispatch':x['gateway_chat'].get('dispatch_age_us'),'persist':x['gateway_chat'].get('persist_us')}),flush=True)
(d/'summary.json').write_text(json.dumps({'status':'DIAGNOSTIC_COMPLETED','runs':results,'warning':'Sampled metrics not full P99; no causal inference'},indent=2)+'\n')
PY
