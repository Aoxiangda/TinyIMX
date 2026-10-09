#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,shlex,shutil,os,signal,datetime,re,statistics
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'file-begin-snapshot-build-test-20261006-attempt2';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
source=json.loads((b/'file-begin-snapshot-source-20261006/summary.json').read_text())
helper=json.loads((b/'file-begin-snapshot-build2-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==helper['head']
assert all(sha(r/n)==h for n,h in source['files'].items()) and all(sha(r/n)==h for n,h in helper['files'].items())
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'group-partial-drain-control-20261006/restore-summary.json').read_text())['runtime']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
 assert shutil.disk_usage(r).free>2*1024**3
resources();cache=r/'build/linux-release'
original_elf='f1b9f84fc297789dc5fa462129c01fc9d252994b499fbd682e3e2db61ca5c1c1'
assert sha(cache/'file_service_demo')==original_elf
filec=json.loads(run(['docker','inspect','tinyimx-m21-file-service-1']))[0]
assert run(['docker','exec',filec['Id'],'sha256sum','/opt/tinyimx/bin/file_service_demo']).split()[0]==original_elf
base='tinyimx/runtime:m21-final';baseinfo=json.loads(run(['docker','image','inspect',base]))[0];assert baseinfo['Id']==filec['Image']
baseid=filec['Image'];image='tinyimx/runtime:codex-file-begin-snapshot-v1-20261006'
assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
probes=['tinyimx-codex-file-snapshot-ldd-20261006','tinyimx-codex-file-snapshot-exec-20261006']
assert not set(probes)&set(run(['docker','ps','-a','--format','{{.Names}}']).splitlines())
links={name:shlex.split((cache/('CMakeFiles/'+name+'.dir/link.txt')).read_text()) for name in ['file_service_demo','file_repository_integration_tests','file_application_service_tests']}
borrowed={}
for name,args in links.items():
 linkfile=cache/('CMakeFiles/'+name+'.dir/link.txt');borrowed[str(linkfile)]=sha(linkfile)
 for x in args:
  p=pathlib.Path(x) if pathlib.Path(x).is_absolute() else cache/x
  if p.is_file():borrowed[str(p.resolve())]=sha(p)
borrowed[str(cache/'file_service_demo')]=sha(cache/'file_service_demo')
borrowed[str(r/'tests/file/file_application_service_test.cpp')]=sha(r/'tests/file/file_application_service_test.cpp')
flagsfile=cache/'CMakeFiles/tinyimx_file_core.dir/flags.make';borrowed[str(flagsfile)]=sha(flagsfile);flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:
 flags+=shlex.split(next(v for v in flagsfile.read_text().splitlines() if v.startswith(key+' =')).split('=',1)[1])
assert all(not any('--wrap' in x for x in args) for args in links.values())
assert not any(t in (r/'tests/file/file_application_service_test.cpp').read_text() for t in ['remove_all','DELETE FROM','DROP TABLE','TRUNCATE'])
def sql(q):
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-file-snapshot-regression',q],30)
durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'
assert sql(durability).strip()=='1\t1\t1\t0\t0'
schemas={(mode,pool):f'codex_file_snapshot_20261006_{mode}_p{pool}' for mode in ['off','on','invalid'] for pool in [1,4]}
assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME IN ('"+"','".join(schemas.values())+"')").strip()=='0'
tables=['im_users','im_files','im_file_upload_sessions','im_file_upload_chunks'];ddl={}
for table in tables:
 create=sql('SHOW CREATE TABLE '+table).split('\t',1)[1].strip()
 assert create.startswith('CREATE TABLE '+chr(96)+table+chr(96)) and ';' not in create
 assert not re.search(r'REFERENCES\s+'+chr(96)+r'[^'+chr(96)+r']+'+chr(96)+r'\.',create)
 assert all(v in tables for v in re.findall(r'REFERENCES\s+'+chr(96)+r'([^'+chr(96)+r']+)'+chr(96),create));ddl[table]=create
