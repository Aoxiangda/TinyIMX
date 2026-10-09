#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re,statistics,collections,hashlib
r=pathlib.Path.cwd(); b=r/'.local/codex'; parent=b/'restored-baseline-finer-rate-curve-20261005'
assert (parent/'summary.json').exists(), 'Completed evidence only'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1, 'No active capacity workers'
d=b/'restored-curve-causal-review-20261005-attempt3'; assert not d.exists(); d.mkdir()
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Read-only analysis of already completed finer rate curve, current startup numeric settings and timestamp-bounded numeric phase samples','writes':'Fresh diagnostic directory only','runtime_changes':[],'load_tests_started':False,'limits':'Phase samples are rate-limited slow samples, not population quantiles; timestamps include ramp/drain. No SQL/environment/private-log export.','rollback':'Keep original evidence; no deletion or mutation'},indent=2)+'\n')
settings=[]
for name in ['tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1']:
 logs=subprocess.run(['docker','logs',name],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30,check=True).stdout
 for line in logs.splitlines():
  if 'gateway message runtime started' in line:
   values={k:int(v) for k,v in re.findall(r'([a-z_]+)=(\d+)',line)}
   settings.append({'container':name,**values})
 del logs
if len(settings)!=2:
 settings=[]
 for name in ['tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1']:
  c=json.loads(subprocess.check_output(['docker','inspect',name],text=True))[0]
  matches=[]
  candidates=set()
  for arg in c['Config'].get('Cmd') or []:
   if not isinstance(arg,str) or not arg.endswith('.json'):continue
   target=pathlib.PurePosixPath(arg)
   for m in c['Mounts']:
    mount=pathlib.PurePosixPath(m['Destination'])
    if target==mount:candidates.add(pathlib.Path(m['Source']))
    elif target.is_relative_to(mount):candidates.add(pathlib.Path(m['Source'])/str(target.relative_to(mount)))
  for p in candidates:
   if p.is_file() and p.suffix=='.json':
    config=json.loads(p.read_text())
    if 'business_runtime' in config:
     fields={k:v for k,v in config['business_runtime'].items() if k in ['worker_threads','max_pending_tasks','stripe_count','per_stripe_queue_capacity','default_deadline_ms','shutdown_timeout_ms'] and isinstance(v,int)}
     fields['config_sha256']=hashlib.sha256(p.read_bytes()).hexdigest();matches.append(fields)
  assert len(matches)==1,'Exactly one actual mounted business config'
  env=dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
  workers=env.get('TINYIMX_MESSAGE_WORKER_THREADS'); assert workers=='16'
  settings.append({'container':name,'source':'Mounted config plus allowlisted message worker override; INFO startup not available','message_workers':int(workers),**matches[0]})
(d/'runtime-message-numeric-settings.json').write_text(json.dumps(settings,indent=2)+'\n')
def metrics(rows,keys):
 out={}
 for k in keys:
  v=[x[k]/1000 for x in rows if x.get(k,-1)>=0]
  if v:out[k]={'samples':len(v),'median_ms':statistics.median(v),'mean_ms':statistics.mean(v),'max_ms':max(v)}
 return out
results=[]
for run,n in [('base1k300a',1000),('base10k150a',10000),('base10k200a',10000),('base10k250a',10000)]:
 p=parent/run; raw=b/('capacity-'+run); assert (raw/'container-identity-after.json').exists()
 out=d/run; out.mkdir(); summary=json.loads((raw/'summary.json').read_text()); reasons=collections.Counter()
 for f in raw.glob('worker-*/failure-responses.jsonl'):
  for line in f.read_text().splitlines():
   x=json.loads(line); reasons[x.get('kind','')+':'+x.get('reason','')]+=1
 since=json.loads((p/'audit-before.json').read_text())['utc']; until=datetime.datetime.fromtimestamp((p/'summary.json').stat().st_mtime,datetime.timezone.utc).isoformat(); repo=[]; gateway=[]
 for name in ['tinyimx-m21-message-service-1','tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1']:
  logs=subprocess.run(['docker','logs','--since',since,'--until',until,name],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30,check=True).stdout
  for line in logs.splitlines():
   if 'private_persist_phase ' in line:
    x={k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line.split('private_persist_phase ',1)[1])}
    if 700001<=x.get('from',0)<=700000+n: repo.append(x)
   elif 'gateway private chat phase sample, ' in line:
    x={}
    for item in line.split('gateway private chat phase sample, ',1)[1].strip().split(', '):
     k,sep,v=item.partition('=')
     if k=='path' and v in ['chat','peer']:x[k]=v
     elif sep and re.fullmatch('[a-z_]+',k) and re.fullmatch('-?[0-9]+',v):x[k]=int(v)
    if 700001<=x.get('user_id',0)<=700000+n:gateway.append({'gateway':name,**x})
  del logs
 byid={x['mid']:x for x in repo if x.get('mid',0)>0}
 matched=[{'mid':x['message_id'],'outside_repository_us':x['persist_us']-byid[x['message_id']]['total_us']} for x in gateway if x.get('path')=='chat' and x.get('message_id',0) in byid and x.get('persist_us',-1)>=0]
 for name,rows in [('repository',repo),('gateway',gateway),('matched',matched)]: (out/(name+'-numeric.json')).write_text(json.dumps(rows,indent=2)+'\n')
 x={'run':run,'since':since,'until':until,'summary':summary,'saved_negative_response_counts':dict(reasons),'saved_response_count':sum(reasons.values()),'negative_response_count_complete':sum(reasons.values())==summary['metrics']['chat_ack_fail'],'repository':metrics(repo,['total_us','acquire_us','precheck_us','begin_us','insert_us','identity_read_us','outbox_insert_us','commit_us']),'gateway_chat':metrics([x for x in gateway if x.get('path')=='chat'],['dispatch_age_us','entry_budget_us','work_us','permission_us','persist_us','route_us','unread_us']),'matched':metrics(matched,['outside_repository_us']),'source_summary_sha256':hashlib.sha256((raw/'summary.json').read_bytes()).hexdigest()}
 (out/'summary.json').write_text(json.dumps(x,indent=2)+'\n');results.append(x)
 print(json.dumps({'run':run,'p99':summary['positive_ack_p99_ms_upper_bin'],'neg':summary['metrics']['chat_ack_fail'],'skipped':summary['metrics']['skipped_scheduled_requests'],'queue':x['gateway_chat'].get('dispatch_age_us'),'acquire':x['repository'].get('acquire_us'),'reasons':dict(reasons)}),flush=True)
(d/'summary.json').write_text(json.dumps({'status':'READ_ONLY_ANALYSIS_COMPLETED','runtime_numeric_settings':settings,'results':results,'warning':'No new load or deployment; phase samples not population P99; source facts distinguish causal hypotheses'},indent=2)+'\n')
print('SETTINGS='+json.dumps(settings))
PY
