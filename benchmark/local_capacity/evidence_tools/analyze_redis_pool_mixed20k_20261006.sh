#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re,hashlib,math,statistics,collections
r=pathlib.Path.cwd();b=r/'.local/codex';raw=b/'capacity-rad20kON';control=b/'redis-acquire-mixed20k-20261006';d=b/'redis-acquire-mixed20k-analysis-20261006';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
point=json.loads((control/'summary.json').read_text());preserved=json.loads((control/'preservation-summary.json').read_text());assert preserved['status']=='ALL19_INSTANCES_CONFIGS_AND_DURABILITY_PRESERVED'
outer=b/'redis-acquire-mixed20k-control-20261006'
restored=json.loads((outer/'restore-summary.json').read_text());assert restored['status']=='ACCEPTED_A2BB_GATEWAYS_RESTORED_DIAGNOSTIC_STOPPED'
original_runtime=json.loads((outer/'audit-before.json').read_text())['before'];expected_runtime={**original_runtime,**restored['gateways']}
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
 cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
 return {c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
before=runtime();assert before==expected_runtime
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
inputs=[control/'summary.json',raw/'summary.json',raw/'reconciliation.json',*sorted(raw.glob('worker-*/ledger.tsv')),*sorted(raw.glob('worker-*/final.json')),control/'redis20k-ON/list-openloop/requests.jsonl',b/'cross-feature-radfeat20kON/operations.json']
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly completed20kRedisdiagnostic: all8100exactIDs andsameTID-phase-interval Redislease join; acceptedruntimealreadyrestored. No newload/runtimechange','input_sha256':{str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'runtime':before,'limits':'Sparse slow-biased/rate-limited phases notpopulationP99; wallminusCPU cannot uniquelyattribute IO/scheduler; dockerstats are intervals, PSI notCPUusage. Preserve allfailures/partialcases/rawlogs privately'})
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
 if 'gateway-' in name:
  source=outer/'runtime-private'/('diagnostic-final-'+name[1:]+'.log')
  assert source.is_file();path.write_bytes(source.read_bytes())
 else:
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
  for prefix,kind in [('private_persist_phase ','repository'),('storage_rpc_phase ','handler'),('redis_acquire_phase ','pool')]:
   if prefix not in line:continue
   x={k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line.split(prefix,1)[1])};x['container']=name
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
lists=[json.loads(x) for x in (control/'redis20k-ON/list-openloop/requests.jsonl').read_text().splitlines()]
ops=json.loads((b/'cross-feature-radfeat20kON/operations.json').read_text());windows=[]
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

# Match only same process namespace TID, exact MID gatewaywork and measured
# nested interval. Most unmatched events can be unsampled work; do not label
# them as presence solely from absence of a matching Gateway slow sample.
pooljoins=[];poolambiguous=[]
for lease in phases['pool']:
 candidates=[]
 for g in phases['gateway']:
  if g.get('container')!=lease['container'] or g.get('tid',0)!=lease.get('tid',0) or not g.get('tid'):continue
  for phase in ['permission','route','persist','unread']:
   left=g.get(phase+'_started_us',-1);right=g.get(phase+'_finished_us',-1)
   if left>=0 and left<=lease['started_us'] and lease['started_us']+lease['total_us']<=right+2:
    candidates.append((g,phase))
 if len(candidates)==1:
  g,phase=candidates[0]
  pooljoins.append({'gateway':lease['container'],'mid':g['message_id'],'path':g['path'],'phase':phase,'tid':lease['tid'],'gateway_phase_ms':g[phase+'_us']/1000,'acquire_total_ms':lease['total_us']/1000,'mutex_ms':lease['mutex_wait_us']/1000,'slot_ms':lease['slot_wait_us']/1000,'ping_ms':lease['ping_us']/1000 if lease['ping_us']>=0 else None,'thread_cpu_ms':lease['thread_cpu_us']/1000 if lease['thread_cpu_us']>=0 else None,'status':lease['status'],'free_slots_before':lease['free_slots_before'],'limits':'Totalclock excludes subsequent trace logging; phase mayrepeat (unionrange). OnlysameTID/timeuniquejoin. Notpopulation.'})
 elif len(candidates)>1:poolambiguous.append({'lease':lease,'matches':[{'mid':g['message_id'],'phase':phase} for g,phase in candidates]})
