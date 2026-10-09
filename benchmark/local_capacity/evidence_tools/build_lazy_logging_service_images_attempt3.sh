#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,shlex,re,datetime,os,signal,shutil,sys
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'lazy-logging-service-build-20261005-attempt3';assert not d.exists()
def run(a,timeout=20):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
head=run(['git','rev-parse','HEAD']).strip();assert head==json.loads((b/'lazy-logging-service-build-attempt3-source-20261005/summary.json').read_text())['head']
assert json.loads((b/'lazy-logging-regression-20261005/summary.json').read_text())['status']=='LAZY_LOGGING_NATIVE_REGRESSION_PASS'
assert not run(['git','diff','--name-only']).strip() and subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def resources():
 assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024 and shutil.disk_usage(r).free>3*1024**3
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
resources();before=runtime();assert before==json.loads((b/'private-batch-message-deployment-retained-on-20261005/runtime-after.json').read_text())
cache=r/'build/linux-release';cm=cache/'CMakeFiles';units=['private_persistence_trace_tests','message_application_service_tests','m16_crash_window_contract_tests','private_receiver_ack_tests','private_chat_ack_boundary_tests'];services=['gateway_demo','message_service_demo'];links={};borrowed={};plans={};flags_cache={}
def flags(target):
 p=cm/(target+'.dir')/'flags.make';borrowed[str(p)]=sha(p);text=p.read_text();values=[]
 for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:values+=shlex.split(next(x for x in text.splitlines() if x.startswith(key+' =')).split('=',1)[1])
 flags_cache[target]=values;return values
for target in services+units:
 p=cm/(target+'.dir')/'link.txt';borrowed[str(p)]=sha(p);links[target]=shlex.split(p.read_text());flags(target)
 for token in links[target]:
  if token.endswith(('.a','.o')) and (cache/token).is_file():borrowed[str((cache/token).resolve())]=sha((cache/token).resolve())
for archive in sorted({x for t in services for x in links[t] if x.startswith('libtinyimx_')}):
 target=archive.removeprefix('lib').removesuffix('.a');td=cm/(target+'.dir');assert td.is_dir();members=run(['ar','t',str(cache/archive)]).splitlines();assert len(members)==len(set(members))
 for dep in sorted(td.rglob('*.cpp.o.d')):
  text=dep.read_text()
  if '/common/logging/LogMacros.h' not in text and '/common/logging/Logger.h' not in text:continue
  match=re.search(r'(/home/jackson7/projects/TinyIMX_publish/[^\s]+\.cpp)(?:\s|$)',text);assert match,str(dep);source=pathlib.Path(match.group(1));member=dep.name[:-2];assert source.is_relative_to(r) and members.count(member)==1
  plans[str(source.relative_to(r))]={'target':target,'archive':archive,'member':member}
  borrowed[str(dep)]=sha(dep);flags(target)
# Cached libraries have held previous rejected experiments. These exact current
# translation units must override them even if no logging header appears in .d.
for source,target in [('services/message/application/MessageApplicationService.cpp','tinyimx_message_core'),('services/message/server/MessageServiceServer.cpp','tinyimx_message_grpc')]:
 archive='lib'+target+'.a';member=pathlib.Path(source).name+'.o';assert run(['ar','t',str(cache/archive)]).splitlines().count(member)==1;plans[source]={'target':target,'archive':archive,'member':member};flags(target)
assert 'gateway/business/BusinessExecutor.cpp' in plans and 'common/net/TcpConnection.cpp' in plans and 'gateway/GatewayServer.cpp' in plans
for target in services+units:
 old=[x for x in links[target] if x.endswith('.cpp.o')];assert old
 for obj in old:
  source=obj.split('.dir/',1)[1][:-2];assert (r/source).is_file() and source not in plans;plans[source]={'target':target,'object_for':target,'old':obj}
