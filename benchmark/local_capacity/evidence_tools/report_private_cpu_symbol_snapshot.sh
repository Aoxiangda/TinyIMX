#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,time,re,os,shutil
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'private-cpu-symbol-snapshot-20261005';assert not d.exists()
def run(args,timeout=20):return subprocess.check_output(args,text=True,timeout=timeout)
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
head=run(['git','rev-parse','HEAD']).strip();source=json.loads((b/'private-cpu-symbol-snapshot-source-20261005/summary.json').read_text());assert head==source['head']
parent=b/'private-cpu-load-profile-20261005';raw_private=parent/'runtime-private';raw=b/'capacity-cpuprofile150';assert json.loads((parent/'helper-failed.json').read_text())['status']=='FAIL'
cfgroot=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');ref=json.loads((b/'private-batch-message-deployment-retained-on-20261005/runtime-after.json').read_text())
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfgroot.glob('*.json')}}
before=runtime();assert before==ref
audit=json.loads((parent/'profile-audit-before.json').read_text());tool_hashes=audit['inputs_sha256'];settings=audit['settings_before'];assert settings=={k:pathlib.Path('/proc/sys/kernel/'+k).read_text().strip() for k in settings}
launcher=b/'private-cpu-profiler-bootstrap-20261005-attempt2/perf_uid_launcher';native=pathlib.Path('/usr/lib/linux-tools/6.8.0-138-generic/perf');ldd=run(['ldd',str(native)]);libs={x for x in re.findall(r'(/[^\s]+)',ldd) if pathlib.Path(x).is_file()}
mounts=[(launcher,'/opt/codex-perf/launcher'),(native,'/opt/codex-perf/perf'),(pathlib.Path('/usr/bin/sleep'),'/opt/codex-perf/sleep')]+[(pathlib.Path(p),p) for p in sorted(libs)]
assert tool_hashes=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p,_ in mounts}
base=['docker','create','--pull','never','--read-only','--network','none','--cap-drop','ALL','--security-opt','no-new-privileges:true','--pids-limit','64','--memory','128m','--cpus','1','--ulimit','memlock=8388608:8388608','--log-driver','none','--tmpfs','/tmp:rw,noexec,nosuid,size=16m']
for p,dst in mounts:base+=['--mount','type=bind,src='+str(p.resolve())+',dst='+dst+',readonly']
profile_image='sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060';targets=[x for x in audit['targets'] if x['label'] in ['gateway-a','gateway-b']];assert len(targets)==2;profile_results=[]
inputs=[parent/'profile-audit-before.json',parent/'helper-failed.json',parent/'observer-error.json',raw/'summary.json',raw/'reconciliation.json']
for target in targets:
 c=json.loads(run(['docker','inspect',target['container_id']]))[0];assert c['Image']==target['container_image'] and c['State']['StartedAt']==target['container_started'] and c['Name']==target['container_name']
 pid=target['namespace_service_pid'];stat=run(['docker','exec','--user','1000',target['container_id'],'cat','/proc/'+str(pid)+'/stat']);assert stat.rsplit(')',1)[1].split()[19]==target['service_start_ticks']
 assert run(['docker','exec','--user','1000',target['container_id'],'sha256sum',target['exe']]).split()[0]==target['binary_sha256']
 record=json.loads(run(['docker','inspect','tinyimx-codex-real-cpu-'+target['label']+'-20261005']))[0];assert record['State']['Status']=='exited' and record['State']['ExitCode']==0 and record['HostConfig']['PidMode']=='container:'+target['container_id']
 p=raw_private/(target['label']+'-record.perf.pipe');assert p.read_bytes()[:8]==b'PERFILE2';inputs.append(p)
 profile_results.append({'target':target,'recorder_id':record['Id'],'raw_bytes':p.stat().st_size,'raw_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'report_complete':False})
msg=json.loads(run(['docker','inspect','tinyimx-codex-real-cpu-message-20261005']))[0];assert msg['State']['ExitCode']==255
input_hashes={str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
d.mkdir();private_dir=d/'runtime-private';private_dir.mkdir(mode=0o700)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Recover existing two successfulGateway rawCPU recordings, no newload orsampling','inputs_sha256':input_hashes,'targets':targets,'runtime_before':before,'reports':'UID1000 zeroCAP/no-net/readonly/defaultseccomp/nnp; exacttargetcontainerPIDnamespace andsymfsactualprocessroot; stdin readonlyownraw;90sownstop','writes':'Freshownreportstage/privateerrors only; oldfailedstage/allraw byteexact preserved','message_failure':'exit255 andignoredESRCH threadopen warnings, exactfatalcause stillOPEN; no completeMsgCPU result inferred','limits':'Diagnosticinstrumentedload notacceptance; userCPUflat excludeskernel andwallwait; no callstack; inspect samplecount/unknown symbols beforeattribution'},indent=2)+'\n')

assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024 and shutil.disk_usage(d).free>3*1024**3
bundle=d/'bundle';bundle.mkdir(mode=0o700);snapshots={};snapshot_manifest=[];total_bytes=0
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
for target in targets:
 root=bundle/target['label'];root.mkdir(mode=0o700);snapshots[target['label']]=root
 maps=run(['docker','exec','--user','1000',target['container_id'],'cat','/proc/'+str(target['namespace_service_pid'])+'/maps']);paths=set()
 (private_dir/(target['label']+'-maps.txt')).write_text(maps)
 for line in maps.splitlines():
  fields=line.split(None,5)
  if len(fields)!=6 or 'x' not in fields[1] or not fields[5].startswith('/'):continue
  path=pathlib.PurePosixPath(fields[5]);assert '..' not in path.parts and '\\' not in str(path) and ' ' not in str(path)
  allowed=str(path)=='/opt/tinyimx/bin/gateway_demo' or (str(path).startswith(('/usr/lib/','/lib/','/lib64/')) and re.fullmatch(r'[A-Za-z0-9_+.-]+\.so(?:\.[A-Za-z0-9_.+-]+)?',path.name))
  assert allowed,'Executable mapping notallowlisted: '+str(path);paths.add(str(path))
 assert '/opt/tinyimx/bin/gateway_demo' in paths and len(paths)<=50
 for index,path in enumerate(sorted(paths)):
  expected=run(['docker','exec','--user','1000',target['container_id'],'sha256sum',path]).split()[0];size=int(run(['docker','exec','--user','1000',target['container_id'],'stat','-Lc','%s',path]).strip());total_bytes+=size
  assert size<=1024**3 and total_bytes<=2*1024**3
  dest=root/path.lstrip('/');assert not dest.exists() and dest.resolve().is_relative_to(root.resolve())
  entry={'target':target['label'],'container_id':target['container_id'],'source_path':path,'size':size,'sha256':expected,'destination':str(dest.relative_to(d)),'operation':'Readonly Dockerfile copy toabsentownphysicalsymfs, no prodwrites'}
  (d/(target['label']+'-copy-'+str(index)+'-audit-before.json')).write_text(json.dumps(entry,indent=2)+'\n')
  dest.parent.mkdir(parents=True,exist_ok=True);subprocess.run(['docker','cp','-L',target['container_id']+':'+path,str(dest)],check=True,timeout=60)
  assert not dest.is_symlink() and dest.is_file() and dest.stat().st_size==size and dest.stat().st_uid==os.getuid() and sha(dest)==expected
  with dest.open('rb') as f:assert f.read(4)==b'\x7fELF'
  assert run(['docker','exec','--user','1000',target['container_id'],'sha256sum',path]).split()[0]==expected
  if path==target['exe']:assert expected==target['binary_sha256']=='c2894a21cf308ad35ef665c603d17b1a2577c9216aa9e74856772befa99d640f'
  entry['build_id_lines']=[line.strip() for line in run(['readelf','-n',str(dest)]).splitlines() if 'Build ID:' in line];snapshot_manifest.append(entry)
(d/'symbol-snapshot-manifest.json').write_text(json.dumps({'files':snapshot_manifest,'bytes':total_bytes,'copied_whole_root':False,'old_reports_function_labels_rejected':True,'raw_samples_unchanged':True},indent=2)+'\n')

def make_probe(target,report=False):
 label=target['label'];probe_name='tinyimx-codex-snapshot-cpu-'+label+('-report' if report else '')+'-20261005';assert subprocess.run(['docker','inspect',probe_name],capture_output=True).returncode!=0
 pid=target['namespace_service_pid']
 args=['report','-i','-','--stdio','--no-children','--sort','dso,symbol','--show-nr-samples','--percent-limit','0','--field-separator',';','--fields','overhead,sample,dso,symbol','--symfs','/opt/codex-symbols'] if report else ['1000','1000','/opt/codex-perf/perf','record','-B','-N','-e','cpu-clock:u','-F','49','-m','8','-p',str(pid),'-o','-','--','/opt/codex-perf/sleep','45']
 entry='/opt/codex-perf/perf' if report else '/opt/codex-perf/launcher';cmd=base+['--name',probe_name,'--user','1000' if report else '0','--mount','type=bind,src='+str(snapshots[label])+',dst=/opt/codex-symbols,readonly']
 if report:cmd+=['-i']
 else:cmd+=['--cap-add','PERFMON','--cap-add','SETUID','--cap-add','SETGID','--cap-add','SETPCAP']
 cmd+=['--entrypoint',entry,profile_image,*args];key=label+('-report' if report else '-record')
 (d/(key+'-create-audit-before.json')).write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'argv':cmd,'target':target,'initial_caps':[] if report else ['PERFMON','SETUID','SETGID','SETPCAP'],'sampler_final_caps':[] if report else ['PERFMON'],'writes':'Nohostwrite mounts; rawstdout only or reportstdin fromownraw'},indent=2)+'\n')
 cid=subprocess.check_output(cmd,text=True,timeout=20).strip();c=json.loads(subprocess.check_output(['docker','inspect',cid],text=True))[0];h=c['HostConfig']
 assert c['Id']==cid and c['Name']=='/'+probe_name and c['Image']==profile_image and c['State']['Status']=='created' and c['Config']['Entrypoint']==[entry] and c['Config']['Cmd']==args
 assert h['ReadonlyRootfs'] and h['NetworkMode']=='none' and h['PidMode']=='' and set(h['CapDrop'])=={'ALL'} and set(h['CapAdd'] or [])==(set() if report else {'CAP_PERFMON','CAP_SETUID','CAP_SETGID','CAP_SETPCAP'}) and not h['Privileged'] and h['SecurityOpt']==['no-new-privileges:true'] and h['LogConfig']['Type']=='none' and h['Memory']==128*1024*1024 and not any(x.get('RW') for x in c['Mounts'])
 return {'id':cid,'name':probe_name,'created':c['Created'],'args':args,'entry':entry,'target':target,'key':key,'report':report}
