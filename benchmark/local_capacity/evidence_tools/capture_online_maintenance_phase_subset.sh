#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re,hashlib,statistics
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-maintenance-phase-subset-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
deployment=json.loads((b/'online-maintenance-gateway-deployment-20261005/summary.json').read_text());cs=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1','tinyimx-m21-message-service-1'],text=True))
for c in cs:
 if c['Name'] in deployment['gateway_ids']:assert c['Id']==deployment['gateway_ids'][c['Name']] and c['Image']==deployment['image']
 else:assert c['Image']=='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898'
lookup={};inputs={};runs=[name for name in ['batch150A1','batch150B1','batch150B2'] if (b/('online-maintenance-control-'+name)/'summary.json').exists()]
for name in runs:
 for p in sorted((b/('capacity-'+name)).glob('worker-*/ledger.tsv')):
  inputs[str(p.relative_to(b))]=hashlib.sha256(p.read_bytes()).hexdigest()
  for line in p.read_text().splitlines():
   kind,uid,to,mid,seq,cid,when=line.split('\t')
   if kind=='ack':assert int(mid)>0 and int(mid) not in lookup;lookup[int(mid)]={'run':name,'sender':int(uid),'recipient':int(to),'client_seq':int(seq),'client_ack_ns':int(when)}
since=json.loads((b/'online-maintenance-control-batch150A1/audit-before.json').read_text())['utc'];until=datetime.datetime.now(datetime.timezone.utc).isoformat();d.mkdir();private=d/'runtime-private';private.mkdir()
(d/'audit-before.json').write_text(json.dumps({'utc':until,'operation':'Readonly preserve originalMessage andcandidateGW phase logs before rollback plus matchednumeric subset','inputs_sha256':inputs,'container_refs':{c['Name']:{'id':c['Id'],'image':c['Image']} for c in cs},'reads':'SavedbaselineGWpredeployprivate logs andboundedtime existingDockerlogs sinceA1 audit; no newtrace/instrumentation/configs/SQLwrites','writes':'Ownprivate fullinterval logs plus allowlistednumeric rows matchingactualownedpositiveMID identities, no fullLogs/env/config export','limits':'Gateway>=100ms/rate8 andpersistence>=10ms/rate8 slowbiased populations; meansnotpopulationP99/CPUcausalpartition, zero/missing rotatedsamples notzero work','rollback':'Keepallprivate/raw/numeric/source/images, no deletion'},indent=2)+'\n')
paths=[]
for c in cs:
 p=private/(c['Name'].split('/')[-1]+'.log')
 with p.open('w') as f:subprocess.run(['docker','logs','--since',since,'--until',until,c['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=45)
 paths.append((c['Name'],p,'candidate-gateway' if c['Name'] in deployment['gateway_ids'] else 'original-message'))
for side in ['a','b']:
 p=b/'online-maintenance-gateway-deployment-20261005/runtime-private'/('tinyimx-m21-gateway-'+side+'-1.before.log');assert p.exists();paths.append(('/tinyimx-m21-gateway-'+side+'-1',p,'baseline-gateway'))
gw=[];persist=[];prefix='gateway private chat phase sample, ';numeric={'user_id','message_id','request_seq','session_epoch','dispatch_age_us','entry_budget_us','has_deadline','work_us','permission_us','permission_budget_us','route_us','persist_us','persist_budget_us','unread_us','peer_validate_us','peer_validate_budget_us','failed','suppressed_samples'}
for container,path,role in paths:
 with path.open() as stream:
  for line in stream:
   if prefix in line:
    row={'gateway':container,'role':role}
    for pair in line.split(prefix,1)[1].strip().split(', '):
     key,sep,value=pair.partition('=')
     if sep and key=='path' and value in ['chat','peer']:row[key]=value
     elif sep and key in numeric and re.fullmatch('-?[0-9]+',value):row[key]=int(value)
    identity=lookup.get(row.get('message_id',0))
    if identity is None:continue
    if role=='baseline-gateway':assert identity['run']=='batch150A1'
    elif role=='candidate-gateway':assert identity['run'] in ['batch150B1','batch150B2']
    else:raise AssertionError('GatewayphaseinMessageLog')
    assert row.get('path') in ['chat','peer']
    if row['path']=='chat':assert row['user_id']==identity['sender'] and row['request_seq']==identity['client_seq'] and row['session_epoch']>0
    else:assert row['user_id']==0 and row['session_epoch']==0 and row['request_seq']>0
    row['run']=identity['run'];gw.append(row)
   if 'private_persist_phase ' in line:
    assert role=='original-message';row={k:int(v) for k,v in re.findall(r'([a-z_]+)=(-?\d+)',line.split('private_persist_phase ',1)[1])};mid=row.get('message_id',row.get('mid',0));identity=lookup.get(mid)
    if identity is None:continue
    assert row.get('from',0)==identity['sender'];row['run']=identity['run'];persist.append(row)
(d/'gateway-phase-numeric.json').write_text(json.dumps(gw,indent=2)+'\n');(d/'persistence-phase-numeric.json').write_text(json.dumps(persist,indent=2)+'\n')
def summary(rows,keys):
 return {key:{'samples':len(v),'mean_ms':statistics.mean(v),'max_ms':max(v)} for key in keys if (v:=[row[key]/1000 for row in rows if row.get(key,-1)>=0])}
reports=[]
for name in runs:
 chat=[row for row in gw if row['run']==name and row['path']=='chat'];repo=[row for row in persist if row['run']==name];intersection=set(row['message_id'] for row in chat)&set(row.get('message_id',row.get('mid',0)) for row in repo)
 reports.append({'run':name,'chat_phase_samples':len(chat),'persistence_phase_samples':len(repo),'shared_MID_sample_count':len(intersection),'chat_biased_metrics':summary(chat,['dispatch_age_us','work_us','permission_us','route_us','persist_us','unread_us']),'persistence_biased_metrics':summary(repo,['total_us','acquire_us','precheck_us','begin_us','insert_us','identity_read_us','outbox_insert_us','commit_us']),'limits':'Differentthreshold/rate-limited populations; never subtractpopulationmeans or infer CPU/normalP99 from these samples'})
x={'status':'ONLINE_MAINTENANCE_PHASE_SUBSET_COMPLETE','runs':runs,'reports':reports,'private_log_sha256':{str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for _,p,_ in paths},'performance_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
