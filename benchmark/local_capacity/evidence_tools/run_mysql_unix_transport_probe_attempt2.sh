#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime,re,sys,os,signal,shlex,shutil
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'mysql-unix-transport-probe-20261005-attempt2';assert not d.exists()
def run(a,timeout=20):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
head=run(['git','rev-parse','HEAD']).strip();assert head==json.loads((b/'mysql-unix-transport-probe-attempt2-source-20261005/summary.json').read_text())['head']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024 and shutil.disk_usage(r).free>3*1024**3
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');config=cfg/'message.json'
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'private-batch-message-deployment-retained-on-20261005/runtime-after.json').read_text())
settings={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in ['perf_event_paranoid','kptr_restrict']};assert settings=={'perf_event_paranoid':'4','kptr_restrict':'1'}
clockpath=pathlib.Path('/sys/devices/system/clocksource/clocksource0/current_clocksource');assert clockpath.read_text().strip()=='tsc'
mysql=json.loads(run(['docker','inspect','tinyimx-m21-mysql-1']))[0];assert mysql['Id']=='cc86cf7be0401c4a3727a8da2f9f95392f6824995617b06001bf04bb66d100d3' and mysql['Image']=='sha256:d58ac93387f644e4e040c636b8f50494e78e5afc27ca0a87348b2f577da2b7ff' and mysql['State']['Pid']==2425
network='tinyimx-m21_backend';assert list(mysql['NetworkSettings']['Networks'])==[network];ip=mysql['NetworkSettings']['Networks'][network]['IPAddress'];assert ip
sock='/var/run/mysqld/mysqld.sock';socksource='/proc/2425/root'+sock
def socket_proof():
 c=json.loads(run(['docker','inspect',mysql['Id']]))[0];assert c['Id']==mysql['Id'] and c['State']['Pid']==2425 and c['State']['StartedAt']==mysql['State']['StartedAt']
 x=run(['docker','exec',mysql['Id'],'stat','-Lc','%F %a %u %g %i',sock]).strip();assert x=='socket 777 999 999 3932320';return x
old=b/'mysql-unix-transport-probe-20261005';assert json.loads((old/'helper-failed.json').read_text())['status']=='FAIL' and sha(old/'runtime-private/compile.log')=='78fd5d4dd56fe785e9da6508967f7f0a7b67ceb4764ed06a674415e372bad946' and not list(old.glob('*container*'));socket_before=socket_proof();fields=json.loads(config.read_text())['mysql'];assert fields['pool_size']==16;del fields
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def failed(k,e,t):
 (d/'helper-failed.json').write_text(json.dumps({'status':'FAIL','type':k.__name__,'message':str(e),'head':head,'product_changes':False})+'\n');sys.__excepthook__(k,e,t)