def stop_own(job,reason):
 c=json.loads(subprocess.check_output(['docker','inspect',job['id']],text=True))[0]
 assert c['Id']==job['id'] and c['Name']=='/'+job['name'] and c['Image']==profile_image and c['Created']==job['created'] and c['Config']['Cmd']==job['args'] and c['Config']['Entrypoint']==[job['entry']]
 if c['State']['Status']=='running':
  (d/(job['key']+'-stop-audit.json')).write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'action':'Stop only exact owned diagnostic probe','id':job['id'],'created':job['created'],'pid':c['State']['Pid'],'started':c['State']['StartedAt'],'reason':reason},indent=2)+'\n');subprocess.run(['docker','stop','-t','2',job['id']],check=True,timeout=10)
def reports_after_load():
 for target in targets:
  job=make_probe(target,True);inp=(raw_private/(target['label']+'-record.perf.pipe')).open('rb');report=d/(target['label']+'-cpu-symbols.txt');out=report.open('wb');err=(private_dir/(target['label']+'-report.stderr')).open('wb')
  try:
   p=subprocess.Popen(['docker','start','-ai',job['id']],stdin=inp,stdout=out,stderr=err)
   try:p.wait(timeout=90)
   except subprocess.TimeoutExpired:stop_own(job,'90s ownreport bound');p.wait(timeout=10);raise
  finally:inp.close();out.close();err.close()
  c=json.loads(subprocess.check_output(['docker','inspect',job['id']],text=True))[0];assert c['State']['Status']=='exited' and c['State']['ExitCode']==0 and report.stat().st_size>200
  result=next(x for x in profile_results if x['target']['label']==target['label']);result.update(report_complete=True,report_id=job['id'],report_sha256=hashlib.sha256(report.read_bytes()).hexdigest())
 assert len(profile_results)==2 and all(x['report_complete'] for x in profile_results)
 assert tool_hashes=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p,_ in mounts} and settings=={k:pathlib.Path('/proc/sys/kernel/'+k).read_text().strip() for k in settings}
 (d/'cpu-profile-summary.json').write_text(json.dumps({'status':'PRIVATE_GATEWAY_EXACT_SYMBOL_SNAPSHOT_REPORTS_COMPLETE','targets':profile_results,'flat_user_CPU_only':True,'callstack_or_memory_collected':False,'actual_product_DSO_via_target_symfs':True,'performance_acceptance':False},indent=2)+'\n')

