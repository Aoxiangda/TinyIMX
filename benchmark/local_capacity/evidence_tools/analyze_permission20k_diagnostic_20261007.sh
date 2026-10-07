#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re,hashlib,math,statistics,collections
r=pathlib.Path.cwd();b=r/'.local/codex';raw=b/'capacity-per20kA';control=b/'permission20k-diagnostic-20261007';wrapper=b/'permission20k-runtime-control-20261007-attempt2';d=b/'permission20k-diagnostic-analysis-20261007';assert not d.exists();assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
point=json.loads((control/'summary.json').read_text());assert point['status']=='MIXED20K_POINT_COMPLETE';restored=json.loads((wrapper/'restore-summary.json').read_text());assert restored['status']=='ORIGINAL3IMAGES_FULLENV_HEALTH_HOSTCONFIG_MOUNTS_AND_OTHER16_RESTORED'
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs);return {c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
before=runtime();assert before==restored['runtime'];d.mkdir(mode=0o700);save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10))
logs=sorted((control/'runtime-private').glob('*-since-start.log'));assert len(logs)==4
inputs=[control/'summary.json',raw/'summary.json',raw/'reconciliation.json',control/'resource-samples.jsonl',*logs,*sorted(raw.glob('worker-*/ledger.tsv'))];save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Only readonly completed20k logs/ledger/resource analysis after exact3restoration; numeric exactrequestID/callerhash/from/to samecall client-handler-repo; exact8100sendACKwire; no new load/deployment/queries','input_sha256':{str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'runtime_before':before,'limits':'Independent per-side first8/s rate gates deterministic biased intersection, no populationphaseP99 claim; outsidehandler includes gRPC/admission/logging/scheduling, not pure network. Finish span includes Metrics+span ending, not unique mutex proof.'})
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

phases={1:[],2:[],3:[]};outer=[];repo_private=[]
for p in logs:
 for line in p.read_text().splitlines():
  prefix='permission_boundary_phase '
  if prefix in line:
   x={k:int(v) for k,v in re.findall(r'([a-z0-9_]+)=(-?\d+)',line.split(prefix,1)[1])};x['log']=p.name
   if x.get('side') in phases and start//1000<=x.get('started_us',0)<end//1000 and 700001<=x.get('from',0)<=720000:phases[x['side']].append(x)
  prefix='gateway private chat phase sample, '
  if prefix in line:
   x={}
   for part in line.split(prefix,1)[1].strip().split(', '):
    k,sep,v=part.partition('=')
    if k=='path':x[k]=v
    elif sep and re.fullmatch('[a-z_]+',k) and re.fullmatch('-?[0-9]+',v):x[k]=int(v)
   if x.get('path')=='chat' and x.get('message_id') in acks:outer.append(x)
  prefix='private_persist_phase '
  if prefix in line:
   x={k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line.split(prefix,1)[1])}
   if x.get('mid') in acks:repo_private.append(x)
for side,rows in phases.items():save('permission-side-'+str(side)+'-numeric.json',rows)
save('outer-chat-numeric.json',outer);save('private-repository-numeric.json',repo_private)
def identity(x):return (x['from'],x['to'],x['rid_hash'],x['caller_hash'])
groups={side:collections.defaultdict(list) for side in phases}
for side,rows in phases.items():
 for x in rows:groups[side][identity(x)].append(x)
