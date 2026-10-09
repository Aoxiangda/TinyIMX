#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'message-user-system-cpu-review-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
out=[]
for stage,run in [('storage-wait-diagnostic-control-20261005','sw150a'),('message-poller-diagnostic-control-20261005','mp150diag')]:
 samples=[json.loads(x) for x in (b/stage/'scheduler-numeric.jsonl').read_text().splitlines()];start=int((b/('capacity-'+run)/'control/start_ns').read_text())
 samples=[x for x in samples if start<=x['monotonic_ns']<=start+60_000_000_000];assert len(samples)>=8
 a,z=samples[0],samples[-1];seconds=(z['monotonic_ns']-a['monotonic_ns'])/1e9;rows=[]
 for name,u in a['processes'].items():
  v=z['processes'][name];assert u['host_pid']==v['host_pid'];assert u['cpu_max']==v['cpu_max'];before,after=u['cpu_stat'],v['cpu_stat']
  delta={key:after[key]-before[key] for key in ['usage_usec','user_usec','system_usec','throttled_usec']};assert all(x>=0 for x in delta.values())
  rows.append({'container':name,'seconds':seconds,'cpu_max':u['cpu_max'],'cpu_cores':delta['usage_usec']/1e6/seconds,'user_cpu_cores':delta['user_usec']/1e6/seconds,'system_cpu_cores':delta['system_usec']/1e6/seconds,'system_fraction_of_user_plus_system':delta['system_usec']/(delta['system_usec']+delta['user_usec']) if delta['system_usec']+delta['user_usec'] else None,'own_throttled_usec_delta':delta['throttled_usec'],'pid_sampled_is_init':True})
 out.append({'run':run,'first_utc':a['utc'],'last_utc':z['utc'],'samples':len(samples),'max_snapshot_ms':max(x['snapshot_elapsed_ms'] for x in samples),'container_cpu':sorted(rows,key=lambda x:x['cpu_cores'],reverse=True)})
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Read-only cgroup user/system CPU breakdown from frozen original-poller and rejected-poller diagnostic windows','reads':'Existing cgroup cpu.stat counters only, exact active enclosed first/last snapshot','writes':'Fresh numeric summary','limits':'Cgroup includes service children even when sampled process PID is init; never use init/task counts as service threads; cgroup system CPU does not include all interrupt/softirq accounting and user+system may differ slightly fromusage; sequential shared-host trials limit causal percentages; no schedstat runqueue claims','runtime_changes':False,'new_load':False},indent=2)+'\n')
x={'status':'MESSAGE_USER_SYSTEM_CPU_REVIEW_COMPLETED','runs':out,'performance_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
