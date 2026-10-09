#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,shlex,shutil,os,signal,datetime,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-completion-batch-build-test-20261006';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
source=json.loads((b/'group-completion-batch-source-20261006/summary.json').read_text())
helper=json.loads((b/'group-completion-batch-build-source-v2-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==helper['head']
assert all(sha(r/n)==h for n,h in source['files'].items()) and all(sha(r/n)==h for n,h in helper['files'].items())
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'file-begin-snapshot-control-20261006-attempt3/restore-summary.json').read_text())['runtime']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
 assert shutil.disk_usage(r).free>2*1024**3
resources()
cache=r/'build/linux-release';accepted=b/'private-batch-message-build-image-20261005'
link=json.loads((accepted/'link-full-message-service-command.json').read_text())
assert sha(accepted/'message_service_demo')=='2548733766409d282d30f6ffbbbe47f9c12b5d4fa3cacfdcc3d3e47f72582699'
oldrepo=[x for x in link if x.endswith('/MessageRepository.o')];main=[x for x in link if x.endswith('/demo-main.o')]
assert len(oldrepo)==len(main)==1 and not any('--wrap' in x for x in link)
accepted_audit=json.loads((accepted/'audit-before.json').read_text())
assert json.loads((b/'group-completion-batch-source-20261006/audit-before.json').read_text())['source_before']['services/repository/MessageRepository.cpp']=='a7b029f2152055058f6a80f4f83f9e005c74f6228aff9ca40778759915aa0f30'
borrowed={}
for x in link:
 p=pathlib.Path(x) if pathlib.Path(x).is_absolute() else cache/x
 if p.is_file():borrowed[str(p.resolve())]=sha(p)
borrowed[str(accepted/'message_service_demo')]=sha(accepted/'message_service_demo')
flagsfile=cache/'CMakeFiles/tinyimx_repository.dir/flags.make';borrowed[str(flagsfile)]=sha(flagsfile);flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
 flags+=shlex.split(next(v for v in flagsfile.read_text().splitlines() if v.startswith(key+' =')).split('=',1)[1])
macro=subprocess.check_output(['git','show','30452a51d08561593a6aba2fbe1ef188ff06d1c7:common/logging/LogMacros.h'])
assert hashlib.sha256(macro).hexdigest()=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
def sql(q):
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-group-completion-regression',q],30)
durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'
assert sql(durability).strip()=='1\t1\t1\t0\t0'
schemas={(mode,pool):f'codex_group_complete_20261006_{mode}_p{pool}' for mode in ['reference','batch'] for pool in [1,4]}
assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME IN ('"+"','".join(schemas.values())+"')").strip()=='0'
tables=['im_users','im_groups','im_group_messages','im_group_message_deliveries'];ddl={}
for table in tables:
 create=sql('SHOW CREATE TABLE '+table).split('\t',1)[1].strip()
 assert create.startswith('CREATE TABLE '+chr(96)+table+chr(96)) and ';' not in create
 assert not re.search(r'REFERENCES\s+'+chr(96)+r'[^'+chr(96)+r']+'+chr(96)+r'\.',create)
 assert all(v in tables for v in re.findall(r'REFERENCES\s+'+chr(96)+r'([^'+chr(96)+r']+)'+chr(96),create));ddl[table]=create
