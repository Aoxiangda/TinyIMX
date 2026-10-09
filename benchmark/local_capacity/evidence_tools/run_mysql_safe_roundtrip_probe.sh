#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,subprocess,hashlib,shlex,re,time,os,signal,shutil
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'mysql-safe-roundtrip-probe-20261005';assert not d.exists()
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();source=json.loads((b/'mysql-safe-roundtrip-probe-source-20261005/summary.json').read_text());assert source['head']==head
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'mysql-safe-roundtrip-probe-source-20261005/runtime-after.json').read_text())
assert before['containers']['/tinyimx-m21-mcp-server-1']['image']=='sha256:bc85c186271873610ff759c008d5f36d49d1a9c331e064eb7fa1121d822485a3'
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
assert shutil.disk_usage(r).free>1024**3
disk=subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','df','-Pk','/var/lib/mysql'],text=True,timeout=10);assert int(disk.splitlines()[-1].split()[3])*1024>1024**3
assert json.loads((b/'mysql-roundtrip-readonly-preflight-20261005/summary.json').read_text())['status']=='MYSQL_ROUNDTRIP_READONLY_PREFLIGHT_COMPLETE'
cpp=r/'benchmark/local_capacity/mysql_safe_roundtrip_probe.cpp';build=r/'build/linux-release';d.mkdir();binary=d/'mysql_safe_roundtrip_probe';p=None
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Own durablecopied table transaction same6SQL safe BEGINinsertread1vs3 networkcalls ABBA, no production changes','head':head,'source_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest(),'SQL':'Onlytwofixed ownedcopied tables, exact24k syntheticmessages+24k ownoutbox rows, realCOMMIT/healthyPING; production tables/config/durability unchanged; NoDDL; exact existing own schema/FKs/counts pinned before reuse','connections':'16fresh exclusive nativeworker connections; native-to-Docker differs from production route','cases':['sqlsafeA1','sqlsafeB1','sqlsafeB2','sqlsafeA2'],'compiler':'Existing mysql_pool_demo flags/link/cache, no SDK install or product build','runtime_before':before,'runtime_changes':False,'cleanup':'Own180s compiler/40s cases newprocessgroups with cmdline/starttime/PGID guard only','acceptance':False,'limits':'Normal own6SQL transaction model with validation before outbox COMMIT, notproductionEventCodec/failure/uncertaincommit/idempotentraces/tampering or10k50kcapacity proof. Same native-to-Docker route percontrol differsproduction; MySQL cgroup includesbackground/setup/audit, probeCPU callerloop only'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');(d/'guest-memory-before.txt').write_text(pathlib.Path('/proc/meminfo').read_text())
flags_text=(build/'CMakeFiles/mysql_pool_demo.dir/flags.make').read_text();flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in flags_text.splitlines() if x.startswith(key+' =')).split('=',1)[1])
(d/'cached-flags.make').write_text(flags_text);link=shlex.split((build/'CMakeFiles/mysql_pool_demo.dir/link.txt').read_text());old='CMakeFiles/mysql_pool_demo.dir/examples/mysql_pool_demo.cpp.o';assert link.count(old)==1
link[link.index(old)]=str(d/'probe.o');link[link.index('-o')+1]=str(binary)
excluded=[x for x in link if x.startswith('libtinyimx_')];link=[x for x in link if x not in excluded];assert not any(x.startswith('libtinyimx_') for x in link);(d/'excluded-product-archives.json').write_text(json.dumps(excluded,indent=2)+'\n')
(d/'linked-project-libraries.json').write_text(json.dumps([x for x in link if x.startswith('libtinyimx_')],indent=2)+'\n')
def interrupted(signum,frame):raise KeyboardInterrupt('Own component diagnostic interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def compile_call(args,label):
 (d/(label+'-command.json')).write_text(json.dumps(args,indent=2)+'\n')
 child=None;error=None;code=None;proc=None;identity=None
 with (d/(label+'.log')).open('w') as f:
  try:
   child=subprocess.Popen(args,cwd=build,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path(f'/proc/{child.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   (d/(label+'-own-process.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n');code=child.wait(timeout=180)
  except BaseException as exc:error=exc
  finally:
   if child is not None and child.poll() is None:
    assert identity is not None and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid
    assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
    (d/(label+'-stop-audit.json')).write_text(json.dumps({'operation':'Stoponlyownverifiedcompiler/linkerprocessgroup','pid':child.pid,'cmdline_starttime_pgid_verified':True})+'\n');os.killpg(child.pid,signal.SIGTERM)
    try:child.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
 if error is not None or code!=0:
  (d/'failed.json').write_text(json.dumps({'status':'FAIL','phase':label,'exit_code':code,'type':type(error).__name__ if error else 'CompilerExit','message':str(error) if error else label+' nonzero exit'})+'\n');after=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
  if error is not None:raise error
  raise RuntimeError(label+' failed, all logs preserved')
compile_call(['/usr/bin/c++',*flags,'-c',str(cpp),'-o',str(d/'probe.o')],'compile');compile_call(link,'link')
(d/'probe-binary-sha256.txt').write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'\n')
mysql=json.loads(subprocess.check_output(['docker','inspect','tinyimx-m21-mysql-1'],text=True))[0]
ips={x['IPAddress'] for x in mysql['NetworkSettings']['Networks'].values() if x.get('IPAddress')};assert len(ips)==1;ip=next(iter(ips))
config=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json')
fields=json.loads(config.read_text())['mysql'];assert set(['host','port','database','user','password']).issubset(fields);assert fields['pool_size']==16;del fields
cg=next(line.split(':',2)[2] for line in pathlib.Path('/proc/'+str(mysql['State']['Pid'])+'/cgroup').read_text().splitlines() if line.startswith('0::'))
cpu=pathlib.Path('/sys/fs/cgroup')/cg.lstrip('/')/'cpu.stat';assert cpu.is_file()
def snapshot():
 started=time.monotonic_ns();values={k:int(v) for k,v in (line.split() for line in cpu.read_text().splitlines())};ended=time.monotonic_ns()
 return {'start_monotonic_ns':started,'end_monotonic_ns':ended,'cpu_stat':values}
def own_sql(q):return subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-sql-roundtrip-guard',q],text=True,timeout=20)
durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'
objects=['codex_sql_rtt_messages_20261005','codex_sql_rtt_outbox_20261005']
assert own_sql(durability).strip()=='1\t1\t1\t0\t0'
assert json.loads((b/'mysql-transaction-roundtrip-probe-20261005/summary.json').read_text())['status']=='MYSQL_TRANSACTION_ROUNDTRIP_COMPONENT_DIAGNOSTIC_COMPLETED'
assert own_sql('SELECT COUNT(*) FROM codex_sql_rtt_messages_20261005').strip()==own_sql('SELECT COUNT(*) FROM codex_sql_rtt_outbox_20261005').strip()=='24000'
assert own_sql("SELECT COUNT(*) FROM codex_sql_rtt_messages_20261005 WHERE client_message_id LIKE 'sqlsafe%'").strip()=='0'
assert own_sql("SELECT COUNT(*),SUM(status=1 AND username=CONCAT('codex50k_20261004_',LPAD(user_id-700000,6,'0'))) FROM im_users WHERE user_id BETWEEN 700001 AND 710000").strip()=='10000\t10000'
assert own_sql("SELECT COUNT(*) FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA=DATABASE() AND EVENT_OBJECT_TABLE IN ('im_private_messages','im_event_outbox')").strip()=='0'
schema_sql="SELECT TABLE_NAME,COLUMN_NAME,ORDINAL_POSITION,COLUMN_TYPE,IS_NULLABLE,IFNULL(COLUMN_DEFAULT,'<NULL>'),EXTRA FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('im_private_messages','im_event_outbox') ORDER BY TABLE_NAME,ORDINAL_POSITION"
index_sql="SELECT TABLE_NAME,INDEX_NAME,SEQ_IN_INDEX,COLUMN_NAME,NON_UNIQUE,INDEX_TYPE FROM information_schema.STATISTICS WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME IN ('im_private_messages','im_event_outbox') ORDER BY TABLE_NAME,INDEX_NAME,SEQ_IN_INDEX"
original_schema=own_sql(schema_sql);original_indexes=own_sql(index_sql);(d/'original-schema-before.tsv').write_text(original_schema);(d/'original-indexes-before.tsv').write_text(original_indexes)
for name in objects:(d/(name+'-schema.tsv')).write_text(own_sql('SHOW CREATE TABLE '+name))
for old,new in [('im_private_messages',objects[0]),('im_event_outbox',objects[1])]:
 assert own_sql("SELECT ENGINE,TABLE_COLLATION FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='"+new+"'")==own_sql("SELECT ENGINE,TABLE_COLLATION FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='"+old+"'")
 assert own_sql(schema_sql.replace("('im_private_messages','im_event_outbox')","('"+new+"')")).replace(new,old)==own_sql(schema_sql.replace("('im_private_messages','im_event_outbox')","('"+old+"')"))
 assert own_sql(index_sql.replace("('im_private_messages','im_event_outbox')","('"+new+"')")).replace(new,old)==own_sql(index_sql.replace("('im_private_messages','im_event_outbox')","('"+old+"')"))
assert own_sql("SELECT COUNT(*) FROM information_schema.REFERENTIAL_CONSTRAINTS WHERE CONSTRAINT_SCHEMA=DATABASE() AND TABLE_NAME='codex_sql_rtt_messages_20261005' AND REFERENCED_TABLE_NAME='im_users' AND UPDATE_RULE='CASCADE' AND DELETE_RULE='RESTRICT'").strip()=='2'
(d/'own-reuse-audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'objects':objects,'DDL':False,'before_messages':24000,'before_outbox':24000,'expected_additional_pairs':24000,'prefixes':['sqlsafeA1','sqlsafeB1','sqlsafeB2','sqlsafeA2'],'SQL':'Identical PING/precheck/BEGIN INSERT SELECT and separate outbox COMMIT; only batch first3. C++ identity rejects before outbox COMMIT; two invalid identity/middleSQL fixtures rollback on own tables, subsequent health verified. No original table writes, no deletes.'},indent=2)+'\n')
reports=[]
try:
 for name,mode in [('sqlsafeA1','single'),('sqlsafeB1','safe'),('sqlsafeB2','safe'),('sqlsafeA2','single')]:
  result=d/(name+'-result.json');args=[str(binary),mode,'300','20',ip,str(config),str(result),name]
  (d/(name+'-run-audit-before.json')).write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Bounded own durable transaction process on exacttwoauditedcopied probe tables','argv':args,'timeout_seconds':40,'own_table_writes_only':True,'expected_results':6000,'initial_connections':16,'no_business_acceptance':True},indent=2)+'\n')
  first=snapshot();(d/(name+'-mysql-cpu-before.json')).write_text(json.dumps(first,indent=2)+'\n')
  with (d/(name+'-stdout.log')).open('w') as out,(d/(name+'-stderr.log')).open('w') as err:
   p=subprocess.Popen(args,stdout=out,stderr=err,start_new_session=True);proc=pathlib.Path(f'/proc/{p.pid}');start=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   (d/(name+'-own-process.json')).write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'starttime_ticks':start,'argv':args})+'\n')
   try:code=p.wait(timeout=40)
   finally:
    if p.poll() is None:
     assert proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==start and os.getpgid(p.pid)==p.pid
     assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
     (d/(name+'-stop-audit.json')).write_text(json.dumps({'operation':'Stop onlyownverifiedconnectionprobe session','pid':p.pid,'identity_cmdline_pgid_verified':True})+'\n');os.killpg(p.pid,signal.SIGTERM)
     try:p.wait(timeout=3)
     except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
  last=snapshot();(d/(name+'-mysql-cpu-after.json')).write_text(json.dumps(last,indent=2)+'\n');assert code==0,name+' nonzero, preserve logs'
  data=json.loads(result.read_text());assert data['status']=='SAFE_TRANSACTION_ROUNDTRIP_COMPONENT_COMPLETE' and data['planned']==data['committed_identity_correct']==6000 and data['failures']==0
  assert len(data['raw'])==6000 and len({x[0] for x in data['raw']})==6000 and data['thread_cpu']['samples']==6000
  assert len(data['before_commit_rejection_controls'])==2 and all(x['rejected_before_outbox_and_commit'] and x['connection_reusable'] and x['row_after_rollback']==0 for x in data['before_commit_rejection_controls'])
  assert data['ping_calls']==6000 and data['query_calls']==6000*(6 if mode=='single' else 4)
  count_sql="SELECT COUNT(*),COUNT(DISTINCT m.message_id),COUNT(DISTINCT m.client_message_id),SUM(o.outbox_id IS NOT NULL),SUM(m.delivery_status=0 AND m.message_type=1 AND LENGTH(m.content)=128 AND CAST(JSON_UNQUOTE(JSON_EXTRACT(o.payload,'$.message_id')) AS UNSIGNED)=m.message_id AND CAST(JSON_UNQUOTE(JSON_EXTRACT(o.payload,'$.from_user_id')) AS UNSIGNED)=m.from_user_id AND CAST(JSON_UNQUOTE(JSON_EXTRACT(o.payload,'$.to_user_id')) AS UNSIGNED)=m.to_user_id AND o.status=0) FROM codex_sql_rtt_messages_20261005 m LEFT JOIN codex_sql_rtt_outbox_20261005 o ON o.event_id=CONCAT('codex.rtt:',m.message_id) WHERE m.client_message_id LIKE '"+name+"n%'"
  counts=own_sql(count_sql).strip();(d/(name+'-durable-row-verification.tsv')).write_text(counts+'\n');assert counts=='6000\t6000\t6000\t6000\t6000'
  assert own_sql(schema_sql)==original_schema and own_sql(index_sql)==original_indexes and own_sql(durability).strip()=='1\t1\t1\t0\t0'
  delta={k:last['cpu_stat'][k]-v for k,v in first['cpu_stat'].items()};assert all(v>=0 for v in delta.values())
  elapsed=(last['end_monotonic_ns']-first['end_monotonic_ns'])/1e9
  report={'case':name,**{k:v for k,v in data.items() if k not in ['raw','raw_columns']},'mysql_cgroup':{'elapsed_seconds':elapsed,'delta':delta,'mean_cpu_cores':delta['usage_usec']/1e6/elapsed,'limits':'Whole MySQL cgroup including ownstartup/close and existingbackground work, notexclusive probe attribution'}}
  reports.append(report);(d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n')
except BaseException as error:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(error).__name__,'message':str(error),'completed_cases':[x['case'] for x in reports]})+'\n');raise
finally:
 after=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n');(d/'guest-memory-after.txt').write_text(pathlib.Path('/proc/meminfo').read_text())