reports_after_load();assert runtime()==before
assert input_hashes=={str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
parsed=[]
for target in targets:
 text=(d/(target['label']+'-cpu-symbols.txt')).read_text();rows=[]
 for line in text.splitlines():
  fields=[f.strip() for f in line.split(';')]
  if len(fields)==4 and re.fullmatch(r'[0-9.]+%?',fields[0]) and fields[1].isdigit():
   rows.append({'cpu_sample_weight_pct':float(fields[0].rstrip('%')),'samples':int(fields[1]),'dso':fields[2],'symbol':re.sub(r'^\[.\]\s*','',fields[3]).strip()})
 assert rows and sum(x['samples'] for x in rows)==({'gateway-a':620,'gateway-b':625}[target['label']])
 unknown=sum(x['cpu_sample_weight_pct'] for x in rows if x['symbol'].startswith('0x'))
 parsed.append({'target':target['label'],'sample_rows':len(rows),'sample_count_from_all_symbol_rows':sum(x['samples'] for x in rows),'unknown_symbol_weight_pct':unknown,'top_symbols':rows[:20]})
private=json.loads((raw/'summary.json').read_text());x={'status':'PRIVATE_CPU_PROFILE_EXACT_SNAPSHOT_COMPLETE','head':head,'gateway_cpu':parsed,'old_function_labels_unusable_due_procroot_symfs_reset':True,'physical_snapshot_files':len(snapshot_manifest),'message_cpu_complete':False,'message_exit':255,'diagnostic_private':{k:v for k,v in private.items() if k!='workers'},'original_run_observer_failed':True,'all19_runtime_configs_preserved':True,'old_evidence_sha_preserved':True,'new_load':False,'full_feature_acceptance':False,'limits':'Only gateway raw samples with nowactualpinnedELF symbols; physicalsymfs fixesconfirmedprocroot reset; vdso unresolved offsets remain unknown. Messagefailure notfixed; no silentmissingthread/noinherit shortcut. Instrumentedrunlatency notbaselineacceptance. No P99arithmetic or taskwaitCPU substitution.'}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