byfrom=collections.defaultdict(list)
for x in population:byfrom[(x['from'],x['to'])].append(x)
assert all(len(v)==1 for v in byfrom.values()),'Require unique ring sender-to identity before linking permission to exactMID'
matched=[];ambiguities=[];unpaired=[]
for key in set(groups[1])|set(groups[2])|set(groups[3]):
 rows=[groups[side].get(key,[]) for side in [1,2,3]]
 if any(len(v)>1 for v in rows):ambiguities.append({'identity':key,'counts':list(map(len,rows))});continue
 if not all(len(v)==1 for v in rows):unpaired.append({'identity':key,'counts':list(map(len,rows))});continue
 c,h,q=[v[0] for v in rows];poprows=byfrom.get((c['from'],c['to']),[])
 if len(poprows)!=1:unpaired.append({'identity':key,'population_count':len(poprows)});continue
 pop=poprows[0];assert c['status']==h['status']==q['status']==0
 ce=c['started_us']+c['total_us'];he=h['started_us']+h['total_us'];qe=q['started_us']+q['total_us']
 assert c['m3_us']<=h['started_us']<=q['started_us']<=qe<=he<=c['m4_us'], 'Reject impossible samecall time hierarchy'
 for x,count in [(c,5),(h,4),(q,4)]:assert all(x['started_us']<=x['m'+str(i)+'_us']<=x['started_us']+x['total_us'] for i in range(1,count+1))
 row={**pop,'rid_hash':c['rid_hash'],'caller_hash':c['caller_hash'],'client_total_ms':c['total_us']/1000,'client_pre_rpc_ms':(c['m3_us']-c['started_us'])/1000,'client_rpc_ms':(c['m4_us']-c['m3_us'])/1000,'client_span_finish_ms':(c['m5_us']-c['m4_us'])/1000,'handler_total_ms':h['total_us']/1000,'handler_span_setup_ms':(h['m1_us']-h['started_us'])/1000,'handler_application_ms':(h['m2_us']-h['m1_us'])/1000,'handler_span_finish_ms':(h['m4_us']-h['m3_us'])/1000,'repository_total_ms':q['total_us']/1000,'repository_acquire_ms':(q['m2_us']-q['m1_us'])/1000,'repository_query_ms':(q['m4_us']-q['m3_us'])/1000,'before_handler_ms':(h['started_us']-c['m3_us'])/1000,'after_handler_ms':(c['m4_us']-he)/1000,'rpc_outside_handler_ms':((c['m4_us']-c['m3_us'])-h['total_us'])/1000,'client_cpu_ms':c['cpu_us']/1000,'handler_cpu_ms':h['cpu_us']/1000,'repository_cpu_ms':q['cpu_us']/1000}
 g=[v for v in outer if v['message_id']==pop['mid']];rp=[v for v in repo_private if v['mid']==pop['mid']]
 if len(g)==1:row.update({'gateway_permission_ms':g[0]['permission_us']/1000,'outer_minus_client_ms':g[0]['permission_us']/1000-c['total_us']/1000})
 if len(rp)==1:row.update({'private_repository_ms':rp[0]['total_us']/1000,'private_commit_ms':rp[0]['commit_us']/1000,'private_precheck_ms':rp[0]['precheck_us']/1000})
 matched.append(row)
assert matched and not ambiguities;save('permission-same-call-matched.json',matched);save('permission-unpaired.json',unpaired);save('permission-ambiguous.json',ambiguities)
def stats(v):
 a=sorted(v);return {'samples':len(a),'mean_ms':statistics.mean(a),'p50_ms':a[math.ceil(len(a)*.5)-1],'p99_ms':a[math.ceil(len(a)*.99)-1],'max_ms':a[-1]} if a else {'samples':0}
