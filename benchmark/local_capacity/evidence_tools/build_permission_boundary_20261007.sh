#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,shlex,re,os,signal,shutil,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'permission-boundary-build-20261007';assert not d.exists();sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest();run=lambda a:subprocess.check_output(a,text=True,timeout=30)
source=json.loads((b/'permission-boundary-source-20261007/summary.json').read_text());head=run(['git','rev-parse','HEAD']).strip();assert source['head']==head and all(sha(r/n)==h for n,h in source['files'].items())
hist=json.loads((b/'duration-histogram-source-20261007/summary.json').read_text());assert all(sha(r/n)==h for n,h in hist['files'].items());assert json.loads((b/'duration-histogram-native-20261007/summary.json').read_text())['status']=='DURATION_HISTOGRAM_REAL_SDK_RED_GREEN_AND_RUNTIME_PASS'
cache=r/'build/linux-release';plans_path=cache/'compile_commands.json';plans=json.loads(plans_path.read_text());gateway_plan=b/'group-fanout-wake-build-20261006/gateway-link-process.json';gw=json.loads(gateway_plan.read_text())['argv'];social_plan=cache/'CMakeFiles/social_service_demo.dir/link.txt';social=shlex.split(social_plan.read_text());mains={'gateway':[x for x in gw if x.endswith('gateway_demo.cpp.o')],'social':[x for x in social if x.endswith('social_service_demo.cpp.o')]};assert all(len(v)==1 for v in mains.values())
readiness=b/'rpc-readiness-build-20261006/runtime-private';socialmain=readiness/'social-main.cpp.o';socialserver=readiness/'social-server.cpp.o';validated=json.loads((b/'rpc-readiness-build-20261006-attempt2/reuse-audit-before.json').read_text())['sha256'];assert all(sha(p)==validated[str(p)] for p in [socialmain,socialserver]);assert json.loads((b/'rpc-readiness-build-20261006-attempt2/summary.json').read_text())['native_checks']==97
macro=b/'group-completion-rpc-build-20261006/runtime-private/original-includes';generated=b/'group-completion-rpc-build-20261006/runtime-private/generated/rpc';assert sha(macro/'common/logging/LogMacros.h')=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e'
borrowed={str(p):sha(p) for p in [plans_path,gateway_plan,social_plan,socialmain,socialserver]}
for args in [gw,social]:
 assert '--wrap' not in ' '.join(args)
 for x in args:
  p=pathlib.Path(x) if pathlib.Path(x).is_absolute() else cache/x
  if p.is_file():borrowed[str(p.resolve())]=sha(p)
