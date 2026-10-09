#!/usr/bin/env python3
"""Audited insert-only fresh synthetic fixture. Default invocation is SELECT-only.

Never reset old passwords/status/relations, upsert, delete or lower AUTO_INCREMENT.
Apply outside an active owned benchmark; keep partial batches after any failure.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import subprocess

ROOT = pathlib.Path('/home/jackson7/projects/TinyIMX_publish')


def database(statement):
    p = subprocess.run(['docker','exec','-i','tinyimx-m21-mysql-1','sh','-c',
                        'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE"'],
                       input=statement+'\n',text=True,capture_output=True,timeout=120)
    if p.returncode:
        raise RuntimeError('Owned fixture SQL failed (exit '+str(p.returncode)+'); private batch retained; no row reset or delete')
    return p.stdout.strip()


def save(path, obj):
    path.write_text(json.dumps(obj,indent=2)+'\n')


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--run',required=True)
    p.add_argument('--user-id-base',type=int,default=700000)
    p.add_argument('--username-prefix',default='codex50k_20261004_')
    p.add_argument('--users',type=int,default=50000)
    p.add_argument('--apply',action='store_true')
    p.add_argument('--verify-run',help='SELECT-only verification of an already completed, preserved seed plan')
    a=p.parse_args()
    assert re.fullmatch('[a-zA-Z0-9-]{1,20}',a.run)
    assert 100000<=a.user_id_base<=1000000000000 and 2<=a.users<=50000
    assert re.fullmatch('[a-zA-Z0-9_-]{1,40}',a.username_prefix)
    assert a.verify_run is None or (not a.apply and re.fullmatch('[a-zA-Z0-9-]{1,20}',a.verify_run))
    os.chdir(ROOT);os.umask(0o077)
    d=ROOT/'.local/codex'/('owned-fixture-'+a.run)
    assert not d.exists(),'Never overwrite prior seed evidence'
    d.mkdir()
    b=a.user_id_base;n=a.users;hi=b+n;prefix=a.username_prefix
    failure=None;batch_count=0;inserted_users=inserted_relations=0
    audit={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Fresh synthetic fixture dry-run/apply',
           'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'script_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
           'args':vars(a),'scope':{'first_uid':b+1,'last_uid':hi,'username_formula':prefix+' + 6-digit(uid-base)','status':1},
           'preflight':'Refuse any existing user, matching username prefix, referencing row, or owned active worker before INSERT',
           'possible_changes': 'With --apply: only fresh INSERT users and mutual adjacent ring relations, <=1000rows/transaction; im_usersAUTO_INCREMENT may rise and is recorded, never reset',
           'existing_rows_modified':False,'UPDATE_DELETE_UPSERT_IGNORE':False,'runtime_service_changes':False,'user_process_changes':False,
           'credentials':'New synthetic accounts use demo password123456 and one shared random test-fixture salt/PBKDF2_HMAC_SHA256(100000,32bytes), no existing credentials read/exported/reset',
           'rollback':'Keep owned rows, all SQL private batches and partial failure manifest; no automatic deletion/reset. Stop own test only',
           'validation':'Exact postinsert identity/status/hash count; all mutual ring edges; actual authentication in later real benchmark'}
    save(d/'audit-before.json',audit)
    try:
        active=[]
        for path in (ROOT/'.local/codex').glob('capacity-*/worker-pids.json'):
            for row in json.loads(path.read_text()):
                proc=pathlib.Path('/proc')/str(row['pid'])/'cmdline'
                try:args=proc.read_bytes().split(b'\0')
                except (FileNotFoundError,ProcessLookupError,PermissionError):continue
                owned=str(ROOT/'build/linux-release/tinyimx_capacity_worker').encode()
                if args and args[0]==owned and str(path.parent).encode() in b' '.join(args):
                    active.append({'pid':row['pid'],'run':path.parent.name})
        save(d/'owned-active-workers.json',active)
        assert not active,'Never seed during active owned capacity measurement'
        if a.verify_run:
            prior=ROOT/'.local/codex'/('owned-fixture-'+a.verify_run)
            old=json.loads((prior/'audit-before.json').read_text())['args']
            assert old['apply'] and all(old[k]==getattr(a,k) for k in ('users','user_id_base','username_prefix')),'Bind the exact previous seed scope'
            plan=json.loads((prior/'batch-plan-before.json').read_text())
            done=[json.loads(line) for line in (prior/'completed-batches.jsonl').read_text().splitlines()]
            assert [x['batch'] for x in done]==list(range(len(plan))) and all(x['committed'] for x in done),'Incomplete seed requires a new explicit partial-batch audit; never implicitly retry inserts'
            for entry in plan:
                path=(prior/entry['private_file']).resolve()
                assert path.is_relative_to(prior/'runtime-private') and hashlib.sha256(path.read_bytes()).hexdigest()==entry['sql_sha256'],'Exact prior private batch identity'
            first=(prior/plan[0]['private_file']).read_text()
            match=re.search(r"'([0-9a-f]{32})','([0-9a-f]{64})'",first)
            assert match,'Original credential plan missing'
            salt,hashed=match.groups()
            assert hashlib.pbkdf2_hmac('sha256',b'123456',bytes.fromhex(salt),100000,dklen=32).hex()==hashed,'Original PBKDF2 credential contract'
            inserted_users=int(database(f"SELECT COUNT(*) FROM im_users WHERE user_id BETWEEN {b+1} AND {hi} AND username=CONCAT('{prefix}',LPAD(user_id-{b},6,'0')) AND status=1 AND password_salt='{salt}' AND password_hash='{hashed}'"))
            inserted_relations=int(database(f'SELECT COUNT(*) FROM im_user_relations WHERE user_id BETWEEN {b+1} AND {hi} AND peer_user_id BETWEEN {b+1} AND {hi} AND relation_status=1'))
            exact_ring=int(database(f'SELECT COUNT(*) FROM im_user_relations WHERE user_id BETWEEN {b+1} AND {hi} AND relation_status=1 AND (peer_user_id={b+1}+MOD(user_id-{b},{n}) OR peer_user_id={b+1}+MOD(user_id-{b}+{n}-2,{n}))'))
            total=int(database(f'SELECT COUNT(*) FROM im_users WHERE user_id BETWEEN {b+1} AND {hi}'))
            edges=2*n if n>2 else 2
            save(d/'prior-plan-readonly-verification.json',{'prior_run':a.verify_run,'private_plan_SHA_verified':True,'completed_prior_batches':len(done),'canonical_users':inserted_users,'total_users':total,'mutual_relations':inserted_relations,'exact_ring_edges':exact_ring,'auto_increment_after':database("SELECT AUTO_INCREMENT FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='im_users'"),'SQL_writes':False})
            assert total==inserted_users==n and inserted_relations==exact_ring==edges,'Existing plannedfixture verification mismatch'
            summary={'status':'VERIFIED_PASS','phase':'READONLY_EXISTING_PLAN_VERIFY','prior_run':a.verify_run,'users_verified':inserted_users,'relations_verified':inserted_relations,'SQL_writes':False,'real_authentication':'NOT_RUN','capacity_acceptance':False}
            save(d/'summary.json',summary);print(json.dumps(summary,indent=2));return 0
        existing=int(database(f'SELECT COUNT(*) FROM im_users WHERE user_id BETWEEN {b+1} AND {hi}'))
        collision=int(database(f"SELECT COUNT(*) FROM im_users WHERE LEFT(username,{len(prefix)})='{prefix}'"))
        auto=database("SELECT AUTO_INCREMENT FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='im_users'")
        maximum=database('SELECT COALESCE(MAX(user_id),0) FROM im_users')
        save(d/'user-preflight.json',{'existing_target_users':existing,'existing_prefix_users':collision,'auto_increment_before':auto,'maximum_uid_before':maximum})
        assert existing==0 and collision==0,'Target fixture is not empty; refuse any credential/status/identity overwrite'
        references=database("SELECT TABLE_NAME,COLUMN_NAME FROM information_schema.KEY_COLUMN_USAGE WHERE TABLE_SCHEMA=DATABASE() AND REFERENCED_TABLE_NAME='im_users' AND REFERENCED_COLUMN_NAME='user_id'")
        counts=[]
        for row in references.splitlines():
            table,col=row.split('\t')
            assert re.fullmatch('[a-zA-Z0-9_]+',table) and re.fullmatch('[a-zA-Z0-9_]+',col)
            count=int(database(f'SELECT COUNT(*) FROM `{table}` WHERE `{col}` BETWEEN {b+1} AND {hi}'))
            counts.append({'table':table,'column':col,'rows':count})
        save(d/'referenced-row-preflight.json',counts)
        assert all(x['rows']==0 for x in counts),'Existing references in fresh range; refuse fixture insertion'
        if a.apply:
            private=d/'runtime-private';private.mkdir(mode=0o700)
            salt=os.urandom(16);hashed=hashlib.pbkdf2_hmac('sha256',b'123456',salt,100000,dklen=32).hex()
            batches=[]
            for offset in range(0,n,1000):
                end=min(offset+1000,n)
                values=','.join(f"({b+i},'{prefix}{i:06d}','Codex owned fixture {i}',1,'{salt.hex()}','{hashed}')" for i in range(offset+1,end+1))
                batches.append(('users',offset+1,end,'START TRANSACTION; INSERT INTO im_users(user_id,username,nickname,status,password_salt,password_hash) VALUES '+values+'; COMMIT;'))
            edges=set()
            for i in range(1,n+1):
                u=b+i;v=b+(i%n)+1
                edges.add((u,v));edges.add((v,u))
            edges=sorted(edges)
            for offset in range(0,len(edges),1000):
                chunk=edges[offset:offset+1000]
                values=','.join(f'({u},{v},1)' for u,v in chunk)
                batches.append(('relations',offset,offset+len(chunk),'START TRANSACTION; INSERT INTO im_user_relations(user_id,peer_user_id,relation_status) VALUES '+values+'; COMMIT;'))
            plan=[]
            for i,(kind,lo,high,q) in enumerate(batches):
                filename=f'{i:03d}-{kind}.sql';(private/filename).write_text(q+'\n')
                plan.append({'batch':i,'kind':kind,'low':lo,'high':high,'sql_sha256':hashlib.sha256((q+'\n').encode()).hexdigest(),'private_file':'runtime-private/'+filename})
            save(d/'batch-plan-before.json',plan)
            for i,(kind,lo,high,q) in enumerate(batches):
                # This plan and each exact batch are preserved before mutation.
                database(q);batch_count+=1
                with (d/'completed-batches.jsonl').open('a') as f:f.write(json.dumps({'batch':i,'kind':kind,'committed':True})+'\n')
            inserted_users=int(database(f"SELECT COUNT(*) FROM im_users WHERE user_id BETWEEN {b+1} AND {hi} AND username=CONCAT('{prefix}',LPAD(user_id-{b},6,'0')) AND status=1 AND password_salt='{salt.hex()}' AND password_hash='{hashed}'"))
            inserted_relations=int(database(f'SELECT COUNT(*) FROM im_user_relations WHERE user_id BETWEEN {b+1} AND {hi} AND peer_user_id BETWEEN {b+1} AND {hi} AND relation_status=1'))
            exact_ring=int(database(f'SELECT COUNT(*) FROM im_user_relations WHERE user_id BETWEEN {b+1} AND {hi} AND relation_status=1 AND (peer_user_id={b+1}+MOD(user_id-{b},{n}) OR peer_user_id={b+1}+MOD(user_id-{b}+{n}-2,{n}))'))
            save(d/'postinsert-verification.json',{'canonical_users':inserted_users,'mutual_ring_relations':inserted_relations,'exact_ring_edges':exact_ring,'expected_users':n,'expected_edges':len(edges),'auto_increment_after':database("SELECT AUTO_INCREMENT FROM information_schema.TABLES WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='im_users'")})
            assert inserted_users==n and inserted_relations==len(edges) and exact_ring==len(edges),'Fresh fixture verification mismatch; retain all partial rows'
    except BaseException as e:
        failure={'type':type(e).__name__,'message':str(e),'completed_batches':batch_count}
        save(d/'failure.json',failure)
    summary={'status':'FAIL' if failure else ('SEEDED_PASS' if a.apply else 'PREFLIGHT_PASS'),
             'phase':'APPLY' if a.apply else 'READONLY_DRY_RUN','users_verified':inserted_users,'relations_verified':inserted_relations,
             'completed_batches':batch_count,'failure':failure,'real_authentication':'NOT_RUN; verify in actual load','existing_user_changes':False,
             'capacity_acceptance':False}
    save(d/'summary.json',summary);print(json.dumps(summary,indent=2));return 1 if failure else 0


if __name__=='__main__':
    raise SystemExit(main())
