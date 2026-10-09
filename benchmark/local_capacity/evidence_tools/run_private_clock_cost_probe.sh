#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime,re,sys,os,signal,struct,statistics
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'private-clock-cost-probe-20261005';assert not d.exists()
def run(a,timeout=20):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
head=run(['git','rev-parse','HEAD']).strip();assert head==json.loads((b/'private-clock-cost-probe-source-20261005-attempt2/summary.json').read_text())['head']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'private-batch-message-deployment-retained-on-20261005/runtime-after.json').read_text())
settings={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in ['perf_event_paranoid','kptr_restrict']};assert settings=={'perf_event_paranoid':'4','kptr_restrict':'1'}
clockpath=pathlib.Path('/sys/devices/system/clocksource/clocksource0/current_clocksource');clock=clockpath.read_text().strip();assert clock=='tsc' and run(['uname','-r']).strip()=='6.8.0-138-generic'
analysis=b/'private-cpu-symbol-analysis-20261005/summary.json';assert json.loads(analysis.read_text())['status']=='PRIVATE_GATEWAY_CPU_EXACT_SYMBOL_ANALYSIS_PASS'
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def failed(k,e,t):
 (d/'helper-failed.json').write_text(json.dumps({'status':'FAIL','type':k.__name__,'message':str(e),'head':head,'new_load':False})+'\n');sys.__excepthook__(k,e,t)
sys.excepthook=failed
src=r/'benchmark/local_capacity/clock_cost_probe.c';elf=d/'clock_cost_probe';args=['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(src),'-o',str(elf)];inputs={str(src):sha(src),str(analysis):sha(analysis)}
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Fresh ownnativeclock cost and ONLY selfpublickernelvDSO code; no targetmemory/load orproductchanges','runtime':before,'settings':settings,'clocksource':clock,'input_sha256':inputs,'compile_argv':args,'writes':'Ownfreshstage nativeELF/JSON numeric/stdout, privateonlyselfpublicELF binary; chmodownELF0555; boundedowncontainer no-targetPID/network','limits':'4rounds x4modes x20k calls; batchwall/user/system means NOT percallP99 orproduction acceptance; never substituteCOARSE intodeadline clocks','rollback':'Retainallartifacts/ownstoppedcontainer; all19/config/kernel/security/apps preserved'},indent=2)+'\n')
with (d/'compile.log').open('w') as out:
 p=subprocess.Popen(args,stdout=out,stderr=subprocess.STDOUT,start_new_session=True);stat=pathlib.Path('/proc')/str(p.pid)/'stat';ticks=stat.read_text().rsplit(')',1)[1].split()[19]
 (d/'compile-pid.json').write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'start_ticks':ticks,'argv':args})+'\n')
 try:assert p.wait(timeout=180)==0
 except subprocess.TimeoutExpired:
  assert p.poll() is None and stat.read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid;os.killpg(p.pid,signal.SIGTERM);p.wait(timeout=10);raise
assert elf.is_file() and not elf.is_symlink() and elf.resolve().is_relative_to(d.resolve()) and elf.stat().st_size<1024*1024 and 'not found' not in run(['ldd',str(elf)])
(d/'elf-permission-audit-before.json').write_text(json.dumps({'path':str(elf),'sha256':sha(elf),'old_mode':oct(elf.stat().st_mode&0o777),'new_mode':'0o555','scope':'OnlyfreshownELF insideprivate0700stage; no otherfiles'})+'\n');elf.chmod(0o555)
image='sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060';assert json.loads(run(['docker','image','inspect',image]))[0]['Id']==image
name='tinyimx-codex-clock-cost-20261005';assert subprocess.run(['docker','container','inspect',name],capture_output=True).returncode!=0
entry='/opt/codex-clock/probe';cmd=['docker','create','--pull','never','--name',name,'--user','1000:1000','--read-only','--network','none','--cap-drop','ALL','--security-opt','no-new-privileges:true','--pids-limit','32','--memory','128m','--cpus','1','--log-driver','none','--mount','type=bind,src='+str(elf.resolve())+',dst='+entry+',readonly','--entrypoint',entry,image]
(d/'create-audit-before.json').write_text(json.dumps({'argv':cmd,'name':name,'own_elf_sha256':sha(elf),'default_seccomp':True,'pid_isolated':True},indent=2)+'\n');cid=run(cmd).strip();c=json.loads(run(['docker','inspect',cid]))[0];h=c['HostConfig']
assert c['Id']==cid and c['Name']=='/'+name and c['Image']==image and c['State']['Status']=='created' and c['Config']['Entrypoint']==[entry] and not c['Config']['Cmd'] and c['Config']['User']=='1000:1000'
assert h['ReadonlyRootfs'] and h['NetworkMode']=='none' and set(h['CapDrop'])=={'ALL'} and not h['CapAdd'] and h['PidMode']=='' and not h['Privileged'] and not h.get('AutoRemove') and h['LogConfig']['Type']=='none' and h['Memory']==128*1024*1024 and h['PidsLimit']==32 and h['NanoCpus']==1000000000 and h['SecurityOpt']==['no-new-privileges:true']
assert len(c['Mounts'])==1 and c['Mounts'][0]['Source']==str(elf.resolve()) and c['Mounts'][0]['Destination']==entry and not c['Mounts'][0]['RW']
(d/'container-identity-before.json').write_text(json.dumps({'id':cid,'name':name,'image':image,'created':c['Created'],'entrypoint':c['Config']['Entrypoint'],'args':c['Config']['Cmd'],'policy':h,'mounts':c['Mounts']},indent=2)+'\n')
stdout=d/'clock-cost-records.jsonl';vdso=private/'self-public-vdso.elf'
with stdout.open('wb') as out,vdso.open('wb') as err:
 try:code=subprocess.run(['docker','start','-a',cid],stdout=out,stderr=err,timeout=90).returncode
 except subprocess.TimeoutExpired:
  current=json.loads(run(['docker','inspect',cid]))[0];assert current['Id']==cid and current['Name']=='/'+name and current['Image']==image and current['Created']==c['Created'] and current['Config']['Entrypoint']==[entry]
  (d/'timeout-stop-audit-before.json').write_text(json.dumps({'id':cid,'name':name,'created':c['Created'],'action':'Stoponlyexactfreshown90sdiagnostic','pid':current['State']['Pid']})+'\n');subprocess.run(['docker','stop','-t','2',cid],check=True,timeout=8);raise
