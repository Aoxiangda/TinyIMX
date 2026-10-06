#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,shlex,shutil,os,signal,re,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-completion-rpc-build-20261006-attempt5';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
helper=json.loads((b/'group-completion-rpc-build5-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==helper['head']
frozen=b/'group-completion-rpc-build-20261006-attempt4';previous=json.loads((frozen/'audit-before.json').read_text())
assert json.loads((frozen/'failed.json').read_text())['phase']=='compile-rpc-integration'
sources={n:h for n,h in previous['source_sha256'].items() if not n.startswith('docs/')};sources.update(helper['files'])
assert all(sha(r/n)==h for n,h in sources.items())
assert all(sha(p)==h for p,h in previous['borrowed_sha256'].items())
reuse=json.loads((frozen/'reuse-audit-before.json').read_text())['sha256'];assert all(sha(p)==h for p,h in reuse.items())
nativecases=json.loads((frozen/'native-cases.json').read_text());total=sum(x.get('checks',x.get('tests',0)) for x in nativecases)
assert total==128 and all(x.get('failures',0)==0 and 'PASS' in x['status'] for x in nativecases)
unit=(frozen/'original-message-unit.log').read_text();assert 'failed=0' in unit and '[FAIL]' not in unit
protocol=json.loads(next(x for x in (frozen/'protocol-preservation.log').read_text().splitlines() if x.startswith('{')))
assert protocol=={'status':'EXISTING_PROTOCOL_EXACTLY_PRESERVED','checks':1}
fp=frozen/'runtime-private';link=json.loads((frozen/'message-runtime-link-process.json').read_text())['argv']
main=[x for x in link if x.endswith('/message-main.o')];assert len(main)==1 and not any('--wrap' in x for x in link)
exeG=fp/'gateway_demo';exeM=fp/'message_service_demo';probe=b/'rpc-readiness-build-20261006-attempt2/runtime-private/rpc_readiness_probe'
assert sha(probe)=='ee066590e0556beacb799cd7639c77c88d8443ac7c8023189ad265635c69de85'
symbolproof=json.loads((frozen/'runtime-role-symbol-verification.json').read_text());assert all(v['wrappers_absent'] and all(v['required_symbols'].values()) for v in symbolproof.values())
cache=r/'build/linux-release';flagsfile=cache/'CMakeFiles/tinyimx_message_grpc.dir/flags.make'
flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in flagsfile.read_text().splitlines() if x.startswith(key+' =')).split('=',1)[1])
assert '-I'+str(cache/'generated/rpc') in flags
overlay=b/'group-completion-rpc-build-20261006/runtime-private/original-includes'
assert sha(overlay/'common/logging/LogMacros.h')=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
borrowed={str(p):sha(p) for p in [exeG,exeM,probe,flagsfile,overlay/'common/logging/LogMacros.h']}
for token in link:
 p=pathlib.Path(token) if pathlib.Path(token).is_absolute() else cache/token
 if p.is_file():borrowed[str(p.resolve())]=sha(p)
for name in ['native-cases.json','original-message-unit.log','protocol-preservation.log','runtime-role-symbol-verification.json']:
 p=frozen/name;borrowed[str(p)]=sha(p)
borrowed.update(reuse)
for p in (fp/'generated/rpc').rglob('*'):
 if p.is_file():borrowed[str(p)]=sha(p)
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'file-begin-snapshot-control-20261006-attempt3/restore-summary.json').read_text())['runtime']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
def sql(q):
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-completion-rpc',q])
durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'
assert sql(durability).strip()=='1\t1\t1\t0\t0'
schemas={pool:f'codex_group_complete_20261006_rpc_p{pool}' for pool in [1,4]}
assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME IN ('"+"','".join(schemas.values())+"')").strip()=='0'
tables=['im_users','im_groups','im_group_messages','im_group_message_deliveries'];ddl={}
for table in tables:
 create=sql('SHOW CREATE TABLE '+table).split('\t',1)[1].strip()
 assert create.startswith('CREATE TABLE '+chr(96)+table+chr(96)) and ';' not in create
 assert not re.search(r'REFERENCES\s+'+chr(96)+r'[^'+chr(96)+r']+'+chr(96)+r'\.',create)
 assert all(v in tables for v in re.findall(r'REFERENCES\s+'+chr(96)+r'([^'+chr(96)+r']+)'+chr(96),create));ddl[table]=create
