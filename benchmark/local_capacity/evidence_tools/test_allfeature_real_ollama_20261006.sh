#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,http.client,time,socket,secrets,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'allfeature-real-ollama-20261006';assert not d.exists()
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def identity(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
def inspect(cid):return json.loads(run(['docker','inspect',cid]))[0]
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
def get(path):
 c=http.client.HTTPConnection('127.0.0.1',11434,timeout=3)
 try:c.request('GET',path);p=c.getresponse();raw=p.read(1024*1024);assert p.status==200;return json.loads(raw)
 finally:c.close()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
mixed=json.loads((b/'conversation-unread-mixed-10k-20261006/restore-summary.json').read_text());assert mixed['status']=='ORIGINAL_GATEWAYS_RESTORED'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
cs=json.loads(run(['docker','inspect',*names]));before={c['Name']:identity(c) for c in cs}
assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
cfgdir=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:sha(p) for p in cfgdir.glob('*.json')}
tags=get('/api/tags');assert 'qwen2.5:7b' in [m['name'] for m in tags['models']]
mem=pathlib.Path('/proc/meminfo').read_text();assert int(re.search(r'MemAvailable:\s+(\d+)',mem).group(1))>7*1024*1024
with socket.socket() as s:s.bind(('127.0.0.1',18322))
for name in ['tinyimx-codex-allfeature-mcp-20261006','tinyimx-codex-allfeature-ai-cold-20261006','tinyimx-codex-allfeature-ai-warm-20261006']:assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('preflight-audit.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly existingOllama tags/loadedmemory and frozen owned MCP fixtures; twoowncontainers prepared, no inferenceyet','head':run(['git','rev-parse','HEAD']).strip(),'original_containers':before,'configs_sha256':hashes,'no_capacity_concurrency':True})
sys_source=r/'benchmark/local_capacity/cross_feature_actor.py'
import sys;sys.path.insert(0,str(sys_source.parent));from cross_feature_actor import sql
u,p=519870,519872;assert set(sql(f'SELECT user_id,username,status FROM im_users WHERE user_id IN({u},{p})').splitlines())=={f'{x}\tm21b500000_{x-500000:06d}\t1' for x in [u,p]}
assert sql('SELECT group_id,owner_user_id FROM im_groups WHERE group_id=26').strip()==f'26\t{u}'
assert sql('SELECT file_id,owner_user_id,total_size,verified_checksum FROM im_files WHERE file_id=16').strip()==f'16\t{u}\t1835041\t75a4593ddc88e4c7d04a878fde1d7a566d05740c334e408ec479fb1087a5b586'
mc=next(c for c in cs if c['Name']=='/tinyimx-m21-mcp-server-1');net=next(iter(mc['NetworkSettings']['Networks']));image=mc['Image']
token=secrets.token_urlsafe(32);logs=private/'logs';logs.mkdir(mode=0o700)
cfg=json.loads((r/'config/mcp_server.example.json').read_text());cfg['logger']['file']='/run/tinyimx/logs/mcp.log';cfg['logger']['console']=True
cfg['mcp'].update({'listen_host':'0.0.0.0','listen_port':18080,'endpoint':'http://127.0.0.1:18322/mcp','static_user_id':u,'static_subject':'codex:owned-synthetic-519870'});cfg['observability']={'enable':False}
mcpconfig=private/'mcp.json';mcpconfig.write_text(json.dumps(cfg,indent=2)+'\n')
lines=['TINYIMX_MCP_TOKEN='+token];targets={}
for role,kind in [('social-service','SOCIAL'),('user-service','USER'),('message-service','MESSAGE'),('group-service','GROUP'),('file-service','FILE')]:
 c=next(c for c in cs if c['Name']=='/tinyimx-m21-'+role+'-1');ip=c['NetworkSettings']['Networks'][net]['IPAddress'];bindings=[arg for arg in c['Config']['Cmd'] if re.fullmatch(r'(?:0\.0\.0\.0|127\.0\.0\.1):[0-9]+',arg)];assert len(bindings)==1;port=int(bindings[0].rsplit(':',1)[1])
 with socket.create_connection((ip,port),3):pass
 targets[kind]=f'{ip}:{port}';lines.append(f'TINYIMX_{kind}_RPC_TARGET={ip}:{port}')
