#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,os,time,signal
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'private-cpu-profiler-bootstrap-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();source=json.loads((b/'private-cpu-profiler-bootstrap-source-20261005/summary.json').read_text());assert head==source['head']
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
ref=json.loads((b/'private-batch-message-deployment-retained-on-20261005/runtime-after.json').read_text());cfgroot=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
def run(args,timeout=20):return subprocess.check_output(args,text=True,timeout=timeout)
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfgroot.glob('*.json')}}
before=runtime();assert before==ref
assert json.loads((b/'private-cpu-profiler-preflight-20261005/summary.json').read_text())['status']=='PRIVATE_CPU_PROFILER_SELF_PREFLIGHT_PASS'
d.mkdir();private=d/'runtime-private';private.mkdir(mode=0o700)
launcher=r/'benchmark/local_capacity/perf_uid_launcher.cpp';elf=d/'perf_uid_launcher';args=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(launcher),'-o',str(elf)]
(d/'compile-audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Compile own diagnostic launcher only','source_sha256':hashlib.sha256(launcher.read_bytes()).hexdigest(),'argv':args,'output':str(elf),'runtime_before':before,'impact':'No existing cache/product binary modifications,180s owncompiler deadline'},indent=2)+'\n')
with (d/'compile.log').open('w') as output:
 p=subprocess.Popen(args,stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
 stat=pathlib.Path('/proc')/str(p.pid)/'stat';ticks=stat.read_text().rsplit(')',1)[1].split()[19]
 (d/'compile-pid.json').write_text(json.dumps({'pid':p.pid,'start_ticks':ticks,'pgid':p.pid,'argv':args})+'\n')
 try:assert p.wait(timeout=180)==0
 except subprocess.TimeoutExpired:
  assert p.poll() is None and stat.read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid;os.killpg(p.pid,signal.SIGTERM);p.wait(timeout=10);raise
assert elf.is_file() and elf.stat().st_size<1024*1024 and 'not found' not in run(['ldd',str(elf)])
settings={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in ['perf_event_paranoid','kptr_restrict']};assert settings=={'perf_event_paranoid':'4','kptr_restrict':'1'}
kernel=run(['uname','-r']).strip();assert kernel=='6.8.0-138-generic';native=pathlib.Path('/usr/lib/linux-tools')/kernel/'perf';assert native.is_file();ldd=run(['ldd',str(native)]);assert 'not found' not in ldd
libs={x for x in re.findall(r'(/[^\s]+)',ldd) if pathlib.Path(x).is_file()};assert len(libs)>=15
mounts=[(elf,'/opt/codex-perf/launcher'),(native,'/opt/codex-perf/perf'),(pathlib.Path('/bin/sh'),'/opt/codex-perf/sh'),(pathlib.Path('/usr/bin/cat'),'/opt/codex-perf/cat')]+[(pathlib.Path(p),p) for p in sorted(libs)]
inputs={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p,_ in mounts};assert all(p.read_bytes()[:4]==b'\x7fELF' for p,_ in mounts)
image='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898';assert json.loads(run(['docker','image','inspect',image]))[0]['Id']==image
base=['docker','create','--pull','never','--read-only','--network','none','--cap-drop','ALL','--security-opt','no-new-privileges:true','--pids-limit','64','--memory','128m','--cpus','1','--ulimit','memlock=8388608:8388608','--log-driver','none','--tmpfs','/tmp:rw,noexec,nosuid,size=16m']
for p,dst in mounts:base+=['--mount','type=bind,src='+str(p.resolve())+',dst='+dst+',readonly']
(d/'ldd.txt').write_text(ldd);(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Bounded own-process sameUID profiler bootstrap drops every privilege before exec','head':head,'kernel':kernel,'tool_version':run([str(native),'version']).strip(),'docker_version':run(['docker','version','--format','{{.Server.Version}}']).strip(),'inputs_sha256':inputs,'mounts':[{'read_only_source':str(p.resolve()),'destination':dst} for p,dst in mounts],'settings_before':settings,'runtime_before':before,'container_policy':'Fresh own b24 image, no-network/readonly/dropALL/default seccomp/nnp/pids64/memory128MiB/CPU1/tmpfs16MiB; initial PERFMON/SETUID/SETGID/SETPCAP only for launcher; all bootstrap bounding caps dropped before UID1000 change; finalE/P/I/B/A ONLYPERFMON, nnp1/seccomp2; no hostPID or write mounts','event':'cpu-clock:u49Hz flat IP, no callstack/memory/rawkernel','self_work':'Own1thread1sec perf futex benchmark, stdout discarded via exact native shell to preserve binary stream','raw':'record -o - stdout to0600 own private0700 file, log-driver none; report stream stdin, zeroCAP symbol aggregate only','stop':'Only exact freshlycreated ownCID/name/image/argv upon20s timeout; retain stopped containers/files','unchanged':'All19/configs/apps/sysctl and production data/CPP/ELFs untouched','acceptance':'Diagnostic selfpreflight only, NOT workload or realprojectCPU'},indent=2)+'\n')
results=[]
def create(label,uid,cap,entry,args,interactive=False):
 name='tinyimx-codex-perf-'+label+'-20261005';assert subprocess.run(['docker','container','inspect',name],capture_output=True).returncode!=0
 cmd=base+['--name',name,'--user',str(uid)]
 if cap:cmd+=['--cap-add','PERFMON','--cap-add','SETUID','--cap-add','SETGID','--cap-add','SETPCAP']
 if interactive:cmd+=['-i']
 cmd+=['--entrypoint',entry,image,*args]
 (d/(label+'-create-audit-before.json')).write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'argv':cmd,'name':name,'uid':uid,'initial_capabilities':['PERFMON','SETUID','SETGID','SETPCAP'] if cap else [],'final_sampler_capabilities':['PERFMON'] if cap else []},indent=2)+'\n')
 cid=run(cmd).strip();c=json.loads(run(['docker','inspect',cid]))[0]
 assert c['Id']==cid and c['Name']=='/'+name and c['Image']==image and c['State']['Status']=='created' and c['Config']['Entrypoint']==[entry] and c['Config']['Cmd']==args
 h=c['HostConfig'];assert h['ReadonlyRootfs'] and h['NetworkMode']=='none' and set(h['CapDrop'])=={'ALL'} and set(h['CapAdd'] or [])==({'CAP_PERFMON','CAP_SETUID','CAP_SETGID','CAP_SETPCAP'} if cap else set())
 assert h['PidMode']=='' and not h['Privileged'] and not h.get('AutoRemove') and h['LogConfig']['Type']=='none' and h['Memory']==128*1024*1024 and h['PidsLimit']==64 and h['NanoCpus']==1000000000
 assert not any(x.get('RW') for x in c['Mounts']) and h['SecurityOpt']==['no-new-privileges:true']
 (d/(label+'-container-identity.json')).write_text(json.dumps({'id':cid,'name':name,'image':image,'created':c['Created'],'entrypoint':[entry],'args':args,'user':c['Config']['User'],'policy':h,'mounts':c['Mounts']},indent=2)+'\n')
 return cid,name