plans={schema:'CREATE DATABASE '+schema+' CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;\nUSE '+schema+';\n'+';\n'.join(ddl.values())+';\n' for schema in schemas.values()}
mc=json.loads(run(['docker','inspect','tinyimx-m21-mysql-1']))[0];menv=dict(x.split('=',1) for x in mc['Config']['Env'] if '=' in x)
images={'gateway':'tinyimx/runtime:codex-group-completion-batch-gateway-v1-20261006','message':'tinyimx/runtime:codex-group-completion-batch-message-v1-20261006'}
bases={'gateway':('tinyimx/runtime:codex-group-recipient-order-v1-20261006','sha256:1b70a623e1b2a14d91aeb6c2244b8249d904108e48e99fd155fc245a8c59b557'),'message':('tinyimx/runtime:codex-private-begin-insert-read-batch-v1','sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060')}
assert all(subprocess.run(['docker','image','inspect',tag],capture_output=True).returncode!=0 for tag in images.values())
assert all(json.loads(run(['docker','image','inspect',tag]))[0]['Id']==iid for tag,iid in bases.values())
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700);phase='initial';rpccases=[]
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'source_sha256':sources,'borrowed_sha256':borrowed,'runtime_before':before,'operation':'Resume only remaining RPC native+image work from immutable build4. Correct test compile flags from core to grpc (public common PB include required). Runtime/protocol/128native/original unit already passed, no implementation changes. Real 2 absent ownschemas only; seal images no deploy.','inherited_evidence_stage':str(frozen),'inherited_coordinator_checks':128,'protocol_exact_preservation':True,'original_unit_pass':True,'schema_plan_sha256':{n:hashlib.sha256(v.encode()).hexdigest() for n,v in plans.items()},'rollback':'Keep all originals/failed raw/native/rows; verified ownchild stop only; no cache override/deletion/prune/reset/push','performance_acceptance':False})
save('reuse-audit-before.json',{'sha256':borrowed,'source_sha256':sources,'inherited_checks':128,'stages':[str(frozen),str(b/'group-completion-rpc-build-20261006')]})
save('runtime-before.json',before);save('native-cases.json',nativecases)
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
 assert shutil.disk_usage(r).free>2*1024**3
def interrupted(sig,frame):raise RuntimeError('Own RPC continuation interrupted '+str(sig))
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
    save(label+'-stop-audit.json',{'pid':p.pid,'operation':'Only verified own PGID'});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed; raw retained'
 print(json.dumps({'completed':label,'exit':code}),flush=True);return (private/(label+'.log')).read_text()