mysqlc=json.loads(run(['docker','inspect','tinyimx-m21-mysql-1']))[0]
envmysql=dict(v.split('=',1) for v in mysqlc['Config']['Env'] if '=' in v);assert envmysql['MYSQL_DATABASE'] not in schemas.values()
plans={schema:'CREATE DATABASE '+schema+' CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;\nUSE '+schema+';\n'+';\n'.join(ddl.values())+';\n' for schema in schemas.values()}
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'source_head':source['head'],'operation':'One FileRepositoryAdapterTU; exact cached original runtime ELF and all other main/libs; native wrappers only; six fresh ownSQL schemas OFF/ON/invalid pool1/4','source_sha256':source['files'],'borrowed_sha256':borrowed,'runtime_before':before,'base_image':baseid,'original_elf':original_elf,'schemas':list(schemas.values()),'ddl_sha256':{n:hashlib.sha256(v.encode()).hexdigest() for n,v in plans.items()},'data':'Four table metadata only, no production rows; fixtures/invalidstate/faults only newown schemas retained','snapshot_contract':'Complete owner scoped SQLsnapshot as read; later chunk/finalize/Get recheck durable state; no cache/auth bypass','postcommit_fault':'Realcommit then fakeerror testwrapper, notactualnetworkfault','preserve':'No productionSQL/config/durability/deploy; no cached ELF/library overwrite','rollback':'Keep all schemas/stages/raw; timeout onlyverifiedownPGID; no DROP/DELETE/TRUNCATE/prune/reset/push','performance_acceptance':False})
save('runtime-before.json',before);phase='initial';checks=[]
def interrupted(sig,frame):raise RuntimeError('Ownedfile snapshot interrupted '+str(sig))
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
 def compilefile(src,out):
  obj=private/out
  invoke(['/usr/bin/c++',*flags,'-MD','-MF',str(obj)+'.d','-c',str(r/src),'-o',str(obj)],'compile-'+pathlib.Path(src).stem)
  return obj
 obj=compilefile('services/file/repository/FileRepositoryAdapter.cpp','FileRepositoryAdapter.o')
 testobj=compilefile('benchmark/local_capacity/file_begin_upload_snapshot_test.cpp','snapshot-test.o')
 unitobj=compilefile('tests/file/file_application_service_test.cpp','original-application-unit.o')
 def linkfile(name,out,label,native=False,unit=False):
  args=[x for x in links[name] if not x.startswith('-Wl,-Map=')]
  oldmain=[x for x in args if x.endswith('.cpp.o')];assert len(oldmain)==1
  if native:args[args.index(oldmain[0])]=str(testobj)
  if unit:args[args.index(oldmain[0])]=str(unitobj)
  args.insert(next(i for i,x in enumerate(args) if x.endswith('.a')),str(obj))
  args[args.index('-o')+1]=str(out);args+=['-Wl,-Map='+str(out)+'.map']
  if native:args+=['-Wl,--wrap=mysql_query','-Wl,--wrap=mysql_ping','-Wl,--wrap=mysql_commit']
  invoke(args,label)
 binary=private/'file_snapshot_tests';linkfile('file_repository_integration_tests',binary,'native-link',True)
 save('native-binary.json',{'sha256':sha(binary),'wrappers_test_only':True})
 unit=private/'file_application_service_tests';linkfile('file_application_service_tests',unit,'original-application-unit-link',unit=True)
 text=invoke([str(unit)],'original-application-unit',60);assert '[PASS]' in text and '[FAIL]' not in text
 (d/'original-application-unit.log').write_text(text);unit_checks=text.count('[PASS]')
 for (mode,pool),schema in schemas.items():
  phase='schema-'+schema
  assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME='"+schema+"'").strip()=='0'
  (d/(schema+'-create.sql')).write_text(plans[schema])
  save(schema+'-ddl-audit-before.json',{'schema':schema,'absent_before':True,'plan_sha256':hashlib.sha256(plans[schema].encode()).hexdigest(),'production_mutations':False})
  sql(plans[schema]);assert sql("SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA='"+schema+"'").strip()=='4'
  cfg=json.loads(pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/file.json').read_text())
  cfg['mysql'].update({'host':next(v['IPAddress'] for v in mysqlc['NetworkSettings']['Networks'].values() if v.get('IPAddress')),'port':3306,'user':'root','password':envmysql['MYSQL_ROOT_PASSWORD'],'database':schema,'pool_size':pool})
  cfg['logger']['console']=False;cfg['logger']['file']=str(private/(schema+'-logger.log'))
  cfgpath=private/(schema+'.json');cfgpath.write_text(json.dumps(cfg));os.chmod(cfgpath,0o600)
  env=dict(os.environ);env['TINYIMX_FILE_BEGIN_UPLOAD_SNAPSHOT_ENABLE']={'off':'0','on':'1','invalid':'01'}[mode]
  for key in ['TINYIMX_PERSIST_PHASE_TRACE_ENABLE','TINYIMX_STORAGE_WAIT_TRACE_ENABLE']:env.pop(key,None)
  label='native-'+mode+'-p'+str(pool);text=invoke([str(binary),str(cfgpath),'1' if mode=='on' else '0'],label,120,env)
  (d/(label+'.log')).write_text(text)
  result=json.loads(next(v for v in text.splitlines() if v.startswith('{"checks":')))
  assert result['status']=='FILE_SNAPSHOT_REAL_MYSQL_PASS' and result['enabled']==(mode=='on') and result['pool']==pool and result['checks']==text.count('[PASS]') and result['checks']>=20 and '[FAIL]' not in text
  assert len(result['timing'])==80 and all(v['client_key_reads']==(1 if mode=='on' else 2) and v['health_pings']==(1 if mode=='on' else 2) for v in result['timing'])
  result.update({'case':mode,'schema':schema,'mean_us':statistics.mean(z['total_us'] for z in result['timing']),'max_us':max(z['total_us'] for z in result['timing'])});checks.append(result);save('completed-native-cases.json',checks)
  print(json.dumps({k:v for k,v in result.items() if k!='timing'}),flush=True);assert sql(durability).strip()=='1\t1\t1\t0\t0'
 assert len(checks)==6
 executable=private/'file_service_demo';linkfile('file_service_demo',executable,'runtime-link')
 symbols=run(['nm','-C',str(executable)],40);assert all(x not in symbols for x in ['__wrap_mysql','__real_mysql']) and 'FileRepositoryAdapter::BeginUpload(' in symbols
 assert 'LOAD '+str(obj) in pathlib.Path(str(executable)+'.map').read_text()
 save('runtime-provenance.json',{'adapter_obj_sha256':sha(obj),'elf_sha256':sha(executable),'unchanged_inputs':borrowed,'wrappers_absent':True})
 context=private/'image-context';context.mkdir();shutil.copy2(executable,context/'file_service_demo');os.chmod(context/'file_service_demo',0o755)
 dockerfile=private/'Dockerfile';dockerfile.write_text('FROM '+base+'\nCOPY --chmod=0755 file_service_demo /opt/tinyimx/bin/file_service_demo\nLABEL org.opencontainers.image.revision="'+head+'"\nLABEL tinyimx.binary.sha256="'+sha(executable)+'"\n')
 invoke(['docker','build','--pull=false','--network=none','-f',str(dockerfile),'-t',image,str(context)],'seal-image',120)
 common=['docker','run','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges','--label','tinyimx.codex.task=file-snapshot-20261006']
 text=invoke(common+['--name',probes[0],'--entrypoint','/bin/sh',image,'-ec','test -x "$1"; ldd -r "$1"','own-loader','/opt/tinyimx/bin/file_service_demo'],'uid1000-ldd',20)
 assert not any(v in text.lower() for v in ['not found','undefined symbol'])
 invoke(common+['--name',probes[1],'--entrypoint','/opt/tinyimx/bin/file_service_demo',image,'/tmp/own-file-snapshot-missing-config.json'],'uid1000-exec',20,expected=1)
 imageinfo=json.loads(run(['docker','image','inspect',image]))[0]
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in source['files'].items())
 x={'status':'FILE_SNAPSHOT_REAL_SQL_AND_IMAGE_PASS','head':head,'source_head':source['head'],'native_cases':checks,'native_total_checks':sum(z['checks'] for z in checks),'original_unit_checks':unit_checks,'image_tag':image,'image_id':imageinfo['Id'],'file_elf_sha256':sha(executable),'native_elf_sha256':sha(binary),'runtime_deploy':False,'performance_acceptance':False,'schemas_and_raw_retained':True,'durability':'1/1/1/0/0','borrowed_artifacts_preserved':True}
 save('summary.json',x);print(json.dumps({k:v for k,v in x.items() if k!='native_cases'},indent=2),flush=True)
except BaseException as error:
 save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'completed_cases':checks,'runtime_deploy':False,'schemas_preserved':list(schemas.values())});raise
finally:
 after=runtime();assert before==after;save('runtime-after.json',after)
PY
