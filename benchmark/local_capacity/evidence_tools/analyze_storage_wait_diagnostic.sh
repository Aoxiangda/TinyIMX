#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re,statistics,collections,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';control=b/'storage-wait-diagnostic-control-20261005';raw=b/'capacity-sw150a';d=b/'storage-wait-completed-analysis-20261005'
assert (control/'summary.json').exists() and (raw/'summary.json').exists() and not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
deploy=json.loads((b/'storage-wait-diagnostics-message-deployment-20261005/summary.json').read_text());c=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-message-service-1'],text=True))[0];assert c['Id']==deploy['message_service_id']
start=int((raw/'control/start_ns').read_text())//1000;end=start+60_000_000
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Analyze completed diagnostic only: numeric wall/CPU phases and exact M/TID/steady-time associations within60s active window','writes':'Fresh bounded numeric evidence only, no full configs/logs/credentials','reads':'Current Message and Gateway logs in RAM; allowlisted prefixes and numeric fields; own completed scheduler samples','limits':'Limited nonrandom samples; Snowflake M%64 may select first sequence numbers, never population percentiles. Handler excludes admission/transport; wall-minus-threadCPU combines IO, locks and runnable waiting; do not infer IO alone. Observer overhead and diagnostic flags prevent acceptance claim.','runtime_changes':False,'load_started':False,'rollback':'Retain every raw test/result and immutable before image; no deletion'},indent=2)+'\n')
rows={'repository':[],'handler':[],'pool':[],'gateway':[]};since=json.loads((control/'audit-before.json').read_text())['utc'];until=datetime.datetime.now(datetime.timezone.utc).isoformat()
for name in ['tinyimx-m21-message-service-1','tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1']:
 logs=subprocess.run(['docker','logs','--since',since,'--until',until,name],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=40,check=True).stdout
 for line in logs.splitlines():
  if 'gateway private chat phase sample, ' in line:
   x={'gateway':name}
   for pair in line.split('gateway private chat phase sample, ',1)[1].strip().split(', '):
    k,sep,v=pair.partition('=')
    if k=='path' and v in ['chat','peer']:x[k]=v
    elif sep and re.fullmatch('[a-z_]+',k) and re.fullmatch('-?[0-9]+',v):x[k]=int(v)
   if 700001<=x.get('user_id',0)<=710000:rows['gateway'].append(x)
   continue
  for prefix,kind in [('private_persist_phase ','repository'),('storage_rpc_phase ','handler'),('mysql_pool_acquire_phase ','pool')]:
   if prefix not in line:continue
   x={k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line.split(prefix,1)[1])}
   if start<=x.get('started_us',-1)<end and (kind=='pool' or 700001<=x.get('from',0)<=710000 or kind=='handler' and 700001<=x.get('to',0)<=710000):rows[kind].append(x)
 del logs
for kind,rs in rows.items():(d/(kind+'-numeric.json')).write_text(json.dumps(rs,indent=2)+'\n')
def metrics(rs,keys):
 out={}
 for k in keys:
  vs=[x[k]/1000 for x in rs if x.get(k,-1)>=0]
  if vs:out[k]={'samples':len(vs),'median_ms':statistics.median(vs),'mean_ms':statistics.mean(vs),'max_ms':max(vs)}
 return out
matched=[];pools=[];ambiguities=[]
for repo in rows['repository']:
 handlers=[h for h in rows['handler'] if h.get('kind')==1 and h.get('mid')==repo.get('mid') and h.get('tid')==repo.get('tid') and h['started_us']<=repo['started_us'] and h['started_us']+h['total_us']>=repo['started_us']+repo['total_us']]
 if len(handlers)==1:
  h=handlers[0];x={'mid':repo['mid'],'tid':repo['tid'],'repository_us':repo['total_us'],'handler_us':h['total_us'],'handler_outside_repository_us':h['total_us']-repo['total_us'],'repository_cpu_us':repo['cpu_us'],'handler_cpu_us':h['cpu_us']}
  gs=[g for g in rows['gateway'] if g.get('path')=='chat' and g.get('message_id')==repo.get('mid') and g.get('persist_us',-1)>=0]
  if len(gs)==1:x.update({'gateway_rpc_us':gs[0]['persist_us'],'rpc_outside_handler_us':gs[0]['persist_us']-h['total_us']})
  matched.append(x)
 elif handlers:ambiguities.append({'mid':repo.get('mid'),'handler_candidates':len(handlers)})
 candidates=[p for p in rows['pool'] if p.get('tid')==repo.get('tid') and repo['started_us']<=p['started_us'] and p['started_us']+p['total_us']<=repo['started_us']+repo['total_us']]
 if len(candidates)==1:pools.append({'mid':repo['mid'],'tid':repo['tid'],'acquire_us':repo['acquire_us'],'pool_total_us':candidates[0]['total_us'],**candidates[0]})