source_inputs={str(p):sha(p) for p in [r/s for s in plans]+list((r/'common').rglob('*.h'))+list((r/'gateway').rglob('*.h'))+list((r/'services').rglob('*.h'))}
previous=b/'lazy-logging-service-build-20261005-attempt2';previous_audit=json.loads((previous/'audit-before.json').read_text());previous_failure=json.loads((previous/'helper-failed.json').read_text())
assert previous_audit['head']=='3f5b9713ebb2e931b72f18100348bd316f7d619c' and previous_failure['message']=='eager-image-gateway_demo failed: private log retained'
assert plans==previous_audit['plans'] and source_inputs==previous_audit['source_sha256'] and borrowed==previous_audit['borrowed_sha256']
assert all(sha(pathlib.Path(p))==v for group in ['source_sha256','borrowed_sha256'] for p,v in previous_audit[group].items())
reuse_dir=previous/'runtime-private/eager';assert reuse_dir.is_dir();reuse_sha={str(p):sha(p) for p in reuse_dir.rglob('*') if p.is_file()};assert len(reuse_sha)>100
base_tags={'gateway_demo':'tinyimx/runtime:codex-online-maintenance-gateway-v1','message_service_demo':'tinyimx/runtime:codex-private-begin-insert-read-batch-v1'}
base_images={'gateway_demo':'sha256:a8b7d5ea6446a2fdbedac0f3ebbbfb07579155ec19b819959d96eb0262aeb6a9','message_service_demo':'sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060'}
for image in base_images.values():assert json.loads(run(['docker','image','inspect',image]))[0]['Id']==image
for target,tag in base_tags.items():assert json.loads(run(['docker','image','inspect',tag]))[0]['Id']==base_images[target]
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def failure(k,e,t):
 (d/'helper-failed.json').write_text(json.dumps({'status':'FAIL','type':k.__name__,'message':str(e),'head':head,'deployment':False})+'\n');sys.__excepthook__(k,e,t)
sys.excepthook=failure
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'Build paired current-source eager-control and lazy Gateway/Message images. Own archive copies replace exact member names for every logging-dependent cached unit plus original Message application/server and main; cache never written','plans':plans,'borrowed_sha256':borrowed,'source_sha256':source_inputs,'base_images':base_images,'resources':'One compiler, >2GiB available/>3GiB disk per call; own process180s maximum, no other apps stop','variants':'Control overlay contains exact 30452a5 formerLogMacros.h; candidate current macro. Other sources/compiler flags/dependencies/base images identical','units':units,'writes':'Fresh own stage objects/archive copies/ELFs/image contexts, four new image tags and owned stopped loader/exec probes only; no config/data/deployment changes','rollback':'All originals/failed stages/images retained, no delete/reset/push/prune','runtime_before':before},indent=2)+'\n')
(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
(d/'eager-reuse-audit-before.json').write_text(json.dumps({'source_head':previous_audit['head'],'source_and_borrowed_sha_match':True,'frozen_after_failure_before_reuse':True,'artifacts_sha256':reuse_sha,'scope':'Only reuse own compiled eager objects/archives/7ELFs; rerun130 units, never modify prior stage or pretend hashes captured before original compile'},indent=2)+'\n')
def invoke(argv,label,timeout=180,expected=0):
 resources();(d/(label+'-audit-before.json')).write_text(json.dumps({'argv':argv,'timeout':timeout,'owned_outputs':True})+'\n')
 with (private/(label+'.log')).open('w') as out:
  p=subprocess.Popen(argv,cwd=cache,stdout=out,stderr=subprocess.STDOUT,start_new_session=True);proc=pathlib.Path('/proc')/str(p.pid);ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19];(d/(label+'-process.json')).write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'start_ticks':ticks,'argv':argv})+'\n')
  try:code=p.wait(timeout=timeout)
  finally:
   if p.poll() is None:
    assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(argv)]==[x.encode() for x in argv];(d/(label+'-stop-audit-before.json')).write_text(json.dumps({'pid':p.pid,'start_ticks':ticks,'action':'Stop only own verified compiler/test group'})+'\n');os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:
     assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid;os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==expected,label+' failed: private log retained'
 return (private/(label+'.log')).read_text()