sys.excepthook=failed
src=r/'benchmark/local_capacity/mysql_unix_transport_probe.cpp';build=r/'build/linux-release';fp=build/'CMakeFiles/mysql_pool_demo.dir/flags.make';lp=build/'CMakeFiles/mysql_pool_demo.dir/link.txt';assert sha(fp)=='e2ee07aa04f61fd06ed5ab7a24a9d2d5413626e5279e096f33888685d40d0a9a' and sha(lp)=='b7f3b8a222f3d383b8f3b2ffc19bf1a98787abd1fb035564639858c39aecf718'
flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in fp.read_text().splitlines() if x.startswith(key+' =')).split('=',1)[1])
binary=d/'mysql_unix_transport_probe';link=shlex.split(lp.read_text());old='CMakeFiles/mysql_pool_demo.dir/examples/mysql_pool_demo.cpp.o';assert link.count(old)==1;link[link.index(old)]=str(d/'probe.o');link[link.index('-o')+1]=str(binary);link+=['-Wl,--wrap=mysql_real_connect'];assert 'libtinyimx_db.a' in link
inputs={str(p.resolve()):sha(p) for p in [fp,lp,src]+[(build/x).resolve() for x in link if x.endswith(('.a','.so')) and (build/x).is_file()]}
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'runtime':before,'source_sha256':sha(src),'inputs_sha256':inputs,'link_argv':link,'operation':'OwnnativehealthyPING+SELECT1 ABBA exactTCP/socket family, no realtables orSQLwrites','mysql_identity':{'id':mysql['Id'],'image':mysql['Image'],'started':mysql['State']['StartedAt'],'init_pid':2425},'socket_public_only':{'source':socksource,'metadata':socket_before,'readonly':True},'limits':'Ownfixedsocketinodebind diagnostic only, productionMySQLrestart wouldinvalidate bind; notdeployment. NativeSSL negotiation explicitlyreported perphysicaltransport, no globalsettings/authchange. SimpleopP99/processCPU andwholeMySQLcgroup notmessage/fullcapacity.','container_policy':'OwnUID1000zeroCAP/readonly/defaultseccomp/nnp/memory256MiB/CPU1/pids64/lognone; backendnetwork; readonlyownELF+exactconfig+publicsocket, onlyfreshowncaseoutputRW','stop':'180sowncompiler exactPID/start/cmdline/PGID,45sexactownCID/name/image/created/entry','rollback':'Retainallinputs/output/failures/stoppeddiagnosticcontainers, all19/config/durability/apps fixed; no delete'},indent=2)+'\n')
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
compile_call(['/usr/bin/c++',*flags,'-c',str(src),'-o',str(d/'probe.o')],'compile');compile_call(link,'link');assert binary.read_bytes()[:4]==b'\x7fELF' and 'not found' not in run(['ldd',str(binary)])
(d/'elf-permission-audit-before.json').write_text(json.dumps({'path':str(binary),'sha256':sha(binary),'old_mode':oct(binary.stat().st_mode&0o777),'new_mode':'0o555','scope':'FreshownELF only in0700stage'})+'\n');binary.chmod(0o555)
image='sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060';assert json.loads(run(['docker','image','inspect',image]))[0]['Id']==image
cg=next(x.split(':',2)[2] for x in pathlib.Path('/proc/2425/cgroup').read_text().splitlines() if x.startswith('0::'));cpup=pathlib.Path('/sys/fs/cgroup')/cg.lstrip('/')/'cpu.stat'
def snapshot():return {'mono_ns':__import__('time').monotonic_ns(),'cpu_stat':{k:int(v) for k,v in (x.split() for x in cpup.read_text().splitlines())}}
reports=[]
for label,transport in [('tcp-a1','tcp'),('unix-b1','unix'),('unix-b2','unix'),('tcp-a2','tcp')]:
 assert runtime()==before and socket_proof()==socket_before
 case=private/label;case.mkdir(mode=0o700);entry='/opt/codex/probe';argv=['ping-query','300','10',ip,'/opt/codex/config.json','/opt/codex-output/result.json'];name='tinyimx-codex-mysql-transport-'+label+'-20261005-attempt2';assert subprocess.run(['docker','container','inspect',name],capture_output=True).returncode!=0
 mounts=[(str(binary.resolve()),entry,False),(str(config),'/opt/codex/config.json',False),(socksource,'/opt/codex/mysql.sock',False),(str(case),'/opt/codex-output',True)]
 cmd=['docker','create','--pull','never','--name',name,'--user','1000:1000','--read-only','--network',network,'--cap-drop','ALL','--security-opt','no-new-privileges:true','--pids-limit','64','--memory','256m','--cpus','1','--log-driver','none','--env','TINYIMX_READONLY_TRANSPORT_MODE='+transport]
 for path,dst,rw in mounts:cmd+=['--mount','type=bind,src='+path+',dst='+dst+('' if rw else ',readonly')]
 cmd+=['--entrypoint',entry,image,*argv];(d/(label+'-create-audit-before.json')).write_text(json.dumps({'argv':cmd,'expected_queries':3000,'expected_ping':3000,'private_config_sha256':sha(config)},indent=2)+'\n');cid=run(cmd).strip();c=json.loads(run(['docker','inspect',cid]))[0];h=c['HostConfig']
 assert c['Name']=='/'+name and c['Image']==image and c['Config']['User']=='1000:1000' and c['Config']['Entrypoint']==[entry] and c['Config']['Cmd']==argv and c['State']['Status']=='created'
 assert h['ReadonlyRootfs'] and h['NetworkMode']==network and set(h['CapDrop'])=={'ALL'} and not h['CapAdd'] and h['PidMode']=='' and not h['Privileged'] and h['SecurityOpt']==['no-new-privileges:true'] and h['LogConfig']['Type']=='none' and h['Memory']==256*1024*1024 and h['PidsLimit']==64 and h['NanoCpus']==1000000000
 assert len(c['Mounts'])==4 and {x['Destination'] for x in c['Mounts'] if x['RW']}=={'/opt/codex-output'}
 for path,dst,rw in mounts:
  m=next(x for x in c['Mounts'] if x['Destination']==dst);assert m['Source']==path and m['RW']==rw
 (d/(label+'-container-identity.json')).write_text(json.dumps({'id':cid,'name':name,'image':image,'created':c['Created'],'args':argv,'policy':h,'mounts':c['Mounts']},indent=2)+'\n');first=snapshot()
 with (case/'stdout.log').open('wb') as out,(case/'stderr.log').open('wb') as err:
  try:code=subprocess.run(['docker','start','-a',cid],stdout=out,stderr=err,timeout=45).returncode
  except subprocess.TimeoutExpired:
   curr=json.loads(run(['docker','inspect',cid]))[0];assert curr['Id']==cid and curr['Name']=='/'+name and curr['Image']==image and curr['Created']==c['Created'] and curr['Config']['Entrypoint']==[entry];(d/(label+'-stop-audit-before.json')).write_text(json.dumps({'id':cid,'name':name,'created':c['Created'],'action':'Stoponlyownexact45sdiagnostic'})+'\n');subprocess.run(['docker','stop','-t','2',cid],check=True,timeout=8);raise
 last=snapshot();c=json.loads(run(['docker','inspect',cid]))[0];assert code==0 and c['State']['Status']=='exited' and c['State']['ExitCode']==0 and not c['State']['OOMKilled'],label+' nativeconnect/queryfailed; privateoutput retained'
 raw=case/'result.json';assert raw.is_file() and not raw.is_symlink() and raw.stat().st_mode&0o777==0o600;data=json.loads(raw.read_text());assert data['status']=='READONLY_CONNECTION_COMPONENT_COMPLETE' and data['planned']==data['select_one_correct']==data['ping_calls']==3000 and data['exceptions']==0 and len(data['raw'])==3000 and len({x[0] for x in data['raw']})==3000 and data['thread_cpu']['count']==3000 and data['transport_mode']==transport
 families=data['connect_families'];assert families['bad']==0 and families['tcp']==(16 if transport=='tcp' else 0) and families['unix']==(16 if transport=='unix' else 0) and 0<=families['tls']<=16
 (d/(label+'-result.json')).write_text(json.dumps(data)+'\n');delta={k:last['cpu_stat'][k]-v for k,v in first['cpu_stat'].items()};assert all(x>=0 for x in delta.values());elapsed=(last['mono_ns']-first['mono_ns'])/1e9
 report={'case':label,'container_id':cid,**{k:v for k,v in data.items() if k not in ['raw','raw_columns']},'whole_mysql_cgroup':{'before':first,'after':last,'delta':delta,'mean_cores':delta['usage_usec']/1e6/elapsed,'limits':'Wholecgroup includesexistingbackground/setup/close, notexclusiveSQLcallCPU'}};reports.append(report);(d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n');assert runtime()==before and socket_proof()==socket_before;print(json.dumps({'completed':label,'all3000_correct':True,'transport_families':families,'caller_p99_ms':data['caller_wall']['p99_ms']}),flush=True)
assert inputs=={p:sha(pathlib.Path(p)) for p in inputs} and settings=={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in settings} and clockpath.read_text().strip()=='tsc' and runtime()==before and socket_proof()==socket_before
query='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count';sql=run(['docker','exec',mysql['Id'],'sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-durability',query]).strip();assert sql=='1\t1\t1\t0\t0'
x={'status':'MYSQL_UNIX_TCP_READONLY_COMPONENT_PROBE_PASS','head':head,'probe_elf_sha256':sha(binary),'cases':reports,'all19_runtime_configs_preserved':True,'production_data_changes':False,'SQL_durability':'1/1/1/0/0','limits':'PING+SELECT1, sameactualruntimeimage+network/CPU/memory, physicalTCP/Unix families verified. SSL negotiatedstatus explicit; privateinodebind cannotclaimrestartrobust deployment. Simplequery/measurednativeprocess/wholeMySQLcgroup notrealmessage/allfeatures/capacity orcausalwallP99proof.','full_feature_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
