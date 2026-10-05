#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime,re,sys,os,signal,shlex,shutil
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'local-transport-component-probe-20261005';assert not d.exists()
def run(a,timeout=20):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
head=run(['git','rev-parse','HEAD']).strip();assert head==json.loads((b/'local-transport-component-probe-source-20261005/summary.json').read_text())['head']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024 and shutil.disk_usage(r).free>3*1024**3
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));return {'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:sha(p) for p in cfg.glob('*.json')}}
before=runtime();assert before==json.loads((b/'private-batch-message-deployment-retained-on-20261005/runtime-after.json').read_text())
settings={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in ['perf_event_paranoid','kptr_restrict']};assert settings=={'perf_event_paranoid':'4','kptr_restrict':'1'}
clockpath=pathlib.Path('/sys/devices/system/clocksource/clocksource0/current_clocksource');assert clockpath.read_text().strip()=='tsc'
image='sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060';assert json.loads(run(['docker','image','inspect',image]))[0]['Id']==image
mysql=json.loads(run(['docker','inspect','tinyimx-m21-mysql-1']))[0];assert mysql['Id']=='cc86cf7be0401c4a3727a8da2f9f95392f6824995617b06001bf04bb66d100d3' and mysql['State']['Pid']==2425
socksource='/var/lib/containerd/io.containerd.snapshotter.v1.overlayfs/snapshots/458/fs/run/mysqld/mysqld.sock'
mountrows=[x for x in run(['docker','exec',mysql['Id'],'cat','/proc/self/mountinfo']).splitlines() if x.split()[4]=='/'];assert len(mountrows)==1 and 'upperdir=/var/lib/containerd/io.containerd.snapshotter.v1.overlayfs/snapshots/458/fs,' in mountrows[0]
original_socket=run(['docker','exec',mysql['Id'],'stat','-Lc','%F %a %u %g %i %d','/run/mysqld/mysqld.sock']).strip();assert original_socket.startswith('socket 777 999 999 3932320 ')
previous=b/'mysql-unix-transport-probe-20261005-attempt4';assert json.loads((previous/'helper-failed.json').read_text())['status']=='FAIL' and json.loads((previous/'tcp-a1-result.json').read_text())['select_one_correct']==3000 and not (previous/'runtime-private/unix-b1/result.json').exists()
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def failed(k,e,t):
 (d/'helper-failed.json').write_text(json.dumps({'status':'FAIL','type':k.__name__,'message':str(e),'head':head,'product_changes':False})+'\n');sys.__excepthook__(k,e,t)
sys.excepthook=failed
build=r/'build/linux-release';csrc=r/'benchmark/local_capacity/mysql_public_socket_connect_probe.c';cpp=r/'benchmark/local_capacity/message_rpc_transport_probe.cpp';fp=build/'CMakeFiles/message_service_integration_tests.dir/flags.make';lp=build/'CMakeFiles/message_service_integration_tests.dir/link.txt'
flags=[]
for key in ['CXX_DEFINES','CXX_INCLUDES','CXX_FLAGS']:flags+=shlex.split(next(x for x in fp.read_text().splitlines() if x.startswith(key+' =')).split('=',1)[1])
binary=d/'message_rpc_transport_probe';cbinary=d/'mysql_public_socket_connect_probe';link=shlex.split(lp.read_text());old='CMakeFiles/message_service_integration_tests.dir/tests/message/message_service_integration_test.cpp.o';assert link.count(old)==1;link[link.index(old)]=str(d/'probe.o');link[link.index('-o')+1]=str(binary);removed=[x for x in link if x.startswith('libtinyimx_') and x!='libtinyimx_rpc_proto.a'];link=[x for x in link if x not in removed];assert 'libtinyimx_rpc_proto.a' in link and 'libtinyimx_message_grpc.a' not in link;link+=['-Wl,--wrap=bind','-Wl,--wrap=connect']
inputs={str(p.resolve()):sha(p) for p in [fp,lp,csrc,cpp]+[(build/x).resolve() for x in link if x.endswith(('.a','.so')) and (build/x).is_file()]}
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'head':head,'runtime':before,'inputs':inputs,'excluded_product_archives':removed,'original_socket_metadata':original_socket,'mount_row':mountrows[0],'operation':'OnepublicUnixconnect/errno withoutauthSQL then8syntheticRPC TCP/UnixABBA cases3000each atwait0/20ms; no domain data','policy':'Ownbe8 UID1000zeroCAP/readonly/no-net/nnp/defaultseccomp/lognone/CPU1;C64MiB/pids16, RPC512MiB/pids256,ELFRO andonlynewRPCcaseoutputRW','lifecycle':'Framework maycreate/unlink only ownfresh /opt/codex-output/rpc.sock verifiedabsent, neveranyexistingpath','limits':'OwncombinedclientserverCPU/loopback notvethprodnetwork, syntheticnotdomainlatency/capacity; sharedhost variation, no acceptance'},indent=2)+'\n')
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

