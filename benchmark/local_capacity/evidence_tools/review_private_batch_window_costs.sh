#!/usr/bin/env bash
set -euo pipefail
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json
b=pathlib.Path('.local/codex')
for name in ['sqlbatch150A1','sqlbatch150B1','sqlbatch150B2','sqlbatch150A2']:
 d=b/('private-batch-endpoint-control-'+name)
 if not (d/'summary.json').exists():continue
 summary=json.loads((d/'summary.json').read_text());a=json.loads((d/'before-cgroup-cpu.json').read_text());z=json.loads((d/'after-cgroup-cpu.json').read_text());seconds=(z['monotonic_ns']-a['monotonic_ns'])/1e9
 cores={n:(v['usage_usec']-a['cpu'][n]['usage_usec'])/1e6/seconds for n,v in z['cpu'].items()}
 def system(which):
  lines=(d/(which+'-stat.txt')).read_text().splitlines();cpu=list(map(int,lines[0].split()[1:9]));return cpu,{line.split()[0]:int(line.split()[1]) for line in lines if line.startswith(('ctxt ','processes '))}
 s1,c1=system('before');s2,c2=system('after');delta=[y-x for x,y in zip(s1,s2)];total=sum(delta)
 def digest(which):
  values={}
  for line in (d/(which+'-digest.tsv')).read_text().splitlines():
   f=line.split('\t');assert len(f)==6
   values[f[0]]=(f[1],int(f[2]),int(f[3]),int(f[4]),int(f[5]))
  return values
 da=digest('before');dz=digest('after');rows=[]
 for key,v in dz.items():
  old=da.get(key,('',0,0,0,0));count=v[1]-old[1]
  if count<=0:continue
  rows.append({'statement':v[0],'count':count,'mean_statement_wall_ms':(v[2]-old[2])*1e-9/count,'rows_examined':v[4]-old[4]})
 rows.sort(key=lambda x:x['count']*x['mean_statement_wall_ms'],reverse=True)
 pressure={}
 for kind in ['cpu','io','memory']:
  texts=[(d/(which+'-'+kind+'-pressure.txt')).read_text().splitlines() for which in ['before','after']]
  def parse(lines):return {line.split()[0]:int(next(x.split('=',1)[1] for x in line.split() if x.startswith('total='))) for line in lines}
  p1,p2=map(parse,texts);pressure[kind]={k:(v-p1[k])/1e6/seconds*100 for k,v in p2.items()}
 x={'run':name,'phase':summary['phase'],'ack_p99_ms':summary['private']['positive_ack_p99_ms_upper_bin'],'scheduled_p99_ms':summary['private']['scheduled_to_ack_p99_ms_upper_bin'],'window_seconds':seconds,'whole_cgroup_cpu_cores':cores,'sum19_cores':sum(cores.values()),'guest_cpu_busy_pct':100*(total-delta[3]-delta[4])/total,'guest_cpu_system_pct':100*delta[2]/total,'guest_iowait_pct':100*delta[4]/total,'context_switches_per_second':(c2['ctxt']-c1['ctxt'])/seconds,'created_tasks_per_second':(c2['processes']-c1['processes'])/seconds,'PSI_delta_time_pct':pressure,'SQL_statement_wall_not_CPU_top':rows[:12],'limits':'Whole cgroups include all background work; server SQL elapsed time is not CPU. Sequential shared host. Independent p99 values are not additive and not averaged.'}
 print(json.dumps(x))
PY
