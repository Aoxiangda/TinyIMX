#!/usr/bin/env python3
"""Read-only reconciliation of this iteration's three owned demo accounts."""
import datetime,json,pathlib,subprocess,uuid
from provision_desktop_accounts import ROOT,sql,NAMES
def main():
    out=ROOT/'.local/codex'/('desktop-reconciliation-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]);out.mkdir()
    (out/'audit-before.json').write_text(json.dumps({'operation':'SELECT-only owned demo accounts, relations, request and message identity reconciliation','names':NAMES,'runtime_writes':False,'SQL_writes':False,'pressure':False},indent=2))
    rows=[x.split('\t') for x in sql('SELECT user_id,username FROM im_users WHERE username IN ('+','.join("'"+n+"'" for n in NAMES)+') ORDER BY user_id').splitlines()]
    assert len(rows)==3 and {r[1] for r in rows}==set(NAMES)
    ids=','.join(str(int(r[0])) for r in rows);scope=f'from_user_id IN ({ids}) AND to_user_id IN ({ids})'
    messages=int(sql('SELECT COUNT(*) FROM im_private_messages WHERE '+scope))
    duplicates=sql('SELECT from_user_id,client_message_id,COUNT(*) FROM im_private_messages WHERE '+scope+' AND client_message_id IS NOT NULL GROUP BY from_user_id,client_message_id HAVING COUNT(*)>1')
    statuses=[x.split('\t') for x in sql('SELECT delivery_status,COUNT(*) FROM im_private_messages WHERE '+scope+' GROUP BY delivery_status').splitlines()]
    relations=int(sql(f'SELECT COUNT(*) FROM im_user_relations WHERE user_id IN ({ids}) AND peer_user_id IN ({ids}) AND relation_status=1'))
    requests=int(sql('SELECT COUNT(*) FROM im_friend_requests WHERE '+scope+' AND request_status=1'))
    summary={'status':'PASS' if relations==6 and requests==3 and messages>=11 and not duplicates else 'FAIL','out':str(out),'accounts':[{'user_id':r[0],'username':r[1]} for r in rows],
        'mutual_relation_rows':relations,'accepted_friend_requests':requests,'persisted_private_messages_including_retained_test_iterations':messages,'duplicate_sender_CID_groups':bool(duplicates),
        'delivery_status_counts':[{'status':int(s),'count':int(n)} for s,n in statuses],'SQL_writes':False,'pressure':False}
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));return 0 if summary['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
