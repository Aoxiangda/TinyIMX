#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime,re,sys,os,signal,shlex,shutil
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'lazy-logging-regression-20261005';assert not d.exists()
def run(a,timeout=20):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
head=run(['git','rev-parse','HEAD']).strip();assert head==json.loads((b/'lazy-logging-source-20261005/summary.json').read_text())['head']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024 and shutil.disk_usage(r).free>3*1024**3
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'private-batch-message-deployment-retained-on-20261005/runtime-after.json').read_text())
settings={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in ['perf_event_paranoid','kptr_restrict']};assert settings=={'perf_event_paranoid':'4','kptr_restrict':'1'}
clockpath=pathlib.Path('/sys/devices/system/clocksource/clocksource0/current_clocksource');assert clockpath.read_text().strip()=='tsc'
image='sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060';assert json.loads(run(['docker','image','inspect',image]))[0]['Id']==image
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def failed(k,e,t):
 (d/'helper-failed.json').write_text(json.dumps({'status':'FAIL','type':k.__name__,'message':str(e),'head':head,'deployment':False})+'\n');sys.__excepthook__(k,e,t)
sys.excepthook=failed
build=r/'build/linux-release';cpp=r/'benchmark/local_capacity/lazy_logging_regression.cpp';fp=build/'CMakeFiles/mysql_pool_demo.dir/flags.make';lp=build/'CMakeFiles/mysql_pool_demo.dir/link.txt'
flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in fp.read_text().splitlines() if x.startswith(key+' =')).split('=',1)[1])
binary=d/'lazy_logging_regression';link=shlex.split(lp.read_text());old='CMakeFiles/mysql_pool_demo.dir/examples/mysql_pool_demo.cpp.o';assert link.count(old)==1;link[link.index(old)]=str(d/'test.o');link[link.index('-o')+1]=str(binary)
allowed={'libtinyimx_logging.a','libtinyimx_config.a','libtinyimx_observability.a'};removed=[x for x in link if x.startswith('libtinyimx_') and x not in allowed];link=[x for x in link if x not in removed];assert 'libtinyimx_logging.a' in link and 'libtinyimx_observability.a' in link and not any('--wrap' in x for x in link)
paths=[fp,lp,cpp,r/'common/logging/Logger.h',r/'common/logging/LogMacros.h']+[(build/x).resolve() for x in link if x.endswith(('.a','.so')) and (build/x).is_file()];inputs={str(p.resolve()):sha(p) for p in paths}
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'inputs':inputs,'operation':'Compile own native regression using current macro/template headers, cached unchanged Logger ABI and formatter libs. Never product runtime or cached objects writes.','cases':'Semantic checks and own same-ELF eager/lazy ABBA 100k discarded records each for scalar/512B text, process CPU and wall means. Not business P99.','filtered_libs':removed,'writes':'Fresh stage objects/ELF; own new log file in new runtime-private/case only, no rotation expected <100MiB; no config/data access','limits':'No actual product deployment or all-feature acceptance; emitted log/direct API/threshold-change check retained, other apps preserved'},indent=2)+'\n')
def compile_call(argv,label):
 (d/(label+'-audit-before.json')).write_text(json.dumps({'argv':argv,'timeout':180,'outputs':'FreshownELF/object only'})+'\n')
 with (private/(label+'.log')).open('wb') as out:
  p=subprocess.Popen(argv,cwd=build,stdout=out,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path('/proc')/str(p.pid);ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19];(d/(label+'-process.json')).write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'start_ticks':ticks,'argv':argv})+'\n')
  try:code=p.wait(timeout=180)
  finally:
   if p.poll() is None:
    assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(argv)]==[x.encode() for x in argv];(d/(label+'-stop-audit-before.json')).write_text(json.dumps({'pid':p.pid,'start_ticks':ticks,'action':'Stoponlyownverifiedcompilegroup'})+'\n');os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:
     assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid;os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==0,label+' failed, privatecompilelog retained'

