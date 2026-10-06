#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'conversation-unread-result-analysis-20261006';assert not d.exists();d.mkdir(mode=0o700)
def read(p):return json.loads((b/p).read_text())
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly aggregate complete native, endpoint, mixed, acceptedruntime,AI successes andfailures andactualcrossfeature latencies; no newpressure/config/model/service/data changes','do_not':'No combinedP99 averages, no failedsamples discarded, no low-samplecontrolresults calledcapacity'})
mixed=read('conversation-unread-mixed-10k-20261006/summary.json')
cross={}
for name in ['cuall20261006','cumixfeatA1','cumixfeatB1','cumixfeatB2','cumixfeatA2']:
 p='cross-feature-'+name;summary=read(p+'/summary.json');operations=read(p+'/operations.json')
 groups={}
 for row in operations:groups.setdefault(row['name'],[]).append(row['ms'])
 cross[name]={'summary':summary,'operations':operations,'max_operation_ms':max(x['ms'] for x in operations),'observed_request_types':summary['public_request_types_observed']}
x={'head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'git_iterations':subprocess.check_output(['git','log','--reverse','--format=%H %s','764dbd9b3dc533a852ef19a46184cf3d91896f87..HEAD'],text=True).splitlines(),'native':read('conversation-unread-batch-api-20261006/summary.json'),'native_component':{mode:read('conversation-unread-batch-api-20261006/'+mode+'/component-performance.json') for mode in ['p1','p4']},'endpoint':read('conversation-unread-batch-gateway-run-20261006/summary.json'),'mixed':mixed,'cross':cross,'accepted':read('accepted-conversation-unread-20261006/summary.json'),'AI_first_fail':read('allfeature-real-ollama-20261006/failed.json'),'MCP_first':read('allfeature-real-ollama-20261006/mcp-summary.json'),'AI_ready':read('allfeature-real-ollama-20261006-attempt2/summary.json'),'model_preload':read('allfeature-real-ollama-20261006-attempt2/model-readiness-result.json'),'AI_restore':read('allfeature-real-ollama-20261006-attempt2/restore-summary.json'),'ollama_loading':read('real-ollama-timeout-analysis-20261006/ollama-loading-log-analysis.json')}
ons=[q for q in mixed['cases'] if q['mode']=='on'];offs=[q for q in mixed['cases'] if q['mode']=='off']
x['bounded_gains']={'list_p99_reduction_at_least_percent':(1-max(q['list']['sent_to_response']['p99_ms'] for q in ons)/min(q['list']['sent_to_response']['p99_ms'] for q in offs))*100,'private_p99_reduction_at_least_percent':(1-max(q['capacity']['positive_ack_p99_ms_upper_bin'] for q in ons)/min(q['capacity']['positive_ack_p99_ms_upper_bin'] for q in offs))*100,'derivation':'Conservative largestON P99 vs smallestOFF P99; casecomparisonatfixedmix, notP99 averaging orprediction'}
save('analysis-inputs.json',x)
print(json.dumps({'status':'RESULTS_AGGREGATED','private_messages':sum(q['reconciliation']['positive_ack'] for q in mixed['cases']),'list_pages':sum(q['list']['recorded'] for q in mixed['cases']),'bounded_gains':x['bounded_gains'],'cross_case_max_ms':{n:q['max_operation_ms'] for n,q in cross.items()},'model_preload_ms':x['model_preload']['wall_ms'],'AI_ready_ms':[q['elapsed_ms'] for q in x['AI_ready']['AI_cases']]},indent=2))
PY
