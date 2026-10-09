#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time,hashlib,threading,sys
r=pathlib.Path.cwd();root=r/'.local/codex/restored-baseline-finer-rate-curve-20261005';assert not root.exists();root.mkdir()
assert json.loads((r/'.local/codex/receiver-ack-live-boundaries-20261004-attempt2/summary.json').read_text())['status']=='PASS'
assert json.loads((r/'.local/codex/offered-count-boundary-build-20261004/summary.json').read_text())['status']=='PASS'
(root/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Restored1d8/16 lower-online300 and finer10k150/200/250 offered-rate diagnostic, unchanged b24/pool16/statusindex','source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'images':{'gateway':'1d8d71','message':'b24e7b'},'compiled_revisions':{'gateway':'33fc9bb','message':'ddc7e8e','worker':'2bb6b32'},'offering':'1k300 then10k150/200/250messages/s60s; exact18000/9000/12000/15000 plan, no deadline/P99 relaxation','writes':'Normal owned private messages/ACK only; previous49operation chain is separate evidence; missing mutual fixture edges explicitly audited','reads':'Runtime identities and configSHA/pool16 before/after, PSI/resources, normalized SQL digest counters and20 live lock snapshots; no resets','other_apps':'Preserved','validation':'Allauth, positiveM/C/recipientSQL+wire, nonzeroHB, bothP99<=100ms; chain alloperationsinsidewindow','limits':'Private-only increasing business activity; no all-feature capacity claim; every failure kept','rollback':'Ownclientsdrain/abort; keepallrows/evidence, no servicerecreation/cacheflush/deletion'},indent=2)+'\n')
base=['python3','benchmark/local_capacity/capacity_run.py','--user-id-base','700000','--username-prefix','codex50k_20261004_','--gateway-image','sha256:1d8d71faa91a5a1e0a261c2a89efa1027a93515ae2b94b2c2d06f48010a7486e','--message-image','sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898','--host','192.168.220.128','--source-ips','192.168.220.128,192.168.220.129,192.168.58.129,172.18.0.1,172.17.0.1']
def sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw "$MYSQL_DATABASE" -e "$1"','readonly-ack-control-digest',q],text=True)
digest='SELECT DIGEST,LEFT(DIGEST_TEXT,180),COUNT_STAR,SUM_TIMER_WAIT,SUM_LOCK_TIME,SUM_ROWS_EXAMINED FROM performance_schema.events_statements_summary_by_digest WHERE SCHEMA_NAME=DATABASE()'
results=[]
pool_path=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json')
deployment=json.loads((r/'.local/codex/private-single-lease-message-deployment-20261004/summary.json').read_text()); assert deployment['status']=='SINGLE_LEASE_MESSAGE_READY'
sys.path.insert(0,str(r/'benchmark/local_capacity'))
from apply_pending_recipient_index import definitions,EXPECTED,NAME
expected_indexes={**EXPECTED,NAME:[('delivery_status','1','YES'),('to_user_id','1','YES')]}
gateway_deployment=json.loads((r/'.local/codex/stripe-fair-handoff-rejected-rollback-20261005/summary.json').read_text());assert gateway_deployment['status']=='RESTORED_PREVIOUS_1D8_MESSAGE_WORKERS16'
def pool_identity():
 for name,cid in gateway_deployment['gateway_ids'].items():
  c=json.loads(subprocess.check_output(['docker','inspect',cid],text=True))[0]
  assert c['Name']==name and c['Id']==cid and c['Image']==gateway_deployment['image']
 assert definitions()==expected_indexes
 assert hashlib.sha256(pool_path.read_bytes()).hexdigest()==deployment['config_sha256'] and json.loads(pool_path.read_text())['mysql']['pool_size']==16
 c=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-message-service-1'],text=True))[0]
 assert c['Id']==deployment['message_service_id'] and c['Image']==deployment['image']
 return {'gateway_image':gateway_deployment['image'],'gateway_ids':gateway_deployment['gateway_ids'],'index_sha256':hashlib.sha256(json.dumps(expected_indexes,sort_keys=True).encode()).hexdigest(),'indexes':expected_indexes,'config_sha256':deployment['config_sha256'],'pool_size':16,'container_id':c['Id'],'image':c['Image']}