compile_call(['gcc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(csrc),'-o',str(cbinary)],'compile-connect')
compile_call(['/usr/bin/c++',*flags,'-c',str(cpp),'-o',str(d/'probe.o')],'compile-rpc');compile_call(link,'link-rpc')
for exe in [binary,cbinary]:
 assert exe.read_bytes()[:4]==b'\x7fELF' and 'not found' not in run(['ldd',str(exe)]);(d/(exe.name+'-chmod-audit-before.json')).write_text(json.dumps({'path':str(exe),'sha256':sha(exe),'before':oct(exe.stat().st_mode&0o777),'after':'0o555','scope':'OnlynewownELF in0700stage'})+'\n');exe.chmod(0o555)
def invoke(exe,label,args,mounts,mem,pids,timeout):
 assert runtime()==before
 name='tinyimx-codex-local-transport-'+label+'-20261005';assert subprocess.run(['docker','container','inspect',name],capture_output=True).returncode!=0
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
connect,cid=invoke(cbinary,'mysql-public-connect',['public-connect'],[(socksource,'/opt/codex/mysql.sock',False)],64,16,10)
assert connect['status']=='PUBLIC_SOCKET_CONNECT_DIAGNOSTIC_COMPLETE' and connect['stat_ino']==3932320 and connect['auth_or_SQL']==False and connect['connect_result'] in [-1,0]
(d/'mysql-public-connect-classification.json').write_text(json.dumps({'diagnostic':connect,'original_overlay_socket':original_socket,'public_upper_inode_same_number_not_sufficient':True,'config_auth_SQL':False},indent=2)+'\n');print(json.dumps({'public_socket_connect':connect}),flush=True)
reports=[]
for wait in [0,20]:
 for side,transport in [('a1','tcp'),('b1','unix'),('b2','unix'),('a2','tcp')]:
  label='rpc-wait'+str(wait)+'-'+side;case=private/label;case.mkdir(mode=0o700);assert not (case/'rpc.sock').exists()
  data,cid=invoke(binary,label,[transport,'300','10',str(wait)],[(str(case),'/opt/codex-output',True)],512,256,35)
  assert data['status']=='SYNTHETIC_COMPONENT_COMPLETE' and data['planned']==data['synthetic_result_ok']==3000 and data['exceptions']==data['negative_matched_extra']==0 and len(data['raw'])==3000 and len({x[0] for x in data['raw']})==3000 and all(x[1] for x in data['raw']) and data['workers']==16 and data['controlled_handler_wait_ms']==wait and data['mode']==transport
  phy=data['physical_sockets'];assert phy['bad_peer']==0 and phy['bind_'+transport]>0 and phy['connect_'+transport]>0 and phy['bind_'+('unix' if transport=='tcp' else 'tcp')]==0 and phy['connect_'+('unix' if transport=='tcp' else 'tcp')]==0
  assert data['self_security']=={'CapEff':'0000000000000000','CapPrm':'0000000000000000','CapInh':'0000000000000000','CapBnd':'0000000000000000','CapAmb':'0000000000000000','NoNewPrivs':'1','Seccomp':'2'}
  report={'case':label,'container_id':cid,**{k:v for k,v in data.items() if k not in ['raw','raw_columns']}};reports.append(report);(d/'completed-case-summaries.json').write_text(json.dumps(reports,indent=2)+'\n');print(json.dumps({'completed':label,'synthetic_correct':3000,'physical':phy,'p99_ms':data['caller_wall']['p99_ms'],'CPU_cores':data['process_mean_cpu_cores']}),flush=True)
assert inputs=={p:sha(pathlib.Path(p)) for p in inputs} and runtime()==before and settings=={p:pathlib.Path('/proc/sys/kernel/'+p).read_text().strip() for p in settings} and clockpath.read_text().strip()=='tsc'
sql=run(['docker','exec',mysql['Id'],'sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-durability','SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count']).strip();assert sql=='1\t1\t1\t0\t0'
x={'status':'LOCAL_TRANSPORT_COMPONENT_PROBE_PASS','head':head,'mysql_public_connect':connect,'rpc_elf_sha256':sha(binary),'connect_elf_sha256':sha(cbinary),'cases':reports,'all19_runtime_configs_preserved':True,'production_data_changes':False,'SQL_durability':'1/1/1/0/0','full_feature_acceptance':False,'limits':'Syntheticsame-processcaller+server loopback/Unix300/s0or20mswait, physicalfamily verified, no actualveth/database/auth/10k/receiver/otherfeatures. Matchedrepeated CPU/tails required; no extremeperformance claim.'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({'status':x['status'],'cases':len(reports),'total_synthetic_correct':24000,'all19_preserved':True}),flush=True)
PY
