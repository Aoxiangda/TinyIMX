#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,datetime,statistics,subprocess
b=pathlib.Path('.local/codex');d=b/'online-maintenance-joined-phases-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
phase=b/'online-maintenance-phase-subset-20261005';endpoint=b/'online-maintenance-endpoint-review-abba-20261005/summary.json'
e=json.loads(endpoint.read_text());assert e['status']=='ONLINE_MAINTENANCE_ENDPOINT_REVIEW_COMPLETE' and e['comparison_population_qualified']
p=json.loads((phase/'summary.json').read_text());assert p['status']=='ONLINE_MAINTENANCE_PHASE_SUBSET_COMPLETE'
inputs=[endpoint,phase/'summary.json',phase/'gateway-phase-numeric.json',phase/'persistence-phase-numeric.json']
for name in p['runs']:inputs+=sorted((b/('capacity-'+name)).glob('worker-*/ledger.tsv'))
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly sameMID intersection decomposition of retained biased slow-phase samples','inputs_sha256':{str(x.relative_to(b)):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs},'writes':'Fresh numeric subset only, no runtime/source/config/SQL writes or pressure','limits':'SameVM MONOTONIC microsecond phase vs nanosecond worker timestamps, rounding at most2us. Intersection is tiny biased sample, no population percentiles or solecause CPU attribution; rpc-minus-repository includes application/response/transport/scheduling and is NOT pure gRPC CPU. MissingCPU remains unknown','rollback':'Keep all stages and failures, no deletion'},indent=2)+'\n')
gw=json.loads((phase/'gateway-phase-numeric.json').read_text());repo=json.loads((phase/'persistence-phase-numeric.json').read_text());rows=[];reports=[]
for name in p['runs']:
 sends={};acks={}
 for path in sorted((b/('capacity-'+name)).glob('worker-*/ledger.tsv')):
  for line in path.read_text().splitlines():
   kind,uid,to,mid,seq,cid,when=line.split('\t');row={'uid':int(uid),'to':int(to),'mid':int(mid),'seq':int(seq),'cid':cid,'ns':int(when)}
   if kind=='send':assert cid not in sends;sends[cid]=row
   elif kind=='ack':assert int(mid)>0 and int(mid) not in acks;acks[int(mid)]=row
 assert len(sends)==len(acks)==9000
 chat={};persist={}
 for row in gw:
  if row['run']==name and row['path']=='chat':assert row['message_id'] not in chat;chat[row['message_id']]=row
 for row in repo:
  if row['run']==name:
   mid=row.get('message_id',row.get('mid',0));assert mid>0 and mid not in persist;persist[mid]=row
 selected=[]
 for mid in sorted(chat.keys() & persist.keys()):
  c,q,ack=chat[mid],persist[mid],acks[mid];sent=sends[ack['cid']]
  assert c['user_id']==q['from']==sent['uid']==ack['uid'] and q['to']==sent['to']==ack['to'] and c['request_seq']==sent['seq']==ack['seq']
  assert q['status']==0 and not q['threw'] and c['failed']==0
  ack_ms=(ack['ns']-sent['ns'])/1e6;before=(q['started_us']*1000-sent['ns'])/1e6;repository=q['total_us']/1000;after=(ack['ns']-(q['started_us']+q['total_us'])*1000)/1e6;outside=(c['persist_us']-q['total_us'])/1000
  assert before>=-.002 and after>=-.002 and outside>=-.002 and abs(ack_ms-before-repository-after)<.002
  row={'run':name,'mid':mid,'sender':sent['uid'],'recipient':sent['to'],'send_to_ack_ms':ack_ms,'before_repository_ms':before,'repository_ms':repository,'after_repository_ms':after,'gateway_persist_rpc_ms':c['persist_us']/1000,'persist_rpc_minus_repository_ms':outside,'dispatch_age_ms':c['dispatch_age_us']/1000,'permission_ms':c['permission_us']/1000,'route_ms':c['route_us']/1000,'unread_ms':c['unread_us']/1000,'repository_commit_ms':q['commit_us']/1000,'repository_cpu_ms':q['cpu_us']/1000 if q.get('cpu_us',-1)>=0 else None}
  selected.append(row);rows.append(row)
 metrics={key:{'samples':len(v),'mean_ms':statistics.mean(v),'max_ms':max(v)} for key in ['send_to_ack_ms','before_repository_ms','repository_ms','after_repository_ms','gateway_persist_rpc_ms','persist_rpc_minus_repository_ms','dispatch_age_ms','permission_ms','route_ms','unread_ms','repository_commit_ms'] if (v:=[row[key] for row in selected])}
 reports.append({'run':name,'chat_biased_samples':len(chat),'repository_biased_samples':len(persist),'same_mid_intersection':len(selected),'matched_biased_metrics':metrics,'limits':'Zero intersection means unknown, not zero cost. Exact per-row decomposition only; no disjoint mean subtraction/populationP99 or pure transportCPU interpretation'})
(d/'matched-numeric-rows.json').write_text(json.dumps(rows,indent=2)+'\n');x={'status':'ONLINE_MAINTENANCE_JOINED_PHASES_COMPLETE','reports':reports,'performance_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
