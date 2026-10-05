from pathlib import Path
import json,hashlib,datetime,statistics,math
r=Path(__file__).resolve().parent.parent
g=r/'evidence/gateway-phase-review-attempt2-20261005/gateway-phase-numeric.json'
p=r/'evidence/post-restart-v22-20261005/guarded-update-cost-outcome-review-20261005/baseline-slow-persistence-numeric.json'
l=r/'evidence/ack-persistence-join-20261005/paired-numeric.json'
d=r/'evidence/gateway-repository-common-join-20261005';assert not d.exists()
audit={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Read-only exact sameMID Gateway RPC and repository duration plusclient ledger join, no subtraction of differentpopulations','inputs':{str(x.relative_to(r)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [g,p,l]},'writes':'New owned numeric report only','limits':'Intersection of two threshold/rate-limited logs is selectedslow subset, not fullpopulationP99 or CPU proof. RPC minus repository includes remoteadmission/transport/service work outside measuredrepository/return, not exclusive gRPC/kernel. Gateway work/dispatch is not entireclient latency; ACK can bequeuedbeforephase destructor. No candidate repository phase available, only originalA1 jointsample.','runtime_changes':False,'newload':False,'rollback':'Preserve raw andderived records, no overwrite/delete'}
d.mkdir();(d/'audit-before.json').write_text(json.dumps(audit,indent=2)+'\n',encoding='utf-8')
repos={x['mid']:x for x in json.loads(p.read_text(encoding='utf-8'))};ledgers={x['mid']:x for x in json.loads(l.read_text(encoding='utf-8'))}
rows=[];missing=[]
for gw in json.loads(g.read_text(encoding='utf-8')):
 if gw['run']!='guard150A1' or gw['path']!='chat':continue
 mid=gw['message_id']
 if mid not in repos:missing.append(mid);continue
 repo=repos[mid];ledger=ledgers[mid];assert gw['user_id']==repo['from']==ledger['sender'] and repo['to']==ledger['recipient']
 extra=(gw['persist_us']-repo['total_us'])/1000
 row={'mid':mid,'sender':ledger['sender'],'recipient':ledger['recipient'],'gateway':gw['gateway'],'rpc_repository_duration_order_qualified':extra>=0,'gateway_dispatch_age_ms':gw['dispatch_age_us']/1000,'gateway_work_ms':gw['work_us']/1000,'permission_rpc_ms':gw['permission_us']/1000,'route_ms':gw['route_us']/1000,'persist_rpc_ms':gw['persist_us']/1000,'repository_ms':repo['total_us']/1000,'persist_rpc_outside_repository_ms':extra,'unread_ms':gw['unread_us']/1000,**{key:ledger[key] for key in ['send_to_ack_ms','before_repository_ms','after_repository_to_ack_ms']},'commit_ms':repo['commit_us']/1000}
 rows.append(row)
def stats(values):
 v=sorted(values);assert v
 return {'samples':len(v),'mean_ms':statistics.mean(v),'p50_ms':v[math.ceil(len(v)*.5)-1],'p95_ms':v[math.ceil(len(v)*.95)-1],'max_ms':v[-1]}
keys=['send_to_ack_ms','gateway_dispatch_age_ms','gateway_work_ms','permission_rpc_ms','route_ms','persist_rpc_ms','repository_ms','persist_rpc_outside_repository_ms','unread_ms','before_repository_ms','after_repository_to_ack_ms','commit_ms']
x={'status':'GATEWAY_REPOSITORY_COMMON_MID_JOIN_COMPLETE','same_message_matched':len(rows),'gateway_without_repository_sample':len(missing),'original_repository_samples':len(repos),'all_rpc_repository_duration_order_qualified':all(row['rpc_repository_duration_order_qualified'] for row in rows),'same_sample_only_metrics':{key:stats([row[key] for row in rows]) for key in keys},'limits':audit['limits'],'full_feature_acceptance':False}
(d/'paired-numeric.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8');(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8');print(json.dumps(x,indent=2))
