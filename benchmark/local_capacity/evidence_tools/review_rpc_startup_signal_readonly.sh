#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'rpc-startup-signal-readonly-20261006';assert not d.exists()
def run(a):return subprocess.check_output(a,text=True,timeout=30)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
cs=json.loads(run(['docker','inspect',*run(['docker','ps','--format','{{.Names}}']).splitlines()]))
assert len(cs)==19 and all(c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
d.mkdir(mode=0o700)
runtime={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
audit={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly /proc owned RPC processes sigmask and SDK header metadata; no signals/restarts/health/config/db changes','runtime_before':runtime}
(d/'audit-before.json').write_text(json.dumps(audit,indent=2)+'\n')
rows=[]
for c in cs:
 if not re.search(r'-(user|social|group|file|message)-service-',c['Name']):continue
 pid=c['State']['Pid'];tasks=[]
 for p in (pathlib.Path('/proc')/str(pid)/'task').iterdir():
  text=(p/'status').read_text();blocked=int(re.search(r'^SigBlk:\s+(\w+)',text,re.M).group(1),16)
  tasks.append({'tid':int(p.name),'name':re.search(r'^Name:\s+(.*)',text,re.M).group(1),'SIGINT_blocked':bool(blocked & (1<<1)),'SIGTERM_blocked':bool(blocked & (1<<14)),'sigblk':hex(blocked)})
 rows.append({'name':c['Name'],'pid':pid,'threads':len(tasks),'unblocked_SIGTERM':sum(not x['SIGTERM_blocked'] for x in tasks),'tasks':tasks})
build=r/'build/linux-release';f=build/'CMakeFiles/group_service_demo.dir/flags.make';assert f.exists()
sdk=[]
for p in build.rglob('health_check_service_interface.h'):sdk.append(str(p))
# Header roots from observed cached flags, never traverse user home.
flags=f.read_text()
for inc in re.findall(r'(?:-isystem\s+|-I)([^\s]+)',flags):
 base=pathlib.Path(inc)
 for relative in ['grpcpp/health_check_service_interface.h','grpcpp/generic/generic_stub.h','grpc/health/v1/health.pb.h']:
  q=base/relative
  if q.is_file():sdk.append(str(q))
header_metadata={}
for v in set(sdk):
 q=pathlib.Path(v);header_metadata[v]={'sha256':sha(q),'contents':q.read_text() if q.name=='health_check_service_interface.h' else None}
cs2=json.loads(run(['docker','inspect',*[c['Id'] for c in cs]]));assert {c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs2}==runtime
x={'status':'RPC_STARTUP_SIGNAL_READONLY_COMPLETE','services':rows,'headers':header_metadata,'flags_path':str(f),'flags_sha256':sha(f),'runtime_preserved':True}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n')
print(json.dumps({'status':x['status'],'services':[{k:v for k,v in row.items() if k!='tasks'} for row in rows],'headers':header_metadata,'runtime_preserved':True},indent=2))
PY
