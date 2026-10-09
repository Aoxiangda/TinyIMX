#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-fanout-delay-analysis-20261006';assert not d.exists()
def run(a):return subprocess.check_output(a,text=True,timeout=30)
cs=json.loads(run(['docker','inspect',*run(['docker','ps','--format','{{.Names}}']).splitlines()]));assert len(cs)==19 and all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
d.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
control=b/'group-actor-snapshot-endpoint-control-20261006-attempt4';c=json.loads((control/'summary.json').read_text());assert c['fixed_measured_requests']==800 and c['cross_operations']==216 and c['cross_assertions']==148
runtime={x['Name']:{'id':x['Id'],'image':x['Image'],'started':x['State']['StartedAt']} for x in cs}
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly four real exactMID group send request+senderACK+3recipient wire event intervals; selected non-secret fanout environment; no perf rerun','head':run(['git','rev-parse','HEAD']).strip(),'runtime_before':runtime})
rows=[]
for case in ['A1','B1','B2','A2']:
 stage=b/('cross-feature-gsv4feat'+case);timeline=[json.loads(v) for v in (stage/'timeline.jsonl').read_text().splitlines()]
 req=next(v for v in timeline if v['event']=='request' and v.get('name')=='group-send-with-fanout')
 ack=next(v for v in timeline if v['event']=='response' and v.get('kind')==2050 and v['uid']==req['uid'] and v['seq']==req['seq'])
 assert ack['body']['success'] and ack['body']['result']=='created' and ack['body']['client_message_id']==req['body']['client_message_id']
 mid=ack['body']['message_id'];deliveries=[v for v in timeline if v['event']=='response' and v.get('kind')==2051 and v['body'].get('message_id')==mid]
 audit=json.loads((stage/'audit-before.json').read_text());users=audit['args']['users'];recipients=set(users)-{req['uid']}
 assert len(deliveries)==3 and {v['uid'] for v in deliveries}==recipients
 assert all(v['body']['group_id']==req['body']['group_id'] and v['body']['from_user_id']==req['uid'] and v['body']['content']==req['body']['content'] for v in deliveries)
 parts=[{'uid':v['uid'],'request_to_delivery_ms':(v['mono_ns']-req['mono_ns'])/1e6,'sender_ack_to_delivery_ms':(v['mono_ns']-ack['mono_ns'])/1e6,'delivery_utc':v['utc']} for v in deliveries]
 rows.append({'case':case,'message_id':mid,'group_id':req['body']['group_id'],'sender_uid':req['uid'],'client_message_id':req['body']['client_message_id'],'request_utc':req['utc'],'ack_utc':ack['utc'],'request_to_sender_ack_ms':(ack['mono_ns']-req['mono_ns'])/1e6,'first_request_to_delivery_ms':min(v['request_to_delivery_ms'] for v in parts),'all_request_to_delivery_ms':max(v['request_to_delivery_ms'] for v in parts),'all_ack_to_delivery_ms':max(v['sender_ack_to_delivery_ms'] for v in parts),'recipients':parts,'timeline_sha256':hashlib.sha256((stage/'timeline.jsonl').read_bytes()).hexdigest(),'scope':'One message percase/3recipients, total4messages; exactclientevent intervals, notP99 or soleCPU/DB/network attribution'})
envs={}
keys=['TINYIMX_GROUP_FANOUT_ENABLE','TINYIMX_GROUP_FANOUT_RECOVERY_MS','TINYIMX_GROUP_FANOUT_BATCH_SIZE','TINYIMX_GROUP_FANOUT_ACK_RETRY_MS','TINYIMX_GROUP_FANOUT_FAILURE_RETRY_MS']
for x in cs:
 if x['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:continue
 values=dict(v.split('=',1) for v in x['Config']['Env'] if '=' in v);envs[x['Name']]={k:values.get(k) for k in keys};assert values.get('TINYIMX_GROUP_FANOUT_ENABLE')=='1' and not any(k.startswith('TINYIMX_FAULT_') for k in values)
stop=[json.loads(p.read_text()) for p in sorted(control.glob('stop-*-summary.json'))];assert len(stop)==5 and all(v['exit_code']==0 for v in stop if v['candidate']) and stop[0]['exit_code']==143
threads=[json.loads(p.read_text()) for p in sorted(control.glob('*-thread-mask-summary.json'))];assert len(threads)==4 and all(v['ordinary_workers_all_blocked'] and v['dedicated_sigwait_threads']==1 for v in threads)
x={'status':'GROUP_FANOUT_DELAY_READONLY_COMPLETE','rows':rows,'actual_gateway_fanout_env':envs,'lifecycle_ordinary_workers_verified':threads,'exact_stop_results':stop,'endpoint_results':[{'case':v['case'],'mode':v['mode'],'metrics':[{'name':z['name'],'metrics':z['metrics']} for z in v['endpoint']['metrics']]} for v in c['cases']],'restore':c['restore'],'runtime_before':runtime,'limits':'Sparse4messages/12recipient events, no capacityP99. Existing recovery timer adds up to its interval before claim; source+timing association requires sameELF wake OFF/ON controls before causality/performance acceptance','performance_acceptance':False}
save('summary.json',x);print(json.dumps({'status':x['status'],'rows':rows,'actual_gateway_fanout_env':envs,'stop_exit_codes':[v['exit_code'] for v in stop]},indent=2))
PY