for root in [macro,generated]:
 for p in root.rglob('*'):
  if p.is_file():borrowed[str(p)]=sha(p)
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs);cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
accepted={'gateway':'sha256:5c2645b1e8512bdd3fe68d4fea229a9418a5e115c9d96d636871639dba41a405','social':'sha256:38dca459e0a88141c1385503b28cbd6e29a1eed18d253f808dc1dfa89e0a0316'}
for role in accepted:assert json.loads(run(['docker','inspect','tinyimx-m21-'+('gateway-a' if role=='gateway' else 'social-service')+'-1']))[0]['Image']==accepted[role]
for role in accepted:assert subprocess.run(['docker','image','inspect','tinyimx/runtime:codex-permission-boundary-'+role+'-v1-20261007'],capture_output=True).returncode!=0
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700);save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10));save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Only new permission numeric defaultOFF TUs, corrected seconds hist Runtime, six pure native/real gRPC localhost test modes and two own runtime images; preserve exact accepted Gateway main/cache/archives and five-service validated Social entrypoints. No deployment, real SQL or pressure during build','head':head,'source_sha256':{**source['files'],**hist['files']},'borrowed_sha256':borrowed,'runtime_before':before,'accepted_base_images':accepted,'limits':'Composed sealed incremental link, not a clean-checkout build; native social tests use fake repository ports with real gRPC transport. Actual permission SQL unchanged and must be measured in real subsequent control.'});phase='initial'
def interrupted(sig,frame):raise RuntimeError('Own native regression interrupted '+str(sig))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def invoke(argv,label,expected=0,timeout=180,env=None):
 global phase
 phase=label;assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text())[1])>2*1024*1024 and shutil.disk_usage(r).free>1024**3;save(label+'-audit-before.json',{'argv':argv,'expected':expected,'timeout':timeout})
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(argv,cwd=cache,stdout=f,stderr=subprocess.STDOUT,start_new_session=True,env=env);proc=pathlib.Path('/proc')/str(p.pid)
  try:ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
  except FileNotFoundError:assert p.poll() is not None;ticks=None
  save(label+'-process.json',{'pid':p.pid,'start_ticks':ticks,'argv':argv})
  try:code=p.wait(timeout=timeout)
  finally:
   if p.poll() is None:
    assert ticks and (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(bytes([0]))[:len(argv)]==[str(x).encode() for x in argv];save(label+'-stop-audit.json',{'pid':p.pid,'start_ticks':ticks,'operation':'Stop verified own native process group only'});os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed; full raw preserved';print(json.dumps({'completed':label,'exit':code}),flush=True);return (private/(label+'.log')).read_text()
def compilefile(n,label,template=None):
 entries=[x for x in plans if x['file']==str(r/(template or n))];assert len(entries)==1,n;args=shlex.split(entries[0]['command']);assert args.count('-o')==args.count('-c')==1;obj=private/(label+'.o');args[args.index('-o')+1]=str(obj);args[args.index('-c')+1]=str(r/n);args[1:1]=['-I'+str(macro),'-I'+str(generated)];args+=['-MD','-MF',str(obj)+'.d'];invoke(args,'compile-'+label);return obj
def linkexe(args,oldmain,mainobj,label,extra):
 argv=[str(mainobj) if x==oldmain else x for x in args if not x.startswith('-Wl,-Map=')];exe=private/label;argv[argv.index('-o')+1]=str(exe);pos=argv.index(str(mainobj))+1;argv[pos:pos]=list(map(str,extra));invoke(argv,'link-'+label);return exe
try:
 runtimeobj=compilefile('common/observability/TelemetryRuntime.cpp','TelemetryRuntime');client=compilefile('services/rpc/SocialRpcClient.cpp','SocialRpcClient');handler=compilefile('services/social/service/SocialServiceImpl.cpp','SocialServiceImpl');repo=compilefile('services/repository/FriendRepository.cpp','FriendRepository');test=compilefile('benchmark/local_capacity/permission_boundary_trace_test.cpp','permission-boundary-test','services/rpc/SocialRpcClient.cpp');native=linkexe(gw,mains['gateway'][0],test,'permission_boundary_tests',[]);cases=[]
 for label,value,enabled in [('absent',None,False),('zero','0',False),('one','1',True),('malformed','01',False),('text','true',False)]:
  env=dict(os.environ);env.pop('TINYIMX_PERMISSION_BOUNDARY_TRACE_ENABLE',None)
  if value is not None:env['TINYIMX_PERMISSION_BOUNDARY_TRACE_ENABLE']=value
  raw=invoke([str(native),'1' if enabled else '0'],'boundary-native-'+label,0,30,env);result=[json.loads(s) for s in raw.splitlines() if s.startswith('{')];assert len(result)==1 and result[0]['status']=='PERMISSION_BOUNDARY_NATIVE_PASS' and result[0]['failures']==0;cases.append({'case':label,**result[0]});save('native-cases.json',cases)
 # Real localhost transport crosses modified client/handler; fixture ports are fake,
 # no SQL/deployment or active endpoint is used by these existing tests.
 regressions={}
 for label,n in [('social-integration','tests/social/social_service_integration_test.cpp'),('social-client','tests/rpc/social_rpc_client_test.cpp'),('social-ownership','tests/rpc/social_ownership_rpc_test.cpp')]:
  obj=compilefile(n,label,'services/rpc/SocialRpcClient.cpp');args=social[:];pos=args.index('libtinyimx_config.a');args[pos:pos]=['libtinyimx_rpc_client.a'];exe=linkexe(args,mains['social'][0],obj,label+'-tests',[client,handler,repo,runtimeobj,socialserver])
  for mode in ['0','1']:
   env=dict(os.environ);env['TINYIMX_PERMISSION_BOUNDARY_TRACE_ENABLE']=mode;log=invoke([str(exe)],label+'-'+mode,0,90,env);assert '[FAIL]' not in log;regressions[label+'-'+mode]={'pass_cases_printed':log.count('[PASS]'),'exit':0};save('regression-cases.json',regressions)
 gwexe=linkexe(gw,mains['gateway'][0],mains['gateway'][0],'gateway_demo',[client,runtimeobj]);sex=linkexe(social,mains['social'][0],socialmain,'social_service_demo',[handler,repo,runtimeobj,socialserver]);images={}
 for role,exe in [('gateway',gwexe),('social',sex)]:
  base=json.loads(run(['docker','image','inspect',accepted[role]]))[0];tags=base.get('RepoTags') or [];assert tags and json.loads(run(['docker','image','inspect',tags[0]]))[0]['Id']==accepted[role]
  tag='tinyimx/runtime:codex-permission-boundary-'+role+'-v1-20261007';context=private/(role+'-context');context.mkdir();shutil.copy2(exe,context/exe.name);(context/'Dockerfile').write_text('FROM '+tags[0]+chr(10)+'COPY --chmod=0555 '+exe.name+' /opt/tinyimx/bin/'+chr(10)+'LABEL codex.tinyimx.permission_boundary.revision="'+head+'"'+chr(10));invoke(['docker','build','--pull=false','--network=none','-t',tag,str(context)],role+'-image',0,120)
  iid=json.loads(run(['docker','image','inspect',tag]))[0]['Id'];images[role]={'image_tag':tag,'image_id':iid,'elf_sha256':sha(exe)}
  for label,args,code in [('loader',['/usr/bin/ldd','-r','/opt/tinyimx/bin/'+exe.name],0),('missing-config',['/opt/tinyimx/bin/'+exe.name,'/__codex_permission_missing__.json'],1)]:
   name='codex-permission-'+role+'-'+label+'-20261007';assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0;log=invoke(['docker','run','--name',name,'--label','codex.tinyimx.permission_boundary=20261007','--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges:true',iid,*args],role+'-'+label,code,30)
   if label=='loader':assert 'not found' not in log and 'undefined symbol' not in log
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in {**hist['files'],**source['files']}.items()) and runtime()==before
 x={'status':'PERMISSION_BOUNDARY_NATIVE_RPC_AND_IMAGES_PASS','head':head,'native_cases':cases,'native_checks':sum(v['checks'] for v in cases),'real_grpc_regressions':regressions,'images':images,'runtime_preserved':True,'configs_preserved':True,'borrowed_preserved':True,'deployment':False,'default_permission_trace':False,'real_sql_and_capacity':'NOT_RUN','latency_gain':'NOT_CLAIMED'};save('summary.json',x);print(json.dumps(x,indent=2))
except BaseException as e:save('failed.json',{'status':'FAIL','phase':phase,'type':type(e).__name__,'message':str(e)});raise
finally:after=runtime();save('runtime-after.json',after);assert after==before
PY
