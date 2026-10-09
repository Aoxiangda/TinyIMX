#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,math,statistics
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'guarded-update-cost-outcome-review-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
rollback=json.loads((b/'receiver-guarded-update-rollback-20261005/summary.json').read_text());assert rollback['status']=='ORIGINAL_B24_MESSAGE_RESTORED_VERIFIED'
comparison=json.loads((b/'receiver-guarded-update-comparison-20261005/summary.json').read_text());assert comparison['comparison_population_qualified']
c=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-message-service-1'],text=True))[0];assert c['Id']==rollback['message_service_id'] and c['Image']==rollback['image']
inputs=[b/'receiver-guarded-update-comparison-20261005/summary.json',b/'receiver-guarded-update-rollback-20261005/summary.json']
for name in ['guard150A1','guard150B1']:
 root=b/('receiver-guarded-update-control-'+name)
 inputs += [root/(label+'-'+kind+suffix) for label in ['before','after'] for kind,suffix in [('stat','.txt'),('cpu-pressure','.txt'),('io-pressure','.txt'),('memory-pressure','.txt'),('snapshot','.json')]]
 inputs += [p for p in sorted((b/('capacity-'+name)).glob('worker-*/ledger.tsv'))]
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly complete-ledger latency and active guest pressure review after verified original restoration','writes':'Fresh numerical report only','inputs':{str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'limits':'Shared host sequential A/B; cgroup includes background; ledger timestamp after client enqueue differs slightly from ACK histogram origin. All client/DB gates stay mandatory. CPUpressure and wall samples cannot attribute IO versus runqueue exclusively. Baseline optional slow persistence trace is biased and rate-limited; candidate service phase log not captured before rollback, cannot compare phases.','runtime_changes':False,'load_started':False,'rollback':'All source/image/raw records retained, no deletion'},indent=2)+'\n')
def quantiles(values):
 assert values;v=sorted(values);return {'samples':len(v),'mean_ms':statistics.mean(v),'p50_ms':v[math.ceil(len(v)*.5)-1],'p95_ms':v[math.ceil(len(v)*.95)-1],'p99_ms':v[math.ceil(len(v)*.99)-1],'p999_ms':v[math.ceil(len(v)*.999)-1],'max_ms':v[-1]}
