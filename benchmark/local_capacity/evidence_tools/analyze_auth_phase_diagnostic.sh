#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,math,statistics,collections,re
r=pathlib.Path.cwd();b=r/'.local/codex';control=b/'auth-phase-diagnostic-login-control-20261005';c=json.loads((control/'summary.json').read_text());assert c['status']=='AUTH_DIAGNOSTIC_CONTROL_COMPLETED'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
d=b/'auth-phase-completed-analysis-20261005';assert not d.exists();d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Completed authdiagnostic numeric-only phase/CPU/time-enclosedpair/resource analysis plus exacthistogram boundary correction','reads':'Frozen completedraw/control andexisting3 loginhistograms','writes':'Fresh numeric aggregates/matchedpairs, neverrewrite priorFAIL or histogramreview','runtime_changes':False,'limits':'Successfuluid%16/max8combinedlogs sample; notpopulationP99, handlerexcludeRPCadmission/transport, wallCPUgap doesnotbyitselfproveIO. Resourceintervalsannotated; no scheduler runqueueclaim.'},indent=2)+'\n')
records=[json.loads(s) for s in (control/'auth-phase-numeric.jsonl').read_text().splitlines()];repo=[x for x in records if x['kind']==1 and 700000<x['uid']<=710000];handler=[x for x in records if x['kind']==2 and 700000<x['uid']<=710000]
def stats(values):
 vals=sorted(v for v in values if v>=0)
 if not vals:return None
 return {'count':len(vals),'mean_ms':statistics.mean(vals)/1000,'p50_ms':vals[math.ceil(len(vals)*.5)-1]/1000,'p95_ms':vals[math.ceil(len(vals)*.95)-1]/1000,'p99_ms':vals[math.ceil(len(vals)*.99)-1]/1000,'max_ms':vals[-1]/1000}
def phases(rows):return {'records':len(rows),'distinct_tids':len({x['tid'] for x in rows}),'statuses':dict(collections.Counter(str((x['status'],x['outcome'],x['threw'])) for x in rows)),**{key:stats([x[key] for x in rows]) for key in ['total_us','cpu_us','lookup_us','lookup_cpu_us','password_us','password_cpu_us']}}
pairs=[];ambiguous=0
for x in repo:
 matches=[h for h in handler if h['uid']==x['uid'] and h['tid']==x['tid'] and h['started_us']<=x['started_us'] and h['started_us']+h['total_us']>=x['started_us']+x['total_us']]
 if len(matches)!=1:ambiguous+=len(matches)>1;continue
 h=matches[0];pairs.append({'uid':x['uid'],'tid':x['tid'],'repository_started_us':x['started_us'],'repository_wall_us':x['total_us'],'repository_cpu_us':x['cpu_us'],'handler_started_us':h['started_us'],'handler_wall_us':h['total_us'],'handler_cpu_us':h['cpu_us'],'handler_extra_wall_us':h['total_us']-x['total_us'],'handler_extra_cpu_us':h['cpu_us']-x['cpu_us'] if h['cpu_us']>=0 and x['cpu_us']>=0 else -1})
