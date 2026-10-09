#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,threading,re,sys,signal,os,ast
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'groupcommit-sync-delay-diagnostic-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
baseline=json.loads((b/'storage-io-baseline-after-auth-analysis-20261005/summary.json').read_text());assert baseline['status']=='STORAGE_IO_BASELINE_ANALYSIS_COMPLETED' and baseline['control_run']=='io150base2'
assert baseline['private']['metrics']['chat_ack_ok']==9000 and baseline['private']['positive_ack_p99_ms_upper_bin']==188.0
oldcap=json.loads((b/'capacity-io150base2/audit-before.json').read_text())['runtime'];oldidentity={c['name']:{'id':c['id'],'image':c['image'],'started':c['started']} for c in oldcap}
restore=json.loads((b/'auth-phase-user-restoration-review-20261005/summary.json').read_text());assert restore['status']=='ORIGINAL_USER_38DCA_RESTORATION_VERIFIED'
sys.path.insert(0,str(r/'benchmark/local_capacity'))
from apply_pending_recipient_index import definitions,EXPECTED,NAME
indexes={**EXPECTED,NAME:[('delivery_status','1','YES'),('to_user_id','1','YES')]}
def sql(q,timeout=15):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','owned-groupcommit-diagnostic',q],text=True,timeout=timeout)
def settings():return list(map(int,sql('SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count').strip().split()))
def identity():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
 out={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs};assert out==oldidentity
 for c in cs:
  if c['Name'] not in ['/tinyimx-m21-user-service-1','/tinyimx-m21-message-service-1']:continue
  env=dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x);assert all(k not in env for k in ['TINYIMX_AUTH_PHASE_TRACE_ENABLE','TINYIMX_STORAGE_WAIT_TRACE_ENABLE','TINYIMX_MYSQL_POOL_TRACE'])
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')};assert hashes==json.loads((b/'auth-phase-diagnostics-user-deployment-20261005/summary.json').read_text())['private_config_sha256']
 assert definitions()==indexes;return out,hashes
