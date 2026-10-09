#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'real-ollama-phase-analysis-20261006';assert not d.exists();d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly historicalOllamaGIN timings correlate retainedexact ownAIcontainer State times; no inference/deployment/cleanup/config/process intervention','window_utc':['2026-10-06T05:06:00Z','2026-10-06T05:09:00Z'],'limits':'Secondresolutionjournal matching to nonoverlappingowncontainerwindows with1secondrounding margin; HTTPserverduration includes inference/request handling, notpureCPU orclientwall'})
origin=b/'allfeature-real-ollama-20261006-attempt2';summary=json.loads((origin/'summary.json').read_text());owned=json.loads((origin/'owned-containers.json').read_text())
q=subprocess.run(['journalctl','-u','ollama','--since','2026-10-06 05:06:00 UTC','--until','2026-10-06 05:09:00 UTC','--no-pager','-o','short-iso'],capture_output=True,text=True,timeout=20)
(private/'historical-journal.log').write_text(q.stdout+q.stderr);assert q.returncode==0
states={}
for item in owned:
 name=item['name']
 if '-ai-ready' not in name:continue
 c=json.loads(subprocess.check_output(['docker','inspect',item['id']],text=True))[0];assert c['Name']=='/'+name and not c['State']['Running']
 mode='ready1' if '-ready1-' in name else 'ready2'
 states[mode]={'id':c['Id'],'name':name,'started':c['State']['StartedAt'],'finished':c['State']['FinishedAt'],'exit':c['State']['ExitCode']}
def dt(x):return datetime.datetime.fromisoformat(x.replace('Z','+00:00'))
def duration(s):
 value=0.0;remain=s.strip()
 for number,unit in re.findall(r'([0-9.]+)(ns|µs|us|ms|s|m|h)',remain):
  value+=float(number)*{'ns':1e-6,'µs':1e-3,'us':1e-3,'ms':1,'s':1000,'m':60000,'h':3600000}[unit]
 assert value>0,remain
 return value
rows=[]
for line in q.stdout.splitlines():
 line=re.sub(r'\x1b\[[0-9;]*m','',line)
 match=re.search(r'\[GIN\].*?\|\s*(\d{3})\s*\|\s*([^|]+)\|.*POST\s+"(/v1/chat/completions|/api/generate)"',line)
 if not match:continue
 stamp=line.split()[0];finish=dt(stamp);matched=[mode for mode,s in states.items() if dt(s['started'])-datetime.timedelta(seconds=1)<=finish<=dt(s['finished'])+datetime.timedelta(seconds=1)]
 assert len(matched)<=1,'Ambiguousownrequestwindow'
 rows.append({'journal_finish':stamp,'status':int(match[1]),'server_duration_ms':duration(match[2]),'path':match[3],'owned_case':matched[0] if matched else None,'raw_line':line})
cases=[]
for case in summary['AI_cases']:
 mode=case['case'];parts=[x for x in rows if x['owned_case']==mode and x['path']=='/v1/chat/completions']
 cases.append({'case':mode,'owned_state':states[mode],'client_agent_elapsed_ms':case['elapsed_ms'],'tool_rounds':case['tool_rounds'],'tool_calls':case['tool_calls'],'matched_server_completions':len(parts),'completions':parts,'sum_server_completion_ms':sum(x['server_duration_ms'] for x in parts),'all_server_completions_success':all(x['status']==200 for x in parts),'limits':'GIN measures perHTTP total serverwall includinginference/handling; nottoken throughput, pureCPU, exclusive diskattribution, P99 orcapacity. Numberofmatches explicitlyretained.'})
save('summary.json',{'status':'HISTORICAL_AI_PHASES_CAPTURED','cases':cases,'server_rows':rows,'journal_sha256':hashlib.sha256(q.stdout.encode()).hexdigest(),'inference_repeated':False,'any_runtime_change':False})
print(json.dumps({'status':'HISTORICAL_AI_PHASES_CAPTURED','cases':[{k:x[k] for k in ['case','client_agent_elapsed_ms','matched_server_completions','sum_server_completion_ms']} for x in cases]},indent=2))
PY
