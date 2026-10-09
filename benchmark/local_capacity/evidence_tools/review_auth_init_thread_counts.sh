#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'auth-init-thread-count-review-20261005';assert not d.exists()
a=json.loads((b/'auth-phase-completed-analysis-20261005/summary.json').read_text());assert a['status']=='AUTH_NUMERIC_ANALYSIS_COMPLETED'
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly correction of Docker initPID threadcounts in completed authresourceobserver','reads':'Frozen31 numericresources, DockerStatePid, /proc children/status numeric fields andsame-cgroup paths','writes':'Freshreview only; originalraw/summary neverrewritten','scope':'CurrentPID snapshots distinguish originalUserafterrollback fromdiagnosticUseratmeasurement; no earlier serverthread reconstruction','impact':'One bounded proc/Dockerread; no service/config/source/globalchanges'},indent=2)+'\n')
names=['tinyimx-m21-user-service-1','tinyimx-m21-gateway-a-1','tinyimx-m21-gateway-b-1'];cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True));out=[]
for c in cs:
 pid=c['State']['Pid'];children=[int(x) for x in pathlib.Path(f'/proc/{pid}/task/{pid}/children').read_text().split()];rows=[];parentcg=pathlib.Path(f'/proc/{pid}/cgroup').read_text()
 for child in children:
  status=pathlib.Path(f'/proc/{child}/status').read_text();rows.append({'pid':child,'name':re.search(r'^Name:\s+(.*)$',status,re.M).group(1),'threads':int(re.search(r'^Threads:\s+(\d+)',status,re.M).group(1)),'same_cgroup_as_init':pathlib.Path(f'/proc/{child}/cgroup').read_text()==parentcg})
 out.append({'name':c['Name'],'id':c['Id'],'image':c['Image'],'init_pid':pid,'init_threads':len(list(pathlib.Path(f'/proc/{pid}/task').iterdir())),'current_direct_children':rows})
x={'status':'INIT_THREAD_COUNTS_CORRECTED_REVIEW','old_field_maximum_threads_valid_for_server':False,'reason':'DockerStatePid is docker-init; /proc/init/task measuresinit only. Cgroupusage includesallsame-cgroup children andremainsvalid; SYS_gettid authsamplerecordsareactualserverTIDs.','old_raw_and_summary_preserved':True,'observed_auth_repository_records':a['repository_samples']['records'],'actual_auth_sample_distinct_tids':a['repository_samples']['distinct_tids'],'current_snapshot_is_not_past_diagnostic_thread_count':True,'current_processes':out,'resource_patch_for_future':'Resolve exactone direct service child, confirmsamecgroup/identity; use init_threads andservice_threads distinctfields, never labelinit threadcount asserverthreads'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