reports=[]
for comparison_item in comparison['reports']:
 name=comparison_item['run'];root=b/('receiver-guarded-update-control-'+name);raw=b/('capacity-'+name)
 elapsed=comparison_item['counter_elapsed_seconds'];frames=[json.loads((root/(label+'-snapshot.json')).read_text()) for label in ['before','after']]
 # stat/PSI are read after SQL in each frame. Their exact instants lie inside
 # these bounds; use common started-frame elapsed with explicit uncertainty.
 stat=[]
 for label in ['before','after']:stat.append({row.split()[0]:list(map(int,row.split()[1:])) for row in (root/(label+'-stat.txt')).read_text().splitlines()})
 delta=[v-u for u,v in zip(stat[0]['cpu'][:8],stat[1]['cpu'][:8])];assert all(v>=0 for v in delta) and sum(delta)>0
 names=['user','nice','system','idle','iowait','irq','softirq','steal'];guest={key:val*100/sum(delta) for key,val in zip(names,delta)}
 guest.update({'busy_percent':100-guest['idle']-guest['iowait'],'system_plus_softirq_percent':guest['system']+guest['softirq'],'context_switches_per_second':(stat[1]['ctxt'][0]-stat[0]['ctxt'][0])/elapsed,'guestwide_forks_threads_per_second':(stat[1]['processes'][0]-stat[0]['processes'][0])/elapsed})
 pressure={}
 for kind in ['cpu','io','memory']:
  def totals(label):return {row.split()[0]:int(re.search(r'total=(\d+)',row).group(1)) for row in (root/(label+'-'+kind+'-pressure.txt')).read_text().splitlines()}
  a,z=totals('before'),totals('after');assert a.keys()==z.keys() and all(z[k]>=a[k] for k in a)
  pressure[kind]={key:(z[key]-a[key])/1e6/elapsed*100 for key in a}
 sends={};acks={};deliveries={};mid_to_cid={};kinds={}
 for path in sorted(raw.glob('worker-*/ledger.tsv')):
  data=path.read_bytes();assert data.endswith(b'\n')
  for line in data.decode().splitlines():
   fields=line.split('\t');assert len(fields)==7
   kind=fields[0];uid,to,mid,seq=map(int,fields[1:5]);cid=fields[5];when=int(fields[6]);kinds[kind]=kinds.get(kind,0)+1
   item={'from':uid,'to':to,'mid':mid,'seq':seq,'cid':cid,'when':when}
   if kind=='send':assert cid and cid not in sends;sends[cid]=item
   elif kind=='ack':assert cid in sends and cid not in acks and mid>0 and mid not in mid_to_cid;acks[cid]=item;mid_to_cid[mid]=cid
   elif kind=='delivery':assert mid>0 and mid not in deliveries;deliveries[mid]=item
   else:raise AssertionError('Complete positive population has unexpected ledger kind')
 assert len(sends)==len(acks)==len(deliveries)==9000 and set(acks)==set(sends) and set(deliveries)==set(mid_to_cid)
 paired=[]
 for cid,sent in sends.items():
  ack=acks[cid];delivery=deliveries[ack['mid']]
  assert all((record['from'],record['to'])==(sent['from'],sent['to']) for record in [ack,delivery]) and delivery['cid'] in ['',cid]
  assert ack['when']>=sent['when'] and delivery['when']>=sent['when']
  paired.append({'mid':ack['mid'],'sender':sent['from'],'recipient':sent['to'],'send_to_ack_ms':(ack['when']-sent['when'])/1e6,'send_to_wire_ms':(delivery['when']-sent['when'])/1e6,'wire_minus_sender_ack_ms':(delivery['when']-ack['when'])/1e6})
 (d/(name+'-ledger-paired-numeric.json')).write_text(json.dumps(paired,indent=2)+'\n')
 # Fixed 100us ceiling bins from original worker are preserved separately.
 hs={key:[] for key in ['ack_histogram','scheduled_to_ack_histogram','schedule_lag_histogram','login_histogram']}
 for path in sorted(raw.glob('worker-*/final.json')):
  final=json.loads(path.read_text());assert final['status']=='COMPLETED'
  for key in hs:hs[key].append(final[key])
 hist={}
 for key,values in hs.items():
  bins={};count=0;total=0;maximum=0
  for value in values:
   count+=value['count'];total+=value['sum_us'];maximum=max(maximum,value['max_us'])
   for k,n in value['bins']:bins[k]=bins.get(k,0)+n
  assert count==sum(bins.values()) and count>0
  out={'samples':count,'mean_ms':total/count/1000,'raw_max_ms':maximum/1000}
  for label,fraction in [('p50_ms_upper_bin',.5),('p95_ms_upper_bin',.95),('p99_ms_upper_bin',.99),('p999_ms_upper_bin',.999)]:
   target=math.ceil(count*fraction);cumulative=0
   for k,n in sorted(bins.items()):
    cumulative+=n
    if cumulative>=target:out[label]=k/1000;break
  hist[key]=out
 reports.append({'run':name,'guest':guest,'pressure_stall_percent':pressure,'counter_elapsed_seconds':elapsed,'capture_elapsed_ms':[frame['capture_elapsed_ms'] for frame in frames],'ledger_kinds':kinds,'paired_latency':{key:quantiles([row[key] for row in paired]) for key in ['send_to_ack_ms','send_to_wire_ms','wire_minus_sender_ack_ms']},'delivery_before_sender_ack':sum(row['wire_minus_sender_ack_ms']<0 for row in paired),'original_histograms':hist,'cgroup_19_total_mean_cores':sum(row['mean_cores'] for row in comparison_item['cpu'].values()),'limits':'Guestwide fork/thread counts include all apps/observers. Pressure total is common-elapsed approximation within saved capture bounds; not IO or kernel CPU attribution. Client enqueue and event timestamp origins differ slightly.'})
baseline=b/'receiver-guarded-update-message-deployment-20261005/runtime-private/message-service-before.log';rows=[]
start=int((b/'capacity-guard150A1/control/start_ns').read_text())//1000;end=start+60_000_000
with baseline.open() as stream:
 for line in stream:
  if 'private_persist_phase ' not in line:continue
  row={k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line.split('private_persist_phase ',1)[1])}
  if start<=row.get('started_us',-1)<end and 700001<=row.get('from',0)<=710000:rows.append(row)
(d/'baseline-slow-persistence-numeric.json').write_text(json.dumps(rows,indent=2)+'\n')
phase={key:{'samples':len(values),'mean_ms':statistics.mean(values),'max_ms':max(values)} for key in ['total_us','acquire_us','precheck_us','begin_us','insert_us','identity_read_us','outbox_insert_us','commit_us'] if (values:=[row[key]/1000 for row in rows if row.get(key,-1)>=0])}
x={'status':'GUARDED_UPDATE_COST_OUTCOME_REVIEW_COMPLETE','reports':reports,'baseline_slow_persistence_phase':phase,'baseline_phase_limit':'Threshold>=10ms plus rate limiter, biased slow sample; CPU tracing OFF (-1), no production CPU partition or population P99. No candidate phase log captured before rollback; no phase comparison claim.','decision':'Normal-query mechanism and modest Message CPU decrease confirmed, no convincing endpoint tail improvement or MySQL CPU/commit improvement; runtime restored original, guarded candidate retained as evaluated code/image. Allfeature/extreme target unmet. Next inspect shared busy online/status/social paths and actual durable wait critical path before changing product.','full_feature_acceptance':False,'rollback_verified':rollback};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
