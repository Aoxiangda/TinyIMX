#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time
r=pathlib.Path.cwd();d=r/'.local/codex/message-rpc-pollers-cpu-limits-20261007-attempt2';assert not d.exists();d.mkdir(mode=0o700);save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10));run=lambda a:subprocess.check_output(a,text=True,stderr=subprocess.STDOUT,timeout=15)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly exact own project Gateway/Message/MySQL process identities, CPU quota/affinity/stat. First snapshot State.Pid is docker-init and its Threads1 is not Message thread count. Save correction explicitly; no signals/config/runtime/load/data changes.'})
names=run(['docker','ps','--format','{{.Names}}']).splitlines();cs=json.loads(run(['docker','inspect',*names]));selected=[c for c in cs if c['Config'].get('Labels',{}).get('com.docker.compose.service') in ['gateway-a','gateway-b','message-service','mysql']]
save('previous-attempt-limitation.json',{'stage':'message-rpc-pollers-cpu-limits-20261007','failure':'Unprivileged SSH cannot read /proc/container-init/root/sys/fs/cgroup/cpu.max; failed readonly before summary, audit retained. Attempt2 reads own container cgroup through Docker exec cat, no privilege or filesystem setting change.'})
result=[]
for c in selected:
 role=c['Config']['Labels']['com.docker.compose.service'];pid=c['State']['Pid'];root=pathlib.Path('/proc')/str(pid)/'root/sys/fs/cgroup';hc=c['HostConfig'];top=run(['docker','top',c['Id'],'-eo','pid,ppid,comm,pcpu,nlwp']);process=[]
 for line in top.splitlines()[1:]:
  fields=line.split();ppid=int(fields[0]);status=pathlib.Path('/proc')/str(ppid)/'status';process.append({'pid':ppid,'comm':fields[2],'status':{k:v.strip() for k,v in (x.split(':',1) for x in status.read_text().splitlines()) if k in ['Name','Threads','VmRSS','VmSwap','Cpus_allowed_list','voluntary_ctxt_switches','nonvoluntary_ctxt_switches']}})
 cg={}
 for f in ['cpu.max','cpu.stat','cpu.pressure','cpuset.cpus.effective']:
  probe=subprocess.run(['docker','exec',c['Id'],'cat','/sys/fs/cgroup/'+f],capture_output=True,text=True,timeout=5)
  cg[f]={'exit_code':probe.returncode,'text':probe.stdout,'error':probe.stderr}
 result.append({'role':role,'image':c['Image'],'container_id':c['Id'],'started':c['State']['StartedAt'],'host_cpu_limits':{k:hc.get(k) for k in ['NanoCpus','CpuQuota','CpuPeriod','CpusetCpus','CpuShares','Memory','MemorySwap']},'poller_profile':next((v.split('=',1)[1] for v in c['Config']['Env'] if v.startswith('TINYIMX_MESSAGE_RPC_POLLERS_ENABLE=')),None),'processes':process,'cgroup':cg})
save('summary.json',{'status':'READONLY_CPU_LIMITS_AND_INIT_PID_CORRECTION_SAVED','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'roles':result,'limits':'Single timestamp counters cumulative not attribution of full measured run; docker-init thread count must not be interpreted as Message polling threads.'});print(json.dumps({'status':'READONLY_CPU_LIMITS_AND_INIT_PID_CORRECTION_SAVED','roles':[{'role':v['role'],'host_cpu_limits':v['host_cpu_limits'],'poller_profile':v['poller_profile'],'processes':v['processes'],'cgroup':v['cgroup']} for v in result]},indent=2))
PY
