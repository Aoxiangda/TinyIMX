#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';parent=b/'storage-io-baseline-after-auth-review-control-20261005';d=b/'storage-io-baseline-after-auth-analysis-20261005';assert (parent/'summary.json').exists() and not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
s=json.loads((parent/'summary.json').read_text());assert not s['observer_error'],'Counter diagnostics must succeed before attribution'
meta={k:json.loads((parent/(k+'-snapshot.json')).read_text()) for k in ['before','after']};elapsed=(meta['after']['started_monotonic_ns']-meta['before']['started_monotonic_ns'])/1e9
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Read-only active-window counter-delta analysis after full control completion','writes':'Fresh numeric/normalized digest output only','limits':'Two nonsimultaneous read snapshots, samplingoverhead bounded in metadata; rates use actual elapsed interval not60s. SUM_TIMER and CPU/PSI aggregates are not quantiles or exclusive elapsed time. fileMISC includes morethanfsync; redoFsync separate exactcount. Counters cannot bythemselves prove physicaldisk or singlecause.','runtime_mutations':False,'rollback':'Original counters/raw stages unchanged, keep evidence'},indent=2)+'\n')
def tsv(name):return [x.split('\t') for x in (parent/name).read_text().splitlines() if x]
files={};old={x[0]:list(map(int,x[1:])) for x in tsv('before-file.tsv')}
for x in tsv('after-file.tsv'):
 name=x[0];now=list(map(int,x[1:]));assert name in old
 delta=[a-z for a,z in zip(now[:-1],old[name][:-1])];assert all(x>=0 for x in delta),'Counter reset'
 read,write,misc,readps,writeps,miscps=delta
 files[name]={'read_count':read,'write_count':write,'misc_count':misc,'misc_per_second':misc/elapsed,'write_per_second':write/elapsed,'read_wait_ms_sum':readps/1e9,'write_wait_ms_sum':writeps/1e9,'misc_wait_ms_sum':miscps/1e9,'misc_mean_ms':miscps/1e9/misc if misc else None,'misc_max_is_cumulative_not_window_ms':now[-1]/1e9}
oldstatus={x[0]:int(x[1]) for x in tsv('before-status.tsv')};status={}
for name,v in tsv('after-status.tsv'):
 value=int(v);delta=value-oldstatus.get(name,0);status[name]={'before':oldstatus.get(name),'after':value,'delta':delta,'per_second':delta/elapsed if name not in ['Threads_running'] else None}
old={x[0]:x for x in tsv('before-digest.tsv')};digests=[]
for row in tsv('after-digest.tsv'):
 previous=old.get(row[0]);values=list(map(int,row[2:]));prior=list(map(int,previous[2:])) if previous else[0]*len(values);delta=[a-z for a,z in zip(values,prior)]
 assert all(x>=0 for x in delta),'Digest counter reset'
 if delta[0]:digests.append({'digest':row[0],'normalized_sql':row[1],'count':delta[0],'per_second':delta[0]/elapsed,'total_wait_ms':delta[1]/1e9,'mean_wait_ms':delta[1]/1e9/delta[0],'lock_wait_ms_sum':delta[2]/1e9,'rows_examined':delta[3]})
digests.sort(key=lambda x:x['total_wait_ms'],reverse=True)
def stat(label):return {x.split()[0]:list(map(int,x.split()[1:])) for x in (parent/(label+'-stat.txt')).read_text().splitlines()}
before,after=stat('before'),stat('after');cpu=[a-z for a,z in zip(after['cpu'],before['cpu'])];total=sum(cpu[:8]);assert total>0 and all(x>=0 for x in cpu)
names=['user','nice','system','idle','iowait','irq','softirq','steal'];cpus={key:cpu[i]*100/total for i,key in enumerate(names)};cpus.update({'logical_cpu_count':len([k for k in after if k.startswith('cpu') and k!='cpu']),'context_switches_per_second':(after['ctxt'][0]-before['ctxt'][0])/elapsed,'forks_threads_per_second':(after['processes'][0]-before['processes'][0])/elapsed})
pressure={}
for kind in ['cpu','io','memory']:
 a,before_pressure=(parent/('after-'+kind+'-pressure.txt')).read_text(),(parent/('before-'+kind+'-pressure.txt')).read_text()
 def values(text):return {row.split()[0]:{k:float(v) for k,v in [pair.split('=') for pair in row.split()[1:]]} for row in text.splitlines()}
 av,bv=values(a),values(before_pressure);pressure[kind]={name:{'total_us_delta':row['total']-bv[name]['total'],'stall_share_pct':(row['total']-bv[name]['total'])/(elapsed*1e6)*100,'after_avg10':row['avg10']} for name,row in av.items()}
x={'status':'STORAGE_IO_BASELINE_ANALYSIS_COMPLETED','control_run':s['run'],'private':s['private'],'counter_elapsed_seconds':elapsed,'snapshot_capture_ms':{k:v['capture_elapsed_ms'] for k,v in meta.items()},'file_waits':files,'global_status':status,'statement_deltas':digests,'guest_cpu':cpus,'pressure':pressure,'settings_changed':False,'performance_acceptance':False,'limits':'Activewindow deltaapprox58.4s notfull60; fileMISC not purefsync, sumwaits overlap; PSI is stall probability not CPUusage; CPUsteal0 cannot exclude VMware/host scheduling delays; no counter reset and no hypothesis accepted automatically.'}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps({k:v for k,v in x.items() if k not in ['private','statement_deltas']},indent=2));print('TOP_STATEMENTS='+json.dumps(digests[:10]));print('P99='+str(s['private']['positive_ack_p99_ms_upper_bin']))
PY