c=json.loads(run(['docker','inspect','tinyimx-m21-mysql-1']))[0]
envmysql=dict(v.split('=',1) for v in c['Config']['Env'] if '=' in v)
assert envmysql['MYSQL_DATABASE'] not in schemas.values()
plans={schema:'CREATE DATABASE '+schema+' CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;\nUSE '+schema+';\n'+';\n'.join(ddl.values())+';\n' for schema in schemas.values()}
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'source_head':source['head'],'operation':'Onlyone repositoryTU/newnative; exactaccepted Message otherlibs/main aslinkreference; fourfresh ownSQL schemas reference64single andatomicbatch1, pool1/4; native only no service image','source_sha256':source['files'],'borrowed_sha256':borrowed,'runtime_before':before,'schemas':list(schemas.values()),'ddl_sha256':{n:hashlib.sha256(v.encode()).hexdigest() for n,v in plans.items()},'data':'Onlyfour table metadata, noproductionrows; freshown fixtures/faults/triggers retained;20concurrentACKcompletion and256uint64 boundedrows onlyownschemas','preserve':'No productionSQL/config/durability/deploy/image; original macrooverlay; oldsinglecalls unchanged and no cachedobjectoverwrite','postcommit_fault':'ActualautocommitUPDATE then fakequeryerror testwrapper, notnetworkfault','rollback':'Retain schemas/stages/raw; timeout closesonly verifiedownPGID; no DROP/DELETE/TRUNCATE/prune/reset/push','performance_acceptance':False})
save('runtime-before.json',before);phase='initial';checks=[]
def interrupted(sig,frame):raise RuntimeError('Ownedgroupcompletion interrupted '+str(sig))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def invoke(a,label,seconds=240,env=None,expected=0):
 global phase
 phase=label;resources();save(label+'-audit-before.json',{'argv':a,'timeout':seconds,'operation':'Onlyown compiler/link/native/seal/stopped probe child'})
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(a,cwd=cache,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path('/proc')/str(p.pid)
  try:ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
  except FileNotFoundError:assert p.poll() is not None;ticks=None
  save(label+'-process.json',{'pid':p.pid,'start_ticks':ticks,'argv':a})
  try:code=p.wait(timeout=seconds)
  finally:
   if p.poll() is None:
    assert ticks and (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(a)]==[str(x).encode() for x in a]
    save(label+'-stop-audit.json',{'pid':p.pid,'operation':'OnlyverifiedownPGID'});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed; raw retained'
 print(json.dumps({'completed':label,'exit':code}),flush=True);return (private/(label+'.log')).read_text()
try:
 overlay=private/'original-includes/common/logging';overlay.mkdir(parents=True);(overlay/'LogMacros.h').write_bytes(macro)
 def compilefile(src,out):
  obj=private/out
  invoke(['/usr/bin/c++','-I'+str(overlay.parents[1]),*flags,'-MD','-MF',str(obj)+'.d','-c',str(r/src),'-o',str(obj)],'compile-'+pathlib.Path(src).stem)
  return obj
 obj=compilefile('services/repository/MessageRepository.cpp','MessageRepository.o')
 testobj=compilefile('benchmark/local_capacity/group_delivery_completion_batch_test.cpp','completion-test.o')
 assert str(overlay/'LogMacros.h') in pathlib.Path(str(obj)+'.d').read_text()
 def linkfile(out,label,native=False):
  args=[str(obj) if x==oldrepo[0] else str(testobj) if native and x==main[0] else x for x in link if not x.startswith('-Wl,-Map=')]
  args[args.index('-o')+1]=str(out);args+=['-Wl,-Map='+str(out)+'.map']
  if native:args+=['-Wl,--wrap=mysql_query','-Wl,--wrap=mysql_ping']
  invoke(args,label)
 binary=private/'group_completion_tests';linkfile(binary,'native-link',True);save('native-binary.json',{'sha256':sha(binary),'wrappers_test_only':True})
 for (mode,pool),schema in schemas.items():
  phase='schema-'+schema
  assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME='"+schema+"'").strip()=='0'
  (d/(schema+'-create.sql')).write_text(plans[schema])
  save(schema+'-ddl-audit-before.json',{'schema':schema,'absent_before':True,'plan_sha256':hashlib.sha256(plans[schema].encode()).hexdigest(),'production_mutations':False})
  sql(plans[schema]);assert sql("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA='"+schema+"'").strip()=='4'
  cfg=json.loads(pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json').read_text())
  cfg['mysql'].update({'host':next(v['IPAddress'] for v in c['NetworkSettings']['Networks'].values() if v.get('IPAddress')),'port':3306,'user':'root','password':envmysql['MYSQL_ROOT_PASSWORD'],'database':schema,'pool_size':pool})
  cfg['logger']['console']=False;cfg['logger']['file']=str(private/(schema+'-logger.log'))
  cfgpath=private/(schema+'.json');cfgpath.write_text(json.dumps(cfg));os.chmod(cfgpath,0o600)
  env=dict(os.environ);env['TINYIMX_GROUP_DELIVERY_CLAIM_BATCH_ENABLE']='1'
  for key in ['TINYIMX_PERSIST_PHASE_TRACE_ENABLE','TINYIMX_STORAGE_WAIT_TRACE_ENABLE']:env.pop(key,None)
  label='native-'+mode+'-p'+str(pool);text=invoke([str(binary),str(cfgpath),'1' if mode=='batch' else '0'],label,120,env)
  (d/(label+'.log')).write_text(text)
  result=json.loads(next(v for v in text.splitlines() if v.startswith('{"batch":')))
  assert result['status']=='GROUP_COMPLETION_REAL_MYSQL_PASS' and result['batch']==(mode=='batch') and result['pool']==pool and result['checks']==text.count('[PASS]') and result['checks']>=25 and '[FAIL]' not in text
  assert len(result['timing'])==8 and all(v['completion_commands']==(1 if mode=='batch' else 64) and v['health_pings']==(1 if mode=='batch' else 64) and v['affected_rows']==64 for v in result['timing'])
  result.update({'case':mode,'schema':schema,'mean_us':sum(z['total_us'] for z in result['timing'])/len(result['timing']),'max_us':max(z['total_us'] for z in result['timing'])});checks.append(result);save('completed-native-cases.json',checks)
  print(json.dumps({k:v for k,v in result.items() if k!='timing'}),flush=True);assert sql(durability).strip()=='1\t1\t1\t0\t0'
 assert len(checks)==4
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in source['files'].items())
 x={'status':'GROUP_COMPLETION_REAL_SQL_NATIVE_PASS','head':head,'source_head':source['head'],'native_cases':checks,'native_total_checks':sum(z['checks'] for z in checks),'native_elf_sha256':sha(binary),'runtime_wiring':False,'runtime_deploy':False,'performance_acceptance':False,'schemas_and_raw_retained':True,'durability':'1/1/1/0/0','borrowed_artifacts_preserved':True}
 save('summary.json',x);print(json.dumps({k:v for k,v in x.items() if k!='native_cases'},indent=2),flush=True)
except BaseException as error:
 save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'completed_cases':checks,'runtime_deploy':False,'schemas_preserved':list(schemas.values())});raise
finally:
 after=runtime();assert before==after;save('runtime-after.json',after)
PY
