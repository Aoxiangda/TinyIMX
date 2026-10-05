#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime,re,sys,os,signal,struct
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'private-clock-callsite-probe-20261005';assert not d.exists()
def run(a,timeout=20):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
head=run(['git','rev-parse','HEAD']).strip();assert head==json.loads((b/'private-clock-callsite-probe-source-20261005/summary.json').read_text())['head']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'private-batch-message-deployment-retained-on-20261005/runtime-after.json').read_text())
settings={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in ['perf_event_paranoid','kptr_restrict']};assert settings=={'perf_event_paranoid':'4','kptr_restrict':'1'}
clockpath=pathlib.Path('/sys/devices/system/clocksource/clocksource0/current_clocksource');assert clockpath.read_text().strip()=='tsc'
parent=b/'message-rpc-kernel-probe-attempt2-20261005';binary=parent/'message_rpc_kernel_probe';assert sha(binary)=='1f331148e08fb6ce3dbffa9d9a329093f0d4f7e65103b429ec5bdb8a1d27e5b8'
assert binary.read_bytes()[:6]==b'\x7fELF\x02\x01';assert json.loads((parent/'grpc-result.json').read_text())['synthetic_result_ok']==6000
ldd=run(['ldd',str(binary)]);assert 'not found' not in ldd
libs={str(pathlib.Path(x).resolve()):pathlib.Path(x).resolve() for x in re.findall(r'(/[^\s]+)',ldd) if pathlib.Path(x).is_file()};libs[str(binary.resolve())]=binary.resolve();assert len(libs)<64
inputs={name:sha(p) for name,p in libs.items()}
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def failure(k,e,t):
 (d/'helper-failed.json').write_text(json.dumps({'status':'FAIL','type':k.__name__,'message':str(e),'head':head,'production_changes':False})+'\n');sys.__excepthook__(k,e,t)
sys.excepthook=failure
src=r/'benchmark/local_capacity/clock_callsite_counter.c';so=d/'clock_callsite_counter.so';args=['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fPIC','-shared',str(src),'-ldl','-o',str(so)]
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'operation':'OwnsyntheticRPC only scopedpreciseclock codecaller counter; no targetapps/PID/data read','runtime':before,'inputs':inputs,'source_sha256':sha(src),'compiler_argv':args,'cases':'wrapped direct10s plusnormalA1/wrappedB1/B2/normalA2 grpc10s300/s, exact3000echoresultchecks each','counter':'4096slots x7buckets atomic, immediateowncode returnaddr0, no stack/frame/userdata, realclock values/result/errno preserved, ctorfallback0/overflow0 required','writes':'Freshown SO/numericreport plus privateonlycodeDSO offsets; ownO_EXCL0600output, no productionLD_PRELOAD/config/code/boot/security changes','limits':'Wholeprocessstartup/shutdown calls caller+server combines; counters instrument andsharedhost variation; never capacity or latencyacceptance','rollback':'Onlyownverifiedcompiler/processgroup <=180/35s; retain all inputs/results/stages, no cleanup'},indent=2)+'\n')
def invoke(argv,label,timeout,env=None):
 (d/(label+'-run-audit-before.json')).write_text(json.dumps({'argv':argv,'timeout':timeout,'environment_keys':sorted(env) if env else [],'only_child_interposition':bool(env and env.get('LD_PRELOAD')),'output_new':True})+'\n')
 with (d/(label+'-result.json')).open('wb') as out,(private/(label+'-stderr.log')).open('wb') as err:
  p=subprocess.Popen(argv,stdout=out,stderr=err,start_new_session=True,env=env);proc=pathlib.Path('/proc')/str(p.pid);ticks=(proc/'stat').read_text().rsplit(')',1)[1].split()[19]
  (d/(label+'-process.json')).write_text(json.dumps({'pid':p.pid,'pgid':p.pid,'start_ticks':ticks,'argv':argv})+'\n')
  try:code=p.wait(timeout=timeout)
  finally:
   if p.poll() is None:
    assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid and (proc/'cmdline').read_bytes().split(b'\0')[:len(argv)]==[x.encode() for x in argv]
    (d/(label+'-stop-audit-before.json')).write_text(json.dumps({'action':'Stoponlyownverifiedprocessgroup','pid':p.pid,'start_ticks':ticks,'argv':argv})+'\n');os.killpg(p.pid,signal.SIGTERM)
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:
     assert (proc/'stat').read_text().rsplit(')',1)[1].split()[19]==ticks and os.getpgid(p.pid)==p.pid;os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=3)
 assert code==0,label+' failed'
