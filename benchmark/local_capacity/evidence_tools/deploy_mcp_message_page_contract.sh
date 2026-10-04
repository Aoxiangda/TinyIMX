#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,datetime,hashlib,subprocess,time,http.client,signal
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'mcp-message-page-contract-deployment-20261005';assert not d.exists()
def run(a,timeout=25):return subprocess.check_output(a,text=True,timeout=timeout)
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
image=json.loads((b/'mcp-message-page-contract-image-20261005/summary.json').read_text());assert image['status']=='MCP_MESSAGE_PAGE_CONTRACT_IMAGE_SEALED'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));target=next(c for c in cs if c['Name']=='/tinyimx-m21-mcp-server-1')
assert target['Image']==image['original_image'] and target['State']['Health']['Status']=='healthy';assert run(['docker','exec',target['Id'],'sha256sum','/opt/tinyimx/bin/tinyimx_mcp_server']).split()[0]==image['original_mcp_binary_sha256']
for line in run(['docker','exec',target['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
 f=line.split()
 if len(f)>3 and f[1].endswith(':46A0') and f[3]=='01':raise RuntimeError('Refuse activeMCPclientinterruption')
def envmap(c):
 pairs=[x.split('=',1) for x in c['Config']['Env']];assert len(pairs)==len({x[0] for x in pairs});return dict(pairs)
env=envmap(target);before={c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs};cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')];scfg=json.loads(run(base+['config','--format','json']))['services']['mcp-server'];assert scfg['command']==target['Config']['Cmd'] and all(env.get(k)==str(v) for k,v in scfg.get('environment',{}).items())
assert json.loads(run(['docker','image','inspect',image['image_tag']]))[0]['Id']==image['image_id']
d.mkdir();private=d/'runtime-private';private.mkdir();(private/'original-container-inspect.json').write_text(json.dumps(target,indent=2)+'\n')
candidate=d/'candidate.override.json';rollback=d/'rollback.override.json';candidate.write_text(json.dumps({'services':{'mcp-server':{'image':image['image_tag']}}})+'\n');rollback.write_text(json.dumps({'services':{'mcp-server':{'image':image['original_image_tag']}}})+'\n')
restore=base+['-f',str(rollback),'up','-d','--no-deps','--pull','never','mcp-server']
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'MCP-only functionalpagecontract deployment after originalred/candidate89green/core andreal8domain validation','compiled_head':image['compiled_head'],'old_image':target['Image'],'old_binary_sha256':image['original_mcp_binary_sha256'],'new_image':image['image_id'],'new_binary_sha256':image['binary_sha256'],'before_containers':before,'private_config_sha256':hashes,'scope':'OnlyMCPbriefrecreation, exactenvironment/cmd/health/principal/model/config preserved; other18 exactIDs/images/started','active_clients':'None onMCP18080; no ownedcapacityworkers','rollback_command':restore,'validation':'Healthy exactbinary/env/cmd/health andtools schemasunderexistingtoken; configSHA/other18 unchanged','limits':'Formalprincipal1 remainsmissing, existingAIprovider mismatch remains, do notclaimfullproductiondomain/AI/50kperformancepass','other_apps':'Preserved'},indent=2)+'\n')
with (private/'original-mcp.log').open('w') as f:subprocess.run(['docker','logs','--timestamps',target['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=25)
def interrupted(signum,frame):raise KeyboardInterrupt('MCPdeployment interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def deploy(path,log):
 with (d/log).open('w') as f:subprocess.run(base+['-f',str(path),'up','-d','--no-deps','--pull','never','mcp-server'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
 end=time.monotonic()+50
 while True:
  c=json.loads(run(['docker','inspect','tinyimx-m21-mcp-server-1']))[0]
  if c['State']['Status']=='running' and c['State'].get('Health',{}).get('Status')=='healthy':return c
  assert time.monotonic()<end,'MCP nothealthy';time.sleep(1)
def verify_other():
 after=json.loads(run(['docker','inspect',*names]));assert all(c['Name']==target['Name'] or before[c['Name']]=={'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in after)
 assert {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}==hashes
try:
 c=deploy(candidate,'deployment.log');assert c['Image']==image['image_id'] and envmap(c)==env and c['Config']['Cmd']==target['Config']['Cmd'] and c['Config']['Healthcheck']==target['Config']['Healthcheck'] and c['RestartCount']==0
 assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/tinyimx_mcp_server']).split()[0]==image['binary_sha256'];verify_other()
 token=env['TINYIMX_MCP_TOKEN'];ip=next(iter(c['NetworkSettings']['Networks'].values()))['IPAddress'];connection=http.client.HTTPConnection(ip,18080,timeout=5)
 body=json.dumps({'jsonrpc':'2.0','id':1,'method':'tools/list','params':{}}).encode();headers={'Content-Type':'application/json','MCP-Protocol-Version':'2026-07-28','Mcp-Method':'tools/list','Authorization':'Bearer '+token}
 try:connection.request('POST','/mcp',body,headers);response=connection.getresponse();data=json.loads(response.read(1024*1024));assert response.status==200
 finally:connection.close()
 tools=data['result']['tools'];schema={x['name']:x['inputSchema'] for x in tools};assert len(tools)==9
 for name in ['tinyimx.message.list_history','tinyimx.message.list_conversations']:assert schema[name]['properties']['limit']['maximum']==50
 for name in ['tinyimx.social.list_friends','tinyimx.group.list_my_groups','tinyimx.group.list_members']:assert schema[name]['properties']['limit']['maximum']==100
 (d/'tools-schema-review.json').write_text(json.dumps({'tools':tools,'authenticated_tools_list':'PASS','token_exported':False},indent=2)+'\n');verify_other()
 x={'status':'MCP_MESSAGE_PAGE_CONTRACT_DEPLOYED_HEALTHY','compiled_head':image['compiled_head'],'image':image['image_id'],'binary_sha256':image['binary_sha256'],'mcp_service_id':c['Id'],'other18_preserved':True,'all_private_configs_preserved':True,'environment_cmd_healthcheck_preserved':True,'tools_schema_verified':True,'rollback_command':restore,'formal_principal_domain_validation':'NOT_PASS_existingprincipal1missing','AI_inference':'NOT_RUN','performance_acceptance':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e)})+'\n');c=deploy(rollback,'rollback.log');assert c['Image']==image['original_image'] and envmap(c)==env and c['Config']['Cmd']==target['Config']['Cmd'];assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/tinyimx_mcp_server']).split()[0]==image['original_mcp_binary_sha256'];verify_other();(d/'rollback-verified.json').write_text(json.dumps({'original_image_env_cmd_binary_verified':True,'other18_configs_preserved':True})+'\n');raise
PY
