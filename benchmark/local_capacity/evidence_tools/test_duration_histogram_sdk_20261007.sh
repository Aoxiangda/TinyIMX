#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,shlex,re,os,signal,shutil,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'duration-histogram-native-20261007';assert not d.exists();sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest();run=lambda a:subprocess.check_output(a,text=True,timeout=30)
source=json.loads((b/'duration-histogram-source-20261007/summary.json').read_text());head=run(['git','rev-parse','HEAD']).strip();assert head==source['head'] and all(sha(r/n)==h for n,h in source['files'].items())
cache=r/'build/linux-release';flagsfile=cache/'CMakeFiles/tinyimx_observability.dir/flags.make';linkfile=cache/'CMakeFiles/m20_observability_runtime_tests.dir/link.txt';link=shlex.split(linkfile.read_text());main=[x for x in link if x.endswith('observability_runtime_test.cpp.o')];assert len(main)==1
borrowed={str(p):sha(p) for p in [flagsfile,linkfile]};borrowed.update({str((cache/x).resolve()):sha((cache/x).resolve()) for x in link if (cache/x).is_file()})
macro=b/'group-completion-rpc-build-20261006/runtime-private/original-includes';assert sha(macro/'common/logging/LogMacros.h')=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e';borrowed[str(macro/'common/logging/LogMacros.h')]=sha(macro/'common/logging/LogMacros.h')
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));assert all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs);cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700);save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10));save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'New native objects/ELFs only; real installed SDK MeterProvider/Reader collection original-default RED vs seconds-bucket GREEN; compile modified Runtime and run existing shutdown/unavailable-collector/threadpool regression; no Docker deployment, pressure, or external collector use','runtime_before':before,'borrowed_sha256':borrowed,'source_sha256':source['files']});phase='initial'
def interrupted(sig,frame):raise RuntimeError('Own native regression interrupted '+str(sig))
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def invoke(argv,label,expected=0,timeout=180):
 global phase
 phase=label;assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text())[1])>2*1024*1024 and shutil.disk_usage(r).free>1024**3;save(label+'-audit-before.json',{'argv':argv,'expected':expected,'timeout':timeout})
 with (private/(label+'.log')).open('w') as f:
  p=subprocess.Popen(argv,cwd=cache,stdout=f,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path('/proc')/str(p.pid)
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
flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(s for s in flagsfile.read_text().splitlines() if s.startswith(key+' =')).split('=',1)[1])
def compilefile(n,label):
 obj=private/(label+'.o');invoke(['/usr/bin/c++','-I'+str(macro),*flags,'-c',str(r/n),'-o',str(obj)],'compile-'+label);return obj
def linkexe(obj,label,extra=None):
 exe=private/label;args=[str(obj) if x==main[0] else x for x in link];args[args.index('-o')+1]=str(exe)
 if extra:args.insert(args.index(str(obj))+1,str(extra))
 invoke(args,'link-'+label);return exe
try:
 test=compilefile('benchmark/local_capacity/duration_histogram_sdk_test.cpp','sdk-test');exe=linkexe(test,'duration_histogram_sdk_tests');red=invoke([str(exe),'--legacy'],'real-sdk-original-default-red',1,30);green=invoke([str(exe)],'real-sdk-seconds-buckets-green',0,30);assert '[FAIL]' in red and '[FAIL]' not in green and '[PASS]' in green
 runtimeobj=compilefile('common/observability/TelemetryRuntime.cpp','TelemetryRuntime');oldtest=compilefile('tests/observability/observability_runtime_test.cpp','existing-runtime-test');native=linkexe(oldtest,'observability_runtime_tests',runtimeobj);log=invoke([str(native)],'existing-runtime-regression',0,30);assert 'M20_OBSERVABILITY_RUNTIME_TESTS=PASS' in log
 assert all(sha(p)==h for p,h in borrowed.items()) and all(sha(r/n)==h for n,h in source['files'].items()) and runtime()==before
 x={'status':'DURATION_HISTOGRAM_REAL_SDK_RED_GREEN_AND_RUNTIME_PASS','head':head,'red_checks':red.count('[PASS]')+red.count('[FAIL]'),'red_detected_failures':red.count('[FAIL]'),'green_checks':green.count('[PASS]'),'green_failures':green.count('[FAIL]'),'existing_runtime_regression':'PASS','native_elf_sha256':sha(exe),'runtime_regression_elf_sha256':sha(native),'runtime_deployment':False,'runtime_preserved':True,'borrowed_preserved':True,'capacity_or_latency_gain':'NOT_CLAIMED','bucket_rollout':'Not yet applied to running processes; changing buckets requires new service process and resets; cross-version quantiles not comparable without new control'};save('summary.json',x);print(json.dumps(x,indent=2))
except BaseException as e:save('failed.json',{'status':'FAIL','phase':phase,'type':type(e).__name__,'message':str(e)});raise
finally:after=runtime();save('runtime-after.json',after);assert after==before
PY
