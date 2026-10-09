#!/usr/bin/env python3
"""Actual Compose render negative/positive mount preflight; no up/stop or load."""
import pathlib,json,subprocess,os,copy,datetime,hashlib
from permission_runtime_mount_preflight import validate_mounts
r=pathlib.Path('/home/jackson7/projects/TinyIMX_publish');b=r/'.local/codex';d=b/'permission20k-mount-preflight-20261007';assert not d.exists();original=json.loads((b/'permission20k-runtime-control-20261007/runtime-private/original-inspect.json').read_text());cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');roles=list(original);d.mkdir(mode=0o700);private=d/'runtime-private';private.mkdir(mode=0o700)
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')];cases=[]
(d/'audit-before.json').write_text(json.dumps({'operation':'Only real Compose config renders with stateenv absent/present and ownprivate exact image/env override; validate exact source/target/readonly/existingdirectory and Binds API. No Dockerup/stop, workload, configwrite or directorydelete','source_sha256':hashlib.sha256((r/'benchmark/local_capacity/permission_runtime_mount_preflight.py').read_bytes()).hexdigest(),'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})+chr(10))
override=private/'original-env.override.json';override.write_text(json.dumps({'services':{role:{'image':c['Image'],'environment':dict(v.split('=',1) for v in c['Config']['Env'] if '=' in v)} for role,c in original.items()}}));os.chmod(override,0o600)
def render(label,state):
 env=dict(os.environ);env.pop('TINYIMX_M21_STATE_DIR',None)
 if state is not None:env['TINYIMX_M21_STATE_DIR']=state
 with (private/(label+'-stderr.log')).open('w') as f:x=json.loads(subprocess.check_output(base+['-f',str(override),'config','--format','json'],env=env,stderr=f,text=True,timeout=20))
 return x['services']
def rejected(name,data):
 try:validate_mounts(data,original)
 except AssertionError as e:cases.append({'name':name,'pass':True,'rejected':str(e)});return
 raise AssertionError('Invalid mounts accepted '+name)
bad=render('missing-state',None);rejected('actual_missing_state_directory_variable',bad);good=render('correct-state',str(cfg.parent));validate_mounts(good,original);cases.append({'name':'actual_original_state_directory_render','pass':True})
for name,field,value in [('readonly','read_only',False),('wrong-target','target','/unapproved/task'),('wrong-source','source','/config')]:
 data=copy.deepcopy(good);data[roles[0]]['volumes'][0][field]=value;rejected(name,data)
data=copy.deepcopy(good);data[roles[0]]['volumes'][0]['bind']={'create_host_path':False};rejected('different_Docker_mount_API_representation',data)
data=copy.deepcopy(good);data[roles[0]]['volumes'].append(copy.deepcopy(data[roles[0]]['volumes'][0]));rejected('duplicate_mount',data)
x={'status':'REAL_COMPOSE_MOUNT_PREFLIGHT_POSITIVE_NEGATIVE_PASS','checks':len(cases),'cases':cases,'runtime_or_data_change':False};(d/'summary.json').write_text(json.dumps(x,indent=2)+chr(10));print(json.dumps(x))
