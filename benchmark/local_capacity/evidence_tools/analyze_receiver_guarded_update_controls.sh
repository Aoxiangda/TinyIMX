#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,hashlib,datetime
b=pathlib.Path('.local/codex');d=b/'receiver-guarded-update-comparison-20261005';assert not d.exists();d.mkdir()
latency_gates={'positive_ack_p99_le_100ms','scheduled_to_ack_p99_le_100ms'}
reports=[]
def digest(path):
 result={}
 for line in path.read_text().splitlines():
  parts=line.split('\t');assert len(parts)==6
  key,text=parts[:2];assert key not in result;result[key]={'text':text,'count':int(parts[2]),'time':int(parts[3]),'lock':int(parts[4]),'rows':int(parts[5])}
 return result
for name,phase in [('guard150A1','baseline'),('guard150B1','candidate')]:
 root=b/('receiver-guarded-update-control-'+name);s=json.loads((root/'summary.json').read_text());assert s['status']=='PRIVATE_GUARDED_UPDATE_CONTROL_COMPLETED' and s['phase']==phase
 private=s['private'];gates=private.get('gates',{});non_latency={key:value for key,value in gates.items() if key not in latency_gates}
 qualified=bool(non_latency) and all(non_latency.values()) and s['heartbeat_exact_equality'] and not s['observer_error']
 item={'run':name,'phase':phase,'summary_sha256':hashlib.sha256((root/'summary.json').read_bytes()).hexdigest(),'message_image':s['message_image'],'message_binary_sha256':s['message_binary_sha256'],'private_exit':s['private_exit'],'positive_ack_p99_ms_upper_bin':private.get('positive_ack_p99_ms_upper_bin'),'scheduled_to_ack_p99_ms_upper_bin':private.get('scheduled_to_ack_p99_ms_upper_bin'),'raw_positive_ack_max_ms':private.get('raw_positive_ack_max_ms'),'metrics':private.get('metrics',{}),'gates':gates,'heartbeat_exact_equality':s['heartbeat_exact_equality'],'qualified_complete_non_latency_population':qualified,'cpu':{},'statement_deltas':[],'counter_window_valid':False}
 if not s['observer_error']:
  first=json.loads((root/'before-cgroup-cpu.json').read_text());last=json.loads((root/'after-cgroup-cpu.json').read_text());elapsed=(last['monotonic_ns']-first['monotonic_ns'])/1e9;assert elapsed>0
  start=int((root/'observed-active-start-ns.txt').read_text());frames=[json.loads((root/(label+'-snapshot.json')).read_text()) for label in ['before','after']]
  item['counter_window_valid']=all(frame['started_monotonic_ns']>=start and frame['ended_monotonic_ns']<=start+60_000_000_000 for frame in frames);item['counter_elapsed_seconds']=elapsed;item['counter_capture_elapsed_ms']=[frame['capture_elapsed_ms'] for frame in frames]
  for key,values in first['cpu'].items():
   delta={field:last['cpu'][key][field]-value for field,value in values.items()};assert all(value>=0 for value in delta.values())
   item['cpu'][key]={'mean_cores':delta['usage_usec']/1e6/elapsed,'user_cores':delta['user_usec']/1e6/elapsed,'system_cores':delta['system_usec']/1e6/elapsed,'throttled_usec':delta.get('throttled_usec'),'limits':'Container total includes background; near-synchronous19counter frame and onecommonelapsed, notexclusive RPCCPU'}
  before=digest(root/'before-digest.tsv');after=digest(root/'after-digest.tsv')
  for key,value in after.items():
   old=before.get(key,{'count':0,'time':0,'lock':0,'rows':0});count=value['count']-old['count'];ns=value['time']-old['time'];assert count>=0 and ns>=0
   text=value['text'].replace('`','')
   if count and ('im_private_messages' in text or text=='COMMIT'):
    item['statement_deltas'].append({'digest':key,'text':value['text'],'count':count,'mean_ms':ns/count/1e9,'rows_examined':value['rows']-old['rows'],'limits':'Normalized SQLcountermean, no perrequestP99; statements include background andsum times overlap'})
 reports.append(item)
valid=all(item['qualified_complete_non_latency_population'] and item['counter_window_valid'] for item in reports)
x={'status':'GUARDED_UPDATE_PRIVATE_COMPARISON_COMPLETE','reports':reports,'comparison_population_qualified':valid,'full_feature_acceptance':False,'limits':'Sequential sharedhost, fixedprivate10k150/s only. Noallfeatures/20k50k/AI/soak acceptance. Compare actualP99 andCPU/statementcost, notonlyworker/TIDcounts. Original100usupperbins used directly; no extra0.1ms. Invalidpartial/counterwindows notcost-attribution evidence.'}
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly completed controlled populations andcounterdelta review','writes':'Fresh report only','input_summaries':[item['summary_sha256'] for item in reports]},indent=2)+'\n');(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
