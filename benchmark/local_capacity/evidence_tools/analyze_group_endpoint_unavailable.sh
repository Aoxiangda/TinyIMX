#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime,re,shutil
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-endpoint-unavailable-analysis-20261006';assert not d.exists()
case=b/'group-actor-snapshot-endpoint-control-20261006-attempt2';audit=json.loads((case/'audit-before.json').read_text());restore=json.loads((case/'restore-summary.json').read_text())
names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly exactfailedOFF requesttimeline + preservedcandidateGroup logs andGateway/ZK window; no restart/data/config change','source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'restore':restore,'raw_logs':'Privateonly, publicnumericrequestcounts and selectedregistryerrorlines, no env/config export'})
records=[json.loads(x) for x in (case/'A1/timeline.jsonl').read_text().splitlines()]
responses=[x for x in records if x['event']=='response'];errors=[x for x in responses if x.get('body',{}).get('success') is False]
requests=[x for x in records if x['event']=='request'];counts={}
for q in requests:
 key=q['name'];row=counts.setdefault(key,{'issued':0,'success':0,'failure':0});row['issued']+=1
 reply=[v for v in responses if v.get('uid')==q['uid'] and v.get('seq')==q['seq'] and v.get('kind')==q['kind']+1]
 if len(reply)==1:
  good=reply[0].get('body',{}).get('success') is True;row['success' if good else 'failure']+=1
pairs=[]
for e in errors:
 req=[q for q in requests if q['uid']==e['uid'] and q['seq']==e['seq'] and q['kind']+1==e['kind']]
 pairs.append({'response':e,'request':req[0] if len(req)==1 else None,'wall_ms_to_response_event':(e['mono_ns']-req[0]['mono_ns'])/1e6 if len(req)==1 else None})
save('request-counts.json',counts);save('failed-response-pairs.json',pairs)
candidate=case/'runtime-private/restore-original-group-before.log';assert candidate.exists();shutil.copy2(candidate,private/'candidate-group.log')
targets=[('/tinyimx-m21-gateway-a-1','gateway-a'),('/tinyimx-m21-gateway-b-1','gateway-b'),('/tinyimx-m21-zookeeper-1','zookeeper'),('/tinyimx-m21-group-service-1','restored-group')]
for name,label in targets:
 c=next(c for c in cs if c['Name']==name)
 with (private/(label+'.log')).open('w') as f:subprocess.run(['docker','logs','--timestamps','--since',audit['utc'],c['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=35)
patterns=['session expired','Session expired','registration failed','registration recovery','owned by another','Connection reset','connection loss','disconnected','closing session','Unable to read','Connection refused','UnresolvedAddress','instance','ERROR','WARN','EndOfStreamException']
log_summary={}
for p in private.glob('*.log'):
 lines=p.read_text(errors='replace').splitlines();selected=[line for line in lines if any(x in line for x in patterns)]
 # Own rawfull logs remain private. This publicdiagnosis only registry-lines,
 # not all gatewaybusiness payload orconfig.
 registry=[line for line in selected if any(x in line for x in ['ZooKeeper','zookeeper','Session','session','registration','EndOfStream','GroupService','Connection refused','Connection reset','discovery'])]
 log_summary[p.name]={'total_lines':len(lines),'selected_registry_lines':registry[-100:],'private_sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
save('registry-log-summary.json',log_summary)
x={'status':'GROUP_OFF_ENDPOINT_FAILURE_READONLY_ANALYSIS_COMPLETE','failed_pairs':pairs,'request_counts':counts,'registry_logs':log_summary,'current_group_restart_count':next(c['RestartCount'] for c in cs if c['Name']=='/tinyimx-m21-group-service-1'),'all19_healthy':True,'other18_preserved':all(c['Name']=='/tinyimx-m21-group-service-1' or {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}==audit['runtime_before'][c['Name']] for c in cs),'whole_runtime_identity':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'candidate_is_stopped_original_restored':True,'capacity_acceptance':False}
save('summary.json',x)
print(json.dumps({'status':x['status'],'request_counts':counts,'failed_pairs':pairs,'registry_logs':log_summary,'all19_healthy':True,'other18_preserved':x['other18_preserved']},indent=2))
PY
