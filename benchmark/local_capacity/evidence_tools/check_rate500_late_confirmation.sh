#!/usr/bin/env bash
set -euo pipefail
umask 077
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,subprocess,datetime,collections,hashlib
r=pathlib.Path.cwd();b=r/'.local/codex';d=b/'rate500-late-confirmation-readonly-20261005';assert not d.exists();d.mkdir()
(d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Readonly later status of exact500/s run IDs, preserve original failed snapshot','writes':'New numeric diagnostic only; no repair SQL','limits':'Later background recovery can change states; cannot retroactively pass original run'})+'\n')
p=b/'capacity-acrate500a';recon=json.loads((p/'reconciliation.json').read_text());old={}
for line in (p/'sql-reconciliation.tsv').read_text().splitlines():
 mid,u,v,cid,status=line.split('\t');assert cid.startswith('lacrate500a-');old[cid]=(int(mid),int(u),int(v),int(status))
q="SELECT message_id,from_user_id,to_user_id,client_message_id,delivery_status FROM im_private_messages WHERE client_message_id LIKE 'lacrate500a-%' ORDER BY message_id"
raw=subprocess.check_output(['docker','exec','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"','owned-late-status-readonly',q],text=True)
new={}
for line in raw.splitlines():
 mid,u,v,cid,status=line.split('\t');assert cid.startswith('lacrate500a-');new[cid]=(int(mid),int(u),int(v),int(status))
bad=recon['positive_ack_identity_or_confirmation_or_wire_mismatches'];assert all(cid in old for cid in bad)
out={'old_rows':len(old),'later_rows':len(new),'old_pending':sum(v[3]==0 for v in old.values()),'later_pending':sum(v[3]==0 for v in new.values()),'identity_changes':sum(cid not in new or new[cid][:3]!=v[:3] for cid,v in old.items()),'original_anomaly_count':len(bad),'original_anomalies_now_confirmed':sum(cid in new and new[cid][3]==1 for cid in bad),'original_run_status':'FAIL retained','original_sql_sha256':hashlib.sha256((p/'sql-reconciliation.tsv').read_bytes()).hexdigest(),'later_sql_sha256':hashlib.sha256(raw.encode()).hexdigest(),'note':'SQL confirmation later cannot prove original wire delivery or deadline; no direct data repair'}
(d/'later-sql-reconciliation.tsv').write_text(raw);(d/'summary.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
PY
