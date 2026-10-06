#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime,shlex,shutil,os,signal,time,re,sys
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'rpc-readiness-build-20261006-attempt2';assert not d.exists()
def run(a,t=30):return subprocess.check_output(a,text=True,timeout=t)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
source=json.loads((b/'rpc-readiness-source-20261006/summary.json').read_text());helper=json.loads((b/'rpc-readiness-build-repair-source-20261006/summary.json').read_text())
assert run(['git','rev-parse','HEAD']).strip()==helper['head'] and all(sha(r/n)==h for n,h in source['files'].items() if n not in helper['files']) and all(sha(r/n)==h for n,h in helper['files'].items())
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 return {c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
before=runtime();assert before==json.loads((b/'group-endpoint-unavailable-analysis-20261006/summary.json').read_text())['whole_runtime_identity']
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');cfgsha={p.name:sha(p) for p in cfg.glob('*.json')}
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
build=r/'build/linux-release';plans=json.loads((build/'compile_commands.json').read_text());linkpath=build/'CMakeFiles/group_service_demo.dir/link.txt';link=shlex.split(linkpath.read_text())
borrow={str(linkpath):sha(linkpath),str(build/'compile_commands.json'):sha(build/'compile_commands.json')}
for v in link:
 if (build/v).is_file():borrow[str((build/v).resolve())]=sha((build/v).resolve())
core=b/'group-actor-snapshot-build-20261006/runtime-private'
frozen=json.loads((b/'group-actor-snapshot-image-20261006-attempt2/audit-before.json').read_text())['freeze_before_reuse_sha256']
assert all(sha(p)==h for p,h in frozen.items())
for p,h in frozen.items():borrow[p]=h
macro=subprocess.check_output(['git','show','30452a51d08561593a6aba2fbe1ef188ff06d1c7:common/logging/LogMacros.h']);assert hashlib.sha256(macro).hexdigest()=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
base='tinyimx/runtime:m21-final';baseid='sha256:38dca459e0a88141c1385503b28cbd6e29a1eed18d253f808dc1dfa89e0a0316';assert json.loads(run(['docker','image','inspect',base]))[0]['Id']==baseid
tag='tinyimx/runtime:codex-group-readiness-v2-20261006';assert subprocess.run(['docker','image','inspect',tag],capture_output=True).returncode!=0
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Isolated 5RPC entrypoints+5servers verifiedobjectreuse + real 5service generated-interface health protocol native gate, Group-only newELF+probe image; do not deploy','head':helper['head'],'source_head':source['head'],'sources':source['files'],'borrowed_sha256':borrow,'runtime_before':before,'config_sha256':cfgsha,'writes':'Newprivate objects/copiedGroupgrpc archive/linkmaps/test/probe/ownimage; retain all artifacts','preserve':'Eageroriginal logging overlay, GroupJOIN original accepted native archives only; User/Message/others main compile only, do not deploy unbound old cache','rollback':'No activeinstance changes; stop only own identified childgroup ontimeout; no deletion','performance_acceptance':False})
phase='initial'
original=b/'rpc-readiness-build-20261006'
assert json.loads((original/'failed.json').read_text())['phase']=='native-five-service-readiness'
reuse={str(p):sha(p) for p in (original/'runtime-private').iterdir() if p.is_file() and (p.name.endswith('.cpp.o') or p.name=='libtinyimx_group_grpc.a')}
assert len(reuse)==14
borrow.update(reuse)
save('reuse-audit-before.json',{'operation':'Freeze first compile success before test-fixture-only repair reuse','sha256':reuse,'original_failure_retained':True,'runtime_before':before})
def fail(k,e,t):save('failed.json',{'status':'FAIL','phase':phase,'type':k.__name__,'message':str(e),'evidence_retained':True,'runtime':runtime()});sys.__excepthook__(k,e,t)
sys.excepthook=fail
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
 assert shutil.disk_usage(r).free>2*1024**3
def invoke(a,label,timeout=240,expected=0):
 global phase
 phase=label;resources();save(label+'-audit-before.json',{'operation':'Onlyown compiler/link/test/docker builder child','argv':a,'timeout':timeout})
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(a,cwd=build,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
  proc=pathlib.Path('/proc')/str(p.pid);ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19];save(label+'-process.json',{'pid':p.pid,'start_ticks':ticks,'argv':a})
  try:code=p.wait(timeout=timeout)
  finally:
   if p.poll() is None:
    assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(a)]==[str(x).encode() for x in a]
    save(label+'-stop-audit-before.json',{'operation':'Stoponlyverifiedownchildgroup','pid':p.pid});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed; privateownlog retained'
 print(json.dumps({'completed':label,'exit':code}),flush=True)
 return (private/(label+'.log')).read_text()
overlay=private/'original-includes/common/logging';overlay.mkdir(parents=True);(overlay/'LogMacros.h').write_bytes(macro)
def compilefile(n,label,template=None):
 entries=[x for x in plans if x['file']==str(r/(template or n))];assert len(entries)==1
 args=shlex.split(entries[0]['command']);assert args.count('-o')==1 and args.count('-c')==1
 obj=private/(label+'.cpp.o');args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=str(r/n)
 args.insert(1,'-I'+str(overlay.parents[1]));invoke(args,'compile-'+label);return obj
