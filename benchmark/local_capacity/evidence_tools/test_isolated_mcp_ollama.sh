#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,http.client,time,socket,secrets,sys,types,traceback,re
r=pathlib.Path.cwd();d=r/'.local/codex/isolated-mcp-existing-ollama-20261004-attempt5';assert not d.exists();d.mkdir()
private=d/'runtime-private';private.mkdir()
def save(name,v): (d/name).write_text(json.dumps(v,indent=2)+'\n')
def run(a,timeout=30):return subprocess.check_output(a,text=True,timeout=timeout)
def inspect(name):return json.loads(run(['docker','inspect',name]))[0]
def identity(c):return {'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
def ollama_get(path):
 c=http.client.HTTPConnection('127.0.0.1',11434,timeout=3)
 try:
  c.request('GET',path);p=c.getresponse();assert p.status==200;return json.loads(p.read(1024*1024))
 finally:c.close()
save('preflight-audit.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly preflight for owned MCP fixture and existing Ollama','writes':'Fresh evidence only; no inference or runtime changes in preflight'})
assert run(['git','rev-parse','HEAD']).strip()=='ddc7e8ec9a4c29a14f46b2f3c7970843a1850432'
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
names=[n for n in run(['docker','ps','--format','{{.Names}}']).splitlines() if n.startswith('tinyimx-m21-')];assert len(names)==19
cs=json.loads(run(['docker','inspect',*names]));before={c['Name']:identity(c) for c in cs}
configs=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in configs.glob('*.json')}
model=ollama_get('/api/tags');assert 'qwen2.5:7b' in [m['name'] for m in model['models']];loaded=ollama_get('/api/ps');save('ollama-preflight.json',{'tags':model,'loaded':loaded,'inference_called':False})
mem=run(['free','-m']);(d/'guest-memory-before.txt').write_text(mem)
with socket.socket() as s:s.bind(('127.0.0.1',18321))
name='tinyimx-codex-mcp-ollama-20261004-attempt5';assert subprocess.run(['docker','inspect',name],capture_output=True).returncode!=0
sys.path.insert(0,str(r/'benchmark/local_capacity'));from cross_feature_actor import Actor,Run,sql
u,p=519870,519872
rows=sql(f'SELECT user_id,username FROM im_users WHERE user_id IN ({u},{p}) ORDER BY user_id').strip().splitlines()
assert rows==[f'{x}\tm21b500000_{x-500000:06d}' for x in (u,p)]
assert int(sql(f'SELECT COUNT(*) FROM im_private_messages WHERE from_user_id={u} AND to_user_id={p}').strip())>=1
# Owned available file provenance has already been byte verified; assert exact state again through SQL.
q='SELECT file_id,owner_user_id,total_size,verified_checksum,status FROM im_files WHERE file_id=16'
file_row=sql(q).strip();(d/'owned-file-before.tsv').write_text(file_row+'\n')
assert file_row.split('\t')[:4]==['16',str(u),'1835041','75a4593ddc88e4c7d04a878fde1d7a566d05740c334e408ec479fb1087a5b586']
network=next(iter(next(c for c in cs if c['Name']=='/tinyimx-m21-mcp-server-1')['NetworkSettings']['Networks']))
image='tinyimx/runtime:codex-private-single-lease-v1';assert json.loads(run(['docker','image','inspect',image]))[0]['Id']=='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898'
token=secrets.token_urlsafe(32)
logs=private/'logs';logs.mkdir()
cfg=json.loads((r/'config/mcp_server.example.json').read_text());cfg['logger']['file']='/run/tinyimx/logs/mcp.log';cfg['logger']['console']=True
cfg['mcp'].update({'listen_host':'0.0.0.0','listen_port':18080,'endpoint':'http://127.0.0.1:18321/mcp','static_user_id':u,'static_subject':'codex:owned-synthetic-519870'})
cfg['observability']={'enable':False};config=private/'mcp.json';config.write_text(json.dumps(cfg,indent=2)+'\n');env=private/'mcp.env'
envlines=['TINYIMX_MCP_TOKEN='+token]
targets={}
for service,kind in [('social-service','SOCIAL'),('user-service','USER'),('message-service','MESSAGE'),('group-service','GROUP'),('file-service','FILE')]:
 c=next(c for c in cs if c['Name']=='/tinyimx-m21-'+service+'-1');ip=c['NetworkSettings']['Networks'][network]['IPAddress']
 bindings=[arg for arg in c['Config']['Cmd'] if re.fullmatch(r'(?:0\.0\.0\.0|127\.0\.0\.1):[0-9]+',arg)];assert len(bindings)==1;port=int(bindings[0].rsplit(':',1)[1])
 with socket.create_connection((ip,port),3):pass
 targets[kind]=f'{ip}:{port}';envlines.append(f'TINYIMX_{kind}_RPC_TARGET={ip}:{port}')
