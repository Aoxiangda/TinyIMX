#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,hashlib,re,statistics,collections,math
r=pathlib.Path.cwd();base=r/'.local/codex';d=base/'completed-capacity-distributions-v2-20261005';assert not d.exists();d.mkdir()
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly full population histogram percentiles and original steady-window resource samples','reads':'Completed worker final histograms, barrier monotonic values and original file mtime plus source resource logs','writes':'Fresh bounded numeric evidence only','limits':'Histogram0.1ms upper bins; resource5s samples, PSIavg10 is CPU waiting pressure not utilization; barrierUTC estimated from original all-online write mtime, no clock or service changes'})
runs=[]
for run in ['p161k1','p1610k1','p1620k1','p1630k1','slease1k1','slease10k1','pending1k1','pending10k1','pending20k1','pending30k1','acount1k1','acount10k1']:
 p=base/('capacity-'+run)
 if not (p/'summary.json').exists() or not (p/'all-online.json').exists():continue
 source=json.loads((p/'summary.json').read_text());assert source['scenario']['duration']==60
 finals=[json.loads(q.read_text()) for q in sorted(p.glob('worker-*/final.json'))];assert finals and all(x['status']=='COMPLETED' for x in finals)
 hists={}
 for key in ['ack_histogram','scheduled_to_ack_histogram','schedule_lag_histogram','login_histogram']:
  hs=[x[key] for x in finals];bins=collections.Counter()
  for h in hs:
   assert sum(v for k,v in h['bins'])==h['count'];bins.update({int(k):int(v) for k,v in h['bins']})
  count=sum(bins.values());values={}
  for name,frac in [('p50',.5),('p95',.95),('p99',.99),('p999',.999)]:
   target=math.ceil(count*frac);acc=0
   for upper,n in sorted(bins.items()):
    acc+=n
    if acc>=target:values[name+'_upper_ms']=upper/1000;break
  hists[key]={'count':count,'mean_ms':sum(h['sum_us'] for h in hs)/count/1000 if count else None,'max_ms':max(h['max_us'] for h in hs)/1000,**values}
 online=json.loads((p/'all-online.json').read_text());start_ns=int((p/'control/start_ns').read_text());start_utc=p.joinpath('all-online.json').stat().st_mtime+(start_ns-online['monotonic_ns'])/1e9
 end_utc=start_utc+60;raw=(p/'guest-resources.log').read_text();chunks=re.split(r'(?m)^(\d{4}-\d\d-\d\dT[^\n]+)\n',raw)
 samples=[]
 for i in range(1,len(chunks),2):
  stamp=datetime.datetime.fromisoformat(chunks[i]);t=stamp.timestamp()
  if not start_utc<=t<end_utc:continue
  text=chunks[i+1];pressure=re.findall(r'^some avg10=([0-9.]+) avg60=([0-9.]+) avg300=([0-9.]+) total=(\d+)$',text,re.M);assert len(pressure)==2
  cpu={name:float(v) for name,v in re.findall(r'^(tinyimx-[^ ]+) CPU=([0-9.]+)%',text,re.M)}
  mem=int(re.search(r'^MemAvailable:\s+(\d+)',text,re.M).group(1))/1024
  samples.append({'utc':stamp.isoformat(),'guest_available_mib':mem,'memory_some_avg10':float(pressure[0][0]),'cpu_some_avg10':float(pressure[1][0]),'container_cpu_percent_one_core100':cpu})
 save(run+'-steady-resources.json',samples)
 def distribution(v):return {'samples':len(v),'min':min(v),'median':statistics.median(v),'mean':statistics.mean(v),'max':max(v)} if v else None
 cpu_names=sorted({n for s in samples for n in s['container_cpu_percent_one_core100']})
 resources={'window_start_utc_estimated':datetime.datetime.fromtimestamp(start_utc,datetime.timezone.utc).isoformat(),'duration_s':60,'sample_count':len(samples),'guest_available_mib':distribution([s['guest_available_mib'] for s in samples]),'memory_some_avg10':distribution([s['memory_some_avg10'] for s in samples]),'cpu_some_avg10':distribution([s['cpu_some_avg10'] for s in samples]),'container_cpu_percent':{n:distribution([s['container_cpu_percent_one_core100'][n] for s in samples if n in s['container_cpu_percent_one_core100']]) for n in cpu_names}}
 x={'run':run,'status':source['status'],'users':source['scenario']['users'],'rate':source['scenario']['rate'],'histograms':hists,'resources':resources,'source_sha256':{q.name:hashlib.sha256(q.read_bytes()).hexdigest() for q in [p/'summary.json',p/'all-online.json',p/'control/start_ns',p/'guest-resources.log']}}
 save(run+'-summary.json',x);runs.append(x);print(json.dumps({'run':run,'ack':hists['ack_histogram'],'cpu_pressure':resources['cpu_some_avg10'],'guest_available':resources['guest_available_mib'],'memory_pressure':resources['memory_some_avg10']}),flush=True)
save('summary.json',{'status':'COMPLETED_DIAGNOSTIC','runs':runs,'capacity_all_features_pass':False,'heartbeat_seconds':15,'heartbeat_source':'Current capacity_run.py passes --heartbeat-seconds15; do not confuse resource sampling5s with heartbeat interval'})
PY