try:
 obj=private/'rpc-integration.o'
 invoke(['/usr/bin/c++','-I'+str(overlay),'-I'+str(fp/'generated/rpc'),*flags,'-MD','-MF',str(obj)+'.d','-c',str(r/'benchmark/local_capacity/group_delivery_completion_rpc_test.cpp'),'-o',str(obj)],'compile-rpc-integration')
 assert str(cache/'generated/rpc/tinyimx/common/v1/common.pb.h') in pathlib.Path(str(obj)+'.d').read_text()
 binary=private/'completion-rpc-native';args=[str(obj) if x==main[0] else x for x in link if not x.startswith('-Wl,-Map=')];args[args.index('-o')+1]=str(binary)
 args+=['-Wl,-Map='+str(binary)+'.map','-Wl,--wrap=mysql_query','-Wl,--wrap=mysql_ping'];invoke(args,'rpc-native-link')
 for pool,schema in schemas.items():
  phase='schema-'+schema;assert sql("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME='"+schema+"'").strip()=='0'
  (d/(schema+'-create.sql')).write_text(plans[schema]);save(schema+'-ddl-audit-before.json',{'schema':schema,'absent_before':True,'plan_sha256':hashlib.sha256(plans[schema].encode()).hexdigest(),'production_mutations':False})
  sql(plans[schema])
  cfg=json.loads(pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json').read_text())
  cfg['mysql'].update({'host':next(v['IPAddress'] for v in mc['NetworkSettings']['Networks'].values() if v.get('IPAddress')),'port':3306,'user':'root','password':menv['MYSQL_ROOT_PASSWORD'],'database':schema,'pool_size':pool})
  cfgpath=private/(schema+'.json');cfgpath.write_text(json.dumps(cfg));os.chmod(cfgpath,0o600)
  env=dict(os.environ);env['TINYIMX_GROUP_DELIVERY_CLAIM_BATCH_ENABLE']='1'
  label='rpc-p'+str(pool);text=invoke([str(binary),str(cfgpath)],label,120,env);(d/(label+'.log')).write_text(text)
  result=json.loads(next(x for x in text.splitlines() if x.startswith('{"checks":')))
  assert result['status']=='GROUP_COMPLETION_RPC_REAL_MYSQL_PASS' and result['checks']==text.count('[PASS]') and result['pool']==pool and '[FAIL]' not in text
  rpccases.append({'schema':schema,**result});save('rpc-native-cases.json',rpccases);print(json.dumps(result),flush=True)
 assert sql(durability).strip()=='1\t1\t1\t0\t0'
 for kind,exe in [('gateway',exeG),('message',exeM)]:
  context=private/(kind+'-context');context.mkdir();shutil.copy2(exe,context/exe.name)
  if kind=='message':shutil.copy2(probe,context/'rpc_readiness_probe')
  for p in context.iterdir():os.chmod(p,0o755)
  (context/'Dockerfile').write_text('FROM '+bases[kind][0]+'\nCOPY --chmod=0755 '+exe.name+(' rpc_readiness_probe' if kind=='message' else '')+' /opt/tinyimx/bin/\nLABEL org.opencontainers.image.revision="'+head+'"\n')
  invoke(['docker','build','--pull=false','--network=none','-t',images[kind],str(context)],kind+'-image',120)
  for check,argv,code in [('ldd',['/usr/bin/ldd','-r','/opt/tinyimx/bin/'+exe.name],0),('missing-config',['/opt/tinyimx/bin/'+exe.name,'/__codex_completion_missing__.json'],1)]:
   name='codex-group-complete-'+kind+'-'+check+'-20261006';assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0
   text=invoke(['docker','run','--name',name,'--label','codex.tinyimx.group_complete=20261006','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges:true',images[kind],*argv],kind+'-'+check,30,expected=code)
   if check=='ldd':assert 'not found' not in text and 'undefined symbol' not in text
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in sources.items())
 info={kind:{'image_tag':images[kind],'image_id':json.loads(run(['docker','image','inspect',images[kind]]))[0]['Id'],'elf_sha256':sha(exe)} for kind,exe in [('gateway',exeG),('message',exeM)]}
 x={'status':'GROUP_COMPLETION_RPC_NATIVE_AND_IMAGES_PASS','head':head,'source_head':'e58cd9a119f7e4aba26a490904a81b73be8f797a','images':info,'coordinator_checks':128,'native_cases':nativecases,'rpc_checks':sum(x['checks'] for x in rpccases),'rpc_cases':rpccases,'protocol_exact_preservation_checks':1,'original_message_unit_pass':True,'inherited_checks_stage':str(frozen),'probe_elf_sha256':sha(probe),'borrowed_artifacts_preserved':True,'durability':'1/1/1/0/0','runtime_deploy':False,'performance_acceptance':False}
 save('summary.json',x);print(json.dumps({k:v for k,v in x.items() if k not in ['rpc_cases','native_cases']},indent=2),flush=True)
except BaseException as error:
 save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'runtime_deploy':False});raise
finally:
 after=runtime();assert before==after;save('runtime-after.json',after)
PY
