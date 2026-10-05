#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
export TINYIMX_M21_STATE_DIR=/home/jackson7/.local/share/tinyimx/m21
export TINYIMX_RUNTIME_UID="$(id -u)" TINYIMX_RUNTIME_GID="$(id -g)"
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime,time
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'receiver-guarded-update-rollback-20261005';assert not d.exists()
deployed=b/'receiver-guarded-update-message-deployment-20261005';summary=json.loads((deployed/'summary.json').read_text());assert summary['status']=='RECEIVER_GUARDED_UPDATE_MESSAGE_READY'
original=json.loads((deployed/'runtime-private/original-container-inspect.json').read_text());before=json.loads((deployed/'audit-before.json').read_text())
assert subprocess.run(['pgrep','-f','^/home/jackson7/projects/TinyIMX_publish/build/linux-release/tinyimx_capacity_worker'],capture_output=True).returncode==1
def run(args,timeout=25):return subprocess.check_output(args,text=True,timeout=timeout)
def envmap(c):
 pairs=[pair.split('=',1) for pair in c['Config']['Env']];assert len(pairs)==len({pair[0] for pair in pairs});return dict(pairs)
current=json.loads(run(['docker','inspect','tinyimx-m21-message-service-1']))[0];old='sha256:b24e7bc15c532c455d2e614b16cdd79f1377d8331a11ba13fe5007615e0dc898'
assert (current['Image']==summary['image'] and current['Id']==summary['message_service_id']) or current['Image']==old
cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config');assert {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}==before['private_config_sha256']
base=['docker','compose','--env-file',str(r/'deploy/production/.env'),'-f',str(r/'deploy/production/docker-compose.yml'),'-f',str(deployed/'rollback.override.json'),'up','-d','--no-deps','--pull','never','message-service']
d.mkdir();(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Restore only Message originalb24 image afterprivatecontrol, or verify alreadyoriginal withoutrecreation','current_id':current['Id'],'current_image':current['Image'],'target_image':old,'command':base,'scope':'No other18/env/config/durability/dependency changes','preserve':'Candidate/image/evidence/DBrows and stoppedprobes retained; no delete/prune/reset/push','config_sha256':before['private_config_sha256']},indent=2)+'\n')
if current['Image']!=old:
 private=d/'runtime-private';private.mkdir()
 try:
  with (private/'message-service-before-rollback.log').open('w') as out:
   recorded=subprocess.run(['docker','logs','--since',before['utc'],'--timestamps',current['Id']],stdout=out,stderr=subprocess.STDOUT,timeout=25)
  (d/'phase-capture-status.json').write_text(json.dumps({'captured_container_id':current['Id'],'exit_code':recorded.returncode,'private_log_retained_not_exported':True})+'\n')
 except BaseException as error:
  (d/'phase-capture-status.json').write_text(json.dumps({'status':'FAIL','type':type(error).__name__,'rollback_still_required':True})+'\n')
 with (d/'rollback.log').open('w') as out:subprocess.run(base,stdout=out,stderr=subprocess.STDOUT,check=True,timeout=60)
end=time.monotonic()+50
while True:
 current=json.loads(run(['docker','inspect','tinyimx-m21-message-service-1']))[0]
 if current['Image']==old and current['State'].get('Health',{}).get('Status')=='healthy':break
 assert time.monotonic()<end,'OriginalMessage unhealthy';time.sleep(1)
assert envmap(current)==envmap(original) and current['Config']['Cmd']==original['Config']['Cmd'] and current['Config']['Healthcheck']==original['Config']['Healthcheck']
assert run(['docker','exec',current['Id'],'sha256sum','/opt/tinyimx/bin/message_service_demo']).split()[0]=='c11925efbdb97adfbb0bd3082ec1e932ddfa38b92534af078bf597f5b93dcf00'
names=run(['docker','ps','--format','{{.Names}}']).splitlines();assert len(names)==19;cs=json.loads(run(['docker','inspect',*names]));assert all(c['Name']==current['Name'] or before['before_containers'][c['Name']]=={'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs)
assert {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}==before['private_config_sha256']
query='SELECT @@innodb_flush_log_at_trx_commit,@@sync_binlog,@@log_bin,@@binlog_group_commit_sync_delay,@@binlog_group_commit_sync_no_delay_count'
assert run(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-rollback-readonly',query]).strip()=='1\t1\t1\t0\t0'
x={'status':'ORIGINAL_B24_MESSAGE_RESTORED_VERIFIED','image':old,'message_service_id':current['Id'],'binary_sha256':'c11925efbdb97adfbb0bd3082ec1e932ddfa38b92534af078bf597f5b93dcf00','other18_preserved':True,'config_env_cmd_health_durability_preserved':True,'candidate_image_retained':summary['image']};(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