invoke(args,'compile-counter',180);assert so.is_file() and not so.is_symlink() and so.stat().st_size<1024*1024
reports=[];symbols_cache={}
for label,mode,wrapped in [('direct-counter','direct',True),('grpc-a1','grpc',False),('grpc-b1','grpc',True),('grpc-b2','grpc',True),('grpc-a2','grpc',False)]:
 env={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8'};raw=private/(label+'.jsonl');assert not raw.exists()
 if wrapped:env.update(LD_PRELOAD=str(so),TINYIMX_CLOCK_COUNTER_OUTPUT=str(raw))
 invoke([str(binary),mode,'300','10'],label,35,env)
 data=json.loads((d/(label+'-result.json')).read_text());assert data['status']=='SYNTHETIC_COMPONENT_COMPLETE' and data['planned']==data['synthetic_result_ok']==3000 and data['exceptions']==data['negative_matched_extra']==0 and len(data['raw'])==3000 and len({x[0] for x in data['raw']})==3000
 report={'case':label,'wrapped':wrapped,**{k:v for k,v in data.items() if k not in ['raw','raw_columns']}}
 if wrapped:
  assert raw.stat().st_size<1024*1024 and raw.stat().st_mode&0o777==0o600;rows=[json.loads(line) for line in raw.read_text().splitlines()];totals=rows.pop();assert totals['type']=='totals' and totals['records']==len(rows) and totals['fallback']==totals['overflow']==0 and totals['clock_bucket_ids']==[0,1,2,3,5,6,-1]
  assert all(x['type']=='caller' and len(x['counts'])==7 and all(isinstance(v,int) and v>=0 for v in x['counts']) for x in rows) and sum(sum(x['counts']) for x in rows)==totals['calls']>0
  grouped={}
  for row in rows:
   p=pathlib.Path(row['dso']).resolve();assert str(p) in libs and sha(p)==inputs[str(p)];elf=p.read_bytes();kind=struct.unpack_from('<H',elf,16)[0];assert kind in [2,3]
   address=row['offset']
   if kind==2:
    raise RuntimeError('ET_EXEC exactloadbias unverified; keepraw and refusewrong symbols')
   key=(str(p),address)
   if key not in symbols_cache:symbols_cache[key]=run(['addr2line','-f','-C','-e',str(p),hex(address)]).splitlines()[0]
   symbol=symbols_cache[key];group=(p.name,symbol);bucket=grouped.setdefault(group,[0]*7)
   for i,value in enumerate(row['counts']):bucket[i]+=value
  groups=[{'dso':k[0],'symbol':k[1],'counts':v,'calls':sum(v)} for k,v in grouped.items()];groups.sort(key=lambda x:x['calls'],reverse=True)
  report['clock_counter']={'calls':totals['calls'],'fallback':0,'overflow':0,'code_callers':len(rows),'clock_bucket_ids':totals['clock_bucket_ids'],'calls_per_synthetic_result_whole_process':totals['calls']/3000,'groups':groups};(d/(label+'-clock-symbol-groups.json')).write_text(json.dumps(report['clock_counter'],indent=2)+'\n')
 reports.append(report);assert runtime()==before;(d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n');print(json.dumps({'completed':label,'clock_calls':report.get('clock_counter',{}).get('calls'),'synthetic_correct':3000}),flush=True)
assert inputs=={name:sha(p) for name,p in libs.items()} and settings=={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in settings} and clockpath.read_text().strip()=='tsc' and runtime()==before
x={'status':'PRIVATE_CLOCK_CALLSITE_COMPONENT_PROBE_PASS','head':head,'counter_sha256':sha(so),'cases':reports,'all19_runtime_configs_preserved':True,'product_or_business_data_changes':False,'limits':'Sameownsynthetic20ms echoes loopback300/s10s,wholeprocessstartup/shutdown/client+server counts not actualGateway orMessageclockfrequency, immediatecaller symbols only; interpositionoverhead andhostvariance, no productionlatencyacceptance.','full_feature_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