save('rpc-targets.json',targets)
env.write_text('\n'.join(envlines)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Create isolated MCP with own principal and read previously created own group26','source_head':run(['git','rev-parse','HEAD']).strip(),'before_containers':before,'private_config_sha256':hashes,'principal':u,'peer':p,'file_id':16,'group_mutation':'No new group mutation in attempt3; reuse exact own group26 created by attempt2 public API; preserve old evidence','mcp_container_name':name,'image':image,'network':network,'host_port':'127.0.0.1:18321','limits':'Own MCP512MiB/1CPU; read-only rootfs and config, no capability, no-new-privileges','token':'Random owned token in private0600 env file only','original_configs':'Preserve exact SHA','other_apps':'Preserved','validation':'Eight structured domain tool results, two auth/schema negatives, real identities and persistent file checksum','rollback':'Stop only exact new container ID after label/name verification. Retain group, evidence and private config; do not delete any business row. No inference in this phase.'})
client=Run(types.SimpleNamespace(run='mcpollama1',host='192.168.220.128',port=9000,users=[u,p]),d)
cid=None
try:
 a=Actor(client,u,'192.168.220.128',9000);b=Actor(client,p,'192.168.220.128',9000)
 old_fixture=json.loads((r/'.local/codex/isolated-mcp-existing-ollama-20261004-attempt2/owned-fixture.json').read_text());assert old_fixture=={'principal':u,'peer':p,'group_id':26,'file_id':16};gid=26
 reply=client.request(a,'owned-mcp-group-read',2025,{'group_id':gid});assert reply['group']['owner_user_id']==u and reply['group']['group_id']==gid
 save('owned-fixture.json',old_fixture)
 client.until(lambda:all(c.hb_ack for c in client.clients),'nonzero real heartbeats')
 save('fixture-operations.json',{'operations':client.operations,'heartbeats':[{'user_id':c.uid,'sent':len(c.hb_sent),'ack':len(c.hb_ack)} for c in client.clients]})
 for c in list(client.clients):c.close()
 argv=['docker','create','--name',name,'--label','codex.task=isolated-mcp-ollama-20261004','--user','1000:1000','--read-only','--cap-drop','ALL','--security-opt','no-new-privileges','--memory','512m','--cpus','1','--network',network,'--publish','127.0.0.1:18321:18080','--env-file',str(env),'--mount',f'type=bind,src={config},dst=/run/tinyimx/config/mcp.json,readonly','--mount',f'type=bind,src={logs},dst=/run/tinyimx/logs',image,'/opt/tinyimx/bin/tinyimx_mcp_server','/run/tinyimx/config/mcp.json']
 cid=run(argv).strip();save('owned-container.json',{'name':name,'id':cid,'label':'isolated-mcp-ollama-20261004','config_sha256':hashlib.sha256(config.read_bytes()).hexdigest()});run(['docker','start',cid])
 deadline=time.monotonic()+20
 while True:
  try:
   c=http.client.HTTPConnection('127.0.0.1',18321,timeout=2);c.request('GET','/health');res=c.getresponse();res.read();c.close();assert res.status==200;break
  except Exception:assert time.monotonic()<deadline,'Owned MCP unavailable';time.sleep(.5)
 results=[]
 def call(method,args=None,authorized=True):
  params=args or {};raw=json.dumps({'jsonrpc':'2.0','id':len(results)+1,'method':method,'params':params}).encode()
  headers={'Content-Type':'application/json','MCP-Protocol-Version':'2026-07-28','Mcp-Method':method}
  if method=='tools/call':headers['Mcp-Name']=params['name']
  if authorized:headers['Authorization']='Bearer '+token
  c=http.client.HTTPConnection('127.0.0.1',18321,timeout=7);started=time.monotonic_ns()
  try:c.request('POST','/mcp',raw,headers);response=c.getresponse();data=response.read(1024*1024);obj=json.loads(data)
  finally:c.close()
  row={'method':method,'params':params,'authorized':authorized,'http_status':response.status,'elapsed_ms':(time.monotonic_ns()-started)/1e6,'response':obj};results.append(row);save('mcp-results.json',results);return row
 unauthorized=call('tools/list',authorized=False);assert unauthorized['http_status']==401 and unauthorized['response']['error']['code']==-32040
 listed=call('tools/list');assert listed['http_status']==200;assert len(listed['response']['result']['tools'])==9
 cases=[('tinyimx.user.get_self_profile',{},lambda x:x['profile']['user_id']==u),('tinyimx.social.list_friends',{'limit':50},lambda x:any(f['friend_user_id']==p for f in x['friends'])),('tinyimx.message.list_conversations',{'limit':50},lambda x:any(f['peer_user_id']==p for f in x['conversations'])),('tinyimx.message.list_history',{'peer_user_id':p,'limit':50},lambda x:bool(x['messages']) and all({m['from_user_id'],m['to_user_id']}=={u,p} for m in x['messages'])),('tinyimx.group.get',{'group_id':gid},lambda x:x['group']['group_id']==gid and x['group']['owner_user_id']==u and x['group']['status']=='active'),('tinyimx.group.list_my_groups',{'limit':50},lambda x:any(g['group_id']==gid for g in x['groups'])),('tinyimx.group.list_members',{'group_id':gid,'limit':50},lambda x:{m['user_id'] for m in x['members']}=={u,p}),('tinyimx.file.get_metadata',{'file_id':16},lambda x:x['file']['file_id']==16 and x['file']['total_size']==1835041 and x['file']['verified_checksum']=='75a4593ddc88e4c7d04a878fde1d7a566d05740c334e408ec479fb1087a5b586')]
 for tool,args,check in cases:
  row=call('tools/call',{'name':tool,'arguments':args});result=row['response'].get('result',{});assert row['http_status']==200 and result.get('isError') is False and check(result['structuredContent']),tool
 injected=call('tools/call',{'name':'tinyimx.user.get_self_profile','arguments':{'user_id':p}});obj=injected['response'];assert 'error' in obj or obj.get('result',{}).get('isError') is True
 after=json.loads(run(['docker','inspect',*names]));assert all(before[c['Name']]==identity(c) for c in after)
 assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h for p,h in hashes.items())
 save('summary.json',{'status':'MCP_REAL_DOMAIN_PASS','domain_tools_passed':8,'negative_cases_passed':2,'principal':u,'group_id':gid,'file_id':16,'all19_production_instances_preserved':True,'private_configs_preserved':True,'owned_mcp_running_for_next_ai_phase':True,'inference_called':False,'capacity_proof':False});print((d/'summary.json').read_text())
except BaseException as e:
 save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),'traceback':traceback.format_exc()})
 if cid:
  c=inspect(cid);assert c['Name']=='/'+name and c['Config']['Labels']['codex.task']=='isolated-mcp-ollama-20261004';run(['docker','stop','--time','3',cid])
 raise
finally:
 for c in list(client.clients):c.close()
 if cid:
  with (private/'mcp-container.log').open('w') as f:subprocess.run(['docker','logs',cid],stdout=f,stderr=subprocess.STDOUT,timeout=15)
PY
