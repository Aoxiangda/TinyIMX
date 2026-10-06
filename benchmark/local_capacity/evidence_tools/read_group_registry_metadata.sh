#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,re,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'group-registry-readonly-metadata-20261006';assert not d.exists()
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
g=json.loads((cfg/'group.json').read_text());gw=json.loads((cfg/'gateway-a.json').read_text())
zk=g['zookeeper'];root=zk['service_root'];host=zk['advertise_host']
assert re.fullmatch(r'/[a-zA-Z0-9_/-]+',root) and re.fullmatch(r'[a-zA-Z0-9_.-]+',host)
target=host+':50054';node=root+'/group/group@'+target
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
audit={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'ReadonlyexistingZooKeeperCLI ls/getstat only exactGroup registration; oneowneddiagnosticsession closes byquit','commands':['ls '+root+'/group','get -s '+node,'quit'],'namespace_mutations':False,'raw_config_orcredentials_exported':False}
(d/'audit-before.json').write_text(json.dumps(audit,indent=2)+'\n')
argv=['docker','exec','-i','tinyimx-m21-zookeeper-1','/apache-zookeeper-3.8.5-bin/bin/zkCli.sh','-server','127.0.0.1:2181']
p=subprocess.run(argv,input='\n'.join(audit['commands'])+'\n',capture_output=True,text=True,timeout=30);(private/'zkcli.log').write_text(p.stdout+'\n'+p.stderr)
rows=[]
for line in p.stdout.splitlines():
 if line.startswith('{'):
  try:
   v=json.loads(line)
   if set(v)=={'schema_version','service_name','instance_id','target','protocol','version'}:rows.append(v)
  except ValueError:pass
stats=[line for line in p.stdout.splitlines() if any(line.startswith(v) for v in ['ephemeralOwner =','dataVersion =','cZxid =','mZxid =','ctime =','mtime =','numChildren ='])]
x={'status':'GROUP_CURRENT_REGISTRY_METADATA_READONLY','exit':p.returncode,'root':root,'requested_node':node,'instances':rows,'node_stat':stats,'no_node_reported':'Node does not exist' in p.stdout+p.stderr,'gateway_discovery':gw.get('service_discovery',{}),'group_public_zookeeper_fields':{k:zk.get(k) for k in ['enable','session_timeout_ms','connect_timeout_ms','registration_timeout_ms','advertise_host','service_version']},'raw_private_log_sha256':hashlib.sha256((private/'zkcli.log').read_bytes()).hexdigest(),'limits':'Currentnodeafterrestore, nothistoricalfaultsnapshotorGatewayprivatecache; no mutations'}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
