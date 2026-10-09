#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,math,statistics,collections,hashlib,datetime,os
r=pathlib.Path.cwd();b=r/'.local/codex';control=b/'login-path-diagnostic-run-20261006';raw=b/'capacity-loginpath10k1';d=b/'login-path-diagnostic-analysis-20261006';assert not d.exists()
c=json.loads((control/'summary.json').read_text());assert c['status']=='LOGIN_PATH_DIAGNOSTIC_COMPLETED' and c['restore']['status']=='ORIGINAL_THREE_SERVICES_RESTORED'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
inputs=[control/'summary.json',control/'restore-summary.json',raw/'summary.json',raw/'worker-0/final.json',raw/'worker-0/ledger.tsv',control/'numeric-resources.jsonl']+[control/(role+'-numeric.jsonl') for role in ['gateway-a','gateway-b','user-service']]
d.mkdir(mode=0o700);(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Same-request additive client/GW/RPC/handler phases, exact successfulpopulation loginhistogram, interval-matched nativeworker CPU and guest/container CPU','inputs_sha256':{str(p.relative_to(b)):sha(p) for p in inputs},'sampling':'Successful uid%16 ratecapped, matched subset not population','boundary':'Gateway received_at is pre-submit dispatcher clock after its cheap username JSONprobe; response_start before response assembly/SendPacket, finished may be after clientACK. RPC prehandler gap includes client stub/gRPC transport/admission; posthandler includes transport/client completion. No pure-network or unique-scheduler attribution','writes':'New numeric aggregate and every unmatched/invalid join','runtime_changes':False},indent=2)+'\n')
def rows(p):return [json.loads(x) for x in p.read_text().splitlines()]
ledger=collections.defaultdict(list)
for line in (raw/'worker-0/ledger.tsv').read_text().splitlines():
 f=line.split('\t');assert len(f)==7
 if f[0] in ['login_sent','login_ack']:ledger[(int(f[1]),int(f[4]),f[0])].append(int(f[6]))
gateways=rows(control/'gateway-a-numeric.jsonl')+rows(control/'gateway-b-numeric.jsonl')
users=rows(control/'user-service-numeric.jsonl')
handlers=collections.defaultdict(list);repos=collections.defaultdict(list);gwcounts=collections.Counter((x['uid'],x['seq']) for x in gateways if x['success']==1)
for h in users:
 if 700001<=h['uid']<=710000 and h['status']==0 and h['outcome']==0 and not h['threw']:
  (handlers if h['kind']==2 else repos if h['kind']==1 else collections.defaultdict(list))[h['uid']].append(h)
def stats(v):
 a=sorted(v)
 if not a:return None
 return {'count':len(a),'mean_ms':statistics.mean(a)/1000,'p50_ms':a[math.ceil(len(a)*.50)-1]/1000,'p95_ms':a[math.ceil(len(a)*.95)-1]/1000,'p99_ms':a[math.ceil(len(a)*.99)-1]/1000,'max_ms':a[-1]/1000}
matched=[];unmatched=[];invalid=[]
for g in gateways:
 key=(g['uid'],g['seq']);sent=ledger.get((*key,'login_sent'),[]);ack=ledger.get((*key,'login_ack'),[])
 if g['success']!=1 or g['failed']!=0 or gwcounts[key]!=1 or len(sent)!=1 or len(ack)!=1:
  unmatched.append({'gateway':g,'cause':'Not unique complete successful client/Gateway pair','sent_count':len(sent),'ack_count':len(ack)});continue
 t=[sent[0]/1000,g['received_us'],g['started_us'],g['auth_start_us'],g['auth_end_us'],g['presence_start_us'],g['presence_end_us'],g['unread_start_us'],g['unread_end_us'],g['response_us'],ack[0]/1000]
 if any(z<a for a,z in zip(t,t[1:])):
  invalid.append({'gateway':g,'client_sent_ns':sent[0],'client_ack_ns':ack[0],'cause':'Timestamp ordering invalid','timeline_us':t});continue
 v={'uid':g['uid'],'seq':g['seq'],'service':g['service'],'client_sent_ns':sent[0],'client_ack_ns':ack[0],'client_total_us':t[-1]-t[0],
    'before_gateway_dispatch_us':t[1]-t[0],'gateway_submit_to_work_us':t[2]-t[1],'gateway_work_before_auth_us':t[3]-t[2],
    'gateway_auth_rpc_us':t[4]-t[3],'gateway_auth_to_presence_us':t[5]-t[4],'gateway_presence_us':t[6]-t[5],
    'gateway_presence_to_unread_us':t[7]-t[6],'gateway_unread_us':t[8]-t[7],'gateway_unread_to_response_us':t[9]-t[8],
    'response_start_to_client_ack_us':t[10]-t[9],'gateway_auth_thread_cpu_us':g['auth_cpu_us'],'gateway_presence_thread_cpu_us':g['presence_cpu_us'],'gateway_unread_thread_cpu_us':g['unread_cpu_us'],'gateway_finished_us':g['finished_us'],'timeline_us':t}
 parts=['before_gateway_dispatch_us','gateway_submit_to_work_us','gateway_work_before_auth_us','gateway_auth_rpc_us','gateway_auth_to_presence_us','gateway_presence_us','gateway_presence_to_unread_us','gateway_unread_us','gateway_unread_to_response_us','response_start_to_client_ack_us']
 assert abs(sum(v[k] for k in parts)-v['client_total_us'])<.001
 hs=[h for h in handlers[g['uid']] if h['started_us']>=g['auth_start_us'] and h['started_us']+h['total_us']<=g['auth_end_us']]
 if len(hs)==1:
  h=hs[0];v.update({'handler_started_us':h['started_us'],'handler_total_us':h['total_us'],'handler_thread_cpu_us':h['cpu_us'],'auth_before_handler_us':h['started_us']-g['auth_start_us'],'auth_after_handler_us':g['auth_end_us']-h['started_us']-h['total_us']})
  assert v['auth_before_handler_us']+v['handler_total_us']+v['auth_after_handler_us']==v['gateway_auth_rpc_us']
  rs=[x for x in repos[g['uid']] if x['tid']==h['tid'] and x['started_us']>=h['started_us'] and x['started_us']+x['total_us']<=h['started_us']+h['total_us']]
  if len(rs)==1:
   x=rs[0];v.update({'repository_total_us':x['total_us'],'repository_thread_cpu_us':x['cpu_us'],'lookup_us':x['lookup_us'],'lookup_thread_cpu_us':x['lookup_cpu_us'],'password_us':x['password_us'],'password_thread_cpu_us':x['password_cpu_us'],'password_wall_minus_thread_cpu_us':x['password_us']-x['password_cpu_us'] if x['password_cpu_us']>=0 else -1})
 v['dominant_additive_part']=max(parts,key=lambda k:v[k]);matched.append(v)
(d/'matched-client-gateway.json').write_text(json.dumps(matched,indent=2)+'\n');(d/'unmatched-gateway.json').write_text(json.dumps(unmatched,indent=2)+'\n');(d/'invalid-time-order.json').write_text(json.dumps(invalid,indent=2)+'\n')
allstats={k:stats([v[k] for v in matched if k in v and v[k]>=0]) for k in parts+['client_total_us','gateway_auth_thread_cpu_us','gateway_presence_thread_cpu_us','gateway_unread_thread_cpu_us']}
hmatched=[v for v in matched if 'handler_total_us' in v];rmatched=[v for v in hmatched if 'repository_total_us' in v]
hstats={k:stats([v[k] for v in hmatched if v[k]>=0]) for k in ['client_total_us','gateway_auth_rpc_us','auth_before_handler_us','handler_total_us','handler_thread_cpu_us','auth_after_handler_us']}
rstats={k:stats([v[k] for v in rmatched if v[k]>=0]) for k in ['repository_total_us','repository_thread_cpu_us','lookup_us','lookup_thread_cpu_us','password_us','password_thread_cpu_us','password_wall_minus_thread_cpu_us']}
final=json.loads((raw/'worker-0/final.json').read_text());h=final['login_histogram']
hist={'status':final['status'],'count':h['count'],'mean_ms':h['sum_us']/h['count']/1000 if h['count'] else None,'max_ms':h['max_us']/1000,'quantile_method':'Stored ceil100us-bin is already upper boundary; failed attempts excluded and reported separately'}
for name,q in [('p50',.5),('p95',.95),('p99',.99)]:
 n=0
 for upper,count in h['bins']:
  n+=count
  if n>=math.ceil(h['count']*q):hist[name+'_upper_ms']=upper/1000;break
samples=rows(control/'numeric-resources.jsonl');online=raw/'all-online.json'
end=json.loads(online.read_text())['monotonic_ns'] if online.exists() else final['snapshot_steady_ns']
def kv(s):return {k:int(v) for k,v in (x.split() for x in s.splitlines())}
def pstat(s):
 f=s.rsplit(')',1)[1].split();return {'ticks':f[19],'cpu_ticks':int(f[11])+int(f[12])}
hz=os.sysconf('SC_CLK_TCK');intervals=[]
for a,z in zip(samples,samples[1:]):
 seconds=(z['monotonic_ns']-a['monotonic_ns'])/1e9;v={'start_utc':a['utc'],'end_utc':z['utc'],'seconds':seconds,'fully_before_ramp_end':z['monotonic_ns']<end,'containers':{},'owned_processes':{}}
 av=list(map(int,a['guest']['stat'].splitlines()[0].split()[1:9]));zv=list(map(int,z['guest']['stat'].splitlines()[0].split()[1:9]));delta=[y-x for x,y in zip(av,zv)];total=sum(delta)
 v['guest_busy_fraction']=1-(delta[3]+delta[4])/total if total else None;v['guest_iowait_fraction']=delta[4]/total if total else None;v['guest_steal_fraction']=delta[7]/total if total else None
 for role,x in a['containers'].items():
  aa=kv(x['cpu_stat']);zz=kv(z['containers'][role]['cpu_stat'])
  v['containers'][role]={'cpu_cores':(zz['usage_usec']-aa['usage_usec'])/1e6/seconds,'user_cores':(zz['user_usec']-aa['user_usec'])/1e6/seconds,'system_cores':(zz['system_usec']-aa['system_usec'])/1e6/seconds,'throttled_usec_delta':zz.get('throttled_usec',0)-aa.get('throttled_usec',0)}
 for x in a['owned_processes']:
  aa=pstat(x['stat']);zs=[y for y in z['owned_processes'] if y['pid']==x['pid'] and pstat(y['stat'])['ticks']==aa['ticks']]
  if len(zs)==1:v['owned_processes'][x['kind']]={'cpu_cores':(pstat(zs[0]['stat'])['cpu_ticks']-aa['cpu_ticks'])/hz/seconds,'pid':x['pid'],'start_ticks':aa['ticks']}
 intervals.append(v)
(d/'resource-intervals.json').write_text(json.dumps(intervals,indent=2)+'\n')
ramp=[v for v in intervals if v['fully_before_ramp_end']];seconds=sum(v['seconds'] for v in ramp)
resource={'samples':len(samples),'fully_before_ramp_end_intervals':len(ramp),'min_mem_available_kib':min(v['mem_available_kib'] for v in samples),'max_observer_capture_ms':max(v['capture_elapsed_ms'] for v in samples),'weighted_ramp_guest_busy':sum(v['guest_busy_fraction']*v['seconds'] for v in ramp)/seconds if seconds else None,'weighted_ramp_guest_iowait':sum(v['guest_iowait_fraction']*v['seconds'] for v in ramp)/seconds if seconds else None,'ramp_containers':{},'ramp_owned_processes':{}}
for role in ['gateway-a','gateway-b','user-service']:
 resource['ramp_containers'][role]={'weighted_cpu_cores':sum(v['containers'][role]['cpu_cores']*v['seconds'] for v in ramp)/seconds if seconds else None,'throttled_usec_delta':sum(v['containers'][role]['throttled_usec_delta'] for v in ramp),'max_threads':max(v['containers'][role]['threads'] for v in samples)}
for kind in ['native_worker','coordinator']:
 vs=[v for v in ramp if kind in v['owned_processes']];secs=sum(v['seconds'] for v in vs)
 resource['ramp_owned_processes'][kind]={'valid_intervals':len(vs),'weighted_cpu_cores':sum(v['owned_processes'][kind]['cpu_cores']*v['seconds'] for v in vs)/secs if secs else None,'limits':'Only exact named process PID/start_ticks, no assignment of unmatched whole-guest CPU'}
summary={'status':'LOGIN_PATH_SAME_REQUEST_ANALYSIS_COMPLETE','capacity_status':c['capacity']['status'],'all_online_reached':online.exists(),'failure_reasons':c['capacity'].get('failure_reason_counts',{}),'full_successful_login_histogram':hist,'gateway_records':len(gateways),'numeric_user_records':len(users),'unique_client_gateway_pairs':len(matched),'unmatched_gateway_records':len(unmatched),'invalid_time_order_records':len(invalid),'client_gateway_phases':allstats,'dominant_part_counts':dict(collections.Counter(v['dominant_additive_part'] for v in matched)),'same_client_gateway_handler_count':len(hmatched),'same_handler_phase_stats':hstats,'same_client_gateway_handler_repository_count':len(rmatched),'same_repository_phase_stats':rstats,'top20_client_latency_samples':sorted(matched,key=lambda v:v['client_total_us'],reverse=True)[:20],'resources':resource,'restore_verified':True,'performance_acceptance':False,'limits':'One defaultON diagnostic run, selective/ratecapped successful joins, no new baseline/candidate causalAB or allfeature50k acceptance'}
assert all(sha(p)==json.loads((d/'audit-before.json').read_text())['inputs_sha256'][str(p.relative_to(b))] for p in inputs)
(d/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k!='top20_client_latency_samples'},indent=2),flush=True)
PY
