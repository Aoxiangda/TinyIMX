#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,urllib.request,urllib.parse,datetime,hashlib,subprocess
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'permission-prometheus-window-20261007';assert not d.exists()
parent=b/'current20k-baseline-20261007-attempt4';records=[json.loads(s) for s in (parent/'resource-samples.jsonl').read_text().splitlines()];start=int((b/'capacity-cur20kT/control/start_ns').read_text());end=start+60_000_000_000
record=min(records,key=lambda x:abs(x['mono_ns']-start));wall=datetime.datetime.fromisoformat(record['utc']).timestamp();st=wall+(start-record['mono_ns'])/1e9;et=st+60
before=st-30;after=et+45
names=['rpc_client_call_duration_seconds_count','rpc_client_call_duration_seconds_sum','rpc_client_call_duration_seconds_bucket','rpc_server_call_duration_seconds_count','rpc_server_call_duration_seconds_sum','rpc_server_call_duration_seconds_bucket'];d.mkdir(mode=0o700);save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10))
save('audit-before.json',{'operation':'GET only existing Prometheus retrospective counter/histogram snapshots bracketing one accepted-runtime20k steady; no pressure/config/code/runtime change','barrier_monotonic_ns':start,'wall_interpolation_record':record['utc'],'steady_start_epoch':st,'steady_end_epoch':et,'before_epoch':before,'after_epoch':after,'limits':'Collector periodic export/scrape is delayed. Counter difference uses wider quiet bracket and includes four cross actors; not exact8100 population nor same-call boundaries. Histogram buckets are coarse.'})
values={}
for label,t in [('before',before),('after',after)]:
 values[label]={}
 for name in names:
  q=name+'{rpc_method="CheckPrivateChatPermission"}';u='http://127.0.0.1:19090/api/v1/query?'+urllib.parse.urlencode({'query':q,'time':t})
  with urllib.request.urlopen(u,timeout=15) as f:x=json.load(f)
  assert x['status']=='success';save(label+'-'+name+'.json',x);values[label][name]=x['data']['result']
def key(x):return tuple(sorted((k,v) for k,v in x['metric'].items() if k!='__name__'))
deltas={};checks=[]
for n in names:
 a={key(x):x for x in values['before'][n]};z={key(x):x for x in values['after'][n]};out=[]
 for k,x in z.items():
  if k not in a:checks.append({'metric':n,'missing_before':dict(k)});continue
  v=float(x['value'][1])-float(a[k]['value'][1]);assert v>=0,'Counter reset; no delta valid';out.append({'labels':dict(k),'delta':v})
 deltas[n]=out
summary=[]
for side in ['client','server']:
 c=deltas['rpc_'+side+'_call_duration_seconds_count'];s=deltas['rpc_'+side+'_call_duration_seconds_sum']
 for row in c:
  match=[x for x in s if x['labels']==row['labels']];assert len(match)==1;count=row['delta'];summary.append({'side':side,'instance':row['labels'].get('service_instance_id'),'count':count,'mean_ms':match[0]['delta']*1000/count if count else None,'buckets':[{'le':x['labels']['le'],'delta':x['delta']} for x in deltas['rpc_'+side+'_call_duration_seconds_bucket'] if {k:v for k,v in x['labels'].items() if k!='le'}==row['labels']]})
save('deltas.json',deltas);save('summary.json',{'status':'PERMISSION_RETROSPECTIVE_METRIC_DIFFERENCES_COMPLETE','steady_start_utc':datetime.datetime.fromtimestamp(st,datetime.timezone.utc).isoformat(),'steady_end_utc':datetime.datetime.fromtimestamp(et,datetime.timezone.utc).isoformat(),'bracket_before_utc':datetime.datetime.fromtimestamp(before,datetime.timezone.utc).isoformat(),'bracket_after_utc':datetime.datetime.fromtimestamp(after,datetime.timezone.utc).isoformat(),'permission':summary,'missing_series':checks,'limits':'Wider quiet bracket; collector/export delays and crossfeature requests included. Difference of separate aggregate means is not any individual call wait. Coarse histogram is not exact P99; no unique network/poller/CPU cause proved.'});print((d/'summary.json').read_text())
PY