mcpenv=private/'mcp.env';mcpenv.write_text('\n'.join(lines)+'\n');mcpenv.chmod(0o600)
original_ai=json.loads((cfgdir/'ai-agent.json').read_text());aicfg=json.loads(json.dumps(original_ai))
aicfg['ai']['endpoint']='http://127.0.0.1:11434/v1/chat/completions';aicfg['agent']['model']='qwen2.5:7b';aicfg['mcp']['endpoint']='http://127.0.0.1:18322/mcp';aicfg['observability']['enable']=False
aiconfig=private/'ai-agent.json';aiconfig.write_text(json.dumps(aicfg,indent=2)+'\n')
aienv=private/'ai.env';aienv.write_text(aicfg['mcp']['token_env']+'='+token+'\n');aienv.chmod(0o600)
assert aicfg['ai']['request_timeout_ms']==original_ai['ai']['request_timeout_ms'] and aicfg['agent']['allowed_tools']==original_ai['agent']['allowed_tools']
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Real8domainMCP+2negative authenticated fixture checks, then actualsameAIbinary existingqwen2.5:7b cold/warm oneprofiletool loop','original_containers':before,'original_configs_sha256':hashes,'image':image,'own_principal':u,'owned_peer':p,'owned_group_id':26,'owned_file_id':16,'new_private_configs':'Newown filesonly endpoint/model adjusted; originalprivate configshashes neverwritten','model':'Alreadyinstalled qwen2.5:7b. No download/providerfake/Ollama service/listen/parameters/start/stop/unload changes','client_timeout_ms':aicfg['ai']['request_timeout_ms'],'AIclient_network':'hostforlocalexisting127.0.0.1Ollama +ownloopbackMCP; CLIclientnoports/mountoutsideconfig, read-only/capdropall/nonewprivileges512MiB1CPU','MCPnetwork':net,'MCP_publish':'127.0.0.1:18322only, ownrandomprivateenvtoken, read-onlyrootexceptownlogs512MiB1CPU','writes':'OwnDockercontainers/files/logs. MCP8toolsreadonlyonlyexistingverifiedfixtures. Ollamainferenceinternalnormallyloadsmodel; no businesswrites/systemchanges/cleanup','rollback':'StoponlyexactownIDs withname/labelverified, retaincontainers/logs/privateconfigs/allresults. All19originals/apps preserved; leaveOllamaunchanged','limits':'Actualmodeltoolloop functional/per-requestwall only, notAI10k50kcapacity/100ms acceptance; originalnetwork/model remainbroken untilseparateauditedintegration'})
save('ollama-before.json',{'tags':tags,'loaded':get('/api/ps'),'inference_called':False});(d/'guest-memory-before.txt').write_text(mem)
label='allfeature-real-ollama-20261006';owned=[];results=[];answers=[]
def create(name,args):
 cid=run(['docker','create','--name',name,'--label','codex.task='+label,*args]).strip();owned.append((cid,name));save('owned-containers.json',[{'id':c,'name':n} for c,n in owned]);return cid
def preserved():
 now=json.loads(run(['docker','inspect',*names]));assert {c['Name']:identity(c) for c in now}==before and all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in now)
 assert {p.name:sha(p) for p in cfgdir.glob('*.json')}==hashes
def call(method,params=None,authorized=True):
 params=params or {};c=http.client.HTTPConnection('127.0.0.1',18322,timeout=7);headers={'Content-Type':'application/json','MCP-Protocol-Version':'2026-07-28','Mcp-Method':method}
 if authorized:headers['Authorization']='Bearer '+token
 if method=='tools/call':headers['Mcp-Name']=params['name']
 start=time.monotonic_ns()
 try:
  c.request('POST','/mcp',json.dumps({'jsonrpc':'2.0','id':len(results)+1,'method':method,'params':params}),headers);p=c.getresponse();obj=json.loads(p.read(1024*1024));status=p.status
 finally:c.close()
 row={'method':method,'params':params,'authorized':authorized,'http_status':status,'elapsed_ms':(time.monotonic_ns()-start)/1e6,'response':obj};results.append(row);save('mcp-results.json',results);return row
