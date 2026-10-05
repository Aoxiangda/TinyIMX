#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,os,signal,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-maintenance-gateway-drain-rollback-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
deployment=json.loads((b/'online-maintenance-gateway-deployment-20261005/summary.json').read_text());cs=json.loads(subprocess.check_output(['docker','inspect',*deployment['gateway_ids'].keys()],text=True))
for c in cs:assert c['Id']==deployment['gateway_ids'][c['Name']] and c['Image']==deployment['image']
script=r/'benchmark/local_capacity/evidence_tools/rollback_online_maintenance_gateways.sh';assert script.exists();d.mkdir();private=d/'runtime-private';private.mkdir();since=datetime.datetime.now(datetime.timezone.utc).isoformat()
(d/'audit-before.json').write_text(json.dumps({'utc':since,'operation':'Keep ownread-onlyDockerlogstreams through authorized2GWrollback to preserve shutdownmaintenance accounting','candidate_containers':deployment['gateway_ids'],'image':deployment['image'],'delegated_exact_rollback_source_sha256':hashlib.sha256(script.read_bytes()).hexdigest(),'scope':'Existingrollback verifies noactiveclients, only2GWoriginal1d8/envflagremoval andother17/configpreserved. Start2ownreadonlylogfollowers inprivate beforecompose; captureerrors doNOTblock rollback','cleanup':'Onlyownfollower PID/starttime/argv/PGID verified termination ifnotEOF afterrestore. Do not stop unrelatedprocesses or delete containers/images/files','limits':'Drainlines actualaccepted/terminal/pending counters; missingline isunconfirmedproof, neverinvented','rollback':'Delegatedoriginal restore command alreadyaudited/saved; allraw/private/failures retained'},indent=2)+'\n')
followers=[];capture_errors=[];restore_error=None
try:
 for c in cs:
  args=['docker','logs','--follow','--since',since,c['Id']];path=private/(c['Name'].split('/')[-1]+'.shutdown.log');output=path.open('w')
  try:
   child=subprocess.Popen(args,stdout=output,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path(f'/proc/{child.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19];followers.append((child,proc,identity,args,output,path,c['Name']));(private/(c['Name'].split('/')[-1]+'.follower.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n')
  except BaseException as error:capture_errors.append({'container':c['Name'],'type':type(error).__name__});output.close()
 try:
  with (d/'restore.log').open('w') as output:subprocess.run(['bash',str(script)],stdout=output,stderr=subprocess.STDOUT,check=True)
 except BaseException as error:restore_error=error
finally:
 for child,proc,identity,args,output,path,name in followers:
  try:child.wait(timeout=10)
  except subprocess.TimeoutExpired:
   assert proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid and proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[a.encode() for a in args]
   (private/(name.split('/')[-1]+'.stop-before.json')).write_text(json.dumps({'operation':'Stoponlyownread-onlyDockerlogfollower','pid':child.pid,'identity_verified':True})+'\n');os.killpg(child.pid,signal.SIGTERM)
   try:child.wait(timeout=3)
   except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
  finally:output.close()
if restore_error is not None:(d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(restore_error).__name__,'message':str(restore_error),'capture_errors':capture_errors})+'\n');raise restore_error
rollback=json.loads((b/'online-maintenance-gateway-rejected-rollback-20261005/summary.json').read_text());assert rollback['status']=='RESTORED_PREVIOUS_1D8_MESSAGE_WORKERS16';reports=[]
for child,proc,identity,args,output,path,name in followers:
 rows=[]
 for line in path.read_text().splitlines():
  if 'gateway online maintenance batching drained' in line:rows.append({key:int(value) for key,value in re.findall(r'([a-z_]+)=(-?\d+)',line)})
 valid=len(rows)==1 and rows[0].get('accepted')==rows[0].get('terminals') and rows[0].get('pending')==0 and rows[0].get('max_batch',17)<=16
 reports.append({'gateway':name,'lines':rows,'accounting_verified':valid,'private_log_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'missing_log_is_unconfirmed':not rows})
x={'status':'ONLINE_MAINTENANCE_DRAIN_ROLLBACK_COMPLETE','rollback':rollback,'drain_reports':reports,'all_two_drains_verified':len(reports)==2 and all(row['accounting_verified'] for row in reports),'capture_errors':capture_errors,'performance_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
