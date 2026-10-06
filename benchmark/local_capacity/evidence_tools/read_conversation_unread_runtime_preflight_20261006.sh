#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,socket,http.client,re
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'conversation-unread-batch-runtime-preflight-20261006';assert not d.exists();d.mkdir(mode=0o700)
def run(a):return subprocess.check_output(a,text=True,timeout=30)
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(n,x):(d/n).write_text(json.dumps(x,indent=2)+'\n')
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly selection of existing owned offline users with unused pairs and Ollama/network inventory','writes':'Own evidence only; SQL SELECTs and Redis AUTH/SELECT/EXISTS, HTTP GET tags/ps; no fixture or inference/runtime/config mutation','preserve':'All histories/apps/images/configs'})
def sql(q):
 assert q.startswith('SELECT ') and ';' not in q
 return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','owned-list-preflight',q]).splitlines()
try:
 assert run(['git','rev-parse','HEAD']).strip()=='232a172b41a825ede81b9886e5c410bacf4e6734'
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19
 cs=json.loads(run(['docker','inspect',*names]));assert all(c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy' for c in cs)
 configs=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');hashes={p.name:sha(p) for p in configs.glob('*.json')}
 cfg=json.loads((configs/'gateway-a.json').read_text())['redis']
 redis=next(c for c in cs if c['Name']=='/tinyimx-m21-redis-1');ips=[v['IPAddress'] for v in redis['NetworkSettings']['Networks'].values() if v.get('IPAddress')];assert len(ips)==1
 users=[]
 for row in sql('SELECT user_id,username,status FROM im_users WHERE user_id BETWEEN 519800 AND 519950 ORDER BY user_id'):
  uid,name,status=row.split('\t');uid=int(uid)
  if status=='1' and name==f'm21b500000_{uid-500000:06d}' and uid not in [519870,519872,519890,519892]:users.append(uid)
 occupied=set()
 for q in ['SELECT user_id,peer_user_id FROM im_user_relations WHERE user_id BETWEEN 519800 AND 519950 AND peer_user_id BETWEEN 519800 AND 519950','SELECT from_user_id,to_user_id FROM im_friend_requests WHERE from_user_id BETWEEN 519800 AND 519950 AND to_user_id BETWEEN 519800 AND 519950']:
  for row in sql(q):occupied.add(tuple(sorted(map(int,row.split('\t')))))
 s=socket.create_connection((ips[0],cfg['port']),3);s.settimeout(3);f=s.makefile('rb')
 def call(*args):
  wire=b'*'+str(len(args)).encode()+b'\r\n'
  for value in args:
   value=str(value).encode();wire+=b'$'+str(len(value)).encode()+b'\r\n'+value+b'\r\n'
  s.sendall(wire);line=f.readline(1024);assert line.endswith(b'\r\n')
  if line[:1]==b'+':return line[1:-2]
  if line[:1]==b':':return int(line[1:-2])
  if line[:1]==b'$':
   n=int(line[1:-2]);assert -1<=n<=1048576
   if n==-1:return None
   value=f.read(n);assert len(value)==n and f.read(2)==b'\r\n';return value
  raise RuntimeError('OwnReadonlyRedisReplyRejected')
 try:
  if cfg.get('password'):assert call('AUTH',cfg['password'])==b'OK'
  if cfg.get('db',0):assert call('SELECT',cfg['db'])==b'OK'
  available=[uid for uid in users if call('EXISTS','tinyimx:online:'+str(uid))==0]
 finally:f.close();s.close()
 cross=json.loads(run(['bash',str(r/'benchmark/local_capacity/evidence_tools/select_online_maintenance_actor_pairs_readonly.sh')]))
 anchor=519950;assert anchor in available
 for row in sql(f'SELECT DISTINCT from_user_id,to_user_id FROM im_private_messages WHERE from_user_id={anchor} OR to_user_id={anchor}'):
  occupied.add(tuple(sorted(map(int,row.split('\t')))))
 peers=[u for u in available if 519810<=u<519950 and u not in cross['users'] and tuple(sorted((u,anchor))) not in occupied][:50];assert len(peers)==50
 save('fixed-fixture-selection.json',{'anchor':anchor,'peers':peers,'cross_users':cross['users'],'count':50,'identities_verified':True,'online_absent':True,'bothdirection_relations_requests_messages_absent':True,'SQL_writes':False})
 def get(host,path,timeout=2):
  c=http.client.HTTPConnection(host,11434,timeout=timeout)
  try:
   c.request('GET',path);p=c.getresponse();raw=p.read(1024*1024);return {'reachable':True,'status':p.status,'json':json.loads(raw)}
  except (OSError,ValueError) as e:return {'reachable':False,'error_type':type(e).__name__}
  finally:c.close()
 ai=json.loads((configs/'ai-agent.json').read_text());mcp=next(c for c in cs if c['Name']=='/tinyimx-m21-mcp-server-1');net=next(iter(mcp['NetworkSettings']['Networks']));network=json.loads(run(['docker','network','inspect',net]))[0]
 gateway=network['IPAM']['Config'][0]['Gateway']
 tags=get('127.0.0.1','/api/tags');ps=get('127.0.0.1','/api/ps')
 ollama={'original_endpoint':ai['ai']['endpoint'],'original_model':ai['agent']['model'],'guest_tags':tags,'guest_loaded':ps,'bridge_gateway_tags':get(gateway,'/api/tags'),'mcp_network':net,'inference_called':False}
 save('ollama-network-readonly.json',ollama)
 x={'status':'CONVERSATION_UNREAD_RUNTIME_PREFLIGHT_PASS','head':run(['git','rev-parse','HEAD']).strip(),'selection':{'anchor':anchor,'peers':peers,'cross_users':cross['users']},'runtime':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':hashes,'ollama_existing_models':[m['name'] for m in tags.get('json',{}).get('models',[])],'ollama_bridge_reachable':ollama['bridge_gateway_tags']['reachable'],'mem_available_kib':int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1)),'performance_acceptance':False}
 save('summary.json',x);print(json.dumps(x,indent=2))
except BaseException as e:save('failed.json',{'type':type(e).__name__,'message':str(e),'no_product_mutation':True});raise
PY