compile_call(['/usr/bin/c++',*flags,'-c',str(cpp),'-o',str(d/'test.o')],'compile-test');compile_call(link,'link-test')
assert binary.read_bytes()[:4]==b'\x7fELF' and 'not found' not in run(['ldd',str(binary)])
(d/'chmod-audit-before.json').write_text(json.dumps({'path':str(binary),'sha256':sha(binary),'before':oct(binary.stat().st_mode&0o777),'after':'0o555','scope':'Only new own ELF'})+'\n');binary.chmod(0o555)
def invoke(exe,label,args,mounts,mem,pids,timeout):
 assert runtime()==before
 name='tinyimx-codex-lazy-logging-'+label+'-20261005';assert subprocess.run(['docker','container','inspect',name],capture_output=True).returncode!=0
 entry='/opt/codex/probe';allmounts=[(str(exe.resolve()),entry,False),*mounts]
 cmd=['docker','create','--pull','never','--name',name,'--user','1000:1000','--read-only','--network','none','--cap-drop','ALL','--security-opt','no-new-privileges:true','--pids-limit',str(pids),'--memory',str(mem)+'m','--cpus','1','--log-driver','none']
 for source,dst,rw in allmounts:cmd+=['--mount','type=bind,src='+source+',dst='+dst+('' if rw else ',readonly')]
 cmd+=['--entrypoint',entry,image,*args];(d/(label+'-create-audit-before.json')).write_text(json.dumps({'argv':cmd,'timeout':timeout,'onlyowned':True,'config_SQL_domain':False},indent=2)+'\n');cid=run(cmd).strip();c=json.loads(run(['docker','inspect',cid]))[0];h=c['HostConfig']
 assert c['Name']=='/'+name and c['Image']==image and c['Config']['User']=='1000:1000' and c['Config']['Entrypoint']==[entry] and c['Config']['Cmd']==(args or None) and c['State']['Status']=='created'
 assert h['ReadonlyRootfs'] and h['NetworkMode']=='none' and set(h['CapDrop'])=={'ALL'} and not h['CapAdd'] and h['PidMode']=='' and not h['Privileged'] and h['SecurityOpt']==['no-new-privileges:true'] and h['LogConfig']['Type']=='none' and h['Memory']==mem*1024*1024 and h['PidsLimit']==pids and h['NanoCpus']==1000000000
 assert len(c['Mounts'])==len(allmounts)
 for source,dst,rw in allmounts:
  m=next(x for x in c['Mounts'] if x['Destination']==dst);assert m['Source']==source and m['RW']==rw
 (d/(label+'-identity.json')).write_text(json.dumps({'id':cid,'name':name,'image':image,'created':c['Created'],'entry':entry,'args':args,'policy':h,'mounts':c['Mounts']},indent=2)+'\n')
 with (d/(label+'-result.json')).open('wb') as out,(private/(label+'-stderr.log')).open('wb') as err:
  try:code=subprocess.run(['docker','start','-a',cid],stdout=out,stderr=err,timeout=timeout).returncode
  except subprocess.TimeoutExpired:
   curr=json.loads(run(['docker','inspect',cid]))[0];assert curr['Id']==cid and curr['Name']=='/'+name and curr['Image']==image and curr['Created']==c['Created'] and curr['Config']['Entrypoint']==[entry];(d/(label+'-stop-audit-before.json')).write_text(json.dumps({'id':cid,'name':name,'created':c['Created'],'action':'Stoponlyownboundeddiagnostic'})+'\n');subprocess.run(['docker','stop','-t','2',cid],check=True,timeout=8);raise
 curr=json.loads(run(['docker','inspect',cid]))[0];(d/(label+'-exit-state.json')).write_text(json.dumps(curr['State'])+'\n');assert code==0 and curr['State']['Status']=='exited' and curr['State']['ExitCode']==0 and not curr['State']['OOMKilled'],label+' diagnosticfailed'
 assert runtime()==before;return json.loads((d/(label+'-result.json')).read_text()),cid

case=private/'case';case.mkdir(mode=0o700);assert not (case/'lazy.log').exists()
data,cid=invoke(binary,'regression',['owned-regression'],[(str(case),'/opt/codex-output',True)],256,64,30)
assert data['status']=='LAZY_LOGGING_REGRESSION_PASS' and data['checks']>=20 and data['checks_after_performance']==data['checks']+8 and len(data['cases'])==8 and data['records_per_case']==100000
for size in [0,512]:
 rows=[x for x in data['cases'] if x['payload_bytes']==size];assert [x['mode'] for x in rows]==['eager','lazy','lazy','eager']
 assert all(x['filtered']==100000 and x['cpu_seconds']>0 and x['wall_seconds']>0 for x in rows)
assert inputs=={p:sha(pathlib.Path(p)) for p in inputs} and runtime()==before and settings=={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in settings} and clockpath.read_text().strip()=='tsc'
(d/'runtime-after.json').write_text(json.dumps(runtime(),indent=2)+'\n')
x={'status':'LAZY_LOGGING_NATIVE_REGRESSION_PASS','head':head,'container_id':cid,'elf_sha256':sha(binary),'regression':data,'all19_runtime_configs_preserved':True,'product_deployment':False,'full_feature_acceptance':False,'limits':'Own unchanged cached Logger ABI and current headers. Local mean CPU formatting proof, not actual Gateway/Message tail or capacity.'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2),flush=True)
PY
