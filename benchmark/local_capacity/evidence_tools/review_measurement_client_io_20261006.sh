#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime,time,math
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'measurement-client-io-audit-20261006';assert not d.exists()
def run(a):return subprocess.check_output(a,text=True,timeout=25)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def identity(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
head=run(['git','rev-parse','HEAD']).strip();assert head=='5f05f4cc544e48900d7ee1ee5cfaa08b6c3aa887'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
cs=json.loads(run(['docker','inspect',*names]));runtime={c['Name']:identity(c) for c in cs}
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');configs={p.name:sha(p) for p in cfg.glob('*.json')}
inp=b/'group-confirm-coalesce-control-20261006';summary=json.loads((inp/'summary.json').read_text());assert summary['status']=='GROUP_CONFIRM_COALESCE_REAL_ABBA_COMPLETE'
inputs={case:inp/(case+'-all-requests.json') for case in ['A1','B1','B2','A2']}
actor=r/'benchmark/local_capacity/cross_feature_actor.py';actor_lines=actor.read_text().splitlines()
idx=next(i for i,x in enumerate(actor_lines) if 'def emit(' in x)
emit_source='\n'.join(actor_lines[idx:idx+12])
gw=[c for c in cs if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']]
workers={c['Name']:int(next(x.split('=',1)[1] for x in c['Config']['Env'] if x.startswith('TINYIMX_MESSAGE_WORKER_THREADS='))) for c in gw};assert len(workers)==2 and set(workers.values())=={16}
d.mkdir(mode=0o700)
audit={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Offline replay only of exact json.dumps(indent=2)+write_text after send, and append/open/close delivery evidence; no sockets, Docker exec, SQL, service config, pressure or deletion. Own fresh scratch files may be rewritten within this stage only. No fsync added because original has none.','head':head,'inputs_sha256':{k:sha(v) for k,v in inputs.items()},'actor_source_sha256':sha(actor),'emit_source':emit_source,'runtime':runtime,'config_sha256':configs,'actual_message_workers':workers,'scratch_limits':'4 prefix scratch <=2MiB each, 4 timeline scratch <=64KiB each; evidence stays; 8 repeats per case/size after in-memory warmup; elapsed wall/CPU split'}
(d/'audit-before.json').write_text(json.dumps(audit,indent=2)+'\n')
def stats(a):return {'samples':len(a),'mean_ms':sum(a)/len(a),'min_ms':min(a),'max_ms':max(a)}
out=[]
try:
 for case,p in inputs.items():
  rows=json.loads(p.read_text());assert len(rows)==132
  for size in [2,16,65,100]:
   pos=next(i for i,z in enumerate(rows) if z['size']==size and z['index']==30)
   current={k:rows[pos][k] for k in ['size','index','measured','seq','group_id','client_message_id','sent_mono_ns','expected_recipients']}
   prefix=rows[:pos]+[current];json.dumps(prefix,indent=2)
   samples=[];scratch=d/(case+'-prefix-scratch.json')
   for rep in range(8):
    t0=time.perf_counter_ns();c0=time.process_time_ns();payload=json.dumps(prefix,indent=2)+'\n';t1=time.perf_counter_ns();c1=time.process_time_ns()
    scratch.write_text(payload);t2=time.perf_counter_ns();c2=time.process_time_ns()
    samples.append({'encode_wall_ms':(t1-t0)/1e6,'encode_cpu_ms':(c1-c0)/1e6,'write_wall_ms':(t2-t1)/1e6,'write_cpu_ms':(c2-c1)/1e6,'total_wall_ms':(t2-t0)/1e6,'total_cpu_ms':(c2-c0)/1e6})
   assert len(payload.encode())<2*1024**2
   out.append({'case':case,'group_size':size,'prefix_rows':len(prefix),'encoded_bytes':len(payload.encode()),'samples':samples,'statistics':{k:stats([x[k] for x in samples]) for k in samples[0]}})
 timeline=[]
 for case,p in inputs.items():
  row=next(z for z in json.loads(p.read_text()) if z['size']==100 and z['index']==30)
  target=d/(case+'-timeline-scratch.jsonl')
  for rep in range(8):
   t0=time.perf_counter_ns();c0=time.process_time_ns()
   for z in row['recipient_arrivals']:
    record={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'mono_ns':time.monotonic_ns(),'event':'response','uid':z['recipient'],'kind':2051,'seq':row['seq'],'body':{'message_id':row['message_id'],'group_id':row['group_id'],'from_user_id':519870,'message_type':1,'content':'Codex bounded timeline I/O diagnostic'}}
    with target.open('a') as f:f.write(json.dumps(record,ensure_ascii=False)+'\n')
   timeline.append({'case':case,'repeat':rep,'records':99,'wall_ms':(time.perf_counter_ns()-t0)/1e6,'cpu_ms':(time.process_time_ns()-c0)/1e6})
  assert target.stat().st_size<512*1024
 after=json.loads(run(['docker','inspect',*names]));assert {c['Name']:identity(c) for c in after}==runtime and {p.name:sha(p) for p in cfg.glob('*.json')}==configs and run(['git','rev-parse','HEAD']).strip()==head
 assert all(sha(v)==audit['inputs_sha256'][k] for k,v in inputs.items())
 result={'status':'OFFLINE_MEASUREMENT_CLIENT_IO_AUDIT_COMPLETE','head':head,'actual_message_workers':workers,'prefix_replay':out,'timeline_replay':timeline,'timeline_statistics':{k:stats([x[k] for x in timeline]) for k in ['wall_ms','cpu_ms']},'runtime_and_private_configs_preserved':True,'raw_inputs_preserved':True,'service_load_generated':False,'limits':'Diagnostic offline replay on same guest and filesystem, 8 observations per size/case, in-memory warmup; completed previous rows include evidence already known at next send; timeline synthetic body shape, not exact packet. Does not observe network arrival or attribute server delay. No population P99, no subtraction/correction of historical times, no acceptance gates changed.'}
 (d/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({'status':result['status'],'actual_message_workers':workers,'prefix':[{k:z[k] for k in ['case','group_size','prefix_rows','encoded_bytes']}|{'total_wall':z['statistics']['total_wall_ms'],'encode_cpu':z['statistics']['encode_cpu_ms']} for z in out],'timeline':result['timeline_statistics'],'runtime_preserved':True},indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__})+'\n');raise
PY
