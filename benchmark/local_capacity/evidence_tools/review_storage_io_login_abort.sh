#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'storage-io-login-abort-review-20261005';assert not d.exists()
raw=b/'capacity-io150base';s=json.loads((raw/'summary.json').read_text());assert s['phase']=='worker_or_coordinator_abort' and not s['all_online_reached']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Read-only audit of aborted pre-online baseline; compare priorlogin histograms, foreground failure semantics and actualnumeric runtime/config. No rerun ordeadline relaxation','writes':'Fresh bounded numeric failure/quantile/settings review only','runtime_changes':False,'rollback':'All partialraw/stages preserved; no source/config/parameter changes or userapp termination'},indent=2)+'\n')
rows=[]
def histogram(v):
 if not isinstance(v,dict) or not v.get('count'):return None
 bins=v.get('bins',[]);n=v['count'];out={'count':n,'mean_ms':v.get('sum_us',0)/n/1000,'max_ms':v.get('max_us',0)/1000}
 for name,q in [('p50',.5),('p95',.95),('p99',.99)]:
  total=0
  for upper,count in bins:
   total+=count
   if total>=__import__('math').ceil(n*q):out[name+'_upper_bin_ms']=(upper+v.get('step_us',100))/1000;break
 return out
for run in ['sw150a','mp150diag','io150base']:
 path=b/('capacity-'+run)/'worker-0/final.json';x=json.loads(path.read_text());h={k:histogram(v) for k,v in x.items() if 'login'in k and isinstance(v,dict)}
 rows.append({'run':run,'status':x.get('status'),'connected':x.get('metrics',{}).get('connected'),'login_ok':x.get('metrics',{}).get('login_ok'),'login_histograms':h,'final_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
failure=[json.loads(x) for x in (raw/'worker-0/failure-responses.jsonl').read_text().splitlines()];assert len(failure)==1 and failure[0]['reason']=='business_deadline_exceeded'
settings=[]
for name in ['gateway-a','gateway-b','user']:
 p=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')/(name+'.json');cfg=json.loads(p.read_text());v={'name':name,'config_sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
 if 'business_runtime'in cfg:v['business_runtime']={k:z for k,z in cfg['business_runtime'].items() if isinstance(z,int)}
 if 'mysql'in cfg:v['mysql_pool_size']=cfg['mysql'].get('pool_size')
 settings.append(v)
since=json.loads((b/'storage-io-baseline-control-20261005/audit-before.json').read_text())['utc'];until=datetime.datetime.fromtimestamp((raw/'summary.json').stat().st_mtime,datetime.timezone.utc).isoformat();warnings=[]
for name in ['tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1','tinyimx-m21-user-service-1']:
 logs=subprocess.run(['docker','logs','--since',since,'--until',until,name],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=True,timeout=20).stdout
 for line in logs.splitlines():
  for prefix in ['business task','gateway login RPC failed','gateway login rejected','executor','deadline','password verify']:
   if prefix in line.lower():
    warnings.append({'container':name,'category':prefix,'numeric':{k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line)},'known_tokens':[x for x in ['overloaded','deadline','expired','cancelled','unavailable'] if x in line.lower()]});break
 del logs
(d/'login-histograms.json').write_text(json.dumps(rows,indent=2)+'\n');(d/'failure-numeric.json').write_text(json.dumps(failure,indent=2)+'\n');(d/'warnings-numeric.json').write_text(json.dumps(warnings,indent=2)+'\n')
x={'status':'ABORTED_LOGIN_REVIEW_COMPLETED','control_summary':s,'login_histograms':rows,'runtime_numeric_settings':settings,'warning_count':len(warnings),'deadline_source':'GatewayServer::HandleLoginRequest on_prestart_failure emits business_deadline_exceeded; admittedTask expired/cancelled beforework in BusinessExecutor, unlike loginRPC transport error. Must distinguish exact dispatcher/context branch beforeclaimingPBKDF2solecause.','io_counters_valid':False,'database_settings_changed':False,'next_load_started':False,'performance_acceptance':False}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({k:v for k,v in x.items() if k!='control_summary'},indent=2))
PY
