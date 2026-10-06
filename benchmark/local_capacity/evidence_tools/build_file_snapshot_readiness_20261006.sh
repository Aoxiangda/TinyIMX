#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,shlex,shutil,os,signal,datetime,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'file-snapshot-readiness-build-20261006';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
helper=json.loads((b/'file-snapshot-readiness-build-source-20261006/summary.json').read_text())
head=run(['git','rev-parse','HEAD']).strip();assert head==helper['head'] and all(sha(r/n)==h for n,h in helper['files'].items())
def runtime():

 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return {'identities':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before['identities']==json.loads((b/'file-begin-snapshot-control-20261006-attempt2/restore-summary.json').read_text())['runtime']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
 assert shutil.disk_usage(r).free>2*1024**3
resources();cache=r/'build/linux-release'
snapshot=b/'file-begin-snapshot-build-test-20261006-attempt2';snap=json.loads((snapshot/'summary.json').read_text());sa=json.loads((snapshot/'audit-before.json').read_text())
assert snap['status']=='FILE_SNAPSHOT_REAL_SQL_AND_IMAGE_PASS' and snap['native_total_checks']==150
assert all(sha(r/n)==h for n,h in sa['source_sha256'].items() if not n.startswith('docs/')) and all(sha(p)==h for p,h in sa['borrowed_sha256'].items())
ready=b/'rpc-readiness-build-20261006-attempt2';rd=json.loads((ready/'summary.json').read_text())
assert rd['status']=='RPC_READINESS_FIVE_SERVICE_NATIVE_AND_GROUP_IMAGE_PASS' and rd['native_checks']==97
ra=json.loads((ready/'audit-before.json').read_text());reuse=json.loads((ready/'reuse-audit-before.json').read_text())['sha256']
ready_sources={n:h for n,h in ra['sources'].items() if n in ['examples/file_service_demo.cpp','services/file/server/FileServiceServer.cpp','services/file/server/FileServiceServer.h','common/runtime/RpcReadiness.h','common/runtime/RpcReadinessProbe.h','examples/rpc_readiness_probe.cpp']}
assert len(ready_sources)==6 and all(sha(r/n)==h for n,h in ready_sources.items())
original=b/'rpc-readiness-build-20261006/runtime-private';main=original/'file-main.cpp.o';server=original/'file-server.cpp.o';probe=ready/'runtime-private/rpc_readiness_probe';native=ready/'runtime-private/rpc_readiness_tests'
assert sha(main)==reuse[str(main)] and sha(server)==reuse[str(server)] and sha(probe)==rd['probe_elf_sha256'] and sha(native)==rd['native_elf_sha256']
obj=snapshot/'runtime-private/FileRepositoryAdapter.o';provenance=json.loads((snapshot/'runtime-provenance.json').read_text())
assert sha(obj)==provenance['adapter_obj_sha256'] and provenance['wrappers_absent']
links=shlex.split((cache/'CMakeFiles/file_service_demo.dir/link.txt').read_text());oldmain=[x for x in links if x.endswith('/file_service_demo.cpp.o')];assert len(oldmain)==1
borrowed=dict(sa['borrowed_sha256']);borrowed.update({str(p):sha(p) for p in [main,server,probe,native,obj]})
original_elf='f1b9f84fc297789dc5fa462129c01fc9d252994b499fbd682e3e2db61ca5c1c1'
assert sha(cache/'file_service_demo')==original_elf
base='tinyimx/runtime:m21-final';baseid='sha256:38dca459e0a88141c1385503b28cbd6e29a1eed18d253f808dc1dfa89e0a0316'
assert json.loads(run(['docker','image','inspect',base]))[0]['Id']==baseid
image='tinyimx/runtime:codex-file-snapshot-readiness-v2-20261006';assert subprocess.run(['docker','image','inspect',image],capture_output=True).returncode!=0
probes=['tinyimx-codex-file-ready-ldd-20261006','tinyimx-codex-file-ready-exec-20261006','tinyimx-codex-file-ready-probe-20261006']
assert not set(probes)&set(run(['docker','ps','-a','--format','{{.Names}}']).splitlines())
def settings():
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names -e "$1"','ownfile-buildreadonly','SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count']).strip()
assert settings()=='1\t1\t1\t0\t0'
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Bind unchanged150PASS File snapshotAdapter with97PASS namedready FileMain+FileServer andhealthprobe, originalcached allotherlibs, stopped image only','source_sha256':{**sa['source_sha256'],**ready_sources},'borrowed_sha256':borrowed,'runtime_before':before,'base_image':baseid,'original_elf':original_elf,'writes':'Onlynewown link/ELF/maps/image/probes; no productionAPI/SQLwrites/configchanges or cachedobjectoverwrite','diagnosis':'OFF actual154successful repeat then155 failed beforeRPC; oldZKsessionexpired10:43:32.568,newregistered32.686; request32.600 fellgap. PreviousreadinessGetsuccess wasbeforeactualregistryownership','rollback':'Keep alloriginal/failed objects/schemas/data/stages; noDELETE/DROP/prune/reset/push','performance_acceptance':False})
save('runtime-before.json',before);phase='initial';checks=[]
def interrupted(sig,frame):raise RuntimeError('Ownedfile readinessbuild interrupted '+str(sig))
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
 log=invoke([str(native)],'native-five-service-readiness',60);result=[json.loads(x) for x in log.splitlines() if x.startswith('{')]
 assert result[-1]=={'status':'RPC_READINESS_NATIVE_PASS','checks':97}
 (d/'native-five-service-readiness.log').write_text(log)
 args=[str(main) if x==oldmain[0] else x for x in links];args.insert(next(i for i,x in enumerate(args) if x.endswith('.a')),str(obj));args.insert(args.index(str(obj)),str(server))
 executable=private/'file_service_demo';args[args.index('-o')+1]=str(executable);args+=['-Wl,-Map='+str(executable)+'.map'];invoke(args,'runtime-link')
 symbols=run(['nm','-C',str(executable)],40);assert all(x not in symbols for x in ['__wrap_mysql','__real_mysql']) and 'FileServiceServer::SetReady(' in symbols and 'FileRepositoryAdapter::BeginUpload(' in symbols
 m=pathlib.Path(str(executable)+'.map').read_text();assert all('LOAD '+str(x) in m for x in [main,server,obj])
 save('runtime-provenance.json',{'main_sha256':sha(main),'server_sha256':sha(server),'adapter_obj_sha256':sha(obj),'elf_sha256':sha(executable),'probe_elf_sha256':sha(probe),'unchanged_inputs':borrowed,'wrappers_absent':True})
 context=private/'image-context';context.mkdir();shutil.copy2(executable,context/'file_service_demo');shutil.copy2(probe,context/'rpc_readiness_probe')
 for n in ['file_service_demo','rpc_readiness_probe']:os.chmod(context/n,0o755)
 dockerfile=private/'Dockerfile';dockerfile.write_text('FROM '+base+'\nCOPY --chmod=0755 file_service_demo rpc_readiness_probe /opt/tinyimx/bin/\nLABEL org.opencontainers.image.revision="'+head+'"\n')
 invoke(['docker','build','--pull=false','--network=none','-f',str(dockerfile),'-t',image,str(context)],'seal-image',120)
 common=['docker','run','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges','--label','tinyimx.codex.task=file-readiness-20261006']
 text=invoke(common+['--name',probes[0],'--entrypoint','/bin/sh',image,'-ec','ldd -r /opt/tinyimx/bin/file_service_demo; ldd -r /opt/tinyimx/bin/rpc_readiness_probe'],'uid1000-ldd',20)
 assert not any(v in text.lower() for v in ['not found','undefined symbol'])
 invoke(common+['--name',probes[1],'--entrypoint','/opt/tinyimx/bin/file_service_demo',image,'/tmp/own-file-ready-missing-config.json'],'uid1000-exec',20,expected=1)
 invoke(common+['--name',probes[2],'--entrypoint','/opt/tinyimx/bin/rpc_readiness_probe',image,'127.0.0.1:1'],'uid1000-probe-unreachable',10,expected=1)
 imageinfo=json.loads(run(['docker','image','inspect',image]))[0]
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in ready_sources.items()) and settings()=='1\t1\t1\t0\t0'
 x={'status':'FILE_SNAPSHOT_AND_READINESS_IMAGE_PASS','head':head,'native_total_checks':150,'readiness_native_checks':97,'original_unit_checks':snap['original_unit_checks'],'image_tag':image,'image_id':imageinfo['Id'],'file_elf_sha256':sha(executable),'probe_elf_sha256':sha(probe),'runtime_deploy':False,'performance_acceptance':False,'borrowed_artifacts_preserved':True}
 save('summary.json',x);print(json.dumps(x,indent=2),flush=True)
except BaseException as error:
 save('failed.json',{'status':'FAIL','phase':phase,'type':type(error).__name__,'message':str(error),'runtime_deploy':False});raise
finally:
 after=runtime();assert before==after;save('runtime-after.json',after)
PY
