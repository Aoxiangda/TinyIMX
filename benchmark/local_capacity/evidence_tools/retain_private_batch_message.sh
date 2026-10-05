#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export CODEX_SQL_BATCH_PHASE="on" CODEX_SQL_BATCH_STEP="retained-on"
python3 - <<'PY'
import pathlib,json,subprocess,datetime,time,hashlib,socket,os,re
r=pathlib.Path.cwd();b=r/'.local/codex';phase=os.environ['CODEX_SQL_BATCH_PHASE'];step=os.environ['CODEX_SQL_BATCH_STEP']
assert (phase,step)==('on','retained-on')
analysis=json.loads((b/'private-batch-abba-analysis-20261005/summary.json').read_text());assert analysis['status']=='PRIVATE_BATCH_ABBA_ANALYSIS_COMPLETE' and len(analysis['official'])==4 and not analysis['full_feature_acceptance']
values=[x['private']['positive_ack_p99_ms_upper_bin'] for x in analysis['official']];assert max(values[1:3])<min(values[0],values[3])
d=b/('private-batch-message-deployment-'+step+'-20261005');assert not d.exists()
source=json.loads((b/'private-batch-abba-outcome-source-20261005/summary.json').read_text());head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert head==source['head']
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
assert int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))>2*1024*1024
image=json.loads((b/'private-batch-message-build-image-20261005/summary.json').read_text());assert image['status']=='PRIVATE_BATCH_FULL_MESSAGE_SEALED_IMAGE_PASS'
assert image['image_id']=='sha256:be8b5ddea1a874ce017b0e8dfda1c3ac4e5c0f5b8fa938041aae1a4aaba0a060' and image['binary_sha256']=='2548733766409d282d30f6ffbbbe47f9c12b5d4fa3cacfdcc3d3e47f72582699'
original_image='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898';original_tag='tinyimx/runtime:codex-private-single-lease-v1';original_binary='c11925efbdb97adfbb0bd3082ec1e932ddfa38b92534af078bf597f5b93dcf00'
original=json.loads((b/'private-batch-endpoint-controls-source-20261005/runtime-after.json').read_text());cfgroot=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
def run(args,timeout=20):return subprocess.check_output(args,text=True,timeout=timeout)
def runtime():
 names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]))
 current={'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfgroot.glob('*.json')}}
 assert current['config_sha256']==original['config_sha256'] and all(n=='/tinyimx-m21-message-service-1' or v==original['containers'][n] for n,v in current['containers'].items())
 return current,cs
before,cs=runtime();target=next(c for c in cs if c['Name']=='/tinyimx-m21-message-service-1');assert target['State']['Health']['Status']=='healthy'
assert target['Id']=='f642479a9be4c4a0f89a6c3bd7856129fb5b899ca9f4d2ee430a0c6c13ef87ed'
flag='TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE'
def envmap(c):return dict(x.split('=',1) for x in c['Config']['Env'] if '=' in x)
initial=b/'private-batch-message-deployment-off-initial-20261005/runtime-private/original-container-inspect.json'
baseline=target if step=='off-initial' else json.loads(initial.read_text());baseline_env=envmap(baseline);assert baseline['Image']==original_image and flag not in baseline_env
if step=='off-initial':assert target['Id']==original['containers'][target['Name']]['id'] and target['Image']==original_image
elif phase!='original':assert target['Image']==image['image_id'] and envmap(target).get(flag)==('0' if phase=='on' else '1')
else:assert target['Image'] in [original_image,image['image_id']]
assert envmap(target).get('TINYIMX_PERSIST_PHASE_TRACE_ENABLE')=='1' and 'TINYIMX_STORAGE_WAIT_TRACE_ENABLE' not in envmap(target) and 'TINYIMX_MYSQL_POOL_TRACE' not in envmap(target)
assert {k:v for k,v in envmap(target).items() if k!=flag}==baseline_env
ips={v['IPAddress'] for c in cs if c['Name'] in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1'] for v in c['NetworkSettings']['Networks'].values()}
for c in cs:
 if c['Name'] not in ['/tinyimx-m21-gateway-a-1','/tinyimx-m21-gateway-b-1']:continue
 for line in run(['docker','exec',c['Id'],'cat','/proc/net/tcp','/proc/net/tcp6']).splitlines():
  fields=line.split()
  if len(fields)>3 and fields[1].endswith(':2328') and fields[3]=='01':
   address=fields[2].split(':')[0];peer=socket.inet_ntoa(bytes.fromhex(address)[::-1]) if len(address)==8 else 'ipv6';assert peer in ips or peer.startswith('127.'),'Existing business session active; no restart'
def sql(q):return run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-batch-deploy-audit',q])
durability='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count';assert sql(durability).strip()=='1\t1\t1\t0\t0'
assert json.loads((cfgroot/'message.json').read_text())['mysql']['pool_size']==16
os.environ['TINYIMX_M21_STATE_DIR']='/home/jackson7/.local/share/tinyimx/m21';os.environ['TINYIMX_RUNTIME_UID']=str(os.getuid());os.environ['TINYIMX_RUNTIME_GID']=str(os.getgid());os.environ.pop(flag,None)
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml')]
d.mkdir();private=d/'runtime-private';private.mkdir();os.chmod(private,0o700)
(private/'original-container-inspect.json').write_text(json.dumps(target,indent=2)+'\n');(d/'runtime-before.json').write_text(json.dumps(before,indent=2)+'\n')
expected_image=original_image if phase=='original' else image['image_id'];tag=original_tag if phase=='original' else image['image_tag'];value=None if phase=='original' else ('1' if phase=='on' else '0')
override=d/'candidate.override.json';rollback=d/'rollback.override.json'
override.write_text(json.dumps({'services':{'message-service':{'image':tag,'environment':{'TINYIMX_PERSIST_PHASE_TRACE_ENABLE':'1',flag:value}}}})+'\n')
rollback.write_text(json.dumps({'services':{'message-service':{'image':original_tag,'environment':{'TINYIMX_PERSIST_PHASE_TRACE_ENABLE':'1',flag:None}}}})+'\n')
assert json.loads(run(['docker','image','inspect',tag]))[0]['Id']==expected_image
proposed=json.loads(run(base+['-f',str(override),'config','--format','json']));(private/'proposed-compose-config.json').write_text(json.dumps(proposed,indent=2)+'\n')
proposed_service=proposed['services']['message-service'];assert proposed_service['command']==baseline['Config']['Cmd'] and proposed_service['image']==tag
assert (proposed_service.get('environment',{}).get(flag)==value)
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Replace only idle MessageService for same ELF OFF/ON control or exact original restoration','head':head,'phase':phase,'step':step,'before':before,'old_message_image':target['Image'],'new_image':expected_image,'binary_sha256':original_binary if phase=='original' else image['binary_sha256'],'only_environment_difference':{flag:value},'config_pool':16,'durability':'1/1/1/0/0','impact':'Brief RPC reconnect after owned clients drain; other18 exact IDs/images/starts retained, configs/DB/allapps unchanged','private_capture':'Original inspect/log and proposed compose only in fresh0700 private directory, notexported','rollback_command':base+['-f',str(rollback),'up','-d','--no-deps','--pull','never','message-service'],'preserve':'Allrows/source/candidate/base/probes/evidence retained; no arbitrary file/container cleanup'},indent=2)+'\n')
with (private/'message-before.log').open('w') as f:subprocess.run(['docker','logs','--timestamps',target['Id']],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=25)
def deploy(path,label):
 with (d/(label+'.log')).open('w') as f:subprocess.run(base+['-f',str(path),'up','-d','--no-deps','--pull','never','message-service'],stdout=f,stderr=subprocess.STDOUT,check=True,timeout=60)
 end=time.monotonic()+55
 while True:
  current=json.loads(run(['docker','inspect','tinyimx-m21-message-service-1']))[0]
  if current['State']['Status']=='running' and current['State'].get('Health',{}).get('Status')=='healthy':return current
  assert time.monotonic()<end,'MessageService not healthy';time.sleep(2)
def verify(c,imageid,flagvalue,binary):
 assert c['Image']==imageid and c['RestartCount']==0 and c['Config']['Cmd']==baseline['Config']['Cmd']
 assert envmap(c).get(flag)==flagvalue and {k:v for k,v in envmap(c).items() if k!=flag}==baseline_env
 assert run(['docker','exec',c['Id'],'sha256sum','/opt/tinyimx/bin/message_service_demo']).split()[0]==binary
 after,_=runtime();assert sql(durability).strip()=='1\t1\t1\t0\t0'
 ip=next(iter(c['NetworkSettings']['Networks'].values()))['IPAddress']
 with socket.create_connection((ip,50053),3):pass
 return after
try:
 current=deploy(override,'deployment');after=verify(current,expected_image,value,original_binary if phase=='original' else image['binary_sha256'])
 (d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
 x={'status':'PRIVATE_BATCH_MESSAGE_RETAINED_PARTIAL_GAIN','head':head,'phase':phase,'step':step,'image':current['Image'],'message_service_id':current['Id'],'binary_sha256':original_binary if phase=='original' else image['binary_sha256'],'batch_flag':value,'other18_configs_preserved':True,'performance':'ABBA_PARTIAL_GAIN_ALL_FOUR_P99_FAIL','full_feature_acceptance':False}
 (d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
except BaseException as e:
 (d/'failed.json').write_text(json.dumps({'status':'FAIL','type':type(e).__name__,'message':str(e)})+'\n')
 try:
  restored=deploy(rollback,'automatic-original-rollback');after=verify(restored,original_image,None,original_binary);(d/'rollback-outcome.json').write_text(json.dumps({'status':'ORIGINAL_RESTORED','message_service_id':restored['Id'],'other18_preserved':True})+'\n');(d/'runtime-after.json').write_text(json.dumps(after,indent=2)+'\n')
 except BaseException as rollback_error:(d/'rollback-failed.json').write_text(json.dumps({'status':'FAIL','type':type(rollback_error).__name__,'message':str(rollback_error)})+'\n');raise
 raise
PY