c=json.loads(run(['docker','inspect',cid]))[0];assert code==0 and c['State']['Status']=='exited' and c['State']['ExitCode']==0 and not c['State']['OOMKilled']
rows=[json.loads(line) for line in stdout.read_text().splitlines()];assert len(rows)==18 and rows[0]=={'type':'policy','uid':1000,'gid':1000,'all_cap_sets_zero':True,'nnp':1,'seccomp':2}
assert rows[1]['type']=='public_vdso' and rows[1]['own_self_mapping_only'] and 1<=rows[1]['pages']<=16 and rows[1]['bytes']==vdso.stat().st_size<=65536
data=vdso.read_bytes();assert data[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0]==62
elfhdr=struct.unpack_from('<16sHHIQQQIHHHHHH',data);phoff,shoff=elfhdr[5:7];phent,phnum,shent,shnum=elfhdr[9:13];assert phent==56 and shent==64 and phoff+phent*phnum<=len(data) and shoff+shent*shnum<=len(data)
for label,args in [('public-vdso-symbols',['readelf','-Ws','-n',str(vdso)]),('public-vdso-offset',['objdump','-d','--start-address=0x720','--stop-address=0x7b0',str(vdso)])]:
 result=subprocess.run(args,capture_output=True,text=True,timeout=20);assert result.returncode==0;(d/(label+'.txt')).write_text(result.stdout);(private/(label+'.stderr')).write_text(result.stderr)
costs=rows[2:];modes={'CAPI_MONOTONIC','CAPI_REALTIME','SYS_MONOTONIC','CAPI_MONOTONIC_COARSE'};assert len(costs)==16 and {(x['round'],x['mode']) for x in costs}=={(i,m) for i in range(4) for m in modes}
for x in costs:assert x['type']=='clock_cost' and x['calls']==x['success']==20000 and x['wall_ns']>0 and x['user_us']>=0 and x['system_us']>=0 and x['checksum']>0
stats={m:{metric:[x[field]/20000*factor for x in costs if x['mode']==m] for metric,field,factor in [('wall_ns_per_call','wall_ns',1),('user_ns_per_call','user_us',1000),('system_ns_per_call','system_us',1000)]} for m in sorted(modes)}
assert runtime()==before and settings=={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in settings} and clockpath.read_text().strip()==clock and inputs=={str(src):sha(src),str(analysis):sha(analysis)}
x={'status':'PRIVATE_CLOCK_COST_PROBE_PASS','head':head,'container_id':cid,'image':image,'own_elf_sha256':sha(elf),'public_vdso_sha256':sha(vdso),'public_vdso_bytes':len(data),'clocksource':clock,'stats':stats,'all19_runtime_configs_preserved':True,'sysctl_4_1_preserved':True,'new_business_load':False,'production_code_change':False,'limits':'Eachvalue20k batchmean,4rotatingrounds. No percallP99/callchain/productionperrequest share; publicselfvDSO offset disassembly only, targetuserdata neverread. COARSE comparison diagnostic only.','full_feature_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2));print((d/'public-vdso-offset.txt').read_text())
PY
