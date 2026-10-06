#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,shlex,shutil,os,signal,re,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-confirm-coalesce-build-20261006-attempt2';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t,stderr=subprocess.STDOUT)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
source=json.loads((b/'group-confirm-coalesce-source-20261006/summary.json').read_text())
helper=json.loads((b/'group-confirm-coalesce-build2-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==helper['head']
sources={**source['files'],**helper['files']};assert all(sha(r/n)==h for n,h in sources.items())
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'group-runtime-sql-diagnostic-20261006/restore-summary.json').read_text())['runtime']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
cache=r/'build/linux-release';old=b/'group-completion-rpc-build-20261006-attempt4'
link=json.loads((old/'message-runtime-link-process.json').read_text())['argv']
sealed=json.loads((b/'group-completion-rpc-build-20261006-attempt6/summary.json').read_text())
assert sealed['images']['message']['elf_sha256']=='baa2ec54332c5a2bce2d9bf7fe130231450870a26082552308541cddcceee103'
assert sha(old/'runtime-private/message_service_demo')==sealed['images']['message']['elf_sha256']
oldrepo=[x for x in link if x.endswith('/MessageRepository.o')];main=[x for x in link if x.endswith('/message-main.o')]
assert len(oldrepo)==len(main)==1 and not any('--wrap' in x for x in link)
borrowed={}
for x in link:
 p=pathlib.Path(x) if pathlib.Path(x).is_absolute() else cache/x
 if p.is_file():borrowed[str(p.resolve())]=sha(p)
for target in ['tinyimx_repository','tinyimx_message_core','tinyimx_message_grpc']:
 p=cache/('CMakeFiles/'+target+'.dir/flags.make');borrowed[str(p)]=sha(p)
extra=['tests/message/message_application_service_test.cpp','benchmark/local_capacity/group_delivery_completion_rpc_test.cpp','services/message/repository/MessageRepositoryAdapter.cpp','services/message/application/MessageApplicationService.cpp','services/message/service/MessageServiceImpl.cpp','examples/message_service_demo.cpp']
sources.update({x:sha(r/x) for x in extra})
macro=subprocess.check_output(['git','show','30452a51d08561593a6aba2fbe1ef188ff06d1c7:common/logging/LogMacros.h'])
assert hashlib.sha256(macro).hexdigest()=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
def sql(q):
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-group-confirm-regression',q])
durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'
assert sql(durability).strip()=='1\t1\t1\t0\t0'
cases=[('OFF',None,False,1),('ON','1',True,1),('OFF4',None,False,4),('ON4','1',True,4),('INVALID','01',False,1),('ZERO','0',False,1)]
schemas={label:f'codex_group_confirm_20261006_{label.lower()}_p{pool}' for label,value,enabled,pool in cases}
rpcschemas={pool:f'codex_group_complete_20261006_confirm_rpc_p{pool}' for pool in [1,4]}
all_schemas=[*rpcschemas.values()]
assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME IN ('"+"','".join(all_schemas)+"')").strip()=='0'
tables=['im_users','im_groups','im_group_messages','im_group_message_deliveries'];ddl={}
for table in tables:
 create=sql('SHOW CREATE TABLE '+table).split('\t',1)[1].strip()
 assert create.startswith('CREATE TABLE '+chr(96)+table+chr(96)) and ';' not in create
 assert not re.search(r'REFERENCES\s+'+chr(96)+r'[^'+chr(96)+r']+'+chr(96)+r'\.',create)
 assert all(v in tables for v in re.findall(r'REFERENCES\s+'+chr(96)+r'([^'+chr(96)+r']+)'+chr(96),create));ddl[table]=create
mc=json.loads(run(['docker','inspect','tinyimx-m21-mysql-1']))[0];envmysql=dict(v.split('=',1) for v in mc['Config']['Env'] if '=' in v)
plans={name:'CREATE DATABASE '+name+' CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;\nUSE '+name+';\n'+';\n'.join(ddl.values())+';\n' for name in all_schemas}
image='tinyimx/runtime:codex-group-confirm-coalesce-message-v1-20261006'
assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700);phase='initial';results=[]
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'source_sha256':sources,'borrowed_sha256':borrowed,'runtime_before':before,'operation':'Reuse exact unchanged Repo TU, 49 caller and 276 SQL checks plus old application PASS from first attempt. Add correct message_grpc/common generated include for required RPC regression; two still-absent own RPC schemas only. Sealed runtime link replaces only Repo. No cache/runtime mutations.','schema_plan_sha256':{n:hashlib.sha256(v.encode()).hexdigest() for n,v in plans.items()},'durability':'1/1/1/0/0','faults':'Native only mysql_query/ping/commit wrappers, real COMMIT then synthetic API failure not network fault','rollback':'All original/source/preimages/schema/raw preserved. Stop only verified own PGID child on timeout; no DELETE/DROP/prune/reset/push','performance_acceptance':False})
save('runtime-before.json',before)
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
 assert shutil.disk_usage(r).free>2*1024**3