lock_query="SELECT COUNT(*) AS current_waits FROM performance_schema.data_lock_waits; SELECT r.OBJECT_NAME,r.INDEX_NAME,r.LOCK_TYPE,r.LOCK_MODE AS requested_mode,b.LOCK_MODE AS blocking_mode,LEFT(rs.DIGEST_TEXT,160) AS requester_digest,LEFT(bs.DIGEST_TEXT,160) AS blocker_digest FROM performance_schema.data_lock_waits w JOIN performance_schema.data_locks r ON r.ENGINE=w.ENGINE AND r.ENGINE_LOCK_ID=w.REQUESTING_ENGINE_LOCK_ID JOIN performance_schema.data_locks b ON b.ENGINE=w.ENGINE AND b.ENGINE_LOCK_ID=w.BLOCKING_ENGINE_LOCK_ID LEFT JOIN performance_schema.events_statements_current rs ON rs.THREAD_ID=w.REQUESTING_THREAD_ID LEFT JOIN performance_schema.events_statements_current bs ON bs.THREAD_ID=w.BLOCKING_THREAD_ID LIMIT 30; SHOW GLOBAL STATUS WHERE Variable_name IN ('Threads_connected','Threads_running','Innodb_row_lock_current_waits')"
def observe(d,p,bg):
 try:
  deadline=time.monotonic()+350
  while not (bg/'all-online.json').exists() and p.poll() is None and time.monotonic()<deadline:time.sleep(.5)
  if not (bg/'all-online.json').exists():return
  time.sleep(6)
  with (d/'live-lock-samples.jsonl').open('w') as lf:
   for i in range(20):
    if p.poll() is not None:break
    started=time.monotonic(); sample={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'sample':i,'state':sql(lock_query),'query_elapsed_ms':(time.monotonic()-started)*1000};lf.write(json.dumps(sample)+'\n');lf.flush();time.sleep(2)
 except BaseException as e:(d/'observer-error.json').write_text(json.dumps({'type':type(e).__name__,'status':'FAIL'})+'\n')
for n,run,rate in [(1000,'base1k300a',300),(10000,'base10k150a',150),(10000,'base10k200a',200),(10000,'base10k250a',250)]:
 available=int(__import__('re').search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))
 assert available>=2*1024*1024,'Insufficient guest available memory; preserve apps, do not clean globally'
 pool_identity()
 d=root/run;d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Normal auditedowned capacity control','users':n,'rate':rate,'duration':60,'actor_uids':[],'changes':'No configuration orotherapp changes within control; strict new single-lease image pin'},indent=2)+'\n');(d/'digest-before.tsv').write_text(sql(digest));actor_code=None
 (d/'runtime-before.json').write_text(json.dumps(pool_identity(),indent=2)+'\n')
 with (d/'capacity.log').open('w') as f:
  p=subprocess.Popen(base+['--run',run,'--users',str(n),'--rate',str(rate),'--duration','60'],stdout=f,stderr=subprocess.STDOUT)
  observer=threading.Thread(target=observe,args=(d,p,r/'.local/codex'/('capacity-'+run)));observer.start()
  if False:
   bg=r/'.local/codex'/('capacity-'+run);deadline=time.monotonic()+350
   while not (bg/'all-online.json').exists() and p.poll() is None and time.monotonic()<deadline:time.sleep(.5)
   if (bg/'all-online.json').exists() and p.poll() is None:
    time.sleep(10)
    with (d/'actor.log').open('w') as af:actor_code=subprocess.run(['python3','benchmark/local_capacity/cross_feature_actor.py','--run',run,'--background-run',run,'--users','519831','519833','519835','519837'],stdout=af,stderr=subprocess.STDOUT).returncode
   else:(d/'actor-not-run.json').write_text(json.dumps({'status':'NOT_RUN','reason':'No all-online window'})+'\n')
  code=p.wait()
  observer.join()
 (d/'runtime-after.json').write_text(json.dumps(pool_identity(),indent=2)+'\n')
 (d/'digest-after.tsv').write_text(sql(digest));x={'run':run,'users':n,'private_exit':code,'actor_exit':actor_code,'private':json.loads((r/'.local/codex'/('capacity-'+run)/'summary.json').read_text())}
 c=r/'.local/codex'/('cross-feature-'+run)/'summary.json'
 if c.exists():x['chain']=json.loads(c.read_text())
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');results.append(x);(root/'progress.json').write_text(json.dumps(results,indent=2)+'\n');print('CONTROL_RESULT='+json.dumps({k:v for k,v in x.items() if k!='chain'}),flush=True)
(root/'summary.json').write_text(json.dumps({'status':'PASS' if all(x['private_exit']==0 for x in results) else 'FAIL','results':results,'full_feature_acceptance':False},indent=2)+'\n')
PY
