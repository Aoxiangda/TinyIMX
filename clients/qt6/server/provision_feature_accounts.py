#!/usr/bin/env python3
"""Create exactly three fresh demonstration accounts; no existing row changes."""
import datetime,hashlib,json,os,pathlib,subprocess,sys
ROOT=pathlib.Path('/home/jackson7/projects/TinyIMX_publish')
NAMES=['features_alice_20261007','features_bob_20261007','features_carol_20261007']
def sql(statement):
    r=subprocess.run(['docker','exec','-i','tinyimx-m21-mysql-1','sh','-c','MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE"'],input=statement+'\n',text=True,capture_output=True,timeout=30)
    if r.returncode:raise RuntimeError('SQL failed; preserve audit and partial state; never reset/delete accounts')
    return r.stdout.strip()
def write(path,obj):path.write_text(json.dumps(obj,ensure_ascii=False,indent=2)+'\n')
def main():
    apply=sys.argv[1:]==['--apply'];assert apply or not sys.argv[1:]
    os.chdir(ROOT);os.umask(0o077)
    out=ROOT/'.local/codex'/('feature-accounts-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
    out.mkdir(); quoted=','.join("'"+n+"'" for n in NAMES)
    roles=['mysql','redis','zookeeper','rocketmq-namesrv','rocketmq-broker','rocketmq-proxy','user-service','group-service','file-service','message-service','social-service','gateway-a','gateway-b','outbox-relay','unread-projector','mcp-server','nginx','prometheus','otel-collector']
    containers=json.loads(subprocess.check_output(['docker','inspect']+['tinyimx-m21-'+role+'-1' for role in roles],text=True))
    before=[{'name':r['Name'].lstrip('/'),'id':r['Id'],'image':r['Image'],'started':r['State']['StartedAt']} for r in containers]
    assert len(before)==19 and all(r['State']['Running'] for r in containers),'Expected accepted running 19-container deployment'
    audit={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'operation':'insert three new GUI demonstration accounts' if apply else 'read-only preflight',
        'accounts':NAMES,'credential_scope':'new demonstration password 123456 only, existing passwords never read or changed','existing_rows_modified':False,'relations_inserted_directly':False,'delete_update_upsert':False,'runtime_changes':False,
        'auto_increment':'database assigns IDs; no forced IDs or reset','rollback':'retain owned rows and test messages; no automatic deletion','runtime_before':before}
    write(out/'audit-before.json',audit)
    assert sql('SELECT COUNT(*) FROM im_users WHERE username IN ('+quoted+')')=='0','Accounts already exist; refuse INSERT; audit retained'
    audit['auto_increment_before']=sql("SELECT AUTO_INCREMENT FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='im_users'")
    write(out/'preflight.json',audit)
    if not apply:print(json.dumps({'status':'PREFLIGHT_PASS','audit':str(out)}));return
    salt=os.urandom(16);digest=hashlib.pbkdf2_hmac('sha256',b'123456',salt,100000,32).hex()
    values=','.join("('%s','%s',1,'%s','%s')"%(name,nick,salt.hex(),digest) for name,nick in zip(NAMES,['Alice','Bob','Carol']))
    statement='START TRANSACTION; INSERT INTO im_users(username,nickname,status,password_salt,password_hash) VALUES '+values+'; COMMIT;'
    (out/'insert.sql').write_text(statement+'\n');sql(statement)
    rows=[line.split('\t') for line in sql('SELECT user_id,username,nickname,status FROM im_users WHERE username IN ('+quoted+') ORDER BY user_id').splitlines()]
    assert len(rows)==3 and {r[1] for r in rows}==set(NAMES) and all(r[3]=='1' for r in rows)
    accounts=[{'user_id':r[0],'username':r[1],'nickname':r[2]} for r in rows]
    profile={'endpoint':'192.168.220.128:9000','password':'123456','accounts':accounts}
    write(out/'demo-profile.json',profile)
    after=json.loads(subprocess.check_output(['docker','inspect']+[r['name'] for r in before],text=True))
    assert before==[{'name':r['Name'].lstrip('/'),'id':r['Id'],'image':r['Image'],'started':r['State']['StartedAt']} for r in after],'Runtime identity changed'
    summary={'status':'PASS','audit':str(out),'accounts':accounts,'inserted_users':3,'auto_increment_after':sql("SELECT AUTO_INCREMENT FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='im_users'"),'runtime_identity_unchanged':True,'pressure':False}
    write(out/'summary.json',summary);print(json.dumps(summary))
if __name__=='__main__':main()
