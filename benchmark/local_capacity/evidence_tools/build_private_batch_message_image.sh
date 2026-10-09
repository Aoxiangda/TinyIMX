#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,shlex,re,datetime,os,signal,shutil
r=pathlib.Path.cwd();b=r/'.local/codex';head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['git','merge-base','--is-ancestor','ed90d8aaf7cc7d8357fabb7c5f36e12f5626e747',head]).returncode==0
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
assert shutil.disk_usage(r).free>1024**3
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
 cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'private-batch-message-image-source-20261005/runtime-after.json').read_text())
def guarded_run(args,label,seconds,env=None,expected_code=0):
 child=None;proc=None;identity=None;code=None;error=None
 (d/(label+'-command.json')).write_text(json.dumps(args,indent=2)+'\n')
 with (d/(label+'.log')).open('w') as f:
  try:
   child=subprocess.Popen(args,cwd=r/'build/linux-release',stdout=f,stderr=subprocess.STDOUT,start_new_session=True,env=env)
   proc=pathlib.Path(f'/proc/{child.pid}')
   try:identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   except FileNotFoundError:assert child.poll() is not None
   (d/(label+'-own-process.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args,'timeout_seconds':seconds})+'\n')
   code=child.wait(timeout=seconds)
  except BaseException as e:error=e
  finally:
   if child is not None and child.poll() is None:
    assert identity is not None and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid
    assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
    (d/(label+'-stop-audit.json')).write_text(json.dumps({'operation':'Stop only own PID/start/argv/PGID verified process group','pid':child.pid})+'\n');os.killpg(child.pid,signal.SIGTERM)
    try:child.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
 if error:raise error
 assert code==expected_code,label+' failed; exact logs and partial artifacts retained'
 return (d/(label+'.log')).read_text()
def interrupted(signum,frame):raise KeyboardInterrupt('Own regression interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)


d=b/'private-batch-message-build-image-20261005';assert not d.exists();d.mkdir()
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
try:
 assert json.loads((b/'private-batch-isolated-mysql-20261005-v3/summary.json').read_text())['status']=='PRIVATE_BATCH_REAL_ISOLATED_MYSQL_PASS'
 assert json.loads((b/'private-batch-postcommit-fault-20261005/summary.json').read_text())['status']=='PRIVATE_BATCH_OWN_POSTCOMMIT_RECOVERY_PASS'
 allowed=subprocess.check_output(['git','diff','--name-only','b32cae49f45e7b1b780cc3b3411c5cb8f4de9116',head,'--','common','services','examples'],text=True).splitlines();assert not allowed
 assert not subprocess.check_output(['git','diff','--name-only','ddc7e8ec9a4c29a14f46b2f3c7970843a1850432',head,'--','examples/message_service_demo.cpp','services/message/service/MessageServiceImpl.h','common/db/MySqlConnectionPool.h','services/message/repository/MessageRepositoryAdapter.h','services/message/application/MessageRepositoryPort.h'],text=True).strip()
 native=json.loads((b/'private-batch-regression-build-20261005-sql-v3/link-explicit-fault-fixture-command.json').read_text())
 names=['MySqlConnection.o','MessageRepository.o','MessageRepositoryAdapter.o','MessageApplicationService.o']
 own=[]
 for n in names:
  found=[x for x in native if x.endswith('/'+n)];assert len(found)==1;own.append(found[0])
 assert own[-1]==str(b/'private-batch-regression-build-20261005-confirm-fix/MessageApplicationService.o')
 cache=r/'build/linux-release';link=shlex.split((cache/'CMakeFiles/message_service_demo.dir/link.txt').read_text());assert link[0]=='/usr/bin/c++'
 mains=[x for x in link if x.endswith('.cpp.o')];assert len(mains)==1 and 'message_service_demo.cpp.o' in mains[0]
 borrowed={str((cache/x).resolve()):hashlib.sha256((cache/x).resolve().read_bytes()).hexdigest() for x in link+own if x.endswith(('.o','.a'))}
 borrowed[str(cache/'message_service_demo')]=hashlib.sha256((cache/'message_service_demo').read_bytes()).hexdigest()
 source_paths=['examples/message_service_demo.cpp','common/db/MySqlConnectionPool.cpp','services/message/service/MessageServiceImpl.cpp','common/db/MySqlConnection.cpp','services/repository/MessageRepository.cpp','services/message/repository/MessageRepositoryAdapter.cpp','services/message/application/MessageApplicationService.cpp']
 source_sha={p:hashlib.sha256((r/p).read_bytes()).hexdigest() for p in source_paths}
 base='tinyimx/runtime:codex-private-single-lease-v1';baseid='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898'
 assert json.loads(subprocess.check_output(['docker','image','inspect',base],text=True))[0]['Id']==baseid
 image='tinyimx/runtime:codex-private-begin-insert-read-batch-v1';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
 probes=['tinyimx-codex-private-batch-ldd-20261005','tinyimx-codex-private-batch-exec-20261005'];allnames=subprocess.check_output(['docker','ps','-a','--format','{{.Names}}'],text=True).splitlines();assert not set(probes)&set(allnames)
 (d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Build full own MessageService from SHA-verified objects and three own current translation units; seal unique image over pinned b24','head':head,'own_object_paths':own,'borrowed_sha256':borrowed,'source_sha256':source_sha,'base':baseid,'new_image':image,'probes':probes,'flags':'Actual cached tinyimx_db/tinyimx_message_grpc/message_service_demo flags, no configure/cache writes','test_wrapper':'Not linked, no --wrap argument','durable_semantics':'Retain PING, fullidentity beforeoutbox/commit, read-first receiver and original RPCsettings; flag defaultOFF','writes':'Own .o/ELF/map/context/image/stopped probes only; no runtime configs/cache/other apps edits','rollback':'Keep base/current runtime and all artifacts; no prune/delete/reset/push'},indent=2)+'\n')
 def flags(target):
  text=(cache/('CMakeFiles/'+target+'.dir/flags.make')).read_text();(d/(target+'-flags.make')).write_text(text);values=[]
  for k in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:values+=shlex.split(next(x for x in text.splitlines() if x.startswith(k+' =')).split('=',1)[1])
  return values
 more=[]
 for source,target,label in [('examples/message_service_demo.cpp','message_service_demo','demo-main'),('common/db/MySqlConnectionPool.cpp','tinyimx_db','mysql-pool'),('services/message/service/MessageServiceImpl.cpp','tinyimx_message_grpc','service-impl')]:
  obj=d/(label+'.o');guarded_run(['/usr/bin/c++',*flags(target),'-c',str(r/source),'-o',str(obj)],'compile-'+label,180);more.append(str(obj))
 link[link.index(mains[0])]=more[0];link[link.index('-o')+1]=str(d/'message_service_demo')
 firstlib=next(i for i,x in enumerate(link) if x.endswith('.a'));link[firstlib:firstlib]=own+more[1:]+['-Wl,-Map='+str(d/'link.map')]
 assert not any('--wrap' in x for x in link)
 guarded_run(link,'link-full-message-service',180)
 symbols=subprocess.check_output(['nm','-C',str(d/'message_service_demo')],text=True,timeout=25)
 assert 'MySqlConnection::BeginInsertAndQuery(' in symbols and '__wrap_mysql_commit' not in symbols and '__real_mysql_commit' not in symbols
 mapping=(d/'link.map').read_text();assert all('LOAD '+p in mapping for p in own+more)
 (d/'symbol-provenance.json').write_text(json.dumps({'owns_loaded':own+more,'batch_method_present':True,'native_fault_wrapper_absent':True,'link_map_sha256':hashlib.sha256((d/'link.map').read_bytes()).hexdigest(),'link_map_size':(d/'link.map').stat().st_size},indent=2)+'\n')
 binary=d/'message_service_demo';h=hashlib.sha256(binary.read_bytes()).hexdigest();context=d/'image-context';context.mkdir();shutil.copy2(binary,context/'message_service_demo');os.chmod(context/'message_service_demo',0o755)
 dockerfile=d/'Dockerfile';dockerfile.write_text('FROM '+base+'\nCOPY --chmod=0755 message_service_demo /opt/tinyimx/bin/message_service_demo\nLABEL org.opencontainers.image.revision="'+head+'"\nLABEL tinyimx.binary.sha256="'+h+'"\n')
 guarded_run(['docker','build','--pull=false','--network=none','-f',str(dockerfile),'-t',image,str(context)],'seal-image',120)
 common=['docker','run','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges','--label','tinyimx.codex.task=private-batch-message-20261005']
 text=guarded_run(common+['--name',probes[0],'--entrypoint','/bin/sh',image,'-ec','test -x "$1"; ldd -r "$1"','owned-loader-check','/opt/tinyimx/bin/message_service_demo'],'uid1000-ldd',20)
 assert not any(x in text.lower() for x in ['not found','undefined symbol'])
 guarded_run(common+['--name',probes[1],'--entrypoint','/opt/tinyimx/bin/message_service_demo',image,'/tmp/codex-private-batch-owned-nonexistent-config.json'],'uid1000-exec',20,expected_code=1)
 c=json.loads(subprocess.check_output(['docker','image','inspect',image],text=True))[0];assert c['Config']['Labels']['org.opencontainers.image.revision']==head
 assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==v for p,v in borrowed.items())
 assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==v for p,v in source_sha.items())
 x={'status':'PRIVATE_BATCH_FULL_MESSAGE_SEALED_IMAGE_PASS','head':head,'image_tag':image,'image_id':c['Id'],'binary_relative_path':str(binary.relative_to(r)),'binary_sha256':h,'object_provenance':{'driver_repository_adapter':'ed90d8a','application':'b32cae4','pool_service_main':head},'unit_SQL_evidence':420,'own_postcommit_recovery_checks':8,'borrowed_artifacts_preserved':True,'wrapper_absent':True,'uid1000_loader':'PASS','uid1000_executable':'PASS expected missingconfig1','runtime_deploy':False,'performance_acceptance':False}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e)})+'\n');raise
finally:
 after=runtime();assert before==after;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
PY
