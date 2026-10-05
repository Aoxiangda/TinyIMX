#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,datetime,subprocess,os,math,statistics,re
b=pathlib.Path('.local/codex');mode=os.environ.get('CODEX_BATCH_REVIEW','firstpair');assert mode in ['firstpair','abba']
pairs=[('batch150A1','baseline'),('batch150B1','candidate')]+([('batch150B2','candidate'),('batch150A2','baseline')] if mode=='abba' else [])
d=b/('online-maintenance-endpoint-review-'+('abba-' if mode=='abba' else '')+'20261005');assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
inputs=[]
for name,phase in pairs:
 root=b/('online-maintenance-control-'+name);raw=b/('capacity-'+name);assert (root/'summary.json').exists()
 inputs+=[p for p in sorted(root.glob('*')) if p.is_file() and p.suffix in ['.json','.tsv','.txt']]
 inputs+=[p for p in sorted(raw.glob('worker-*/*')) if p.name in ['ledger.tsv','final.json']]
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly complete matchedendpoint population/CPU/SQL/PSI/wire ledger analysis','inputs_sha256':{str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'writes':'Freshowned numericalreports only, no load/reset/config/runtime/SQLwrite','limits':'Fixed10k150 privateonly sharedhostsequentialcontrols; commoncgroupelapsed withcapturebounds, counterSQLmeans notP99/exclusivecost; completeledgerall9000 only, signedwire-minusACK preserved. No allfeature/extreme50k acceptance','rollback':'Keepallraw/source/reports/failures, no deletion'},indent=2)+'\n')
latency_gates={'positive_ack_p99_le_100ms','scheduled_to_ack_p99_le_100ms'}
reports=[]
def digest(path):
 result={}
 for line in path.read_text().splitlines():
  parts=line.split('\t');assert len(parts)==6
  key,text=parts[:2];assert key not in result;result[key]={'text':text,'count':int(parts[2]),'time':int(parts[3]),'lock':int(parts[4]),'rows':int(parts[5])}
 return result
for name,phase in pairs:
 root=b/('online-maintenance-control-'+name);s=json.loads((root/'summary.json').read_text());assert s['status']=='ONLINE_MAINTENANCE_MATCHED_CONTROL_COMPLETED' and s['phase']==phase
 private=s['private'];gates=private.get('gates',{});non_latency={key:value for key,value in gates.items() if key not in latency_gates}
 qualified=bool(non_latency) and all(non_latency.values()) and s['heartbeat_exact_equality'] and not s['observer_error']
 item={'run':name,'phase':phase,'summary_sha256':hashlib.sha256((root/'summary.json').read_bytes()).hexdigest(),'message_image':s['message_image'],'message_binary_sha256':s['message_binary_sha256'],'private_exit':s['private_exit'],'positive_ack_p99_ms_upper_bin':private.get('positive_ack_p99_ms_upper_bin'),'scheduled_to_ack_p99_ms_upper_bin':private.get('scheduled_to_ack_p99_ms_upper_bin'),'raw_positive_ack_max_ms':private.get('raw_positive_ack_max_ms'),'metrics':private.get('metrics',{}),'gates':gates,'heartbeat_exact_equality':s['heartbeat_exact_equality'],'qualified_complete_non_latency_population':qualified,'cpu':{},'statement_deltas':[],'counter_window_valid':False}
 if not s['observer_error']:
  first=json.loads((root/'before-cgroup-cpu.json').read_text());last=json.loads((root/'after-cgroup-cpu.json').read_text());elapsed=(last['monotonic_ns']-first['monotonic_ns'])/1e9;assert elapsed>0
  start=int((root/'observed-active-start-ns.txt').read_text());frames=[json.loads((root/(label+'-snapshot.json')).read_text()) for label in ['before','after']]
  item['counter_window_valid']=all(frame['started_monotonic_ns']>=start and frame['ended_monotonic_ns']<=start+60_000_000_000 for frame in frames);item['counter_elapsed_seconds']=elapsed;item['counter_capture_elapsed_ms']=[frame['capture_elapsed_ms'] for frame in frames]
  for key,values in first['cpu'].items():
   delta={field:last['cpu'][key][field]-value for field,value in values.items()};assert all(value>=0 for value in delta.values())
   item['cpu'][key]={'mean_cores':delta['usage_usec']/1e6/elapsed,'user_cores':delta['user_usec']/1e6/elapsed,'system_cores':delta['system_usec']/1e6/elapsed,'throttled_usec':delta.get('throttled_usec'),'limits':'Container total includes background; near-synchronous19counter frame and onecommonelapsed, notexclusive RPCCPU'}
  before=digest(root/'before-digest.tsv');after=digest(root/'after-digest.tsv')
  for key,value in after.items():
   old=before.get(key,{'count':0,'time':0,'lock':0,'rows':0});count=value['count']-old['count'];ns=value['time']-old['time'];assert count>=0 and ns>=0
   text=value['text'].replace('`','')
   if count and ('im_private_messages' in text or text=='COMMIT'):
    item['statement_deltas'].append({'digest':key,'text':value['text'],'count':count,'mean_ms':ns/count/1e9,'rows_examined':value['rows']-old['rows'],'limits':'Normalized SQLcountermean, no perrequestP99; statements include background andsum times overlap'})
 reports.append(item)

