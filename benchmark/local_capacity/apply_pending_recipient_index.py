#!/usr/bin/env python3
"""Audited single-index experiment; no resets, row edits or service changes."""
import argparse
import datetime
import hashlib
import json
import pathlib
import random
import re
import subprocess
import time
import traceback

ROOT = pathlib.Path(__file__).resolve().parents[2]
NAME = 'idx_im_private_messages_pending_recipient'
OLD = 'idx_im_private_messages_to_status_id'
EXPECTED = {
    'PRIMARY': [('message_id', '0', 'YES')],
    'uk_im_private_messages_client_msg': [('from_user_id', '0', 'YES'), ('client_message_id', '0', 'YES')],
    'idx_im_private_messages_to_status_id': [('to_user_id', '1', 'YES'), ('delivery_status', '1', 'YES'), ('message_id', '1', 'YES')],
    'idx_im_private_messages_from_id': [('from_user_id', '1', 'YES'), ('message_id', '1', 'YES')],
    'idx_im_private_messages_dialog_id': [('from_user_id', '1', 'YES'), ('to_user_id', '1', 'YES'), ('message_id', '1', 'YES')],
}


def sql(q, timeout=60):
    return subprocess.check_output(['docker', 'exec', 'tinyimx-m21-mysql-1',
        'sh', '-c', 'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw '
        '--skip-column-names "$MYSQL_DATABASE" -e "$1"', 'owned-pending-index', q], text=True, timeout=timeout)


def definitions():
    found = {}
    for line in sql("SELECT INDEX_NAME,COLUMN_NAME,NON_UNIQUE,IS_VISIBLE FROM information_schema.statistics "
                    "WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='im_private_messages' "
                    "ORDER BY INDEX_NAME,SEQ_IN_INDEX").splitlines():
        name, column, nonunique, visible = line.split('\t')
        found.setdefault(name, []).append((column, nonunique, visible))
    return found


