from pathlib import Path
import json,hashlib,datetime,math,statistics
r=Path(__file__).resolve().parent.parent
b=r/'evidence/post-restart-v22-20261005/guarded-update-cost-outcome-review-20261005'
raw=r/'evidence/post-restart-v21-20261005/capacity-guard150A1/worker-0/ledger.tsv'
sources=[raw,b/'baseline-slow-persistence-numeric.json',b/'summary.json']
d=r/'evidence/ack-persistence-join-20261005';assert not d.exists()
spec={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Read-only exact MID and monotonic-time join of original9000client ledger and488biased slow persistence samples','inputs':{str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},'writes':'New owned numeric evidence only','runtime_changes':False,'new_load':False,'limits':'Original phase has >=10ms threshold/rate limit; common matched sample only, no populationP99, no phases assigned to unmatchedmessages. Clienteventtimestamps after enqueue differ slightly from rawACK histogram; no threadCPU attribution. Partition pre-repository includes client/nginx/Gateway queue/permission/route/RPC; post-repository includes service response/unread/ACK handoff. Only source review can refine context; no blind attribution toindividualsubstep.','rollback':'Preserve all raw records and reports, no deletion/overwrite'}
d.mkdir();(d/'audit-before.json').write_text(json.dumps(spec,indent=2)+'\n',encoding='utf-8')
sends={};acks={};deliveries={}
for line in raw.read_text(encoding='utf-8').splitlines():
 kind,uid,to,mid,seq,cid,when=line.split('\t');row={'sender':int(uid),'recipient':int(to),'mid':int(mid),'seq':int(seq),'cid':cid,'when_ns':int(when)}
 if kind=='send':assert cid not in sends;sends[cid]=row
 elif kind=='ack':assert row['mid'] not in acks;acks[row['mid']]=row
 elif kind=='delivery':assert row['mid'] not in deliveries;deliveries[row['mid']]=row
 else:raise AssertionError('Unexpected raw positive population event')
assert len(sends)==len(acks)==len(deliveries)==9000
phases=json.loads((b/'baseline-slow-persistence-numeric.json').read_text(encoding='utf-8'));joined=[];unmatched=[];seen=set()
for phase in phases:
 mid=phase.get('mid');assert mid not in seen;seen.add(mid)
 if phase.get('status')!=0 or mid not in acks:
  unmatched.append({'mid':mid,'status':phase.get('status'),'reason':'Phase status/missing positiveMID'});continue
 ack=acks[mid];sent=sends[ack['cid']];delivery=deliveries[mid]
 assert (phase['from'],phase['to'])==(sent['sender'],sent['recipient'])==(ack['sender'],ack['recipient'])==(delivery['sender'],delivery['recipient'])
 started=phase['started_us']*1000;ended=started+phase['total_us']*1000
 values={'send_to_ack_ms':(ack['when_ns']-sent['when_ns'])/1e6,'before_repository_ms':(started-sent['when_ns'])/1e6,'repository_ms':phase['total_us']/1000,'after_repository_to_ack_ms':(ack['when_ns']-ended)/1e6,'after_repository_to_wire_ms':(delivery['when_ns']-ended)/1e6}
 assert abs(values['send_to_ack_ms']-sum(values[k] for k in ['before_repository_ms','repository_ms','after_repository_to_ack_ms']))<1e-9
 qualified=all(values[key]>=0 for key in ['send_to_ack_ms','before_repository_ms','repository_ms','after_repository_to_ack_ms'])
 joined.append({'mid':mid,'sender':sent['sender'],'recipient':sent['recipient'],'phase_started_us':phase['started_us'],'phase_total_us':phase['total_us'],'time_order_qualified':qualified,**values,'phase_commit_ms':phase['commit_us']/1000})
def stats(values):
 v=sorted(values);assert v
 return {'samples':len(v),'mean_ms':statistics.mean(v),'p50_ms':v[math.ceil(len(v)*.5)-1],'p95_ms':v[math.ceil(len(v)*.95)-1],'p99_ms':v[math.ceil(len(v)*.99)-1],'max_ms':v[-1]}
out={'status':'ACK_PERSISTENCE_LEDGER_EXACT_JOIN_COMPLETE','ledger_population':9000,'phase_population':len(phases),'matched':len(joined),'unmatched':unmatched,'all_time_order_qualified':all(row['time_order_qualified'] for row in joined),'sample_only_metrics':{key:stats([row[key] for row in joined]) for key in ['send_to_ack_ms','before_repository_ms','repository_ms','after_repository_to_ack_ms','after_repository_to_wire_ms','phase_commit_ms']},'limits':spec['limits'],'full_feature_acceptance':False}
(d/'paired-numeric.json').write_text(json.dumps(joined,indent=2)+'\n',encoding='utf-8');(d/'summary.json').write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8');print(json.dumps(out,indent=2))
