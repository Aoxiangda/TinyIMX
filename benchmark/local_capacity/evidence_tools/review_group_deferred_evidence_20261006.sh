#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,subprocess,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';inp=b/'group-deferred-evidence-control-20261006';d=b/'group-deferred-evidence-analysis-20261006';assert not d.exists()
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
summary=json.loads((inp/'summary.json').read_text());assert summary['status']=='GROUP_DEFERRED_EVIDENCE_REAL_ABBA_COMPLETE'
names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(names)==19
cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
runtime={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs}
assert runtime==json.loads((inp/'restore-summary.json').read_text())['runtime']
d.mkdir(mode=0o700)
audit={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Only readonly preserved control evidence analysis into one fresh task directory. No sockets/SQL/service changes or pressure. Matched MID/recipient and original captured clocks; biased <=8/s phase samples, not populationP99','runtime':runtime,'input_summary_sha256':sha(inp/'summary.json')}
(d/'audit-before.json').write_text(json.dumps(audit,indent=2)+'\n')
def stats(v):
 return {'samples':len(v),'mean_ms':sum(v)/len(v),'max_ms':max(v),'min_ms':min(v)} if v else {'samples':0}
cases=[]
for c in summary['cases']:
 case=c['case'];rows=json.loads((inp/(case+'-all-requests.json')).read_text());phases=json.loads((inp/(case+'-fanout-phases.json')).read_text());ev=json.loads((inp/(case+'-evidence-summary.json')).read_text())
 assert len(rows)==132 and all(len(z['recipient_arrivals'])==z['size']-1 for z in rows)
 if c['mode']=='on':assert ev['status']=='COMPLETE' and ev['captured_records']==ev['persisted_checkpoint_records'] and ev['failure'] is None
 item={'case':case,'mode':c['mode'],'sizes':[],'evidence_integrity':{k:v for k,v in ev.items() if k!='chunks'},'raw_sha256':{name:sha(inp/name) for name in [case+'-all-requests.json',case+'-fanout-phases.json',case+'-evidence-summary.json']}}
 for size in [2,16,65,100]:
  bymid={z['message_id']:z for z in rows if z['size']==size};linked=[]
  for z in phases:
   if z.get('event')!='group_delivery_task_phase' or z.get('mid') not in bymid:continue
   row=bymid[z['mid']];p=next((v for v in row['recipient_arrivals'] if v['recipient']==z['recipient']),None)
   if p is None:continue
   delta={'kind':z['kind'],'mid':z['mid'],'recipient':z['recipient'],'measured':row['measured'],'gateway':z['gateway'],'submit_since_send_ms':(z['started_us']-z['dispatch_age_us']-row['sent_mono_ns']/1000)/1000,'start_since_send_ms':(z['started_us']-row['sent_mono_ns']/1000)/1000,'queue_ms':z['dispatch_age_us']/1000,'task_wall_ms':z['total_us']/1000,'task_cpu_ms':z['thread_cpu_us']/1000,'get_rpc_ms':z['get_rpc_us']/1000,'confirm_rpc_ms':z['confirm_rpc_us']/1000 if z['confirm_rpc_us']>=0 else None,'recipient_arrival_ms':p['send_to_delivery_ms'],'arrival_minus_task_finish_ms':p['send_to_delivery_ms']-(z['started_us']+z['total_us']-row['sent_mono_ns']/1000)/1000}
   linked.append(delta)
  fs=[z for z in phases if z.get('event')=='group_fanout_phase' and z.get('first_mid') in bymid]
  x={'size':size,'wire_summary':next(z for z in c['sizes'] if z['size']==size),'linked_tasks':linked,'biased_tasks':{},'biased_fanout':{}}
  for kind in [1,2]:
   ts=[z for z in linked if z['kind']==kind and z['measured']];x['biased_tasks'][str(kind)]={k:stats([z[k] for z in ts if z[k] is not None]) for k in ['submit_since_send_ms','start_since_send_ms','queue_ms','task_wall_ms','task_cpu_ms','get_rpc_ms','confirm_rpc_ms','recipient_arrival_ms','arrival_minus_task_finish_ms']}
  for count in sorted({z['claimed'] for z in fs}):
   ff=[z for z in fs if z['claimed']==count];x['biased_fanout'][str(count)]={k:stats([z[k]/1000 for z in ff]) for k in ['claim_rpc_us','dispatch_sum_us','complete_rpc_sum_us','total_us','thread_cpu_us']}
  item['sizes'].append(x)
 cases.append(item)
gains={}
for size in [2,16,65,100]:
 off=[next(z for z in c['sizes'] if z['size']==size)['all_delivery']['mean_ms'] for c in summary['cases'] if c['mode']=='off']
 on=[next(z for z in c['sizes'] if z['size']==size)['all_delivery']['mean_ms'] for c in summary['cases'] if c['mode']=='on']
 gains[str(size)]=100*(min(off)-max(on))/min(off)
result={'status':'GROUP_DEFERRED_EVIDENCE_ANALYZED','head':summary['head'],'cases':cases,'conservative_mean_gain_percent':gains,'messages':528,'actual_recipient_confirmations':23628,'functional_operations':216,'functional_checks':148,'raw_originals_retained':True,'limits':'No allrecipient mean improvement claimed. Phase samples biased/shared <=8/s, cannot sum differentphase mean into a critical path; capture clocks user-space. For group peer-only arrival-minus-task-finish is useful boundary, ACK finish happens after arrival. No service root cause assumed from counters alone.'}
(d/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({'status':result['status'],'gains':gains,'cases':[{'case':c['case'],'evidence_integrity':c['evidence_integrity'],'100peer':next(z for z in c['sizes'] if z['size']==100)['biased_tasks']['1'],'100ack':next(z for z in c['sizes'] if z['size']==100)['biased_tasks']['2'],'100fanout':next(z for z in c['sizes'] if z['size']==100)['biased_fanout']} for c in cases]},indent=2))
PY