def start(cid,name,stdout,stderr,stdin=None):
 try:
  p=subprocess.run(['docker','start','-ai' if stdin is not None else '-a',cid],stdout=stdout,stderr=stderr,stdin=stdin,timeout=20)
 except subprocess.TimeoutExpired:
  c=json.loads(run(['docker','inspect',cid]))[0];assert c['Id']==cid and c['Name']=='/'+name and c['Image']==image
  (d/(name+'-timeout-audit.json')).write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'action':'Stop exact own timedout profiler','id':cid,'name':name,'created':c['Created'],'pid':c['State']['Pid'],'started':c['State']['StartedAt']},indent=2)+'\n');subprocess.run(['docker','stop','-t','2',cid],check=True,timeout=8);raise
 c=json.loads(run(['docker','inspect',cid]))[0];assert c['Id']==cid and c['Name']=='/'+name and c['Image']==image and c['State']['Status']=='exited';return c['State']['ExitCode']
try:
 for label,uid,cap in [('bootstrap-perf',0,True)]:
  cid,name=create(label,uid,cap,'/opt/codex-perf/launcher',['1000','1000','/opt/codex-perf/perf','record','-B','-N','-e','cpu-clock:u','-F','49','-m','8','-o','-','--','/opt/codex-perf/sh','-c','exec /opt/codex-perf/perf bench futex hash -r 1 -t 1 -s >/dev/null 2>&1'])
  raw=private/(label+'.perf.pipe');err=private/(label+'.stderr')
  with raw.open('wb') as output,err.open('wb') as errors:code=start(cid,name,output,errors)
  result={'case':label,'uid':uid,'cap_perfmon':cap,'exit':code,'container_id':cid,'raw_bytes':raw.stat().st_size,'raw_sha256':hashlib.sha256(raw.read_bytes()).hexdigest(),'stderr_sha256':hashlib.sha256(err.read_bytes()).hexdigest(),'report_complete':False}
  if code==0:
   proof='PROFILER_FINAL uid=1000 gid=1000 effective=0000004000000000 permitted=0000004000000000 inheritable=0000004000000000 bounding=0000004000000000 ambient=0000004000000000 nnp=1 seccomp=2'
   assert err.read_text().count(proof)==1;result['final_capability_proof']=proof
   assert cap and raw.stat().st_size>1000 and raw.read_bytes()[:8]==b'PERFILE2'
   reportcid,reportname=create(label+'-report',0,False,'/opt/codex-perf/perf',['report','-i','-','--stdio','--no-children','--sort','dso,symbol','--show-nr-samples','--percent-limit','1'],True)
   report=d/(label+'-symbol-report.txt')
   with raw.open('rb') as inp,report.open('wb') as output,(private/(label+'-report.stderr')).open('wb') as errors:report_code=start(reportcid,reportname,output,errors,inp)
   assert report_code==0 and report.stat().st_size>200;result.update(report_complete=True,report_container_id=reportcid,report_sha256=hashlib.sha256(report.read_bytes()).hexdigest())
  elif not cap:assert code!=0 and ('permission' in err.read_text().lower() or 'access' in err.read_text().lower())
  results.append(result);assert runtime()==before
 assert len(results)==1 and results[0]['report_complete'] and results[0]['final_capability_proof']
 assert inputs=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p,_ in mounts} and settings=={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in settings}
 x={'status':'PRIVATE_CPU_PROFILER_SAMEUID_BOOTSTRAP_PASS','head':head,'results':results,'all19_runtime_configs_preserved':True,'sysctl_original_4_1_preserved':True,'production_profile':False,'business_load':False,'full_feature_acceptance':False,'limits':'Only selftest; Final sampler UID1000 E/P/I/B/A onlyPERFMON, nnp1/seccomp2 actually verified beforeexec; bootstrap caps cannot regain. Flat samples selfperf and child no stack/private data. Realservice attach remains separately auditable.'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e),'results':results,'all19_runtime_configs_preserved':runtime()==before},indent=2)+'\n');raise
PY