def interrupted(sig,frame):raise RuntimeError('Owned confirmation build interrupted '+str(sig))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def invoke(a,label,seconds=240,env=None,expected=0):
 global phase
 phase=label;resources();save(label+'-audit-before.json',{'argv':a,'timeout':seconds})
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(a,cwd=cache,env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path('/proc')/str(p.pid)
  try:ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
  except FileNotFoundError:assert p.poll() is not None;ticks=None
  save(label+'-process.json',{'pid':p.pid,'start_ticks':ticks,'argv':a})
  try:code=p.wait(timeout=seconds)
  finally:
   if p.poll() is None:
    assert ticks and (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(a)]==[str(x).encode() for x in a]
    save(label+'-stop-audit.json',{'operation':'Only verified own PGID','pid':p.pid});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed; raw retained'
 print(json.dumps({'completed':label,'exit':code}),flush=True);return (private/(label+'.log')).read_text()
def envbase(value=None):
 env=dict(os.environ)
 for key in list(env):
  if key.startswith('TINYIMX_GROUP_') or key in ['TINYIMX_PERSIST_PHASE_TRACE_ENABLE','TINYIMX_STORAGE_WAIT_TRACE_ENABLE','TINYIMX_MYSQL_POOL_TRACE']:env.pop(key)
 env['TINYIMX_GROUP_DELIVERY_CLAIM_BATCH_ENABLE']='1'
 if value is not None:env['TINYIMX_GROUP_CONFIRM_COALESCE_ENABLE']=value
 return env
try:
 overlay=private/'original-includes/common/logging';overlay.mkdir(parents=True);(overlay/'LogMacros.h').write_bytes(macro)
 generated=b/'group-completion-rpc-build-20261006/runtime-private/generated/rpc'
 def compilefile(src,name,target='tinyimx_repository'):
  flags=[];fields=(cache/('CMakeFiles/'+target+'.dir/flags.make')).read_text()
  for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(v for v in fields.splitlines() if v.startswith(key+' =')).split('=',1)[1])
  obj=private/name
  invoke(['/usr/bin/c++','-I'+str(overlay.parents[1]),'-I'+str(generated),*flags,'-MD','-MF',str(obj)+'.d','-c',str(r/src),'-o',str(obj)],'compile-'+pathlib.Path(src).stem)
  return obj
 frozen=b/'group-confirm-coalesce-build-20261006'
 original=json.loads((frozen/'audit-before.json').read_text())
 assert json.loads((frozen/'failed.json').read_text())['phase']=='compile-group_delivery_completion_rpc_test'
 assert all(sha(r/n)==h for n,h in original['source_sha256'].items()) and all(sha(p)==h for p,h in original['borrowed_sha256'].items())
 fp=frozen/'runtime-private';obj=fp/'MessageRepository.o'
 reused=[obj,fp/'confirm-test.o',fp/'caller-native',fp/'group_confirm_native',fp/'message-unit']
 reuse_sha={str(p):sha(p) for p in reused}
 for label,name in [('compile-MessageRepository','MessageRepository.o'),('compile-group_confirm_coalesce_test','confirm-test.o')]:
  assert json.loads((frozen/(label+'-process.json')).read_text())['argv'][-1]==str(fp/name)
 caller_text=(frozen/'caller-native.log').read_text()
 caller_result=json.loads(next(v for v in caller_text.splitlines() if v.startswith('{')))
 assert caller_result['checks']==49 and caller_result['failures']==0 and '[FAIL]' not in caller_text
 (d/'caller-native.log').write_text(caller_text)
 results=json.loads((frozen/'native-cases.json').read_text());assert len(results)==6 and sum(v['checks'] for v in results)==276
 for result in results:
  label=result['case'];text=(frozen/('sql-'+label+'.log')).read_text()
  assert result['checks']==text.count('[PASS]') and '[FAIL]' not in text
  assert json.loads(next(v for v in text.splitlines() if v.startswith('{')))['status']=='GROUP_CONFIRM_COALESCE_REAL_MYSQL_PASS'
  (d/('sql-'+label+'.log')).write_text(text)
 save('native-cases.json',results)
 unittext=(frozen/'original-message-unit.log').read_text();assert 'failed=0' in unittext and '[FAIL]' not in unittext
 (d/'original-message-unit.log').write_text(unittext)
 borrowed.update(reuse_sha)
 save('reuse-audit-before.json',{'all_original_source_and_borrowed_exact':True,'reused_sha256':reuse_sha,'inherited_caller_checks':49,'inherited_sql_checks':276,'original_application_pass':True,'original_failure_retained':True,'only_missing_generated_include_fixed':True})
 def linkfile(out,label,mainobj=None,wrap=False):
  argv=[str(obj) if x==oldrepo[0] else str(mainobj) if mainobj is not None and x==main[0] else x for x in link if not x.startswith('-Wl,-Map=')]
  argv[argv.index('-o')+1]=str(out);argv+=['-Wl,-Map='+str(out)+'.map']
  if wrap:argv+=['-Wl,--wrap=mysql_query','-Wl,--wrap=mysql_ping']
  if wrap=='sql':argv+=['-Wl,--wrap=mysql_commit']
  invoke(argv,label)
 def fixture(schema,pool):
  assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME='"+schema+"'").strip()=='0'
  (d/(schema+'-create.sql')).write_text(plans[schema]);save(schema+'-ddl-audit-before.json',{'schema':schema,'absent_before':True,'plan_sha256':hashlib.sha256(plans[schema].encode()).hexdigest(),'production_mutations':False})
  sql(plans[schema])
  cfg=json.loads(pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json').read_text())
  cfg['mysql'].update({'host':next(v['IPAddress'] for v in mc['NetworkSettings']['Networks'].values() if v.get('IPAddress')),'port':3306,'user':'root','password':envmysql['MYSQL_ROOT_PASSWORD'],'database':schema,'pool_size':pool})
  cfg['logger']['console']=False;cfg['logger']['file']=str(private/(schema+'-logger.log'))
  path=private/(schema+'.json');path.write_text(json.dumps(cfg));os.chmod(path,0o600);return path
 rpcobj=compilefile('benchmark/local_capacity/group_delivery_completion_rpc_test.cpp','completion-rpc.o','tinyimx_message_grpc');rpc=private/'completion-rpc'
 linkfile(rpc,'completion-rpc-link',rpcobj,'rpc');rpcresults=[]
 for pool,schema in rpcschemas.items():
  cfg=fixture(schema,pool);text=invoke([str(rpc),str(cfg)],'completion-rpc-p'+str(pool),120,envbase('1'));(d/('completion-rpc-p'+str(pool)+'.log')).write_text(text)
  result=json.loads(next(x for x in text.splitlines() if x.startswith('{"checks":')))
  assert result['status']=='GROUP_COMPLETION_RPC_REAL_MYSQL_PASS' and result['checks']==text.count('[PASS]') and '[FAIL]' not in text
  rpcresults.append(result);save('rpc-regression-cases.json',rpcresults)
 exe=private/'message_service_demo';linkfile(exe,'message-runtime-link')
 symbols=run(['nm','-C',str(exe)],60)
 assert 'MessageRepository::ConfirmGroupMessageDeliveryBatch(' in symbols and 'MessageServiceImpl::CompleteGroupMessageDeliveryAttempts(' in symbols and '__wrap_' not in symbols and '__real_mysql_' not in symbols
 context=private/'message-context';context.mkdir();shutil.copy2(exe,context/exe.name);os.chmod(context/exe.name,0o755)
 (context/'Dockerfile').write_text('FROM '+sealed['images']['message']['image_tag']+'\nCOPY --chmod=0755 message_service_demo /opt/tinyimx/bin/\nLABEL org.opencontainers.image.revision="'+head+'"\n')
 invoke(['docker','build','--pull=false','--network=none','-t',image,str(context)],'message-image',120)
 for label,argv,code in [('ldd',['/usr/bin/ldd','-r','/opt/tinyimx/bin/message_service_demo'],0),('missing-config',['/opt/tinyimx/bin/message_service_demo','/__codex_confirm_missing__.json'],1)]:
  name='codex-group-confirm-'+label+'-20261006';assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0
  text=invoke(['docker','run','--name',name,'--label','codex.tinyimx.group_confirm=20261006','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges:true',image,*argv],label,30,expected=code)
  if label=='ldd':assert 'not found' not in text and 'undefined symbol' not in text
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in sources.items())
 summary={'status':'GROUP_CONFIRM_COALESCE_NATIVE_AND_IMAGE_PASS','head':head,'image_tag':image,'image_id':json.loads(run(['docker','image','inspect',image]))[0]['Id'],'elf_sha256':sha(exe),'caller_checks':caller_result['checks'],'sql_checks':sum(x['checks'] for x in results),'rpc_checks':sum(x['checks'] for x in rpcresults),'checks':caller_result['checks']+sum(x['checks'] for x in results)+sum(x['checks'] for x in rpcresults),'original_message_unit_pass':True,'only_repo_runtime_object_replaced':True,'no_class_layout_or_protocol_change':True,'no_new_product_threads':True,'durability':'1/1/1/0/0','runtime_deploy':False,'performance_acceptance':False}
 save('summary.json',summary);print(json.dumps(summary,indent=2),flush=True)
except BaseException as error:save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'runtime_deploy':False,'completed_cases':results,'schemas_and_raw_retained':True});raise
finally:
 after=runtime();save('runtime-after.json',after);assert before==after
PY
