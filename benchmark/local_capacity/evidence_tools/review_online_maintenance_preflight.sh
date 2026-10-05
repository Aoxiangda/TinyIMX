#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,hashlib,re,socket
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'online-maintenance-mechanism-preflight-20261005';assert not d.exists()
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head=='7e93560b1b892c8f21b070308383140cd3cf0878'
before=json.loads((b/'online-shared-cost-proof-source-20261005/runtime-after.json').read_text())
ns=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();assert len(ns)==19;cs=json.loads(subprocess.check_output(['docker','inspect',*ns],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
now={'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}};assert now==before
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
cached={}
for name in ['flags.make','link.txt']:
 p=r/'build/linux-release/CMakeFiles/redis_pool_demo.dir'/name;assert p.is_file();cached[name]={'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'text':p.read_text()}
ref='33fc9bbbf7b8a8e828ce70d1ae0035d8e8c71ce8';pattern=r'constexpr const char\* kRefreshOnlineIfMatchScript = R"lua\((.*?)\)lua";'
current=(r/'services/cache/OnlineStatusCache.cpp').read_text();original=subprocess.check_output(['git','show',ref+':services/cache/OnlineStatusCache.cpp'],text=True)
script=re.search(pattern,current,re.S).group(1);assert script==re.search(pattern,original,re.S).group(1)
gateway=json.loads((cfg/'gateway-a.json').read_text());other=json.loads((cfg/'gateway-b.json').read_text());redis=gateway['redis'];assert all(redis[k]==other['redis'][k] for k in ['host','port','password','db','pool_size'])
c=next(c for c in cs if c['Name']=='/tinyimx-m21-redis-1');ips={x['IPAddress'] for x in c['NetworkSettings']['Networks'].values() if x.get('IPAddress')};assert len(ips)==1;ip=next(iter(ips))
def resp(stream):
 line=stream.readline();assert line.endswith(b'\r\n');kind=line[:1];value=line[1:-2]
 if kind==b'+':return value.decode()
 if kind==b':':return int(value)
 if kind==b'$':
  length=int(value)
  if length==-1:return None
  data=stream.read(length);assert len(data)==length and stream.read(2)==b'\r\n';return data.decode()
 if kind==b'*':return [resp(stream) for _ in range(int(value))]
 raise RuntimeError('Redis response error; server text and credentials remain private')
def send(sock,stream,*args):
 values=[str(value).encode() for value in args];payload=b'*'+str(len(values)).encode()+b'\r\n'+b''.join(b'$'+str(len(value)).encode()+b'\r\n'+value+b'\r\n' for value in values);sock.sendall(payload);return resp(stream)
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly native-to-existingRedis authenticated topology/resource and exactonlineLua preflight','source_head':head,'redis_container_id':c['Id'],'read_commands':['AUTH onlyownsocket credentialinRAM','SELECT onlyownsocket existingDB','INFO cluster/memory/clients/server','CONFIG GET maxmemory maxmemory-policy maxclients'],'ownplannedprefix':'codex:online-maintenance-probe-20261005:','fixture_mutations':'NONE inthispreflight, newowner-scopedkeys andTTL onlyfutureseparatelyauditedmechanism','config_fields':'OnlyhostIP/port/db/pool_size exported, no password/configfull/env','scope':'No newload/config/SQL/Rediswrite/enabling/reset/cleanup/service changes','rollback':'Closeonlyownsocket, keepnewboundedmetadata'},indent=2)+'\n')
with socket.create_connection((ip,int(redis['port'])),3) as sock:
 sock.settimeout(3)
 with sock.makefile('rb') as stream:
  if redis['password']:assert send(sock,stream,'AUTH',redis['password'])=='OK'
  assert send(sock,stream,'SELECT',redis['db'])=='OK'
  info={}
  for section in ['cluster','memory','clients','server']:
   text=send(sock,stream,'INFO',section);info.update({line.split(':',1)[0]:line.split(':',1)[1] for line in text.splitlines() if ':' in line and not line.startswith('#')})
  values=send(sock,stream,'CONFIG','GET','maxmemory','maxmemory-policy','maxclients');limits=dict(zip(values[::2],values[1::2]));assert info['cluster_enabled']=='0','Vector key mechanics requireactualstandalone; no productclustermigration'
fields={key:info[key] for key in ['redis_version','cluster_enabled','used_memory','used_memory_peak','used_memory_rss','connected_clients','blocked_clients']}
public={key:redis[key] for key in ['port','db','pool_size']};public['actual_native_target_ip']=ip
for name,value in cached.items():(d/('cached-'+name)).write_text(value['text'])
(d/'running-original-refresh.lua').write_text(script)
runtime_keys={}
for c0 in cs:
 if c0['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:continue
 env=dict(pair.split('=',1) for pair in c0['Config']['Env']);runtime_keys[c0['Name']]={key:env[key] for key in ['TINYIMX_PRESENCE_WORKER_THREADS','TINYIMX_MESSAGE_WORKER_THREADS'] if key in env}
x={'status':'ONLINE_MAINTENANCE_READONLY_PREFLIGHT_COMPLETE','head':head,'redis':fields,'limits':limits,'public_connection_fields':public,'gateway_public_runtime_flags':runtime_keys,'gateway_top_level_key_names':list(gateway.keys()),'original_refresh_script_sha256':hashlib.sha256(script.encode()).hexdigest(),'running_gateway_source':ref,'original_refresh_script_matches_current':True,'cached_compile_inputs':{name:value['sha256'] for name,value in cached.items()},'runtime_configs_preserved':True,'redis_fixture_writes':False,'component_only':'Nextisolatedkey/protocolcost prototype, no 10k/50k/productcapacityacceptance'};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