servers={dom:original/'runtime-private'/(dom+'-server.cpp.o') for dom in ['user','social','message','group','file']}
mains={dom:original/'runtime-private'/(dom+'-main.cpp.o') for dom in servers}
assert all(sha(p)==reuse[str(p)] for p in [*servers.values(),*mains.values()])
testobj=compilefile('tests/rpc/rpc_readiness_test.cpp','readiness-test','examples/group_service_demo.cpp')
probeobj=original/'runtime-private/readiness-probe.cpp.o';assert sha(probeobj)==reuse[str(probeobj)]
groupgrpc=private/'libtinyimx_group_grpc.a';shutil.copy2(original/'runtime-private/libtinyimx_group_grpc.a',groupgrpc)
assert sha(groupgrpc)==reuse[str(original/'runtime-private/libtinyimx_group_grpc.a')]
main=[x for x in link if x.endswith('examples/group_service_demo.cpp.o')];assert len(main)==1
def linkfile(obj,exe,label,extra=None,group=True):
 args=[str(obj) if x==main[0] else str(groupgrpc) if x=='libtinyimx_group_grpc.a' and group else str(core/x) if x in ['libtinyimx_group_core.a','libtinyimx_repository.a'] else x for x in link]
 args[args.index('-o')+1]=str(exe)
 if extra:
  args[args.index(str(obj))+1:args.index(str(obj))+1]=list(map(str,extra))
 args+=['-Wl,-Map='+str(exe)+'.map'];invoke(args,label)
native=private/'rpc_readiness_tests';linkfile(testobj,native,'native-link',extra=list(servers.values()),group=False)
log=invoke([str(native)],'native-five-service-readiness',60)
res=[json.loads(x) for x in log.splitlines() if x.startswith('{')]
assert res[-1]=={'status':'RPC_READINESS_NATIVE_PASS','checks':97} and {v['domain'] for v in res[:-1]}=={'user','social','message','group','file'} and all(v['status']=='PASS' for v in res[:-1]);save('native-result.json',res)
exe=private/'group_service_demo';linkfile(mains['group'],exe,'group-link')
probe=private/'rpc_readiness_probe';linkfile(probeobj,probe,'probe-link',group=False)
context=private/'image-context';context.mkdir();shutil.copy2(exe,context/'group_service_demo');shutil.copy2(probe,context/'rpc_readiness_probe')
(context/'Dockerfile').write_text('FROM '+base+'\nCOPY --chmod=0555 group_service_demo rpc_readiness_probe /opt/tinyimx/bin/\nLABEL codex.tinyimx.rpc_readiness.revision="'+helper['head']+'"\n')
invoke(['docker','build','--network','none','--pull=false','-t',tag,str(context)],'group-image',180)
iid=json.loads(run(['docker','image','inspect',tag]))[0]['Id'];assert json.loads(run(['docker','image','inspect',base]))[0]['Id']==baseid
for name,argv,wanted in [('loader',['/usr/bin/ldd','/opt/tinyimx/bin/group_service_demo'],0),('probe-loader',['/usr/bin/ldd','/opt/tinyimx/bin/rpc_readiness_probe'],0),('missing-config',['/opt/tinyimx/bin/group_service_demo','/__codex_rpc_readiness_missing__.json'],1),('probe-unreachable',['/opt/tinyimx/bin/rpc_readiness_probe','127.0.0.1:1'],1)]:
 own='codex-rpc-readiness-v2-'+name+'-20261006';assert subprocess.run(['docker','inspect',own],capture_output=True).returncode!=0
 args=['docker','run','--name',own,'--label','codex.tinyimx.rpc_readiness=20261006','--network','none','--read-only','--cap-drop','ALL','--security-opt','no-new-privileges:true','--user','1000:1000','--pids-limit','96','--memory','512m','--cpus','1','--tmpfs','/tmp:rw,noexec,nosuid,size=16m',iid,*argv]
 out=invoke(args,'image-'+name,60,wanted);c=json.loads(run(['docker','inspect',own]))[0];assert not c['State']['Running'] and c['State']['ExitCode']==wanted and c['Image']==iid and c['Config']['Labels'].get('codex.tinyimx.rpc_readiness')=='20261006'
 if 'loader' in name:assert 'not found' not in out
assert runtime()==before and {p.name:sha(p) for p in cfg.glob('*.json')}==cfgsha and all(sha(p)==h for p,h in borrow.items())
x={'status':'RPC_READINESS_FIVE_SERVICE_NATIVE_AND_GROUP_IMAGE_PASS','head':helper['head'],'source_head':source['head'],'native_checks':97,'compiled_five_entrypoints':True,'compiled_five_servers':True,'group_image_tag':tag,'group_image_id':iid,'group_elf_sha256':sha(exe),'probe_elf_sha256':sha(probe),'native_elf_sha256':sha(native),'runtime_preserved':True,'configs_preserved':True,'borrowed_preserved':True,'snapshot_read_archives_retained':True,'deployment':False,'performance_acceptance':False}
save('summary.json',x);print(json.dumps(x,indent=2))
PY
