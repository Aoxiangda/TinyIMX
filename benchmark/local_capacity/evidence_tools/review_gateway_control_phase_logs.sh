#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re,hashlib,statistics
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'gateway-controlled-phase-log-review-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
rollback=json.loads((b/'receiver-guarded-update-rollback-20261005/summary.json').read_text());assert rollback['status']=='ORIGINAL_B24_MESSAGE_RESTORED_VERIFIED'
names=['tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1'];cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
assert all(c['Image']=='sha256:1d8d71faa91a5a1e0a261c2a89efa1027a93515ae2b94b2c2d06f48010a7486e' for c in cs)
original=json.loads((b/'receiver-guarded-update-controls-source-20261005/runtime-after.json').read_text())
assert all(original['containers'][c['Name']]=={'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs)
lookup={};input_hashes={}
for name in ['guard150A1','guard150B1']:
 raw=b/('capacity-'+name);summary=json.loads((raw/'summary.json').read_text());assert summary['metrics']['chat_ack_ok']==9000
 for path in sorted(raw.glob('worker-*/ledger.tsv')):
  input_hashes[str(path.relative_to(b))]=hashlib.sha256(path.read_bytes()).hexdigest()
  for line in path.read_text().splitlines():
   kind,uid,to,mid,seq,cid,when=line.split('\t')
   if kind=='ack':assert int(mid)>0 and int(mid) not in lookup;lookup[int(mid)]={'run':name,'sender':int(uid),'recipient':int(to),'client_ack_ns':int(when),'client_seq':int(seq)}
assert len(lookup)==18000
since=json.loads((b/'receiver-guarded-update-control-guard150A1/audit-before.json').read_text())['utc'];until=datetime.datetime.now(datetime.timezone.utc).isoformat()
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':until,'operation':'Read-only existing running Gateway phase and log-configuration review after complete matched controls','source_gateway_compiled':'33fc9bb','source_image':'1d8d71','container_ids':{c['Name']:c['Id'] for c in cs},'inputs':input_hashes,'reads':'Gateway existing log interval inRAM, allowlistednumericphasefields only matched to18000ownedpositiveMIDs; logging configuration fields only allowlist','writes':'Fresh bounded numeric report; no full service log/config/environment exported','scope':'No newload/build/deployment/SQLwrite/instrumentationchange','limits':'Gateway thresholddispatch/work>=100ms and8lines/sec, biased andpossibly rotated; samples notpopulationP99 or CPU proof. dispatch_age includes received_at-to-entry, not purequeue. persistRPC duration includes transport/remoteadmission/SQL/reply. Missing logs cannot be invented. Currentlogging source may not equal unlabeledruntime withoutsourceRefs.','rollback':'All stages/source/images retained; no deletion'},indent=2)+'\n')
rows=[];unmatched=0
numeric={'user_id','message_id','request_seq','session_epoch','dispatch_age_us','entry_budget_us','has_deadline','work_us','permission_us','permission_budget_us','route_us','persist_us','persist_budget_us','unread_us','peer_validate_us','peer_validate_budget_us','failed','suppressed_samples'}
for c in cs:
 data=subprocess.run(['docker','logs','--since',since,'--until',until,c['Id']],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,check=True,timeout=45).stdout
 for line in data.splitlines():
  prefix='gateway private chat phase sample, '
  if prefix not in line:continue
  row={'gateway':c['Name']}
  for pair in line.split(prefix,1)[1].strip().split(', '):
   key,sep,value=pair.partition('=')
   if sep and key=='path' and value in ['chat','peer']:row[key]=value
   elif sep and key in numeric and re.fullmatch('-?[0-9]+',value):row[key]=int(value)
  mid=row.get('message_id',0)
  if mid not in lookup:unmatched+=1;continue
  identity=lookup[mid];assert row.get('path') in ['chat','peer']
  assert row['user_id']==identity['sender' if row['path']=='chat' else 'recipient']
  row['run']=identity['run'];rows.append(row)
 del data
configs={}
for name in ['gateway-a.json','gateway-b.json','message.json','social.json','user.json']:
 path=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')/name
 if not path.exists():configs[name]={'status':'CONFIG_NOT_AT_EXPECTED_NAME'};continue
 data=json.loads(path.read_text());allowed={}
 for key in ['logging','logger','log']:
  value=data.get(key)
  if isinstance(value,dict):allowed[key]={field:value[field] for field in ['level','console','async','flush_each_log','max_file_size_mb','max_backup_files'] if field in value};allowed[key]['file_sink_configured']=bool(value.get('file'))
 configs[name]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'logging_only':allowed,'top_level_key_names_if_logging_missing':list(data.keys()) if not allowed else []}
(d/'gateway-phase-numeric.json').write_text(json.dumps(rows,indent=2)+'\n')
reports=[]
for name in ['guard150A1','guard150B1']:
 for kind in ['chat','peer']:
  selected=[row for row in rows if row['run']==name and row['path']==kind];metrics={}
  for key in ['dispatch_age_us','work_us','permission_us','route_us','persist_us','unread_us','peer_validate_us']:
   values=[row[key]/1000 for row in selected if row.get(key,-1)>=0]
   if values:metrics[key]={'samples':len(values),'mean_ms':statistics.mean(values),'max_ms':max(values)}
  reports.append({'run':name,'path':kind,'samples':len(selected),'unique_mids':len({row['message_id'] for row in selected}),'failed_samples':sum(bool(row.get('failed')) for row in selected),'metrics':metrics})
x={'status':'GATEWAY_CONTROLLED_PHASE_LOG_REVIEW_COMPLETE','reports':reports,'unmatched_or_rotated_context_lines':unmatched,'logging_config_allowlist_only':configs,'full_feature_acceptance':False,'limits':'Threshold/rate-limited samples, no population percentiles, no CPU attribution. Some log history can rotate; do not infer zero latency or zero work from zero samples. Readonly current logging configuration not a new performance experiment.','runtime_preserved':True};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
