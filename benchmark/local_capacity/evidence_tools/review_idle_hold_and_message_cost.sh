#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,statistics,re,subprocess,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'idle-hold-message-cost-review-20261005'
assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
raw=b/'capacity-auth10kdiag';capacity=json.loads((raw/'summary.json').read_text())
assert capacity['status']=='PASS' and capacity['scenario']['mode']=='hold' and capacity['metrics']['login_ok']==10000 and capacity['metrics']['chat_ack_ok']==0
samples=[json.loads(x) for x in (b/'auth-phase-diagnostic-login-control-20261005/numeric-resources.jsonl').read_text().splitlines()]
start=int((raw/'control/start_ns').read_text());lo=start+5_000_000_000;hi=start+30_000_000_000
pairs=[(a,z) for a,z in zip(samples,samples[1:]) if lo<=a['monotonic_ns']<z['monotonic_ns']<=hi]
assert len(pairs)>=3
def stat(x):return {row.split()[0]:list(map(int,row.split()[1:])) for row in x['guest']['stat'].splitlines()}
cpu=[0]*8;seconds=0;ctxt=0;forks=0;cg={};pressure={};rows=[]
for a,z in pairs:
 elapsed=(z['monotonic_ns']-a['monotonic_ns'])/1e9;seconds+=elapsed;sa,sz=stat(a),stat(z)
 delta=[v-u for u,v in zip(sa['cpu'][:8],sz['cpu'][:8])];assert all(v>=0 for v in delta)
 cpu=[u+v for u,v in zip(cpu,delta)];ctxt+=sz['ctxt'][0]-sa['ctxt'][0];forks+=sz['processes'][0]-sa['processes'][0]
 for name,x in a['containers'].items():
  def values(s):return {k:int(v) for k,v in (row.split() for row in s.splitlines())}
  u,v=values(x['cpu_stat']),values(z['containers'][name]['cpu_stat']);cg[name]=cg.get(name,0)+v['usage_usec']-u['usage_usec']
 for kind in ['cpu','io','memory']:
  def total(s):return {row.split()[0]:int(re.search(r'total=(\d+)',row).group(1)) for row in s.splitlines()}
  u,v=total(a['guest'][kind+'_pressure']),total(z['guest'][kind+'_pressure'])
  for key in u:pressure[(kind,key)]=pressure.get((kind,key),0)+v[key]-u[key]
 rows.append({'start_utc':a['utc'],'end_utc':z['utc'],'start_monotonic_ns':a['monotonic_ns'],'end_monotonic_ns':z['monotonic_ns'],'seconds':elapsed})
def utc(s):return datetime.datetime.fromisoformat(s.replace('Z','+00:00')).astimezone(datetime.timezone.utc)
anchor=samples[0];start_utc=utc(anchor['utc'])+datetime.timedelta(seconds=(start-anchor['monotonic_ns'])/1e9)
blocks=re.split(r'(?m)^(\d{4}-\d\d-\d\dT[^\n]+)\n',(raw/'guest-resources.log').read_text());docker={};frames=0
for i in range(1,len(blocks),2):
 if not start_utc+datetime.timedelta(seconds=6)<=utc(blocks[i])<=start_utc+datetime.timedelta(seconds=30):continue
 frames+=1
 for name,v in re.findall(r'(tinyimx-[a-zA-Z0-9_-]+) CPU=([\d.]+)%',blocks[i+1]):docker.setdefault(name,[]).append(float(v)/100)
assert frames>=3 and len(docker)==19
names=['user','nice','system','idle','iowait','irq','softirq','steal'];guest={name:v*100/sum(cpu) for name,v in zip(names,cpu)}
guest.update({'busy_percent':100-guest['idle']-guest['iowait'],'system_plus_softirq_percent':guest['system']+guest['softirq'],'context_switches_per_second':ctxt/seconds,'guestwide_forks_threads_per_second':forks/seconds})
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Read-only comparison of existing 10k no-chat steady hold and completed private message counter windows','reads':'Completed auth hold and two completed storage windows; no new load or runtime change','writes':'Fresh bounded numeric summary and exact selected intervals','selection':'Discard first5s of hold; numeric intervals fully enclosed in5..30s, Docker rolling samples6..30s','limits':'Sequential shared-host runs; auth diagnostic User differs but no authentication in selected hold; rolling Docker samples not cgroup integrals; UTC reconstructed from saved simultaneous monotonic/UTC anchor, negligible call gap possible; never infer exclusive kernel overhead from summed Docker CPU; no historical service-thread count from init PID'},indent=2)+'\n')
out={'status':'IDLE_HOLD_MESSAGE_COST_REVIEW_COMPLETED','hold_run':'auth10kdiag','hold_start_monotonic_ns':start,'hold_start_utc_from_anchor':start_utc.isoformat(),'selected_seconds':seconds,'selected_intervals':rows,'guest':guest,'cgroup_exact_selected_cpu_cores':{name:v/1e6/seconds for name,v in cg.items()},'pressure_stall_percent':{kind:{key:v/1e6/seconds*100 for (k,key),v in pressure.items() if k==kind} for kind in ['cpu','io','memory']},'docker_frames':frames,'docker_cpu_rolling_ranking':sorted([{'container':name,'mean_cpu_cores':statistics.mean(vals),'samples':len(vals)} for name,vals in docker.items()],key=lambda x:x['mean_cpu_cores'],reverse=True),'message_comparison':json.loads((b/'commit-comparison-container-cpu-review-20261005/summary.json').read_text()),'performance_acceptance':False,'source_hashes':{str(p.relative_to(b)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [raw/'summary.json',raw/'control/start_ns',raw/'guest-resources.log',b/'auth-phase-diagnostic-login-control-20261005/numeric-resources.jsonl']}}
(d/'summary.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps({k:v for k,v in out.items() if k not in ['message_comparison','source_hashes']},indent=2))
PY