save('same-tid-redis-phase-joins.json',pooljoins);save('pool-join-ambiguities.json',poolambiguous)
def poolstats(rows):
 fields=['total_us','mutex_wait_us','slot_wait_us','ping_us','reconnect_us','thread_cpu_us']
 return {key:stats([x[key]/1000 for x in rows if x.get(key,-1)>=0]) for key in fields}
pool_report={'samples':len(phases['pool']),'by_gateway':{name:poolstats([x for x in phases['pool'] if x['container']==name]) for name in sorted({x['container'] for x in phases['pool']})},'overall':poolstats(phases['pool']),'status_counts':dict(collections.Counter(x['status'] for x in phases['pool'])),'free_slots_before_counts':dict(collections.Counter(x['free_slots_before'] for x in phases['pool'])),'unique_phase_join_count':len(pooljoins),'unmatched_notattributed_count':len(phases['pool'])-len(pooljoins)-len(poolambiguous),'ambiguous_count':len(poolambiguous),'joined_by_phase':{phase:{key:stats([x[key] for x in pooljoins if x['phase']==phase and x.get(key) is not None]) for key in ['gateway_phase_ms','acquire_total_ms','mutex_ms','slot_ms','ping_ms','thread_cpu_ms']} for phase in sorted({x['phase'] for x in pooljoins})},'limits':'8/s preprocessingselectedacquires + Gatewaythresholdslowlogs; biasedintersection notpopulationquantiles. PINGwall includesserver/network/scheduler, CPUdelta cannotseparateIO/lock/runqueue. Unmatched isnotproof ofpresence.'}
save('redis-pool-report.json',pool_report)
report={'status':'REDIS_ACQUIRE_MIXED20K_ANALYSIS_COMPLETE','redis_pool':pool_report,'point':point,'private_failed_gates':[k for k,v in point['capacity']['gates'].items() if not v],'message_population':len(population),'wire_client_message_id_absent_count':sum(not x['cid'] for x in delivery.values()),'wire_identity_definition':'Original wire includes MID/from/to and may omit client_message_id; join exact MID/from/to then originalACK/SQL CID validation retained. First auxiliaryanalysis wronglyrequiredwireCID; failedanalysis retained.','exact_ledger_ack':stats([x['send_to_ack_ms'] for x in population]),'exact_ledger_wire':stats([x['send_to_wire_ms'] for x in population]),'phase_sample_counts':{k:len(v) for k,v in phases.items()},'same_mid_matched_count':len(matched),'same_mid_ambiguities':ambiguous,'matched_slow_biased_metrics':allstats(matched),'cross_max_operations':sorted(ops,key=lambda x:x['ms'],reverse=True)[:8],'guest_available_min_kib':min(mem) if mem else None,'guest_resource_samples':len(samples),'resources_last_sample':samples[-1] if samples else None,'all19_runtime_preserved':runtime()==before,'new_pressure_or_runtime_change':False,'limits':'Onlysparse/rate-limitedslowthreshold phase intersection. LedgerP99 isdiagnosticnotreplacementoriginalhistogramgate. PSIavg10 stallfraction notCPUpercent; currentresourcecapturesinclude loginramp. Thisanalysis doesnotdeclareuniqueCPU/diskroot orallfeaturecapacity.'}
assert report['all19_runtime_preserved'];save('summary.json',report)
print(json.dumps({k:report[k] for k in ['status','private_failed_gates','exact_ledger_ack','phase_sample_counts','same_mid_matched_count','matched_slow_biased_metrics','guest_available_min_kib','guest_resource_samples','cross_max_operations','redis_pool']},indent=2))
PY