try:
 mcp=create('tinyimx-codex-allfeature-mcp-20261006',['--user','1000:1000','--read-only','--cap-drop','ALL','--security-opt','no-new-privileges','--memory','512m','--cpus','1','--network',net,'--publish','127.0.0.1:18322:18080','--env-file',str(mcpenv),'--mount',f'type=bind,src={mcpconfig},dst=/run/tinyimx/config/mcp.json,readonly','--mount',f'type=bind,src={logs},dst=/run/tinyimx/logs','--entrypoint','/opt/tinyimx/bin/tinyimx_mcp_server',image,'/run/tinyimx/config/mcp.json'])
 run(['docker','start',mcp]);deadline=time.monotonic()+20
 while True:
  try:
   c=http.client.HTTPConnection('127.0.0.1',18322,timeout=2);c.request('GET','/health');p0=c.getresponse();p0.read();c.close();assert p0.status==200;break
  except OSError:assert time.monotonic()<deadline;time.sleep(.2)
 denied=call('tools/list',authorized=False);assert denied['http_status']==401 and denied['response']['error']['code']==-32040
 listed=call('tools/list');assert listed['http_status']==200 and len(listed['response']['result']['tools'])==9
 cases=[('tinyimx.user.get_self_profile',{},lambda x:x['profile']['user_id']==u),('tinyimx.social.list_friends',{'limit':50},lambda x:any(f['friend_user_id']==p for f in x['friends'])),('tinyimx.message.list_conversations',{'limit':50},lambda x:any(f['peer_user_id']==p for f in x['conversations'])),('tinyimx.message.list_history',{'peer_user_id':p,'limit':50},lambda x:bool(x['messages']) and all({m['from_user_id'],m['to_user_id']}=={u,p} for m in x['messages'])),('tinyimx.group.get',{'group_id':26},lambda x:x['group']['group_id']==26 and x['group']['owner_user_id']==u and x['group']['status']=='active'),('tinyimx.group.list_my_groups',{'limit':50},lambda x:any(g['group_id']==26 for g in x['groups'])),('tinyimx.group.list_members',{'group_id':26,'limit':50},lambda x:{m['user_id'] for m in x['members']}=={u,p}),('tinyimx.file.get_metadata',{'file_id':16},lambda x:x['file']['file_id']==16 and x['file']['total_size']==1835041 and x['file']['verified_checksum']=='75a4593ddc88e4c7d04a878fde1d7a566d05740c334e408ec479fb1087a5b586')]
 for tool,args,verify in cases:
  row=call('tools/call',{'name':tool,'arguments':args});result=row['response'].get('result',{});assert row['http_status']==200 and result.get('isError') is False and verify(result['structuredContent']),tool
 injected=call('tools/call',{'name':'tinyimx.user.get_self_profile','arguments':{'user_id':p}})['response'];assert 'error' in injected or injected.get('result',{}).get('isError') is True
 save('mcp-summary.json',{'status':'MCP_REAL8DOMAIN_PASS','domain_tools':8,'negative_cases':2,'production_principal_not_used':True,'business_writes':False,'performance_acceptance':False});print(json.dumps({'status':'MCP_REAL8DOMAIN_PASS'}),flush=True)
 prompt='Call tinyimx.user.get_self_profile exactly once to get the authenticated current user. Then return only the user_id and username from the real tool result. Do not use another tool and do not invent values.'
 for mode in ['cold','warm']:
  name='tinyimx-codex-allfeature-ai-'+mode+'-20261006';cid=create(name,['--user','1000:1000','--read-only','--cap-drop','ALL','--security-opt','no-new-privileges','--memory','512m','--cpus','1','--network','host','--env-file',str(aienv),'--mount',f'type=bind,src={aiconfig},dst=/run/tinyimx/config/ai-agent.json,readonly','--entrypoint','/opt/tinyimx/bin/tinyimx_ai_agent_demo',image,'/run/tinyimx/config/ai-agent.json',prompt])
  save('AI-'+mode+'-audit-before.json',{'operation':'Oneactualexistingmodeltoolloop, notsynthetic response','own_container':cid,'timeout_seconds':120,'client_request_timeout_ms':aicfg['ai']['request_timeout_ms'],'pre_model_loaded':get('/api/ps'),'keep_original_model_settings':True})
  start=time.monotonic_ns()
  with (private/('AI-'+mode+'.log')).open('w') as f:
   proc=subprocess.Popen(['docker','start','--attach',cid],stdout=f,stderr=subprocess.STDOUT)
   try:code=proc.wait(timeout=120)
   except subprocess.TimeoutExpired:
    c=inspect(cid);assert c['Name']=='/'+name and c['Config']['Labels']['codex.task']==label
    save('AI-'+mode+'-timeout-audit.json',{'operation':'Stoponlyownedcontainer after120s timeout','id':cid});run(['docker','stop','--time','3',cid]);proc.wait(timeout=10);raise
  elapsed=(time.monotonic_ns()-start)/1e6;log=(private/('AI-'+mode+'.log')).read_text();state=inspect(cid)['State'];ok=state['ExitCode']==0 and code==0
  match=re.search(r'\[tool_rounds=(\d+) tool_calls=(\d+)\]',log)
  actualtool=bool(match) and int(match.group(1))>=1 and int(match.group(2))==1
  answerverified=str(u) in log and f'm21b500000_{u-500000:06d}' in log
  row={'case':mode,'status':'PASS' if ok and actualtool and answerverified else 'FAIL','exit_code':state['ExitCode'],'elapsed_ms':elapsed,'tool_rounds':int(match.group(1)) if match else None,'tool_calls':int(match.group(2)) if match else None,'answer_exact_owned_uid_username_present':answerverified,'raw_log_sha256':sha(private/('AI-'+mode+'.log')),'model':'qwen2.5:7b','client_request_timeout_ms':aicfg['ai']['request_timeout_ms'],'error_lines':[line for line in log.splitlines() if line.startswith('Agent error:')][:2],'model_loaded_after':get('/api/ps'),'capacity_acceptance':False}
  answers.append(row);save('ai-results.json',answers);print(json.dumps(row),flush=True);assert row['status']=='PASS','ActualAItoolloop failed; analyze real error, no repeat/increase timeout'
  preserved()
 save('summary.json',{'status':'REAL_OLLAMA_AI_MCP_PASS','MCP_domain_tools':8,'MCP_negative_cases':2,'AI_cases':answers,'all19original_instances_andconfigs_preserved':True,'original_model_endpoint_repaired_in_private_test_config_only':True,'performance_acceptance':False})
except BaseException as e:save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'MCP_rows_completed':len(results),'AI_cases':answers,'performance_acceptance':False});raise
finally:
 for cid,name in reversed(owned):
  c=inspect(cid);assert c['Name']=='/'+name and c['Config']['Labels']['codex.task']==label
  if c['State']['Running']:
   save('stop-'+name+'-audit-before.json',{'operation':'StoponlyexactownID/name/label, keepcontainer/config/logs/history/modelunchanged','id':cid});run(['docker','stop','--time','3',cid])
  with (private/(name+'-container.log')).open('w') as f:subprocess.run(['docker','logs',cid],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=30)
 preserved();save('restore-summary.json',{'status':'ALL_ORIGINAL19_PRESERVED_OWN_AI_MCP_STOPPED','original':before,'originalconfigs_sha256':hashes,'own_containers_retained_stopped':len(owned),'Ollama_process_configuration_listen_and_models_unchanged':True,'userapps_preserved':True})
 (d/'guest-memory-after.txt').write_text(pathlib.Path('/proc/meminfo').read_text());print(json.dumps({'status':'ALL_ORIGINAL19_PRESERVED_OWN_AI_MCP_STOPPED'}),flush=True)
PY