before,hashes=identity();original=settings();assert original==[1,1,1,0,0]
assert sql('SELECT VERSION()').strip()=='8.0.40'
persisted=sql("SELECT VARIABLE_NAME,VARIABLE_VALUE FROM performance_schema.persisted_variables WHERE VARIABLE_NAME IN ('binlog_group_commit_sync_delay','binlog_group_commit_sync_no_delay_count','innodb_flush_log_at_trx_commit','sync_binlog') ORDER BY VARIABLE_NAME")
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
d.mkdir();run_id='gc150u1000';raw=b/('capacity-'+run_id);assert not raw.exists();mysql_id=before['/tinyimx-m21-mysql-1']['id']
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'One bounded MySQLbinloggroupcommit1000us diagnostic againstoriginal10k150/s60s baseline','head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'runtime_before':before,'private_config_sha256':hashes,'settings_before':original,'candidate_settings':[1,1,1,1000,0],'parameter':'Only SET GLOBAL binlog_group_commit_sync_delay=1000; no SET PERSIST orconfigfile','evidence':'Actual58.4s baselineconfirmUPDATE9.34ms/COMMIT7.54ms, redo222.9sync/s andbinlogmisc1.875ms. CPUbusy85.3%/PSI63.3%, doesnotprovefsyncsolecause','strict_gates':'Originalramp100users/s/deadline3s/9000plan/zeroerror/SQL/wire/HB/P99<=100, no gateorbusinesschange','impact':'Alltransactions in exactownedMySQLcontainer maywait additional1ms groupdelay, potentiallymorecontention; no persistent orhostconfigchange','writes':'Normalownedmessages/metadata andfresh boundedcounters/evidence; allrows retained','rollback':'Finally restoreexactdelay0, retain double1/binlogON/no_delay_count0; independentown450s watchdog restoresonlyexpected1000 onexactsameMySQLID','coordinator_limit_s':360,'other_apps':'Preserved','performance_acceptance':False},indent=2)+'\n')
(d/'persisted-before.tsv').write_text(persisted)
lease={'mysql_id':mysql_id,'original':original,'candidate':[1,1,1,1000,0],'deadline_epoch':time.time()+450};(d/'watchdog-lease.json').write_text(json.dumps(lease)+'\n')
watchcode=r'''import pathlib,json,subprocess,time,sys
d=pathlib.Path(sys.argv[1]);lease=json.loads((d/'watchdog-lease.json').read_text())
def sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-groupcommit-rollback-watchdog',q],text=True,timeout=15)
while time.time()<lease['deadline_epoch']:
 if (d/'watchdog-stop.request').exists():
  (d/'watchdog-summary.json').write_text(json.dumps({'status':'CANCELLED_AFTER_MAIN_VERIFIED_RESTORE','runtime_write':False})+'\n');raise SystemExit(0)
 time.sleep(1)
for attempt in range(3):
 try:
  cid=subprocess.check_output(['docker','inspect','tinyimx-m21-mysql-1','--format','{{.Id}}'],text=True,timeout=10).strip();assert cid==lease['mysql_id']
  actual=list(map(int,sql('SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count').strip().split()))
  if actual==lease['original']:status='ALREADY_ORIGINAL_NO_WRITE'
  else:
   assert actual==lease['candidate'],'Refuse overwrite unexpectedsettings'
   sql('SET GLOBAL binlog_group_commit_sync_delay=0');assert sql('SELECT @@binlog_group_commit_sync_delay').strip()=='0';status='WATCHDOG_RESTORED_EXACT_DELAY0'
  (d/'watchdog-summary.json').write_text(json.dumps({'status':status,'attempt':attempt+1})+'\n');raise SystemExit(0)
 except Exception as e:
  (d/'watchdog-error.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'attempt':attempt+1})+'\n');time.sleep(2)
raise SystemExit(2)
'''
ast.parse(watchcode)
(d/'rollback-watchdog.py').write_text(watchcode)
watchlog=(d/'watchdog.log').open('w');watchdog=subprocess.Popen(['python3',str(d/'rollback-watchdog.py'),str(d)],stdout=watchlog,stderr=subprocess.STDOUT,start_new_session=True);(d/'watchdog-pid.json').write_text(json.dumps({'pid':watchdog.pid,'own_exact_script':str(d/'rollback-watchdog.py'),'deadline_epoch':lease['deadline_epoch']})+'\n')
def interrupted(signum,frame):raise KeyboardInterrupt('Owned diagnostic interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
query_file="SELECT EVENT_NAME,COUNT_READ,COUNT_WRITE,COUNT_MISC,SUM_TIMER_READ,SUM_TIMER_WRITE,SUM_TIMER_MISC,MAX_TIMER_MISC FROM performance_schema.file_summary_by_event_name WHERE EVENT_NAME IN ('wait/io/file/innodb/innodb_log_file','wait/io/file/sql/binlog','wait/io/file/innodb/innodb_data_file','wait/io/file/innodb/innodb_dblwr_file')"
query_status="SHOW GLOBAL STATUS WHERE Variable_name IN ('Innodb_os_log_fsyncs','Innodb_log_waits','Innodb_log_writes','Innodb_data_fsyncs','Innodb_data_reads','Innodb_data_writes','Innodb_data_written','Com_commit','Com_insert','Com_update','Com_select','Threads_running','Threads_created')"
query_digest='SELECT DIGEST,DIGEST_TEXT,COUNT_STAR,SUM_TIMER_WAIT,SUM_LOCK_TIME,SUM_ROWS_EXAMINED FROM performance_schema.events_statements_summary_by_digest WHERE SCHEMA_NAME=DATABASE()'
def snapshot(label):
 t=time.monotonic_ns();out={'label':label,'started_monotonic_ns':t,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 for key,q in [('file',query_file),('status',query_status),('digest',query_digest)]:(d/(label+'-'+key+'.tsv')).write_text(sql(q))
 for key,path in [('stat','/proc/stat'),('diskstats','/proc/diskstats'),('cpu-pressure','/proc/pressure/cpu'),('io-pressure','/proc/pressure/io'),('memory-pressure','/proc/pressure/memory')]: (d/(label+'-'+key+'.txt')).write_text(pathlib.Path(path).read_text())
 out['ended_monotonic_ns']=time.monotonic_ns();out['capture_elapsed_ms']=(out['ended_monotonic_ns']-t)/1e6;(d/(label+'-snapshot.json')).write_text(json.dumps(out,indent=2)+'\n')
def observe(p):
 try:
  end=time.monotonic()+350
  while not (raw/'control/start_ns').exists() and p.poll() is None and time.monotonic()<end:time.sleep(.5)
  if not (raw/'control/start_ns').exists():raise RuntimeError('No activewindow')
  start=int((raw/'control/start_ns').read_text())
  for label,offset in [('before',100_000_000),('after',58_500_000_000)]:
   while time.monotonic_ns()<start+offset and p.poll() is None:time.sleep(.05)
   if p.poll() is not None:raise RuntimeError('Control exited before counterscomplete')
   snapshot(label)
 except BaseException as e:(d/'observer-error.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e)})+'\n')
p=None;o=None;restored=False;code=None
try:
 sql('SET GLOBAL binlog_group_commit_sync_delay=1000');assert settings()==[1,1,1,1000,0];(d/'settings-applied.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'settings':settings()})+'\n')
 args=['python3','benchmark/local_capacity/capacity_run.py','--user-id-base','700000','--username-prefix','codex50k_20261004_','--gateway-image',before['/tinyimx-m21-gateway-a-1']['image'],'--message-image',before['/tinyimx-m21-message-service-1']['image'],'--host','192.168.220.128','--source-ips','192.168.220.128,192.168.220.129,192.168.58.129,172.18.0.1,172.17.0.1','--run',run_id,'--users','10000','--rate','150','--duration','60']
 with (d/'capacity.log').open('w') as f:
  p=subprocess.Popen(args,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);o=threading.Thread(target=observe,args=(p,));o.start();code=p.wait(timeout=360);o.join(timeout=30)
 assert not o.is_alive();assert settings()==[1,1,1,1000,0];identity()
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'coordinator_exit':p.poll() if p else None})+'\n');raise
finally:
 try:
  if p is not None and p.poll() is None:
   assert os.getpgid(p.pid)==p.pid
   os.kill(p.pid,signal.SIGINT)
   try:p.wait(timeout=15)
   except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGTERM);p.wait(timeout=10)
  if o is not None:o.join(timeout=5)
 finally:
  # Cleanup errors must not prevent parameter restoration. The independent
  # watchdog remains live unless this exact restoration has been verified.
  cid=subprocess.check_output(['docker','inspect','tinyimx-m21-mysql-1','--format','{{.Id}}'],text=True).strip();assert cid==mysql_id
  actual=settings();assert actual in [original,[1,1,1,1000,0]],'Refuse overwriteunexpectedsettings'
  if actual!=original:sql('SET GLOBAL binlog_group_commit_sync_delay=0')
  restored=settings()==original;assert restored
  (d/'settings-restored.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'settings':settings(),'exact_original_restored':True})+'\n');(d/'watchdog-stop.request').write_text('Main verified exactrestoration\n');watchdog.wait(timeout=5);watchlog.close()
after,afterhash=identity();assert after==before and afterhash==hashes
assert sql("SELECT VARIABLE_NAME,VARIABLE_VALUE FROM performance_schema.persisted_variables WHERE VARIABLE_NAME IN ('binlog_group_commit_sync_delay','binlog_group_commit_sync_no_delay_count','innodb_flush_log_at_trx_commit','sync_binlog') ORDER BY VARIABLE_NAME")==persisted
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
x={'status':'GROUPCOMMIT_DIAGNOSTIC_COMPLETED_AND_RESTORED','run':run_id,'private_exit':code,'private':json.loads((raw/'summary.json').read_text()),'observer_error':(d/'observer-error.json').exists(),'settings_during':[1,1,1,1000,0],'settings_after':settings(),'exact_settings_restored':restored,'all19_ids_images_started_preserved':True,'all_private_configs_preserved':True,'persisted_variables_unchanged':True,'performance_acceptance':False,'ended_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
