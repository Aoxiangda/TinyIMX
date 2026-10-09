#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,os,time,datetime,re
r=pathlib.Path.cwd();b=r/'.local/codex';failed=b/'permission20k-runtime-control-20261007';d=b/'permission20k-runtime-recovery-20261007-attempt2';assert not d.exists();sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest();run=lambda a:subprocess.check_output(a,text=True,timeout=30);ident=lambda c:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']};envmap=lambda c:dict(s.split('=',1) for s in c['Config']['Env'] if '=' in s)
assert (failed/'failed.json').is_file() and (failed/'restore-failed.json').is_file();assert not (b/'permission20k-diagnostic-20261007').exists();assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/.*tinyimx_capacity_worker'],capture_output=True).returncode==1
original=json.loads((failed/'runtime-private/original-inspect.json').read_text());prior=json.loads((failed/'audit-before.json').read_text());names=list(prior['runtime_before']);assert len(names)==19;roles=['gateway-a','gateway-b','social-service'];target={c['Name'] for c in original.values()};assert len(target)==3
inspect=lambda:json.loads(run(['docker','inspect',*names]));current=inspect();proof=json.loads((b/'permission-boundary-build-20261007/summary.json').read_text())
for c in current:
 if c['Name'] not in target:assert ident(c)==prior['runtime_before'][c['Name']] and c['State']['Running'] and c['State'].get('Health',{}).get('Status','healthy')=='healthy'
 else:
  role=c['Name'].removeprefix('/tinyimx-m21-').removesuffix('-1');assert c['Image']==original[role]['Image'];assert all(m['Source']=='/home/jackson7/.local/share/tinyimx/m21/config' and m['Destination']=='/run/tinyimx/config' for m in c['Mounts'])
  if role in roles[:2]:
   assert c['State']['Running'] and c['State'].get('Health',{}).get('Status')=='healthy'
   ips={v['IPAddress'] for x in current if x['Name'] in target for v in x['NetworkSettings']['Networks'].values()}
   for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
    f=line.split()
    if len(f)>3 and f[1].endswith(':2328') and f[3]=='01':
     import socket
     raw=f[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(raw)[::-1]) if len(raw)==8 else 'ipv6';assert peer in ips or peer.startswith('127.'),'Refuse live client interruption'

cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config').resolve();assert {p.name:sha(p) for p in cfg.glob('*.json')}==prior['config_sha256']
for c in original.values():assert len(c['Mounts'])==1 and c['Mounts'][0]['Type']=='bind' and pathlib.Path(c['Mounts'][0]['Source']).resolve()==cfg and cfg.is_dir()
os.environ['TINYIMX_M21_STATE_DIR']=str(cfg.parent)
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')]
d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700);save=lambda n,x:(d/n).write_text(json.dumps(x,indent=2)+chr(10));wanted={}
for role,c in original.items():
 wanted[role]={'image':c['Image'],'environment':envmap(c),'volumes':[{'type':'bind','source':str(cfg),'target':m['Destination'],'read_only':not m['RW'],'bind':{'create_host_path':True}} for m in c['Mounts']]};assert json.loads(run(['docker','image','inspect',c['Image']]))[0]['Id']==c['Image']
override=private/'exact-original.override.json';override.write_text(json.dumps({'services':wanted})+chr(10));os.chmod(override,0o600);proposed=json.loads(run(base+['-f',str(override),'config','--format','json']))['services']
for role,c in original.items():
 v=proposed[role];assert v['image']==c['Image'] and v['command']==c['Config']['Cmd'] and {k:str(x) for k,x in v['environment'].items()}==envmap(c);assert len(v['volumes'])==1 and v['volumes'][0]['source']==str(cfg) and v['volumes'][0]['target']==c['Mounts'][0]['Destination'] and v['volumes'][0].get('read_only',False)==(not c['Mounts'][0]['RW']) and v['volumes'][0]['bind']['create_host_path'] is True
root=pathlib.Path('/config');unexpected={'path':str(root),'exists':root.exists(),'deletion_or_modification':False,'limits':'Prior existence was not audited; do not claim exclusive ownership or remove it'}
if root.is_dir():
 st=root.stat();unexpected.update({'mode':oct(st.st_mode&0o777),'uid':st.st_uid,'gid':st.st_gid,'mtime_ns':st.st_mtime_ns,'ctime_ns':st.st_ctime_ns,'entries':[p.name for p in root.iterdir()]})
save('unexpected-config-directory-audit.json',unexpected)
save('audit-before.json',{'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Finish exact original Docker Binds/Propagation representation recovery for three already healthy task services; no load started/no nativecapacity workers; originalinspection and initial3healthy identities retained. Set known audited state parent in this child only and explicit original read-only bind with existing directory required and original Binds representation retained. Full compose preflight validates actual source/target/read-only before recreation. No production file/row/config/other16/hostapp changes or removal of /config. Retain failed script/receipts.','runtime_before':{c['Name']:ident(c) for c in current},'target_names':sorted(target),'originals':{s:ident(c) for s,c in original.items()},'state_parent':str(cfg.parent),'config_sha256':prior['config_sha256'],'override_sha256':sha(override),'all_target_bind_preflight_exact':True,'all_sources_existing_directories':True,'root_config_preserved':True})
def host(c):
 x=dict(c['HostConfig'])
 for k in ['Dns','DnsOptions','DnsSearch']:
  if x.get(k) is None:x[k]=[]
 if x.get('Binds') is not None:x['Binds']=sorted(x['Binds'])
 return x
try:
 with (private/'restore-compose.log').open('w') as f:subprocess.run(base+['-f',str(override),'up','-d','--no-deps','--force-recreate','--pull','never',*roles],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
 deadline=time.monotonic()+90
 while True:
  cs=inspect();picked={s:next(c for c in cs if c['Name']==original[s]['Name']) for s in roles}
  if all(c['State']['Running'] and c['State'].get('Health',{}).get('Status')=='healthy' for c in picked.values()):break
  assert time.monotonic()<deadline,'Original3health recovery timeout';time.sleep(1)
 for role,c in picked.items():
  o=original[role];assert c['Image']==o['Image'] and envmap(c)==envmap(o) and host(c)==host(o) and c['Mounts']==o['Mounts'] and c['Config']['Healthcheck']==o['Config']['Healthcheck'] and c['RestartCount']==0
  for k in ['User','WorkingDir','Cmd','Entrypoint','StopSignal']:assert c['Config'].get(k)==o['Config'].get(k)
  elf='/opt/tinyimx/bin/'+('gateway_demo' if role in roles[:2] else 'social_service_demo');assert run(['docker','exec',c['Id'],'sha256sum',elf]).split()[0]==prior['original_elf_sha256'][role]
 for c in cs:
  if c['Name'] not in target:assert ident(c)==prior['runtime_before'][c['Name']]
 assert {p.name:sha(p) for p in cfg.glob('*.json')}==prior['config_sha256'];durability=run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','readonly-permission-recovery','SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count']).strip();assert durability=='1\t1\t1\t0\t0'
 x={'status':'ALL19_HEALTHY_ORIGINAL3IMAGES_ENV_MOUNTS_HOST_HEALTH_ELF_AND_OTHER16_RECOVERED','runtime':{c['Name']:ident(c) for c in cs},'config_durability_preserved':True,'load_not_started':True,'target_restarts_only':sorted(target),'root_config_directory_preserved':True};save('summary.json',x);print(json.dumps({k:v for k,v in x.items() if k!='runtime'},indent=2))
except BaseException as e:save('failed.json',{'type':type(e).__name__,'message':str(e)});raise
PY
