#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,collections
r=pathlib.Path.cwd();b=r/'.local/codex';inp=b/'group-get-boundary-control-20261007-attempt2';d=b/'group-get-boundary-analysis-20261007';assert not d.exists()
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
s=json.loads((inp/'summary.json').read_text());assert s['status']=='GROUP_GET_BOUNDARY_REAL_ABBA_COMPLETE'
cs=json.loads(subprocess.check_output(['docker','inspect',*subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines()],text=True));assert {c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}==json.loads((inp/'restore-summary.json').read_text())['runtime']
d.mkdir(mode=0o700);(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Onlyreadonly preserved wire/clock/numeric boundary evidence. No SQL/network/load/runtime/config changes. Samecall key MID/UID/FNVrid/FNVcaller, require unique all3sides andtimestampnesting; everymissing/duplicate/failed call retained explicitly. SelectedoneUID notpopulationP99, handler residual not pure network.'},indent=2)+chr(10))
def stats(v):return {'samples':len(v),'mean_ms':sum(v)/len(v),'min_ms':min(v),'max_ms':max(v)} if v else {'samples':0}
cases=[]
for case in ['A1','B1','B2','A2']:
 wire=json.loads((inp/(case+'-all-requests.json')).read_text());raw=json.loads((inp/(case+'-get-boundaries.json')).read_text());tasks=json.loads((inp/(case+'-fanout-phases.json')).read_text());clocks=json.loads((inp/(case+'-clock-domains.json')).read_text());assert len(wire)==132 and all(z['monotonic_offset_ns']==0 for z in clocks.values()) and len({z['boot_id'] for z in clocks.values()})==1
 ev=json.loads((inp/(case+'-evidence-summary.json')).read_text());assert ev['captured_records']==ev['persisted_checkpoint_records'] and ev['failure'] is None
 bymid={z['message_id']:z for z in wire};grouped=collections.defaultdict(list)
 for z in raw:grouped[(z['mid'],z['recipient'],z['rid_hash'],z['caller_hash'])].append(z)
 matched=[];unmatched=[]
 for key,rows in sorted(grouped.items()):
  counts=collections.Counter(z['side'] for z in rows)
  reason=[]
  if counts!={1:1,2:1,3:1}:reason.append('MISSING_OR_DUPLICATE_SIDE')
  if key[0] not in bymid:reason.append('NO_WIRE_MID')
  if any(z['status']!=0 for z in rows):reason.append('NON_SUCCESS_OR_EARLY_PATH')
  if reason:unmatched.append({'key':key,'reasons':reason,'records':rows});continue
  c,h,p=[next(z for z in rows if z['side']==side) for side in [1,2,3]]
  def monotone(z,marks):return z['started_us']<=z['m1_us'] and all(z['m'+str(i)+'_us']<=z['m'+str(i+1)+'_us'] for i in range(1,marks)) and z['m'+str(marks)+'_us']<=z['started_us']+z['total_us']
  if not monotone(c,5) or not monotone(h,3) or not monotone(p,4):reason.append('INVALID_OR_MISSING_MARKS')
  if h['started_us']<c['m3_us']-2 or h['started_us']+h['total_us']>c['m4_us']+2 or p['started_us']<h['m1_us']-2 or p['started_us']+p['total_us']>h['m2_us']+2:reason.append('INVALID_CLOCK_NESTING')
  if reason:unmatched.append({'key':key,'reasons':reason,'records':rows});continue
  row=bymid[key[0]];v={'key':key,'size':row['size'],'index':row['index'],'measured':row['measured'],'service':c['service'],'wire_all_ms':row['send_to_all_delivery_ms'],'client_total_ms':c['total_us']/1000,'resolve_ms':(c['m1_us']-c['started_us'])/1000,'stub_ms':(c['m2_us']-c['m1_us'])/1000,'client_setup_ms':(c['m3_us']-c['m2_us'])/1000,'grpc_ms':(c['m4_us']-c['m3_us'])/1000,'client_decode_ms':(c['m5_us']-c['m4_us'])/1000,'handler_total_ms':h['total_us']/1000,'application_ms':(h['m2_us']-h['m1_us'])/1000,'repo_total_ms':p['total_us']/1000,'acquire_including_ping_ms':(p['m1_us']-p['started_us'])/1000,'sql_build_ms':(p['m2_us']-p['m1_us'])/1000,'query_ms':(p['m3_us']-p['m2_us'])/1000,'repo_decode_ms':(p['m4_us']-p['m3_us'])/1000,'repo_release_cleanup_ms':(p['started_us']+p['total_us']-p['m4_us'])/1000,'grpc_outside_handler_ms':(c['m4_us']-c['m3_us']-h['total_us'])/1000,'rpc_before_handler_scope_ms':(h['started_us']-c['m3_us'])/1000,'rpc_after_handler_scope_ms':(c['m4_us']-h['started_us']-h['total_us'])/1000,'client_cpu_ms':c['cpu_us']/1000,'handler_cpu_ms':h['cpu_us']/1000,'repo_cpu_ms':p['cpu_us']/1000,'records':rows}
  same=[z for z in tasks if z['event']=='group_delivery_task_phase' and z['gateway']==c['service'] and z['mid']==key[0] and z['recipient']==key[1] and z['started_us']<=c['started_us'] and c['started_us']+c['total_us']<=z['started_us']+z['total_us']+2]
  v['task_kind']=same[0]['kind'] if len(same)==1 else 0;v['task_phase_candidates']=same;matched.append(v)
 item={'case':case,'trace_on':case.startswith('B'),'wire_summary':next(v for v in s['cases'] if v['case']==case),'records':len(raw),'rpc_keys':len(grouped),'matched_calls':matched,'unmatched_calls':unmatched,'unmatched_reasons':dict(collections.Counter(v for z in unmatched for v in z['reasons'])),'raw_sha256':{name:sha(inp/name) for name in [case+'-get-boundaries.json',case+'-all-requests.json',case+'-fanout-phases.json',case+'-clock-domains.json',case+'-evidence-summary.json']},'sizes':[]}
 for size in [65,100]:
  m=[v for v in matched if v['size']==size and v['measured']];metrics={name:stats([v[name] for v in m]) for name in ['client_total_ms','resolve_ms','stub_ms','client_setup_ms','grpc_ms','client_decode_ms','handler_total_ms','application_ms','repo_total_ms','acquire_including_ping_ms','sql_build_ms','query_ms','repo_decode_ms','repo_release_cleanup_ms','grpc_outside_handler_ms','rpc_before_handler_scope_ms','rpc_after_handler_scope_ms','client_cpu_ms','handler_cpu_ms','repo_cpu_ms']}
  item['sizes'].append({'size':size,'measured_matched_calls':len(m),'phase_stats':metrics,'by_task_kind':{str(kind):{name:stats([v[name] for v in m if v['task_kind']==kind]) for name in metrics} for kind in [0,1,2]}})
 if case.startswith('A'):assert not raw
 cases.append(item)
comparison={}
for size in [2,16,65,100]:
 vals=lambda mode:[next(v for v in c['sizes'] if v['size']==size)['all_delivery']['mean_ms'] for c in s['cases'] if c['mode']==mode]
 a,z=vals('off'),vals('on');comparison[str(size)]={'off_mean_ms':a,'on_mean_ms':z,'conservative_mean_gain_percent':100*(min(a)-max(z))/min(a),'meaning':'Diagnostic perturbation comparison only, no product performance gain claim.'}
x={'status':'GROUP_GET_BOUNDARY_SAME_CALL_ANALYZED','head':s['head'],'cases':cases,'diagnostic_perturbation':comparison,'messages':528,'actual_recipient_confirmations':23628,'functional_operations':216,'functional_checks':148,'limits':'Samecall selectedUID519862 notrepresentative/random sample/P99; max8logs/side/s maylosepair evidence, allmissing/duplicate/non-success/nesting failures explicit, raw preserved. Handlertrace ends beforelogging/return; outsidehandler includes diagnosticlog transport/admission/protobuf/scheduler, not pure network. Acquire includes existingPING andpoolwait, not pure slot. Originalconstantpool16/workers16/batch128/coalescing1/deferred1/fullEnv fixed. No allfeature extreme claim.'}
(d/'summary.json').write_text(json.dumps(x,indent=2)+chr(10));print(json.dumps({'status':x['status'],'diagnostic_perturbation':comparison,'cases':[{'case':c['case'],'records':c['records'],'rpc_keys':c['rpc_keys'],'matched':len(c['matched_calls']),'unmatched':len(c['unmatched_calls']),'reasons':c['unmatched_reasons'],'sizes':[{k:v for k,v in z.items() if k!='by_task_kind'} for z in c['sizes']]} for c in cases]},indent=2))
PY
