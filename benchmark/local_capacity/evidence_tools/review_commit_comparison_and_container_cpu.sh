#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,re,datetime,statistics,subprocess
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'commit-comparison-container-cpu-review-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
paths=[('io150base2','storage-io-baseline-after-auth-review-control-20261005','storage-io-baseline-after-auth-analysis-20261005'),('gc150u1000','groupcommit-sync-delay-diagnostic-20261005','groupcommit-sync-delay-completed-analysis-20261005')]
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly completed pairedcounter/SQLcost/container CPU ranking review, no newparameter trial','reads':'Twofrozen58.4scounteranalyses andrawguestresourceDockerstats frames selectedbyexactUTCsnapshotwindow','writes':'Freshcomparison/ranks only','limits':'Sequentialsharedhost trials notcausal proof ofallchanges; fileMISC notpurefsync, meansnotP99; DockerCPUframes are rollinginterval snapshots notexactcgroupintegrals. PIDS counts cgroupthreads/processes, notpureworkerconcurrency','runtime_changes':False},indent=2)+'\n');out=[]
def utc(s):return datetime.datetime.fromisoformat(s.replace('Z','+00:00')).astimezone(datetime.timezone.utc)
for run,stage,analysis in paths:
 a=json.loads((b/analysis/'summary.json').read_text());control=b/stage;lo=utc(json.loads((control/'before-snapshot.json').read_text())['utc']);hi=utc(json.loads((control/'after-snapshot.json').read_text())['utc'])
 text=(b/('capacity-'+run)/'guest-resources.log').read_text();blocks=re.split(r'(?m)^(\d{4}-\d\d-\d\dT[^\n]+)\n',text);cpu={};memory={};frames=0
 for i in range(1,len(blocks),2):
  if not lo<=utc(blocks[i])<=hi:continue
  frames+=1
  for name,value,mem,pids in re.findall(r'(tinyimx-[a-zA-Z0-9_-]+) CPU=([\d.]+)% MEM=([^\n]+?) PIDS=(\d+)',blocks[i+1]):
   cpu.setdefault(name,[]).append(float(value));memory.setdefault(name,[]).append({'memory':mem,'cgroup_pids':int(pids)})
 assert frames>1 and len(cpu)==19
 ranks=sorted([{'container':name,'sample_count':len(vals),'mean_cpu_cores':statistics.mean(vals)/100,'max_cpu_cores':max(vals)/100,'max_cgroup_pids':max(x['cgroup_pids'] for x in memory[name]),'last_memory':memory[name][-1]['memory']} for name,vals in cpu.items()],key=lambda x:x['mean_cpu_cores'],reverse=True)
 statements={row['digest']:row for row in a['statement_deltas']};commit=next(x for x in statements.values() if x['normalized_sql']=='COMMIT');confirm=next(x for x in statements.values() if x['normalized_sql'].startswith('UPDATE `im_private_messages` SET `delivery_status`'))
 out.append({'run':run,'ack_p99_ms':a['private']['positive_ack_p99_ms_upper_bin'],'scheduled_p99_ms':a['private']['scheduled_to_ack_p99_ms_upper_bin'],'all9000positive':a['private']['metrics']['chat_ack_ok']==9000,'counter_elapsed_seconds':a['counter_elapsed_seconds'],'redo_fsync_per_second':a['global_status']['Innodb_os_log_fsyncs']['per_second'],'binlog_file_misc_per_second':a['file_waits']['wait/io/file/sql/binlog']['misc_per_second'],'binlog_file_misc_mean_ms':a['file_waits']['wait/io/file/sql/binlog']['misc_mean_ms'],'commit_statement_mean_ms':commit['mean_wait_ms'],'confirm_update_mean_ms':confirm['mean_wait_ms'],'guest_busy_percent':100-a['guest_cpu']['idle']-a['guest_cpu']['iowait'],'guest_system_softirq_percent':a['guest_cpu']['system']+a['guest_cpu']['softirq'],'cpu_psi_stall_percent':a['pressure']['cpu']['some']['stall_share_pct'],'guestwide_forks_threads_per_second':a['guest_cpu']['forks_threads_per_second'],'resource_frames':frames,'container_cpu_ranking':ranks})
x={'status':'COMMIT_AND_CONTAINER_CPU_COMPARISON_COMPLETED','runs':out,'candidate_accepted':False,'decision':'Reject1000us: binlogMISC roughlyhalves butredofsnyc unchangedaround220/s, COMMIT/confirmUPDATE andP99 notimproved; allsettings alreadyrestored. Do nottryblinddelaymatrix. AnalyzeactualdominantserviceCPU/RPC/scheduler costs before nextchange.','limitations':'Runtimes/config/index remainidentical; sharedhostvariation limitscausalpercentages; rankingrollingCPUframes notcontinuousintegrals, no payload ornewload.'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
