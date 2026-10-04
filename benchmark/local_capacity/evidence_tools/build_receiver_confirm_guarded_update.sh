#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,shutil,re,os,signal,tarfile,shlex
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'receiver-confirm-guarded-update-build-20261005';assert not d.exists()
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();source=json.loads((b/'receiver-confirm-guarded-update-source-20261005/summary.json').read_text());assert source['head']==head
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def runtime():
 names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'receiver-confirm-guarded-update-source-20261005/runtime-after.json').read_text())
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
build=r/'build/linux-release';d.mkdir();targets=['message_service_demo','message_outbox_integration_tests','message_application_service_tests','m16_crash_window_contract_tests','message_service_integration_tests','unread_snapshot_aggregate_tests']
server_source=r/'services/message/server/MessageServiceServer.cpp';assert 'SetSyncServerOption' not in server_source.read_text()
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Own original adapter/new test red binary, cached singlejob Message+tests green build; no deployment','writes':'Exact hostbuild binary backups and CMake outputs/ownred objects plus logs; no source modification, dependency installation or Gateway service build','targets':targets,'red':'Extract original adapter aff68 sourcebefore, force its ownobject before libtinyimx_message_core.a; run later only newownedred schema','timeouts':'Eachownmanualcompile/link180s, CMake600s, unit90s, ownPID/starttime/cmdline/PGID only','original_sync_server_source_sha256':hashlib.sha256(server_source.read_bytes()).hexdigest(),'runtime_changes':False,'acceptance':False,'rollback':'Running b24 and exact currenthost binaries retained as.before; no delete/reset/push'},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');(d/'guest-memory-before.txt').write_text(pathlib.Path('/proc/meminfo').read_text());old_binaries={}
for name in targets:
 p=build/name
 if p.exists():old_binaries[name]=hashlib.sha256(p.read_bytes()).hexdigest();shutil.copy2(p,d/(name+'.before'))
(d/'host-binary-before-sha256.json').write_text(json.dumps(old_binaries,indent=2)+'\n')
def interrupted(signum,frame):raise KeyboardInterrupt('Own guarded confirmation build interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def owned(args,label,timeout,cwd=build):
 (d/(label+'-command.json')).write_text(json.dumps(args,indent=2)+'\n');child=None;identity=None;code=None
 try:
  with (d/(label+'.log')).open('w') as out:
   child=subprocess.Popen(args,cwd=cwd,stdout=out,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path(f'/proc/{child.pid}');identity=proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]
   (d/(label+'-own-process.json')).write_text(json.dumps({'pid':child.pid,'pgid':child.pid,'starttime_ticks':identity,'argv':args})+'\n');code=child.wait(timeout=timeout)
 finally:
  if child is not None and child.poll() is None:
   assert identity is not None and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity and os.getpgid(child.pid)==child.pid
   assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:len(args)]==[x.encode() for x in args]
   (d/(label+'-stop-audit.json')).write_text(json.dumps({'operation':'Stoponlyownverifiedbuild/test processgroup','pid':child.pid,'identity_verified':True})+'\n');os.killpg(child.pid,signal.SIGTERM)
   try:child.wait(timeout=3)
   except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait(timeout=3)
 return code
def flags(target):
 text=(build/'CMakeFiles'/f'{target}.dir/flags.make').read_text();values=[]
 for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:values+=shlex.split(next(line for line in text.splitlines() if line.startswith(key+' =')).split('=',1)[1])
 return values
checks={}
try:
 oldcpp=d/'original-MessageRepositoryAdapter.cpp'
 with tarfile.open(b/'receiver-confirm-guarded-update-source-20261005/source-before.tar.gz') as archive:oldcpp.write_bytes(archive.extractfile('services/message/repository/MessageRepositoryAdapter.cpp').read())
 assert hashlib.sha256(oldcpp.read_bytes()).hexdigest()=='aff68b77efba9d18efb5c90e4cfe49c06acaa3571559c17912339ec909e4d782'
 assert owned(['/usr/bin/c++',*flags('tinyimx_message_core'),'-c',str(oldcpp),'-o',str(d/'old-adapter.o')],'red-old-adapter-compile',180)==0
 assert owned(['/usr/bin/c++',*flags('message_outbox_integration_tests'),'-c',str(r/'tests/outbox/message_outbox_integration_test.cpp'),'-o',str(d/'new-test.o')],'red-new-test-compile',180)==0
 link=shlex.split((build/'CMakeFiles/message_outbox_integration_tests.dir/link.txt').read_text());old='CMakeFiles/message_outbox_integration_tests.dir/tests/outbox/message_outbox_integration_test.cpp.o';assert link.count(old)==1
 link[link.index(old)]=str(d/'new-test.o');link[link.index('-o')+1]=str(d/'original_adapter_new_tests');link.insert(link.index('libtinyimx_message_core.a'),str(d/'old-adapter.o'))
 assert owned(link,'red-link',180)==0
 assert 'VCPKG_MANIFEST_INSTALL:BOOL=OFF' in (build/'CMakeCache.txt').read_text(),'Refuse dependency installation'
 assert owned(['/usr/bin/cmake','--build',str(build),'--target',*targets,'--parallel','1'],'green-build',600)==0
 server_object=build/'CMakeFiles/tinyimx_message_grpc.dir/services/message/server/MessageServiceServer.cpp.o';assert server_object.is_file()
 nm=subprocess.check_output(['/usr/bin/nm','-u',str(server_object)],text=True);(d/'sync-server-undefined-symbols.txt').write_text(nm);assert 'SetSyncServerOption' not in nm,'Reject cached MAX16 syncserver object'
 for name in ['message_application_service_tests','m16_crash_window_contract_tests','message_service_integration_tests']:
  code=owned([str(build/name)],name,90,cwd=r);text=(d/(name+'.log')).read_text();passes=text.count('[PASS]')+sum(line.startswith('PASS ') for line in text.splitlines());fails=text.count('[FAIL]')+sum(line.startswith('FAIL ') for line in text.splitlines())
  checks[name]={'exit_code':code,'pass':passes,'fail':fails};assert code==0 and passes>0 and fails==0,name+' failed'
 after=runtime();assert after==before
 x={'status':'GUARDED_CONFIRM_BUILD_UNIT_PASS','compiled_head':head,'checks':checks,'message_binary_sha256':hashlib.sha256((build/'message_service_demo').read_bytes()).hexdigest(),'test_binary_sha256':hashlib.sha256((build/'message_outbox_integration_tests').read_bytes()).hexdigest(),'red_binary_sha256':hashlib.sha256((d/'original_adapter_new_tests').read_bytes()).hexdigest(),'sync_server_original_default_source_compiled':True,'all19_runtime_configs_preserved':True,'real_mysql_integration':'NOT_RUN','runtime_deployment':False}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as error:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(error).__name__,'message':str(error),'checks':checks})+'\n');raise
finally:
 after=runtime();assert after==before;(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
PY
