#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,time,datetime,os,subprocess,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'login-path-idle-process-cpu-20261006';assert not d.exists()
restore=json.loads((b/'login-path-diagnostic-run-20261006/restore-summary.json').read_text());assert restore['status']=='ORIGINAL_THREE_SERVICES_RESTORED'
def run(a):return subprocess.check_output(a,text=True,timeout=25)
def identity():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 return {c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in json.loads(run(['docker','inspect',*names]))}
before=identity();assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
d.mkdir(mode=0o700);(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly10second delta census of procstat without argv/env, no owned pressure running, preserveall19 identities','before_runtime':before,'writes':'Own numeric snapshots/summary only','limits':'Only processes alive in both snapshots; short-lived children missed. Idle interval after service restore is not historical failed-ramp attribution.'},indent=2)+'\n')
def capture():
 out={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'monotonic_ns':time.monotonic_ns(),'guest_stat':pathlib.Path('/proc/stat').read_text(),'cpu_pressure':pathlib.Path('/proc/pressure/cpu').read_text(),'io_pressure':pathlib.Path('/proc/pressure/io').read_text(),'meminfo':pathlib.Path('/proc/meminfo').read_text(),'processes':{}}
 for p in pathlib.Path('/proc').iterdir():
  if not p.name.isdigit():continue
  try:
   s=p.joinpath('stat').read_text();f=s.rsplit(')',1)[1].split()
   out['processes'][p.name]={'comm':s.split('(',1)[1].rsplit(')',1)[0],'ppid':int(f[1]),'start_ticks':f[19],'utime':int(f[11]),'stime':int(f[12]),'threads':int(f[17])}
  except (FileNotFoundError,ProcessLookupError,PermissionError):pass
 return out
a=capture();time.sleep(10);z=capture();seconds=(z['monotonic_ns']-a['monotonic_ns'])/1e9;hz=os.sysconf('SC_CLK_TCK')
rows=[]
for pid,x in a['processes'].items():
 y=z['processes'].get(pid)
 if y is None or y['start_ticks']!=x['start_ticks']:continue
 u=(y['utime']-x['utime'])/hz/seconds;s=(y['stime']-x['stime'])/hz/seconds
 rows.append({'pid':int(pid),'comm':x['comm'],'ppid':x['ppid'],'start_ticks':x['start_ticks'],'cpu_cores':u+s,'user_cores':u,'system_cores':s,'threads_start':x['threads'],'threads_end':y['threads']})
v1=list(map(int,a['guest_stat'].splitlines()[0].split()[1:9]));v2=list(map(int,z['guest_stat'].splitlines()[0].split()[1:9]));dt=[y-x for x,y in zip(v1,v2)];tot=sum(dt)
x={'status':'IDLE_PROCESS_CPU_CENSUS_COMPLETE','seconds':seconds,'logical_cpus':os.cpu_count(),'guest_busy_fraction':1-(dt[3]+dt[4])/tot,'guest_user_fraction':(dt[0]+dt[1])/tot,'guest_system_fraction':dt[2]/tot,'guest_softirq_fraction':dt[6]/tot,'guest_iowait_fraction':dt[4]/tot,'guest_steal_fraction':dt[7]/tot,'matched_process_cpu_cores':sum(v['cpu_cores'] for v in rows),'top40_processes':sorted(rows,key=lambda v:v['cpu_cores'],reverse=True)[:40],'process_count_before':len(a['processes']),'process_count_after':len(z['processes']),'runtime_preserved':identity()==before,'limits':'Postrestore idle; active-ramp mix not inferred. Alive-only census misses short-livedhealthcheck subprocesses; kernel accounting and task deltas may differ.'}
assert x['runtime_preserved']
(d/'before-numeric.json').write_text(json.dumps(a,indent=2)+'\n');(d/'after-numeric.json').write_text(json.dumps(z,indent=2)+'\n');(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