def query(cursor, hint=''):
    return f'SELECT DISTINCT to_user_id FROM im_private_messages {hint} WHERE to_user_id>{cursor} AND delivery_status=0 ORDER BY to_user_id ASC LIMIT 257'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', required=True)
    args = parser.parse_args()
    assert re.fullmatch(r'[a-z0-9-]{1,40}', args.run)
    d = ROOT / '.local/codex' / ('pending-recipient-only-' + args.run)
    d.mkdir()
    def save(n, x): (d/n).write_text(json.dumps(x, indent=2)+'\n')
    save('preflight-audit.json', {'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'operation':'Readonly strict single-index/resource/load preflight', 'writes':'Fresh evidence only'})
    before = definitions()
    save('indexes-before.json', before)
    assert before == EXPECTED, 'Unexpected original index set; refuse any DDL'
    for p in pathlib.Path('/proc').iterdir():
        if not p.name.isdigit(): continue
        try: assert p.joinpath('exe').resolve().name != 'tinyimx_capacity_worker', 'Own load active'
        except (FileNotFoundError, PermissionError): pass
    names = subprocess.check_output(['docker','ps','--format','{{.Names}}'],text=True).splitlines()
    assert len(names)==19 and all(n.startswith('tinyimx-m21-') for n in names), 'Isolated inference/MCP or unexpected runtime active'
    cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
    identity=lambda c:{'id':c['Id'],'image':c['Image'],'started':c['State']['StartedAt']}
    ids={c['Name']:identity(c) for c in cs}
    paths=list(pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config').glob('*.json'))
    hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    mem=pathlib.Path('/proc/meminfo').read_text();(d/'memory-before.txt').write_text(mem)
    assert int(re.search(r'MemAvailable:\s+(\d+)',mem).group(1))>=1024*1024
    assert __import__('os').statvfs(ROOT).f_bavail*__import__('os').statvfs(ROOT).f_frsize>=1024**3
    deadline=time.monotonic()+10
    while int(sql('SELECT COUNT(*) FROM information_schema.innodb_trx'))!=0:
        assert time.monotonic()<deadline,'Transactions still active; no DDL';time.sleep(.5)
    migration=ROOT/'db/migrations/013_add_pending_recipient_only_index.sql'
    ddl=migration.read_text();rollback=f'SET SESSION lock_wait_timeout=5; ALTER TABLE im_private_messages DROP INDEX {NAME}, ALGORITHM=INPLACE, LOCK=NONE;\n'
    cursors=[0,500000,519800,700000,750000]
    for cur in cursors: (d/f'plan-before-{cur}.json').write_text(sql('EXPLAIN FORMAT=JSON '+query(cur)))
    save('audit-before.json', {'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'operation':'Add only one nonunique status-leading pending-recipient index',
        'source_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        'migration_sha256':hashlib.sha256(migration.read_bytes()).hexdigest(),'before_indexes':before,
        'added_index':{NAME:[('delivery_status','1','YES'),('to_user_id','1','YES')]},
        'before_instances':ids,'private_config_sha256':hashes,
        'impact':'Online index build CPU/cache/disk outside owned load; session metadata lock bounded5s, no fallback locking ALTER',
        'scope':'No rows, old indexes, unread-dialog index, durability/isolation/global limit/service/other application changes',
        'validation':'Exact full index set, five forced old/new query parity cases in one readonly consistent snapshot; chosen plans and seeded randomized SQL timings; later identical offered capacity controls',
        'rollback':rollback,'limits':'Prior paired index candidate failed; this one remains experimental; query timing alone cannot establish business improvement'})
    (d/'ddl.sql').write_text(ddl);(d/'rollback.sql').write_text(rollback)
    try:
        (d/'ddl-output.log').write_text(sql('SET SESSION lock_wait_timeout=5;\n'+ddl,180))
        expected={**EXPECTED,NAME:[('delivery_status','1','YES'),('to_user_id','1','YES')]}
        after=definitions();save('indexes-after.json',after);assert after==expected
        queries=['START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY']
        for cur in cursors:
            queries += [f"SELECT 'CASE_{cur}_OLD'",query(cur,f'FORCE INDEX ({OLD})'),
                        f"SELECT 'CASE_{cur}_NEW'",query(cur,f'FORCE INDEX ({NAME})')]
        queries += ['COMMIT']
        parity=sql('; '.join(queries));(d/'forced-parity.tsv').write_text(parity)
        for cur in cursors:
            old=parity.split(f'CASE_{cur}_OLD\n')[1].split(f'CASE_{cur}_NEW\n')[0]
            new=parity.split(f'CASE_{cur}_NEW\n')[1].split('CASE_')[0]
            assert old==new,'Query parity mismatch'
            (d/f'plan-after-{cur}.json').write_text(sql('EXPLAIN FORMAT=JSON '+query(cur)))
        order=[(i,idx) for i in range(10) for idx in (OLD,NAME)];random.Random(20261004).shuffle(order)
        statements=[]
        for i,idx in order:
            marker=f'TIME_{i}_{idx}'
            statements += ['SET @codex_query_started=NOW(6)',query(0,f'FORCE INDEX ({idx})'),
                f"SELECT '{marker}',TIMESTAMPDIFF(MICROSECOND,@codex_query_started,NOW(6))"]
        raw=sql('; '.join(statements));(d/'randomized-query-times.tsv').write_text(raw)
        times={idx:[] for idx in (OLD,NAME)}
        for line in raw.splitlines():
            if line.startswith('TIME_'):
                marker,us=line.split('\t');idx=marker.split('_',2)[2];times[idx].append(int(us)/1000)
        assert all(len(v)==10 for v in times.values())
        after_cs=json.loads(subprocess.check_output(['docker','inspect',*names],text=True))
        assert all(ids[c['Name']]==identity(c) for c in after_cs)
        assert all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h for p,h in hashes.items())
        save('summary.json',{'status':'ONE_INDEX_DEFINITION_PARITY_PASS','query_ms':times,
            'all19_instances_preserved':True,'private_configs_preserved':True,'capacity':'NOT_RUN',
            'limits':'Warm randomized query timings, not production percentiles or causal business proof'})
        print((d/'summary.json').read_text())
    except BaseException as e:
        save('failed.json',{'status':'FAIL','type':type(e).__name__,'message':str(e),
            'traceback':traceback.format_exc(),'action':'Preserve every file and exact current indexes; separate audited rollback if required'})
        raise


if __name__=='__main__': main()
