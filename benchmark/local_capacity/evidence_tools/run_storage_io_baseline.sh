#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,time,datetime,hashlib,threading,re,sys
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'storage-io-baseline-control-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert 'SetSyncServerOption(grpc::ServerBuilder::MAX_POLLERS, 16)' not in (r/'services/message/server/MessageServiceServer.cpp').read_text(),'Rejected candidate must be restored in source first'
rollback=json.loads((b/'message-poller-rejected-rollback-20261005/summary.json').read_text());assert rollback['status']=='RESTORED_PREVIOUS_B24_POOL16'
gateway=json.loads((b/'stripe-fair-handoff-rejected-rollback-20261005/summary.json').read_text());sys.path.insert(0,str(r/'benchmark/local_capacity'))
from apply_pending_recipient_index import definitions,EXPECTED,NAME
indexes={**EXPECTED,NAME:[('delivery_status','1','YES'),('to_user_id','1','YES')]};config=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json')
def sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-storage-window-counters',q],text=True,timeout=20)
def settings():return list(map(int,sql('SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count').strip().split()))
def identity():
 c=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-message-service-1'],text=True))[0];assert c['Id']==rollback['message_service_id'] and c['Image']==rollback['image'] and c['State']['Running']
 env=dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x);assert 'TINYIMX_STORAGE_WAIT_TRACE_ENABLE' not in env and 'TINYIMX_MYSQL_POOL_TRACE' not in env
 for name,cid in gateway['gateway_ids'].items():
  g=json.loads(subprocess.check_output(['docker','inspect',cid],text=True))[0];assert g['Name']==name and g['Image']==gateway['image'] and g['State']['Running']
 assert hashlib.sha256(config.read_bytes()).hexdigest()==rollback['private_config_sha256']['message.json'] and definitions()==indexes and settings()==[1,1,1,0,0]
 return {'message_id':c['Id'],'message_image':c['Image'],'gateway_ids':gateway['gateway_ids'],'gateway_image':gateway['image'],'pool_size':16,'config_sha256':rollback['private_config_sha256']['message.json'],'durability_and_group_commit':settings(),'indexes':indexes}
before=identity();assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
d.mkdir();run='io150base';raw=b/('capacity-'+run);assert not raw.exists()
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'One baseline10k150/s60s active-window storage file/fsync/statement counter deltas and aggregate guestCPU context-switch/fork/pressure reads','head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'runtime_before':before,'writes':'Normal owned messages/ACK and own numeric evidence only','observer':'Two readonlySQL counter snapshots near activewindow start/end; no resets/instrument enabling/global settings changes. FileMISC is not purefsync; redoFsync separate exactstatus. CPU/runqueue distinguish only where observed','strict_gates':'Allauth/9000plan/no skip/negative/late/disconnect/fullSQL/wire/HB/P99<=100 retained','other_apps':'Preserved','rollback':'Clientsdrain; preserve all rows/results, no deletion/configuration/image changes'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
query_file="SELECT EVENT_NAME,COUNT_READ,COUNT_WRITE,COUNT_MISC,SUM_TIMER_READ,SUM_TIMER_WRITE,SUM_TIMER_MISC,MAX_TIMER_MISC FROM performance_schema.file_summary_by_event_name WHERE EVENT_NAME IN ('wait/io/file/innodb/innodb_log_file','wait/io/file/sql/binlog','wait/io/file/innodb/innodb_data_file','wait/io/file/innodb/innodb_dblwr_file')"
query_status="SHOW GLOBAL STATUS WHERE Variable_name IN ('Innodb_os_log_fsyncs','Innodb_log_waits','Innodb_log_writes','Innodb_data_fsyncs','Innodb_data_reads','Innodb_data_writes','Innodb_data_written','Com_commit','Com_insert','Com_update','Com_select','Threads_running','Threads_created')"
query_digest='SELECT DIGEST,DIGEST_TEXT,COUNT_STAR,SUM_TIMER_WAIT,SUM_LOCK_TIME,SUM_ROWS_EXAMINED FROM performance_schema.events_statements_summary_by_digest WHERE SCHEMA_NAME=DATABASE()'
def snapshot(label):
 t=time.monotonic_ns();out={'label':label,'started_monotonic_ns':t,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
 for key,q in [('file',query_file),('status',query_status),('digest',query_digest)]:(d/(label+'-'+key+'.tsv')).write_text(sql(q))
 for key,p in [('stat','/proc/stat'),('diskstats','/proc/diskstats'),('cpu-pressure','/proc/pressure/cpu'),('io-pressure','/proc/pressure/io'),('memory-pressure','/proc/pressure/memory')]: (d/(label+'-'+key+'.txt')).write_text(pathlib.Path(p).read_text())
 out['ended_monotonic_ns']=time.monotonic_ns();out['capture_elapsed_ms']=(out['ended_monotonic_ns']-t)/1e6;(d/(label+'-snapshot.json')).write_text(json.dumps(out,indent=2)+'\n')
def observe(p):
 try:
  deadline=time.monotonic()+350
  while not (raw/'control/start_ns').exists() and p.poll() is None and time.monotonic()<deadline:time.sleep(.5)
  if not (raw/'control/start_ns').exists():raise RuntimeError('No activewindow')
  start=int((raw/'control/start_ns').read_text())
  for label,offset in [('before',100_000_000),('after',58_500_000_000)]:
   while time.monotonic_ns()<start+offset and p.poll() is None:time.sleep(.05)
   if p.poll() is not None:raise RuntimeError('Control exited before complete counter window')
   snapshot(label)
 except BaseException as e:(d/'observer-error.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e)})+'\n')
args=['python3','benchmark/local_capacity/capacity_run.py','--user-id-base','700000','--username-prefix','codex50k_20261004_','--gateway-image',gateway['image'],'--message-image',rollback['image'],'--host','192.168.220.128','--source-ips','192.168.220.128,192.168.220.129,192.168.58.129,172.18.0.1,172.17.0.1','--run',run,'--users','10000','--rate','150','--duration','60']
with (d/'capacity.log').open('w') as f:
 p=subprocess.Popen(args,stdout=f,stderr=subprocess.STDOUT);o=threading.Thread(target=observe,args=(p,));o.start();code=p.wait();o.join()
after=identity();assert before==after;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
x={'status':'STORAGE_IO_BASELINE_COMPLETED','run':run,'private_exit':code,'private':json.loads((raw/'summary.json').read_text()),'observer_error':(d/'observer-error.json').exists(),'new_settings_applied':False,'performance_acceptance':False}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
