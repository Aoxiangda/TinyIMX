#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,hashlib,datetime
r=pathlib.Path.cwd();b=r/'.local/codex';old=b/'receiver-confirm-guarded-update-mysql-20261005';d=b/'receiver-confirm-guarded-update-red-result-review-20261005';assert not d.exists()
failed=json.loads((old/'failed.json').read_text());check=failed['checks']['red1-original_adapter_new_tests'];assert check['exit_code']==1
text=(old/'red1-original_adapter_new_tests.log').read_text();raw=[line for line in text.splitlines() if line.startswith('[FAIL]')];individual=[line for line in raw if 'M16-A Transactional Outbox integration tests, failed=' not in line];aggregate=[line for line in raw if 'M16-A Transactional Outbox integration tests, failed=' in line]
assert len(individual)==len(aggregate)==1 and 'Pending normal path avoids SELECT before update' in individual[0] and aggregate[0].endswith('failed=1')
names=['codex_receiver_guard_20261005_red1','codex_receiver_guard_20261005_p1','codex_receiver_guard_20261005_p4']
query="SELECT SCHEMA_NAME FROM information_schema.SCHEMATA WHERE SCHEMA_NAME IN ('"+"','".join(names)+"') ORDER BY SCHEMA_NAME"
actual=subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','own-red-classification',query],text=True,timeout=10).splitlines();assert actual==[names[0]]
before=json.loads((old/'runtime-before.json').read_text());after=json.loads((old/'runtime-after.json').read_text());assert before==after
current_names=subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines();cs=json.loads(subprocess.check_output(['docker','inspect',*current_names],text=True));cfg=pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
current={'containers':{c['Name']:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']} for c in cs},'config_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in cfg.glob('*.json')}};assert current==after
assert not subprocess.check_output(['git','diff','--name-only'],text=True).strip()
d.mkdir();x={'status':'ORIGINAL_RED_EXPECTED_ONE_ASSERTION_CLASSIFIED','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'original_failed_json_sha256':hashlib.sha256((old/'failed.json').read_bytes()).hexdigest(),'original_red_log_sha256':hashlib.sha256(text.encode()).hexdigest(),'individual_failed_assertions':individual,'aggregate_failure_markers':aggregate,'individual_passes':sum(line.startswith('[PASS]') for line in text.splitlines()),'exit_code':1,'first_harness_error':'Counted assertion plus aggregate summary twice; expected old read-before-update behavior correctly observed','planned_schemas_in_original_failed_record':names,'actually_created_schemas':actual,'all19_runtime_configs_preserved':True,'originals_preserved':True,'product_changes_in_review':False,'new_write_SQL':False,'candidate_green_status':'NOT_RUN'}
(d/'summary.json').write_text(json.dumps(x,indent=2)+'\n');(d/'runtime-after.json').write_text(json.dumps(current,indent=2)+'\n');print(json.dumps(x,indent=2))
PY