(d/'matched-message-numeric.json').write_text(json.dumps(matched,indent=2)+'\n');(d/'matched-pool-numeric.json').write_text(json.dumps(pools,indent=2)+'\n')
cpu=[];snapshots=[json.loads(x) for x in (control/'scheduler-numeric.jsonl').read_text().splitlines()] if (control/'scheduler-numeric.jsonl').exists() else[]
for a,z in zip(snapshots,snapshots[1:]):
 elapsed=(z['monotonic_ns']-a['monotonic_ns'])/1e9
 for name,p in z['processes'].items():
  old=a['processes'].get(name,{});item={'container':name,'seconds':elapsed,'monotonic_ns':z['monotonic_ns'],'schedstats_enabled':z['schedstats_enabled']}
  if 'cpu_stat' in p and 'cpu_stat' in old:
   delta={k:v-old['cpu_stat'].get(k,0) for k,v in p['cpu_stat'].items()};item.update({'cgroup_cpu_delta':delta,'cpu_cores_used':delta.get('usage_usec',0)/1e6/elapsed,'cpu_max':p['cpu_max']})
  tids={t['host_tid']:t for t in old.get('threads',[]) if 'error' not in t};deltas=[]
  for t in p.get('threads',[]):
   if 'error' in t or t['host_tid'] not in tids:continue
   prev=tids[t['host_tid']];deltas.append({'ns_tid':t['ns_tid'],'runtime_ns':t['runtime_ns']-prev['runtime_ns'],'runqueue_wait_ns':t['runqueue_wait_ns']-prev['runqueue_wait_ns'],'slices':t['slices']-prev['slices']})
  item.update({'stable_thread_count':len(deltas),'thread_delta':deltas});cpu.append(item)
(d/'scheduler-deltas.json').write_text(json.dumps(cpu,indent=2)+'\n')
resources={}
for name in {x['container'] for x in cpu}:
 rs=[x for x in cpu if x['container']==name];vs=[x['cpu_cores_used'] for x in rs if 'cpu_cores_used'in x];resources[name]={'intervals':len(rs),'cpu_mean_cores':statistics.mean(vs) if vs else None,'cpu_max_cores':max(vs) if vs else None,'throttled_usec_delta':sum(x.get('cgroup_cpu_delta',{}).get('throttled_usec',0) for x in rs),'runqueue_wait_ms_sum_stable_threads':sum(t['runqueue_wait_ns'] for x in rs for t in x['thread_delta'])/1e6,'schedstats_enabled':list({x['schedstats_enabled'] for x in rs})}
x={'status':'COMPLETED_NUMERIC_DIAGNOSTIC_ANALYSIS','run':'sw150a','window_start_us':start,'window_end_us':end,'private':json.loads((raw/'summary.json').read_text()),'repository':metrics(rows['repository'],['total_us','cpu_us','acquire_us','acquire_cpu_us','precheck_us','precheck_cpu_us','begin_us','begin_cpu_us','insert_us','insert_cpu_us','identity_read_us','identity_read_cpu_us','outbox_insert_us','outbox_insert_cpu_us','commit_us','commit_cpu_us']),'persist_handler':metrics([h for h in rows['handler'] if h.get('kind')==1],['total_us','cpu_us']),'confirm_handler':metrics([h for h in rows['handler'] if h.get('kind')==2],['total_us','cpu_us']),'pool':metrics(rows['pool'],['mutex_wait_us','slot_wait_us','ping_us','reconnect_us','total_us','thread_cpu_us']),'matched_messages':metrics(matched,['repository_us','handler_us','handler_outside_repository_us','repository_cpu_us','handler_cpu_us','gateway_rpc_us','rpc_outside_handler_us']),'matched_pools':metrics(pools,['acquire_us','pool_total_us','mutex_wait_us','slot_wait_us','ping_us']),'match_ambiguities':ambiguities,'matched_negative_rpc_outside_handler_count':sum(x.get('rpc_outside_handler_us',0)<0 for x in matched),'scheduler':resources,'scheduler_snapshot_count':len(snapshots),'snapshot_max_ms':max((x['snapshot_elapsed_ms'] for x in snapshots),default=None),'gateway':metrics([g for g in rows['gateway'] if g.get('path')=='chat'],['dispatch_age_us','work_us','permission_us','persist_us','route_us','unread_us']),'private_summary_sha256':hashlib.sha256((raw/'summary.json').read_bytes()).hexdigest(),'limits':'Only rate-limited nonrandom phase samples within active window. Gateway samples constrained by complete control time bounds, joinedM belongs active handler sample. No CPU trace alone differentiates runnable scheduling vs IO/locks. No all-feature or capacity acceptance.'}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({k:v for k,v in x.items() if k not in ['private','repository']},indent=2));print('REPOSITORY='+json.dumps(x['repository']))
PY
