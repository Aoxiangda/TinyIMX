#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,datetime,hashlib,subprocess,socket,secrets,http.client,time,os,signal,re,threading
r=pathlib.Path.cwd();base=r/'.local/codex';d=base/'mcp-message-page-contract-live-20261005';assert not d.exists()
build=json.loads((base/'mcp-message-page-contract-build-20261005/summary.json').read_text());assert build['status']=='MCP_PAGE_CONTRACT_RED_GREEN_BUILD_COMPLETED' and build['green_domain_checks']==89
binary=r/'build/linux-release/tinyimx_mcp_server';assert hashlib.sha256(binary.read_bytes()).hexdigest()==build['candidate_mcp_binary_sha256']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def run(a,timeout=20):return subprocess.check_output(a,text=True,timeout=timeout)
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
def runtime():
 cs=json.loads(run(['docker','inspect',*names]));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
 return cs,{'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}}
cs,before=runtime();assert before==json.loads((base/'mcp-message-page-contract-build-20261005/runtime-after.json').read_text())
def sql(q):return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-owned-mcp-page-check',q])
settings='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count';assert sql(settings).strip()=='1\t1\t1\t0\t0'
fixture=json.loads((base/'isolated-mcp-existing-ollama-20261004-attempt2/owned-fixture.json').read_text());assert fixture=={'principal':519870,'peer':519872,'group_id':26,'file_id':16}
u,p,gid,fid=fixture['principal'],fixture['peer'],fixture['group_id'],fixture['file_id']
assert sql(f'SELECT user_id,username FROM im_users WHERE user_id IN ({u},{p}) ORDER BY user_id').strip().splitlines()==[f'{x}\tm21b500000_{x-500000:06d}' for x in (u,p)]
assert int(sql(f'SELECT COUNT(*) FROM im_private_messages WHERE from_user_id={u} AND to_user_id={p}').strip())>=1
file_row=sql('SELECT file_id,owner_user_id,total_size,verified_checksum,status FROM im_files WHERE file_id=16').strip();assert file_row.split('\t')[:4]==['16',str(u),'1835041','75a4593ddc88e4c7d04a878fde1d7a566d05740c334e408ec479fb1087a5b586']
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
port=18322
with socket.socket() as sock:sock.bind(('127.0.0.1',port))
strace=pathlib.Path('/usr/bin/strace');assert strace.is_file()
d.mkdir();private=d/'runtime-private';private.mkdir();logs=private/'logs';logs.mkdir()
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Launch only owned loopback MCP candidate under parent strace syscall-count summary for real readonly domain validation','head':run(['git','rev-parse','HEAD']).strip(),'compiled_head':build['head'],'binary_sha256':build['candidate_mcp_binary_sha256'],'runtime_before':before,'fixture':fixture,'writes':'Own evidence/private config/token/log and ownedprocess only; no fixture/business mutation','listen':'127.0.0.1:18322','trace':'strace -f -c aggregate syscall counts/times only, no syscallarguments/credentials/payload traces; parent traces itsownchild, no attachingproduction/root/sysctl/install','resource':'Single bounded150s wholevalidation, no inference/load matrix; otherapps preserved','rollback':'Finally SIGTERM only exact recorded ownedMCPPID aftercmdline+starttimeguard; retain files and all19originalservices/config/settings. Never stopunrelatedPID','performance_acceptance':False,'limitations':'Strace perturbslatency; readonlyfunctionaltest not10k50k load orprivateP99proof'},indent=2)+'\n')
(d/'owned-file-before.tsv').write_text(file_row+'\n')
token=secrets.token_urlsafe(32);cfg=json.loads((r/'config/mcp_server.example.json').read_text());cfg['logger'].update({'file':str(logs/'mcp.log'),'console':True});cfg['observability']={'enable':False}
cfg['mcp'].update({'listen_host':'127.0.0.1','listen_port':port,'endpoint':f'http://127.0.0.1:{port}/mcp','static_user_id':u,'static_subject':'codex:owned-synthetic-519870'})
config=private/'mcp.json';config.write_text(json.dumps(cfg,indent=2)+'\n');env=os.environ.copy();env['TINYIMX_MCP_TOKEN']=token
network=next(iter(next(c for c in cs if c['Name']=='/tinyimx-m21-mcp-server-1')['NetworkSettings']['Networks']));targets={}
for service,kind in [('social-service','SOCIAL'),('user-service','USER'),('message-service','MESSAGE'),('group-service','GROUP'),('file-service','FILE')]:
 c=next(c for c in cs if c['Name']=='/tinyimx-m21-'+service+'-1');ip=c['NetworkSettings']['Networks'][network]['IPAddress'];bindings=[arg for arg in c['Config']['Cmd'] if re.fullmatch(r'(?:0\.0\.0\.0|127\.0\.0\.1):[0-9]+',arg)];assert len(bindings)==1;target=f'{ip}:{int(bindings[0].rsplit(":",1)[1])}'
 with socket.create_connection((ip,int(target.rsplit(':',1)[1])),3):pass
 targets[kind]=target;env[f'TINYIMX_{kind}_RPC_TARGET']=target
(d/'rpc-targets.json').write_text(json.dumps(targets,indent=2)+'\n')
end=time.monotonic()+150;results=[];owned=None;identity=None;process=None;watchdog=None
def interrupted(signum,frame):raise KeyboardInterrupt('Owned MCP validation interrupted')
for sig in [signal.SIGINT,signal.SIGTERM,signal.SIGHUP]:signal.signal(sig,interrupted)
def call(method,args=None,authorized=True):
 assert time.monotonic()<end,'Bounded whole validation deadline'
 params=args or {};raw=json.dumps({'jsonrpc':'2.0','id':len(results)+1,'method':method,'params':params}).encode();headers={'Content-Type':'application/json','MCP-Protocol-Version':'2026-07-28','Mcp-Method':method}
 if method=='tools/call':headers['Mcp-Name']=params['name']
 if authorized:headers['Authorization']='Bearer '+token
 c=http.client.HTTPConnection('127.0.0.1',port,timeout=5);started=time.monotonic_ns()
 try:c.request('POST','/mcp',raw,headers);response=c.getresponse();data=response.read(1024*1024);obj=json.loads(data)
 finally:c.close()
 row={'method':method,'params':params,'authorized':authorized,'http_status':response.status,'elapsed_ms_with_strace':(time.monotonic_ns()-started)/1e6,'response':obj};results.append(row);(d/'mcp-results.json').write_text(json.dumps(results,indent=2)+'\n');return row
def tool(name,args):
 row=call('tools/call',{'name':name,'arguments':args});assert row['http_status']==200 and row['response']['result']['isError'] is False,name
 return row['response']['result']['structuredContent']
try:
 with (private/'mcp-console.log').open('w') as f:
  process=subprocess.Popen([str(strace),'-qq','-f','-c','-o',str(d/'syscall-summary.txt'),str(binary),str(config)],stdout=f,stderr=subprocess.STDOUT,env=env,start_new_session=True)
  deadline=time.monotonic()+15
  while owned is None:
   assert process.poll() is None,'Owned MCP exited during startup'
   children=pathlib.Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()
   for item in children:
    pid=int(item);cmd=pathlib.Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
    if cmd[:2]==[str(binary).encode(),str(config).encode()]:owned=pid;identity=pathlib.Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()[19];break
   assert time.monotonic()<deadline;time.sleep(.05)
  (d/'owned-process.json').write_text(json.dumps({'tracer_pid':process.pid,'mcp_pid':owned,'mcp_starttime_ticks':identity,'binary':str(binary),'config_sha256':hashlib.sha256(config.read_bytes()).hexdigest()})+'\n')
  def expire_owned():
   proc=pathlib.Path(f'/proc/{owned}')
   if proc.exists() and proc.joinpath('cmdline').read_bytes().split(b'\0')[:2]==[str(binary).encode(),str(config).encode()] and proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity:
    (d/'watchdog-stop-audit.json').write_text(json.dumps({'operation':'Bounded deadline SIGTERM only own MCP child','pid':owned,'starttime_verified':True,'cmdline_verified':True})+'\n');os.kill(owned,signal.SIGTERM)
  watchdog=threading.Timer(max(.1,end-time.monotonic()),expire_owned);watchdog.start()
  while True:
   try:
    c=http.client.HTTPConnection('127.0.0.1',port,timeout=1);c.request('GET','/health');res=c.getresponse();res.read();c.close();assert res.status==200;break
   except Exception:assert process.poll() is None and time.monotonic()<deadline,'Owned MCP unavailable';time.sleep(.1)
  unauth=call('tools/list',authorized=False);assert unauth['http_status']==401 and unauth['response']['error']['code']==-32040
  listed=call('tools/list');assert listed['http_status']==200;tools=listed['response']['result']['tools'];assert len(tools)==9
  schema={x['name']:x['inputSchema'] for x in tools}
  for name in ['tinyimx.message.list_conversations','tinyimx.message.list_history']:assert schema[name]['properties']['limit']['maximum']==50
  for name in ['tinyimx.social.list_friends','tinyimx.group.list_my_groups','tinyimx.group.list_members']:assert schema[name]['properties']['limit']['maximum']==100
  assert tool('tinyimx.user.get_self_profile',{})['profile']['user_id']==u
  assert any(x['friend_user_id']==p for x in tool('tinyimx.social.list_friends',{'limit':100})['friends'])
  for limit in [None,1,50]:
   args={} if limit is None else {'limit':limit};conversations=tool('tinyimx.message.list_conversations',args);assert conversations['conversations'] and len(conversations['conversations'])<=(50 if limit is None else limit) and all(x['peer_user_id']>0 and x['peer_user_id']!=u for x in conversations['conversations'])
   if limit!=1:assert any(x['peer_user_id']==p for x in conversations['conversations'])
   history=tool('tinyimx.message.list_history',{'peer_user_id':p,**args});assert history['messages'] and all({x['from_user_id'],x['to_user_id']}=={u,p} for x in history['messages'])
  group=tool('tinyimx.group.get',{'group_id':gid})['group'];assert group['group_id']==gid and group['owner_user_id']==u and group['status']=='active'
  assert any(x['group_id']==gid for x in tool('tinyimx.group.list_my_groups',{'limit':100})['groups'])
  assert {x['user_id'] for x in tool('tinyimx.group.list_members',{'group_id':gid,'limit':100})['members']}=={u,p}
  file=tool('tinyimx.file.get_metadata',{'file_id':fid})['file'];assert file['total_size']==1835041 and file['verified_checksum']==file_row.split('\t')[3]
  for name in ['tinyimx.message.list_conversations','tinyimx.message.list_history']:
   for limit in [51,100,0,-1,'50',None]:
    args={'limit':limit}
    if name.endswith('list_history'):args['peer_user_id']=p
    row=call('tools/call',{'name':name,'arguments':args});assert row['http_status']==200 and row['response']['result']['isError'] is True and row['response']['result']['structuredContent']['error']['code']=='invalid_arguments'
  spoof=call('tools/call',{'name':'tinyimx.user.get_self_profile','arguments':{'user_id':p}});assert spoof['response']['result']['isError'] is True
  assert sql('SELECT file_id,owner_user_id,total_size,verified_checksum,status FROM im_files WHERE file_id=16').strip()==file_row
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e),'completed_requests':len(results)})+'\n');raise
finally:
 if owned is not None:
  proc=pathlib.Path(f'/proc/{owned}')
  if proc.exists():
   assert proc.joinpath('cmdline').read_bytes().split(b'\0')[:2]==[str(binary).encode(),str(config).encode()]
   assert proc.joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity
   (d/'stop-audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'SIGTERM only exact owned loopback MCP child','pid':owned,'starttime_verified':True,'cmdline_verified':True})+'\n');os.kill(owned,signal.SIGTERM)
 if process is not None:
  try:process.wait(timeout=10)
  except subprocess.TimeoutExpired:
   assert owned is not None and pathlib.Path(f'/proc/{owned}').joinpath('stat').read_text().rsplit(')',1)[1].split()[19]==identity
   os.kill(owned,signal.SIGKILL);process.wait(timeout=5)
 if watchdog is not None:watchdog.cancel();watchdog.join(timeout=1)
 _,after=runtime();assert after==before and sql(settings).strip()=='1\t1\t1\t0\t0'
 (d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
assert owned is not None and not pathlib.Path(f'/proc/{owned}').exists()
x={'status':'MCP_MESSAGE_PAGE_CONTRACT_REAL_DOMAIN_COMPLETED_AND_STOPPED','compiled_head':build['head'],'binary_sha256':build['candidate_mcp_binary_sha256'],'domain_tools_passed':8,'valid_message_page_cases':6,'invalid_message_limit_cases':12,'unauthorized_and_spoof_cases':2,'all_requests':len(results),'friends_groups100_preserved':True,'owned_process_stopped':True,'all19_runtime_config_preserved':True,'sql_settings_preserved':True,'inference_called':False,'capacity_proof':False,'trace_summary_only':True,'latency_limit':'strace perturbs timing, noperformanceacceptance'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
