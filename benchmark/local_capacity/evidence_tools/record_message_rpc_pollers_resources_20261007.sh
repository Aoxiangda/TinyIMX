#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib
r=pathlib.Path.cwd();d=r/'.local/codex/message-rpc-pollers-resource-snapshot-20261007';assert not d.exists();d.mkdir(mode=0o700)
save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10))
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Only readonly proc pressure/memory and vmstat1s5 and selected numeric Message process/thread counts during existing poller control. No signal/config/load/cache/system tuning.'})
run=lambda a:subprocess.check_output(a,text=True,stderr=subprocess.STDOUT,timeout=12)
save('snapshot-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'meminfo':{k:int(v.split()[0]) for k,v in (line.split(':',1) for line in pathlib.Path('/proc/meminfo').read_text().splitlines()) if k in ['MemTotal','MemAvailable','SwapTotal','SwapFree']},'pressure':{name:(pathlib.Path('/proc/pressure')/name).read_text() for name in ['cpu','memory','io']}})
(d/'vmstat-5-seconds.txt').write_text(run(['vmstat','1','5']))
cs=json.loads(run(['docker','inspect',*run(['docker','ps','--format','{{.Names}}']).splitlines()]));message=[c for c in cs if c['Config'].get('Labels',{}).get('com.docker.compose.service')=='message-service'];assert len(message)==1;c=message[0];pid=c['State']['Pid'];stat=pathlib.Path('/proc')/str(pid)/'status';save('message-process-snapshot.json',{'container':c['Name'],'image':c['Image'],'id':c['Id'],'started':c['State']['StartedAt'],'pid':pid,'status':{k:v.strip() for k,v in (line.split(':',1) for line in stat.read_text().splitlines()) if k in ['Name','Threads','VmRSS','VmSwap','voluntary_ctxt_switches','nonvoluntary_ctxt_switches']},'poller_profile':next((v.split('=',1)[1] for v in c['Config']['Env'] if v.startswith('TINYIMX_MESSAGE_RPC_POLLERS_ENABLE=')),None)})
x={'status':'READONLY_RESOURCE_SNAPSHOT_SAVED','limited_interval_seconds':4,'not_entire_run_or_host_cpu_proof':True,'files':[p.name for p in d.iterdir()]};save('summary.json',x);print(json.dumps(x))
PY