(d/'matched-pairs.json').write_text(json.dumps(pairs,indent=2)+'\n')
histograms=[]
for runid in ['sw150a','mp150diag','io150base','auth10kdiag']:
 p=b/('capacity-'+runid)/'worker-0/final.json';v=json.loads(p.read_text());h=v['login_histogram'];out={'run':runid,'status':v['status'],'count':h['count'],'mean_ms':h['sum_us']/h['count']/1000 if h['count'] else None,'max_ms':h['max_us']/1000,'final_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'quantile_method':'Stored ceil100usbin alreadyisupperboundary; donotaddstepagain'}
 for name,q in [('p50',.5),('p95',.95),('p99',.99)]:
  n=0
  for upper,count in h['bins']:
   n+=count
   if n>=math.ceil(h['count']*q):out[name+'_upper_bin_ms']=upper/1000;break
 histograms.append(out)
resource=[json.loads(s) for s in (control/'numeric-resources.jsonl').read_text().splitlines()];onlinepath=b/'capacity-auth10kdiag/all-online.json';online=json.loads(onlinepath.read_text())['monotonic_ns'] if onlinepath.exists() else None;intervals=[]
def parse_stats(text):return {key:int(v) for key,v in (line.split() for line in text.splitlines())}
for a,z in zip(resource,resource[1:]):
 seconds=(z['monotonic_ns']-a['monotonic_ns'])/1e9;row={'start_utc':a['utc'],'end_utc':z['utc'],'seconds':seconds,'fully_before_all_online':online is not None and z['monotonic_ns']<online,'containers':{}}
 for name,x in a['containers'].items():
  va=parse_stats(x['cpu_stat']);vz=parse_stats(z['containers'][name]['cpu_stat']);delta=vz['usage_usec']-va['usage_usec'];assert delta>=0
  row['containers'][name]={'cpu_cores':delta/1e6/seconds,'throttled_usec_delta':vz.get('throttled_usec',0)-va.get('throttled_usec',0),'threads_start':x['threads'],'threads_end':z['containers'][name]['threads']}
 va=list(map(int,a['guest']['stat'].splitlines()[0].split()[1:9]));vz=list(map(int,z['guest']['stat'].splitlines()[0].split()[1:9]));delta=[v-u for u,v in zip(va,vz)];total=sum(delta);row['guest_busy_fraction_excluding_idle_iowait']=1-(delta[3]+delta[4])/total if total else None;row['guest_iowait_fraction']=delta[4]/total if total else None;row['guest_steal_fraction']=delta[7]/total if total else None
 intervals.append(row)
(d/'resource-intervals.json').write_text(json.dumps(intervals,indent=2)+'\n');ramp=[x for x in intervals if x['fully_before_all_online']];resource_summary={'samples':len(resource),'max_capture_elapsed_ms':max((x['capture_elapsed_ms'] for x in resource),default=None),'minimum_mem_available_kib':min((x['mem_available_kib'] for x in resource),default=None),'all_online_monotonic_ns':online,'fully_before_all_online_intervals':len(ramp),'ramp_guest_busy_mean':statistics.mean(x['guest_busy_fraction_excluding_idle_iowait'] for x in ramp) if ramp else None,'ramp_containers':{}}
for name in resource[0]['containers'] if resource else []:
 vals=[x['containers'][name] for x in ramp];resource_summary['ramp_containers'][name]={'interval_mean_cpu_cores':statistics.mean(x['cpu_cores'] for x in vals) if vals else None,'maximum_threads':max((x['threads'] for s in resource for n,x in s['containers'].items() if n==name),default=None),'own_throttled_usec_delta':sum(x['throttled_usec_delta'] for x in vals)}
x={'status':'AUTH_NUMERIC_ANALYSIS_COMPLETED','capacity_status':c['capacity']['status'],'all_online_reached':online is not None and c['capacity'].get('gates',{}).get('all_logged_in',False),'capacity':c['capacity'],'repository_samples':phases(repo),'handler_samples':phases(handler),'matched_pairs':len(pairs),'ambiguous_pairs':ambiguous,'paired_handler_extra_wall':stats([v['handler_extra_wall_us'] for v in pairs]),'paired_handler_extra_cpu':stats([v['handler_extra_cpu_us'] for v in pairs]),'corrected_login_histograms':histograms,'resource_summary':resource_summary,'performance_acceptance':False,'limitations':['Samplingnonrandom/bounded; phasequantilesarenotpopulation','No per-loginclient to serverjoin, do notsubtractdifferentpopulationmeans','Handler doesnotinclude gRPCadmission/returntransport; CPUwallgap notstandaloneIO/schedulerproof','Sharedhostbeforeafter variation preventsassigning priorfailure todiagnosticcode','Oldreviewoverstated histogramupperboundby0.1ms; rawandoldreviewpreserved; correctboundhere']};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