assert subprocess.run(['pgrep','-f','^'+re.escape(str(binary))+r'( |$)'],capture_output=True).returncode==1
sql=subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-readonly-check','SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'],text=True,timeout=10).strip();assert sql=='1\t1\t1\t0\t0'
(d/'sql-durability-after.txt').write_text(sql+'\n')
x={'status':'MYSQL_SAFE_ROUNDTRIP_COMPONENT_DIAGNOSTIC_COMPLETED','head':head,'cases':reports,'all19_runtime_configs_preserved':True,'own_table_writes_only':True,'performance_acceptance':False,'SQL_durability':'1/1/1/0/0 unchanged','limits':'Same6SQL safe own durabletransaction BEGINinsertread1vs3 packetcalls withPING/precheck/BEGIN. No productEventCodec/fault/duplicate/uncertaincommit/tampering/recovery/real10k50k capacity proof; nativeDockerroute differsproduction, owncallerCPU vswholeMySQLbackground cgroup keptseparate'}
assert own_sql('SELECT COUNT(*) FROM codex_sql_rtt_messages_20261005').strip()==own_sql('SELECT COUNT(*) FROM codex_sql_rtt_outbox_20261005').strip()=='48000'
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({'status':x['status'],'head':head,'cases':[{'case':v['case'],'mean_ms':v['caller_wall']['mean_ms'],'p99_ms':v['caller_wall']['p99_ms'],'client_cores':v['process_mean_cpu_cores'],'mysql_whole_cores':v['mysql_cgroup']['mean_cpu_cores'],'requests':v['native_requests_including_ping'],'correct':v['committed_identity_correct'],'failures':v['failures'],'rejection_controls':v['before_commit_rejection_controls']} for v in reports]},indent=2))
PY
