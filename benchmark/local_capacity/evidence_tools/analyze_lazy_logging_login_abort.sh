#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,datetime,re,collections,subprocess
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'lazy-logging-login-abort-analysis-20261005';assert not d.exists()
inputs=[];runs=[]
def read(p):
 data=p.read_bytes();inputs.append({'path':str(p.relative_to(r)),'sha256':hashlib.sha256(data).hexdigest()});return json.loads(data)
def hist(h):
 out={'count':h['count'],'mean_ms':h['sum_us']/max(1,h['count'])/1000,'max_ms':h['max_us']/1000}
 for q in [.5,.95,.99,.999]:
  total=0
  for t,c in h['bins']:
   total+=c
   if total>=q*h['count']:out['p'+str(q*100).rstrip('0').rstrip('.')+'_upper_ms']=t/1000;break
 return out
for name,logstage in [('logging150A1','lazy-logging-deployment-lazy-before-B1-20261005'),('logging150B1','lazy-logging-deployment-original-rollback-20261005')]:
 raw=b/('capacity-'+name);summary=read(raw/'summary.json');final=read(raw/'worker-0/final.json');audit=read(b/('lazy-logging-endpoint-control-'+name)/'audit-before.json');start=datetime.datetime.fromisoformat(audit['utc']);end=start+datetime.timedelta(seconds=summary['wall_seconds']+20);counts=collections.Counter();logs=[]
 for role in ['gateway-a','gateway-b']:
  p=b/logstage/'runtime-private'/(role+'-before.log');data=p.read_bytes();logs.append({'role':role,'path':str(p.relative_to(r)),'sha256':hashlib.sha256(data).hexdigest(),'private':True})
  for line in data.decode(errors='replace').splitlines():
   stamp=line.split(' ',1)[0]
   try:t=datetime.datetime.fromisoformat(stamp.replace('Z','+00:00'))
   except ValueError:continue
   if not start<=t<=end or '[WARN]' not in line:continue
   m=re.search(r'\[([^\]]+\.cpp:\d+[^\]]*)\] ([^,]+)',line)
   if m:counts[m[1]+' '+m[2]]+=1
 text=(raw/'guest-resources.log').read_text();psi=[float(x) for x in re.findall(r'some avg10=([\d.]+) avg60=[\d.]+ avg300=[\d.]+ total=\d+',text)][1::2]
 runs.append({'run':name,'status':summary['status'],'phase':summary.get('phase'),'failure_reason_counts':summary.get('failure_reason_counts'),'metrics':summary['metrics'],'login_histogram':hist(final['login_histogram']),'warn_counts_in_bounded_run_window':dict(counts),'private_log_provenance':logs,'cpu_psi_avg10_observed_min_max':([min(psi),max(psi)] if psi else None),'ack_population':final['ack_histogram']['count']})
functional=[]
for phase in ['eager','lazy']:
 x=read(b/('lazy-logging-functional-control-'+phase+'-20261005')/'summary.json');f=x['functional'];assert f['status']=='PASS' and len(f['assertions'])==35 and f['operations_completed']==49;functional.append({'phase':phase,'operations':49,'assertions':35,'status':'PASS','latency_samples_ms':f['latency_samples_ms'],'coverage':f['coverage']})
control=read(b/'capacity-logging150O1/summary.json');cf=read(b/'capacity-logging150O1/worker-0/final.json');original={'summary':control,'login_histogram':hist(cf['login_histogram'])}
rollback=read(b/'lazy-logging-deployment-original-rollback-20261005/summary.json');assert rollback['phase']=='original' and rollback['other16_configs_env_mounts_resources_preserved']
d.mkdir(mode=0o700);(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Read-only analysis of retained real runs; write fresh own public numerical evidence only','inputs':inputs,'logs':'Read own private captured logs in RAM; export counts and hashes only, never private contents','settings':'No product, runtime, schema, security, app or deadline changes','quantiles':'Stored ceil100us bins already upper boundaries; no extra step added'},indent=2)+'\n')
x={'status':'LOGGING_LOGIN_ABORT_ANALYSIS_COMPLETED','source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'runs':runs,'functional':functional,'original_runtime_control':original,'runtime_rollback':rollback,'remaining_ABBA':'B2/A2 NOT_RUN because both first variants aborted before active window; no useful paired ACK population','logging_gain':'UNPROVEN; neither new variant reached 10k or produced message load; failed partial populations cannot form gain percentage','causal_limits':'Replay admissions and auth/queue deadlines observed alongside high CPU pressure; no unique root-cause attribution. Sequential original versus rebuilt control cannot alone separate cache/compiler/cold state and host variability. Current host idle samples cannot prove historical paging. No memory cleanup justified.','next_step':'Resolve original-control outcome and build provenance before another product optimization or repetition','all_features_10k_50k_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({'status':x['status'],'runs':[{'run':v['run'],'login':v['login_histogram'],'warn_counts':v['warn_counts_in_bounded_run_window']} for v in runs],'original_status':control['status'],'original_phase':control.get('phase'),'logging_gain':x['logging_gain']}))
PY