def quantiles(values):
 v=sorted(values);assert v
 return {'samples':len(v),'mean_ms':statistics.mean(v),'p50_ms':v[math.ceil(len(v)*.5)-1],'p95_ms':v[math.ceil(len(v)*.95)-1],'p99_ms':v[math.ceil(len(v)*.99)-1],'p999_ms':v[math.ceil(len(v)*.999)-1],'max_ms':v[-1]}
for item in reports:
 name=item['run'];root=b/('online-maintenance-control-'+name);raw=b/('capacity-'+name)
 if not item['qualified_complete_non_latency_population'] or not item['counter_window_valid']:
  item['deeper_comparison']='Unqualified partialpopulation/window; rawfailure retained, no fabricated completequantiles';continue
 elapsed=item['counter_elapsed_seconds'];stat=[]
 for label in ['before','after']:stat.append({row.split()[0]:list(map(int,row.split()[1:])) for row in (root/(label+'-stat.txt')).read_text().splitlines()})
 delta=[z-a for a,z in zip(stat[0]['cpu'][:8],stat[1]['cpu'][:8])];assert all(value>=0 for value in delta) and sum(delta)>0
 guest={key:value*100/sum(delta) for key,value in zip(['user','nice','system','idle','iowait','irq','softirq','steal'],delta)}
 guest.update({'busy_percent':100-guest['idle']-guest['iowait'],'system_plus_softirq_percent':guest['system']+guest['softirq'],'context_switches_per_second':(stat[1]['ctxt'][0]-stat[0]['ctxt'][0])/elapsed,'guestwide_forks_threads_per_second':(stat[1]['processes'][0]-stat[0]['processes'][0])/elapsed});item['guest']=guest;pressure={}
 for kind in ['cpu','io','memory']:
  def totals(label):return {row.split()[0]:int(re.search(r'total=(\d+)',row).group(1)) for row in (root/(label+'-'+kind+'-pressure.txt')).read_text().splitlines()}
  a,z=totals('before'),totals('after');assert a.keys()==z.keys() and all(z[k]>=a[k] for k in a)
  pressure[kind]={key:(z[key]-a[key])/1e6/elapsed*100 for key in a}
 item['pressure_stall_percent']=pressure;item['cgroup19_mean_cores']=sum(row['mean_cores'] for row in item['cpu'].values())
 sends={};acks={};deliveries={};mid_to_cid={};kinds={}
 for path in sorted(raw.glob('worker-*/ledger.tsv')):
  data=path.read_bytes();assert data.endswith(b'\n')
  for line in data.decode().splitlines():
   fields=line.split('\t');assert len(fields)==7
   kind=fields[0];uid,to,mid,seq=map(int,fields[1:5]);cid=fields[5];when=int(fields[6]);kinds[kind]=kinds.get(kind,0)+1;row={'from':uid,'to':to,'mid':mid,'seq':seq,'cid':cid,'when':when}
   if kind=='send':assert cid and cid not in sends;sends[cid]=row
   elif kind=='ack':assert cid in sends and cid not in acks and mid>0 and mid not in mid_to_cid;acks[cid]=row;mid_to_cid[mid]=cid
   elif kind=='delivery':assert mid>0 and mid not in deliveries;deliveries[mid]=row
   else:raise AssertionError('Unexpectedledger kind inqualifiedpositivepopulation')
 assert len(sends)==len(acks)==len(deliveries)==9000 and set(sends)==set(acks) and set(deliveries)==set(mid_to_cid)
 paired=[]
 for cid,sent in sends.items():
  ack=acks[cid];delivery=deliveries[ack['mid']];assert all((row['from'],row['to'])==(sent['from'],sent['to']) for row in [ack,delivery]) and delivery['cid'] in ['',cid];assert ack['when']>=sent['when'] and delivery['when']>=sent['when']
  paired.append({'mid':ack['mid'],'sender':sent['from'],'recipient':sent['to'],'send_to_ack_ms':(ack['when']-sent['when'])/1e6,'send_to_wire_ms':(delivery['when']-sent['when'])/1e6,'wire_minus_sender_ack_ms':(delivery['when']-ack['when'])/1e6})
 (d/(name+'-ledger-paired-numeric.json')).write_text(json.dumps(paired,indent=2)+'\n');item['ledger_kinds']=kinds;item['delivery_before_sender_ack']=sum(row['wire_minus_sender_ack_ms']<0 for row in paired);item['paired_latency']={key:quantiles([row[key] for row in paired]) for key in ['send_to_ack_ms','send_to_wire_ms','wire_minus_sender_ack_ms']}
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
 item['original_histograms']=hist

valid=all(item['qualified_complete_non_latency_population'] and item['counter_window_valid'] for item in reports)
x={'status':'ONLINE_MAINTENANCE_ENDPOINT_REVIEW_COMPLETE','mode':mode,'reports':reports,'comparison_population_qualified':valid,'full_feature_acceptance':False,'limits':'Sequential sharedhost, fixedprivate10k150/s only. Noallfeatures/20k50k/AI/soak acceptance. Compare actualP99 andCPU/statementcost, notonlyworker/TIDcounts. Original100usupperbins used directly; no extra0.1ms. Invalidpartial/counterwindows notcost-attribution evidence.'}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