def allstats(rows):return {k:stats([x[k] for x in rows if k in x]) for k in sorted({k for x in rows for k in x if k.endswith('_ms')})}
lists=[json.loads(x) for x in (control/'mixed20k-ON/list-openloop/requests.jsonl').read_text().splitlines()]
records=[json.loads(x) for x in (control/'resource-samples.jsonl').read_text().splitlines()];intervals=[]
for first,last in zip(records,records[1:]):
 if not (start<=first['mono_ns']<last['mono_ns']<=end):continue
 elapsed=(last['mono_ns']-first['mono_ns'])/1e9
 a=[int(v) for v in first['cpu_stat'].split()[1:9]];z=[int(v) for v in last['cpu_stat'].split()[1:9]];cpu=[y-x for x,y in zip(a,z)];assert all(x>=0 for x in cpu)
 item={'seconds':elapsed,'cpu_ticks':cpu,'cgroup_cores':{name:{k:(last['cgroups'][name]['cpu'][k]-c['cpu'][k])/1e6/elapsed for k in ['usage_usec','user_usec','system_usec','throttled_usec']} for name,c in first['cgroups'].items()},'swap_in_pages':last['vmstat']['pswpin']-first['vmstat']['pswpin'],'swap_out_pages':last['vmstat']['pswpout']-first['vmstat']['pswpout'],'cpu_psi_some_pct':(int(re.search(r'some .*total=(\d+)',last['pressure']['cpu']).group(1))-int(re.search(r'some .*total=(\d+)',first['pressure']['cpu']).group(1)))/1e6/elapsed*100};intervals.append(item)
assert intervals;seconds=sum(x['seconds'] for x in intervals);ticks=[sum(x['cpu_ticks'][i] for x in intervals) for i in range(8)];total_ticks=sum(ticks)
steady_resource={'full_intervals':len(intervals),'full_interval_seconds':seconds,'guest_busy_pct':(total_ticks-ticks[3]-ticks[4])/total_ticks*100,'guest_system_softirq_pct':(ticks[2]+ticks[6])/total_ticks*100,'guest_iowait_pct':ticks[4]/total_ticks*100,'cpu_psi_some_pct':sum(x['cpu_psi_some_pct']*x['seconds'] for x in intervals)/seconds,'swap_in_pages':sum(x['swap_in_pages'] for x in intervals),'swap_out_pages':sum(x['swap_out_pages'] for x in intervals),'cgroup_weighted_cores':{name:{k:sum(x['cgroup_cores'][name][k]*x['seconds'] for x in intervals)/seconds for k in ['usage_usec','user_usec','system_usec','throttled_usec']} for name in intervals[0]['cgroup_cores']},'guest_available_min_kib':min(x['mem_available_kib'] for x in records if start<=x['mono_ns']<=end),'limits':'Only fully bracketed 2sec intervals inside original60sec steady window. Cgroup includes every task in that service, not per-call CPU. PSI stall is not utilisation, unassigned guest CPU is not exclusively generator/kernel/VMware. Retain host applications; no unique network/disk cause claimed.'};save('steady-resource-summary.json',steady_resource);save('steady-resource-intervals.json',intervals)

metrics=allstats(matched);slow=sorted(matched,key=lambda x:x['send_to_ack_ms'],reverse=True)[:20];save('slowest-matched20.json',slow)
report={'status':'PERMISSION20K_EXACTCALL_DIAGNOSIS_COMPLETE','point':point,'private_failed_gates':[k for k,v in point['capacity']['gates'].items() if not v],'exact_ledger_ack':stats([x['send_to_ack_ms'] for x in population]),'phase_sample_counts':{str(k):len(v) for k,v in phases.items()},'same_call_matched_count':len(matched),'unpaired_identities':len(unpaired),'ambiguities':len(ambiguities),'matched_biased_metrics':metrics,'outer_chat_samples':len(outer),'private_repo_samples':len(repo_private),'steady_resources':steady_resource,'accepted_runtime_still_restored':runtime()==before,'limits':'One nativevalidated traceON diagnostic not performancegain/control/50k/allfeatures. Deterministic rate-limited first8/s phase intersection is not populationP99. Client finish includes metrics andspanending; no unique mutex attribution. Handleroutside includes logging/admission/transport/scheduling; CPU-wait is not uniquely IO. Perphase nested timings must not be summed.'};assert report['accepted_runtime_still_restored'];save('summary.json',report);print(json.dumps({k:report[k] for k in ['status','private_failed_gates','exact_ledger_ack','phase_sample_counts','same_call_matched_count','unpaired_identities','matched_biased_metrics','steady_resources']},indent=2))
PY
