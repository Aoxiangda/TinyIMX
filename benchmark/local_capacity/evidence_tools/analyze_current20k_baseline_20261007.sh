#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re,hashlib,math,statistics,collections
r=pathlib.Path.cwd();b=r/'.local/codex';raw=b/'capacity-cur20kT';control=b/'current20k-baseline-20261007-attempt4';d=b/'current20k-baseline-analysis-20261007';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
point=json.loads((control/'summary.json').read_text());preserved=json.loads((control/'preservation-summary.json').read_text());assert preserved['status']=='ALL19_INSTANCES_CONFIGS_AND_DURABILITY_PRESERVED'
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
 cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
 return {c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
before=runtime();assert before==preserved['runtime']
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
inputs=[control/'summary.json',raw/'summary.json',raw/'reconciliation.json',*sorted(raw.glob('worker-*/ledger.tsv')),*sorted(raw.glob('worker-*/final.json')),control/'mixed20k-ON/list-openloop/requests.jsonl',b/'cross-feature-curfeat20kT/operations.json']
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly current completed20k analysis: exact8100send-ACK-wire associations, sparse sameMID Gateway/repository phases, originalresource andcrossfeature/list windows; no newload/runtimechanges','input_sha256':{str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'runtime':before,'limits':'Sparse slow-biased/rate-limited phases notpopulationP99; wallminusCPU cannot uniquelyattribute IO/scheduler; dockerstats are intervals, PSI notCPUusage. Preserve allfailures/partialcases/rawlogs privately'})
sends={};acks={};delivery={}
for p in sorted(raw.glob('worker-*/ledger.tsv')):
 for line in p.read_text().splitlines():
  f=line.split('\t');assert len(f)==7
  x={'kind':f[0],'uid':int(f[1]),'to':int(f[2]),'mid':int(f[3]),'seq':int(f[4]),'cid':f[5],'ns':int(f[6])}
  assert 700001<=x['uid']<=720000 and 700001<=x['to']<=720000
  if x['kind']=='send':assert x['cid'] not in sends;sends[x['cid']]=x
  elif x['kind']=='ack':assert x['mid'] not in acks;acks[x['mid']]=x
  elif x['kind']=='delivery':assert x['mid'] not in delivery;delivery[x['mid']]=x
assert len(sends)==len(acks)==len(delivery)==8100
population=[]
start=int((raw/'control/start_ns').read_text());end=start+60_000_000_000
for mid,ack in acks.items():
 sent=sends[ack['cid']];got=delivery[mid]
 assert sent['uid']==ack['uid']==got['uid'] and sent['to']==ack['to']==got['to'] and sent['seq']==ack['seq'] and (not got['cid'] or got['cid']==ack['cid'])
 assert start<=sent['ns']<end and ack['ns']>=sent['ns'] and got['ns']>=sent['ns']
 population.append({'mid':mid,'from':sent['uid'],'to':sent['to'],'seq':sent['seq'],'send_ns':sent['ns'],'ack_ns':ack['ns'],'wire_ns':got['ns'],'send_to_ack_ms':(ack['ns']-sent['ns'])/1e6,'send_to_wire_ms':(got['ns']-sent['ns'])/1e6,'second':(sent['ns']-start)//1_000_000_000})
save('exact-message-population.json',population)
since=json.loads((control/'audit-before.json').read_text())['utc'];until=datetime.datetime.now(datetime.timezone.utc).isoformat()
phases={'gateway':[],'repository':[],'handler':[],'pool':[]}
for name in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1','/tinyimx-m21-message-service-1']:
 path=private/(name[1:]+'.log')
 with path.open('w') as f:subprocess.run(['docker','logs','--since',since,'--until',until,before[name]['id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=50)
 for line in path.read_text().splitlines():
  prefix='gateway private chat phase sample, '
  if prefix in line:
   x={'container':name}
   for pair in line.split(prefix,1)[1].strip().split(', '):
    k,sep,v=pair.partition('=')
    if k=='path' and v in ['chat','peer']:x[k]=v
    elif sep and re.fullmatch('[a-z_]+',k) and re.fullmatch('-?[0-9]+',v):x[k]=int(v)
   if x.get('message_id',0) in acks:
    ack=acks[x['message_id']]
    if x.get('path')=='chat':assert x['user_id']==ack['uid'] and x['request_seq']==ack['seq']
    phases['gateway'].append(x)
  for prefix,kind in [('private_persist_phase ','repository'),('storage_rpc_phase ','handler'),('mysql_pool_acquire_phase ','pool')]:
   if prefix not in line:continue
   x={k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line.split(prefix,1)[1])}
   if kind=='pool':
    if start//1000<=x.get('started_us',0)<end//1000:phases[kind].append(x)
   elif x.get('mid',0) in acks:
    ack=acks[x['mid']]
    if 'from' in x:assert x['from']==ack['uid']
    if 'to' in x:assert x['to']==ack['to']
    phases[kind].append(x)
for k,v in phases.items():save(k+'-numeric.json',v)
matched=[];ambiguous=[]
for pop in population:
 mid=pop['mid'];gs=[x for x in phases['gateway'] if x.get('path')=='chat' and x.get('message_id')==mid];rs=[x for x in phases['repository'] if x['mid']==mid]
 if len(gs)>1 or len(rs)>1:ambiguous.append({'mid':mid,'gateway':len(gs),'repo':len(rs)});continue
 if len(gs)!=1 or len(rs)!=1:continue
 g,q=gs[0],rs[0];assert q['status']==0 and not q['threw'] and not g['failed']
 before_repo=(q['started_us']*1000-pop['send_ns'])/1e6;after_repo=(pop['ack_ns']-(q['started_us']+q['total_us'])*1000)/1e6;outside=(g['persist_us']-q['total_us'])/1000
 assert min(before_repo,after_repo,outside)>=-.02
 x={**pop,'before_repository_ms':before_repo,'repository_ms':q['total_us']/1000,'after_repository_ms':after_repo,'gateway_persist_rpc_ms':g['persist_us']/1000,'rpc_minus_repository_ms':outside,'dispatch_age_ms':g['dispatch_age_us']/1000,'permission_ms':g['permission_us']/1000,'route_ms':g['route_us']/1000,'unread_ms':g['unread_us']/1000,'commit_ms':q['commit_us']/1000,'acquire_ms':q['acquire_us']/1000,'precheck_ms':q['precheck_us']/1000,'batch_wall_ms':q.get('begin_insert_read_us',-1)/1000,'outbox_ms':q['outbox_insert_us']/1000}
 if q.get('cpu_us',-1)>=0:x['repository_cpu_ms']=q['cpu_us']/1000
 matched.append(x)
save('same-mid-matched.json',matched)
def stats(v):
 a=sorted(v);return {'samples':len(a),'mean_ms':statistics.mean(a),'p50_ms':a[math.ceil(len(a)*.5)-1],'p99_ms':a[math.ceil(len(a)*.99)-1],'max_ms':a[-1]} if a else {'samples':0}
def allstats(rows):return {k:stats([x[k] for x in rows if k in x]) for k in sorted({k for x in rows for k in x if k.endswith('_ms')})}
lists=[json.loads(x) for x in (control/'mixed20k-ON/list-openloop/requests.jsonl').read_text().splitlines()]
ops=json.loads((b/'cross-feature-curfeat20kT/operations.json').read_text());windows=[]
for second in range(60):
 ps=[x for x in population if x['second']==second];ls=[x for x in lists if (x['sent_mono_ns']-start)//1_000_000_000==second];cs=[x for x in ops if (x['start_mono_ns']-start)//1_000_000_000==second]
 windows.append({'second':second,'messages':len(ps),'ack':stats([x['send_to_ack_ms'] for x in ps]),'list':stats([x['sent_to_response_ms'] for x in ls]),'cross_operations':[{'name':x['name'],'ms':x['ms']} for x in cs],'limits':'One-second descriptive alignment, notcausalproof; perbucket lowcountP99 notfullpopulationP99'})
save('steady-second-windows.json',windows)
resource=(raw/'guest-resources.log').read_text();mem=[int(x) for x in re.findall(r'MemAvailable:\s+(\d+)',resource)]
psi={'cpu':[],'memory':[]}
samples=[]
blocks=re.split(r'(?m)^(?=\d{4}-\d{2}-\d{2}T)',resource)
for block in blocks:
 stamp=block.splitlines()[0] if block else '';values=re.findall(r'(some|full) avg10=([0-9.]+) avg60=([0-9.]+) avg300=([0-9.]+) total=(\d+)',block)
 if values:samples.append({'utc':stamp,'psi_raw':[{'kind':x[0],'avg10':float(x[1]),'avg60':float(x[2]),'total_us':int(x[4])} for x in values],'container_cpu_percent':{n:float(v) for n,v in re.findall(r'(tinyimx-[^ \n]+) CPU=([0-9.]+)%',block)}})
save('resource-parsed.json',samples)
records=[json.loads(x) for x in (control/'resource-samples.jsonl').read_text().splitlines()];intervals=[]
for first,last in zip(records,records[1:]):
 if not (start<=first['mono_ns']<last['mono_ns']<=end):continue
 elapsed=(last['mono_ns']-first['mono_ns'])/1e9
 a=[int(v) for v in first['cpu_stat'].split()[1:9]];z=[int(v) for v in last['cpu_stat'].split()[1:9]];cpu=[y-x for x,y in zip(a,z)];assert all(x>=0 for x in cpu)
 item={'seconds':elapsed,'cpu_ticks':cpu,'cgroup_cores':{name:{k:(last['cgroups'][name]['cpu'][k]-c['cpu'][k])/1e6/elapsed for k in ['usage_usec','user_usec','system_usec','throttled_usec']} for name,c in first['cgroups'].items()},'swap_in_pages':last['vmstat']['pswpin']-first['vmstat']['pswpin'],'swap_out_pages':last['vmstat']['pswpout']-first['vmstat']['pswpout'],'cpu_psi_some_pct':(int(re.search(r'some .*total=(\d+)',last['pressure']['cpu']).group(1))-int(re.search(r'some .*total=(\d+)',first['pressure']['cpu']).group(1)))/1e6/elapsed*100};intervals.append(item)
assert intervals;seconds=sum(x['seconds'] for x in intervals);ticks=[sum(x['cpu_ticks'][i] for x in intervals) for i in range(8)];total_ticks=sum(ticks)
steady_resource={'full_intervals':len(intervals),'full_interval_seconds':seconds,'guest_busy_pct':(total_ticks-ticks[3]-ticks[4])/total_ticks*100,'guest_system_softirq_pct':(ticks[2]+ticks[6])/total_ticks*100,'guest_iowait_pct':ticks[4]/total_ticks*100,'cpu_psi_some_pct':sum(x['cpu_psi_some_pct']*x['seconds'] for x in intervals)/seconds,'swap_in_pages':sum(x['swap_in_pages'] for x in intervals),'swap_out_pages':sum(x['swap_out_pages'] for x in intervals),'cgroup_weighted_cores':{name:{k:sum(x['cgroup_cores'][name][k]*x['seconds'] for x in intervals)/seconds for k in ['usage_usec','user_usec','system_usec','throttled_usec']} for name in intervals[0]['cgroup_cores']},'guest_available_min_kib':min(x['mem_available_kib'] for x in records if start<=x['mono_ns']<=end),'limits':'Only fully bracketed 2sec intervals inside original60sec steady window. Cgroup includes every task in that service, not per-call CPU. PSI stall is not utilisation, unassigned guest CPU is not exclusively generator/kernel/VMware. Retain host applications; no unique network/disk cause claimed.'};save('steady-resource-summary.json',steady_resource);save('steady-resource-intervals.json',intervals)
report={'status':'CURRENT20K_ANALYZED_WITH_EXACT_IDENTITIES','point':point,'private_failed_gates':[k for k,v in point['capacity']['gates'].items() if not v],'message_population':len(population),'wire_client_message_id_absent_count':sum(not x['cid'] for x in delivery.values()),'wire_identity_definition':'Original wire includes MID/from/to and may omit client_message_id; join exact MID/from/to then originalACK/SQL CID validation retained. First auxiliaryanalysis wronglyrequiredwireCID; failedanalysis retained.','exact_ledger_ack':stats([x['send_to_ack_ms'] for x in population]),'exact_ledger_wire':stats([x['send_to_wire_ms'] for x in population]),'phase_sample_counts':{k:len(v) for k,v in phases.items()},'same_mid_matched_count':len(matched),'same_mid_ambiguities':ambiguous,'matched_slow_biased_metrics':allstats(matched),'cross_max_operations':sorted(ops,key=lambda x:x['ms'],reverse=True)[:8],'guest_available_min_kib':min(mem) if mem else None,'guest_resource_samples':len(samples),'resources_last_sample':samples[-1] if samples else None,'steady_resources':steady_resource,'all19_runtime_preserved':runtime()==before,'new_pressure_or_runtime_change':False,'limits':'Onlysparse/rate-limitedslowthreshold phase intersection. LedgerP99 isdiagnosticnotreplacementoriginalhistogramgate. PSIavg10 stallfraction notCPUpercent; currentresourcecapturesinclude loginramp. Thisanalysis doesnotdeclareuniqueCPU/diskroot orallfeaturecapacity.'}
assert report['all19_runtime_preserved'];save('summary.json',report)
print(json.dumps({k:report[k] for k in ['status','private_failed_gates','exact_ledger_ack','phase_sample_counts','same_mid_matched_count','matched_slow_biased_metrics','guest_available_min_kib','guest_resource_samples','cross_max_operations','steady_resources']},indent=2))
PY