overlay=private/'control-includes/common/logging';overlay.mkdir(parents=True,mode=0o700);old_macro=subprocess.check_output(['git','show','30452a51d08561593a6aba2fbe1ef188ff06d1c7:common/logging/LogMacros.h']);assert hashlib.sha256(old_macro).hexdigest()=='1d4e7adc92e760e97b06511b105b3f6b7d50ddeae8ce1c040f90fa235aa5836e';(overlay/'LogMacros.h').write_bytes(old_macro)
all_results=[];unit_results={};archive_results={}
for variant in ['eager','lazy']:
 vd=reuse_dir if variant=='eager' else private/variant;objects={};own_archives={};binary_head=previous_audit['head'] if variant=='eager' else head
 if variant=='eager':
  for index,(source,plan) in enumerate(sorted(plans.items())):
   objects[source]=vd/(str(index)+'-'+pathlib.Path(source).name+'.o');assert objects[source].is_file() and sha(objects[source])==reuse_sha[str(objects[source])]
  for archive in sorted({p['archive'] for p in plans.values() if 'archive' in p}):
   p=vd/archive;assert p.is_file() and sha(p)==reuse_sha[str(p)] and run(['ar','t',str(p)]).splitlines()==run(['ar','t',str(cache/archive)]).splitlines();own_archives[archive]=str(p)
  for target in services+units:assert (vd/target).read_bytes()[:4]==b'\x7fELF' and sha(vd/target)==reuse_sha[str(vd/target)]
  print(json.dumps({'variant':'eager','reused_owned_translation_units':len(plans),'compiled_revision':binary_head,'rerun_units':True}),flush=True)
 else:
  vd.mkdir(mode=0o700)
  for index,(source,plan) in enumerate(sorted(plans.items())):
   out=vd/(str(index)+'-'+pathlib.Path(source).name+'.o');extra=['-I'+str(overlay.parents[1])] if variant=='eager' else []
   invoke(['/usr/bin/c++',*extra,*flags_cache[plan['target']],'-MD','-MF',str(out)+'.d','-c',str(r/source),'-o',str(out)],variant+'-compile-'+str(index));objects[source]=out
   dependencies=pathlib.Path(str(out)+'.d').read_text()
   if '/common/logging/LogMacros.h' in dependencies:
    assert str(overlay/'LogMacros.h') in dependencies if variant=='eager' else str(r/'common/logging/LogMacros.h') in dependencies
   print(json.dumps({'variant':variant,'compiled':index+1,'total':len(plans),'source':source}),flush=True)
  for archive in sorted({p['archive'] for p in plans.values() if 'archive' in p}):
   ad=vd/archive.removesuffix('.a');ad.mkdir();output=vd/archive;shutil.copy2(cache/archive,output)
   replacements=[]
   for source,plan in plans.items():
    if plan.get('archive')!=archive:continue
    member=ad/plan['member'];shutil.copy2(objects[source],member);replacements.append(str(member))
   invoke(['ar','rcs',str(output),*replacements],variant+'-archive-'+archive);assert run(['ar','t',str(output)]).splitlines()==run(['ar','t',str(cache/archive)]).splitlines();own_archives[archive]=str(output)
 archive_results[variant]={k:sha(pathlib.Path(v)) for k,v in own_archives.items()}
 def own_link(target):
  args=[own_archives.get(x,x) for x in links[target]]
  for source,plan in plans.items():
   if plan.get('object_for')==target:args[args.index(plan['old'])]=str(objects[source])
  assert not any(x.endswith('.cpp.o') and x.startswith('CMakeFiles/') for x in args);args[args.index('-o')+1]=str(vd/target)
  if target in units:
   base=links['message_service_demo'];first=next(i for i,x in enumerate(base) if x.endswith('.a'));args += [own_archives.get(x,x) for x in base[first:]]
  args+=['-Wl,-Map='+str(vd/(target+'.map'))];assert not any('--wrap' in x for x in args);return args
 if variant=='lazy':
  for target in services+units:invoke(own_link(target),variant+'-link-'+target)
 unit_results[variant]={}
 for target in units:
  text=invoke([str(vd/target)],variant+'-unit-'+target,60);passed=text.count('[PASS]')+sum(x.startswith('PASS ') for x in text.splitlines());failed=text.count('[FAIL]')+sum(x.startswith('FAIL ') for x in text.splitlines());assert passed>0 and failed==0;unit_results[variant][target]={'pass':passed,'fail':failed}
 assert sum(x['pass'] for x in unit_results[variant].values())>=130
 for target in services:
  exe=vd/target;assert exe.read_bytes()[:4]==b'\x7fELF';context=private/(variant+'-'+target+'-image-context');context.mkdir();shutil.copy2(exe,context/target);(context/target).chmod(0o755)
  tag='tinyimx/runtime:codex-lazy-logging-'+variant+'-'+target.replace('_','-')+'-v1';assert subprocess.run(['docker','image','inspect',tag],capture_output=True).returncode!=0
  df=private/(variant+'-'+target+'.Dockerfile');df.write_text('FROM '+base_tags[target]+'\nCOPY --chmod=0755 '+target+' /opt/tinyimx/bin/'+target+'\nLABEL org.opencontainers.image.revision="'+binary_head+'"\nLABEL tinyimx.binary.sha256="'+sha(exe)+'"\nLABEL tinyimx.logging.variant="'+variant+'"\n')
  invoke(['docker','build','--pull=false','--network=none','-f',str(df),'-t',tag,str(context)],variant+'-image-'+target,120)
  probes=[]
  for mode in ['ldd','exec']:
   name='tinyimx-codex-lazy-'+variant+'-'+target.replace('_','-')+'-'+mode+'-20261005';assert subprocess.run(['docker','container','inspect',name],capture_output=True).returncode!=0
   common=['docker','create','--pull','never','--name',name,'--network','none','--read-only','--user','1000:1000','--cap-drop','ALL','--security-opt','no-new-privileges:true','--pids-limit','32','--memory','128m','--log-driver','none']
   args=common+(['--entrypoint','/bin/sh',tag,'-ec','test -x "$1"; ldd -r "$1"','owned-loader','/opt/tinyimx/bin/'+target] if mode=='ldd' else ['--entrypoint','/opt/tinyimx/bin/'+target,tag,'/tmp/codex-owned-lazy-absent-config-20261005.json'])
   label=variant+'-'+target+'-'+mode;(d/(label+'-create-audit-before.json')).write_text(json.dumps({'argv':args,'expected_code':0 if mode=='ldd' else 1})+'\n');cid=run(args).strip();c=json.loads(run(['docker','inspect',cid]))[0];assert c['Config']['User']=='1000:1000' and c['State']['Status']=='created' and c['HostConfig']['NetworkMode']=='none' and c['HostConfig']['ReadonlyRootfs'] and c['HostConfig']['CapDrop']==['ALL'] and not c['Mounts'];(d/(label+'-identity.json')).write_text(json.dumps({'id':cid,'name':name,'image':c['Image'],'created':c['Created'],'entrypoint':c['Config']['Entrypoint'],'args':c['Config']['Cmd']})+'\n')
   with (private/(label+'.log')).open('w') as output:
    try:code=subprocess.run(['docker','start','-a',cid],stdout=output,stderr=subprocess.STDOUT,timeout=20).returncode
    except subprocess.TimeoutExpired:
     now=json.loads(run(['docker','inspect',cid]))[0];assert now['Id']==cid and now['Name']=='/'+name and now['Created']==c['Created'] and now['Image']==c['Image'];(d/(label+'-stop-audit-before.json')).write_text(json.dumps({'id':cid,'created':c['Created'],'action':'Stop only exact own20s loader probe'})+'\n');subprocess.run(['docker','stop','-t','2',cid],check=True,timeout=8);raise
   state=json.loads(run(['docker','inspect',cid]))[0]['State'];assert code==(0 if mode=='ldd' else 1) and state['ExitCode']==code and not state['OOMKilled'];assert not any(x in (private/(label+'.log')).read_text().lower() for x in ['not found','undefined symbol']) if mode=='ldd' else True;probes.append(cid)
  c=json.loads(run(['docker','image','inspect',tag]))[0];assert c['Config']['Labels']['org.opencontainers.image.revision']==binary_head and c['Config']['Labels']['tinyimx.binary.sha256']==sha(exe)
  all_results.append({'variant':variant,'target':target,'image_tag':tag,'image_id':c['Id'],'base_id':base_images[target],'base_tag':base_tags[target],'binary_build_revision':binary_head,'orchestration_revision':head,'elf_sha256':sha(exe),'binary_relative_path':str(exe.relative_to(r)),'loader_exec_probes':probes,'deployment':False});(d/'completed-images.json').write_text(json.dumps(all_results,indent=2)+'\n');print(json.dumps(all_results[-1]),flush=True)
 assert runtime()==before and borrowed=={p:sha(pathlib.Path(p)) for p in borrowed} and source_inputs=={p:sha(pathlib.Path(p)) for p in source_inputs}
assert unit_results['eager']==unit_results['lazy'] and len(all_results)==4
assert reuse_sha=={p:sha(pathlib.Path(p)) for p in reuse_sha}
for target,tag in base_tags.items():assert json.loads(run(['docker','image','inspect',tag]))[0]['Id']==base_images[target]
(d/'runtime-after.json').write_text(json.dumps(runtime(),indent=2)+'\n');x={'status':'LAZY_LOGGING_PAIRED_SERVICE_IMAGES_PASS','head':head,'translation_units_per_variant':len(plans),'unit_results':unit_results,'unit_checks_per_variant':sum(x['pass'] for x in unit_results['lazy'].values()),'images':all_results,'own_archives':archive_results,'reused_eager_artifacts_preserved':True,'all19_runtime_configs_preserved':True,'borrowed_inputs_preserved':True,'deployment':False,'full_feature_acceptance':False,'limits':'Native semantics and build/loader proof only; pending actual feature correctness and matched end-to-end load. Paired same-source variants isolate logging macro from historical cache differences.'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({'status':x['status'],'units_per_variant':len(plans),'unit_checks_per_variant':x['unit_checks_per_variant'],'images':4,'deployment':False}),flush=True)
PY
