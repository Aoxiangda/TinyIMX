#!/usr/bin/env python3
"""Explicit audited MySQL secondary-index candidate; keep all evidence and rows."""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
NAMES = {
    'idx_im_private_messages_unread_dialog': ['to_user_id', 'from_user_id', 'delivery_status'],
    'idx_im_private_messages_pending_recipient': ['delivery_status', 'to_user_id'],
}


def sql(query):
    return subprocess.check_output([
        'docker', 'exec', 'tinyimx-m21-mysql-1', 'sh', '-c',
        'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw '
        '--skip-column-names "$MYSQL_DATABASE" -e "$1"', 'audited-index', query,
    ], text=True, timeout=180)


def definitions():
    rows = sql("SELECT INDEX_NAME,COLUMN_NAME,NON_UNIQUE,IS_VISIBLE FROM "
               "information_schema.statistics WHERE TABLE_SCHEMA=DATABASE() "
               "AND TABLE_NAME='im_private_messages' ORDER BY INDEX_NAME,SEQ_IN_INDEX")
    found = {}
    for line in rows.splitlines():
        name, column, nonunique, visible = line.split('\t')
        found.setdefault(name, []).append((column, nonunique, visible))
    return found


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', required=True)
    args = parser.parse_args()
    assert re.fullmatch(r'[a-z0-9-]{1,40}', args.run)
    stage = ROOT / '.local/codex' / ('private-index-' + args.run)
    stage.mkdir()  # Never overwrite another run.
    migration = ROOT / 'db/migrations/012_add_private_query_indexes.sql'
    ddl = migration.read_text()
    for proc in pathlib.Path('/proc').iterdir():
        if proc.name.isdigit():
            try:
                assert proc.joinpath('exe').resolve().name != 'tinyimx_capacity_worker', 'Active load worker'
            except (FileNotFoundError, PermissionError):
                pass
    before = definitions()
    for name, cols in NAMES.items():
        if name in before:
            assert before[name] == [(c, '1', 'YES') for c in cols], 'Conflicting existing index'
    assert not any(name in before for name in NAMES), 'Candidate already applied; use fresh evidence'
    assert int(sql('SELECT COUNT(*) FROM information_schema.innodb_trx')) == 0
    audit = {
        'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'operation': 'Add two explicit nonunique private query indexes',
        'git_head': subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        'migration_sha256': hashlib.sha256(migration.read_bytes()).hexdigest(),
        'before_indexes': before, 'added_indexes': NAMES,
        'scope': 'Only named secondary indexes; no row writes, deletes, service restart, password or durability change',
        'impact': 'Online build disk/CPU/cache cost; brief metadata locks bounded by session lock_wait_timeout=5',
        'validation': 'Exact definitions, EXPLAIN, forced old/new result parity in consistent read-only snapshot, strict load repeat',
        'rollback': 'Outside load drop only the two exact new indexes using saved rollback.sql; preserve all evidence',
    }
    (stage / 'audit-before.json').write_text(json.dumps(audit, indent=2) + '\n')
    (stage / 'ddl.sql').write_text(ddl)
    (stage / 'rollback.sql').write_text(
        'SET SESSION lock_wait_timeout=5;\nALTER TABLE im_private_messages\n' +
        ',\n'.join('DROP INDEX ' + name for name in NAMES) + ', ALGORITHM=INPLACE, LOCK=NONE;\n')
    pending = 'SELECT DISTINCT to_user_id FROM im_private_messages {hint} WHERE to_user_id>0 AND delivery_status=0 ORDER BY to_user_id ASC LIMIT 257'
    unread = 'SELECT COUNT(*) FROM im_private_messages {hint} WHERE to_user_id=500002 AND from_user_id=500001 AND delivery_status IN(0,1)'
    queries = [(pending, 'idx_im_private_messages_to_status_id', 'idx_im_private_messages_pending_recipient'),
               (unread, 'idx_im_private_messages_to_status_id', 'idx_im_private_messages_unread_dialog')]
    (stage / 'plans-before.tsv').write_text(''.join(sql('EXPLAIN ' + q.format(hint='')) for q, _, _ in queries))
    try:
        (stage / 'ddl-output.log').write_text(sql('SET SESSION lock_wait_timeout=5;\n' + ddl))
        after = definitions()
        for name, cols in NAMES.items():
            assert after[name] == [(c, '1', 'YES') for c in cols]
        (stage / 'plans-after.tsv').write_text(''.join(sql('EXPLAIN ' + q.format(hint='')) for q, _, _ in queries))
        for i, (query, old, new) in enumerate(queries):
            text = sql('START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY; ' +
                       query.format(hint='FORCE INDEX (' + old + ')') + '; SELECT \'PARITY_SPLIT\'; ' +
                       query.format(hint='FORCE INDEX (' + new + ')') + '; COMMIT;')
            (stage / f'parity-{i}.tsv').write_text(text)
            left, right = text.split('PARITY_SPLIT\n')
            assert left == right, 'Result parity mismatch'
        (stage / 'after-indexes.json').write_text(json.dumps(after, indent=2) + '\n')
        (stage / 'summary.json').write_text(json.dumps({'status': 'INDEX_DEFINITION_AND_PARITY_PASS', 'capacity': 'NOT_RUN'}) + '\n')
        print('INDEX_DEFINITION_AND_PARITY_PASS', flush=True)
    except BaseException as error:
        (stage / 'failed.json').write_text(json.dumps({'status': 'FAIL', 'error_type': type(error).__name__, 'action': 'Preserve indexes and evidence for explicit rollback audit; no data mutation'}) + '\n')
        raise


if __name__ == '__main__':
    main()
