#!/usr/bin/env python3
"""Real persistent TCP feature chains; explicitly separate from capacity proof.

Only preverified synthetic identities may mutate through normal public APIs.
No SQL writes, service restarts, cache repairs, fixture resets or row deletion.
Every invocation owns a new immutable evidence directory, even on failure.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import select
import socket
import struct
import subprocess
import time
import traceback

ROOT = pathlib.Path('/home/jackson7/projects/TinyIMX_publish')
HEADER = struct.Struct('!IHHHHII')
MAGIC = 0x54494D58


def command(argv, timeout=45):
    return subprocess.check_output(argv, text=True, timeout=timeout)


def sql(q):
    assert q.startswith('SELECT ') and ';' not in q, 'Read-only single SELECT'
    return command(['docker', 'exec', 'tinyimx-m21-mysql-1', 'sh', '-c',
                    'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"',
                    'cross-feature-select', q])


class Actor:
    def __init__(self, run, uid, host, port):
        self.run, self.uid, self.seq = run, uid, 100
        self.socket = socket.create_connection((host, port), 3)
        self.socket.settimeout(3)
        self.buffer, self.replies = bytearray(), {}
        self.hb_sent, self.hb_ack = set(), set()
        self.next_hb = time.monotonic() + 5
        run.clients.append(self)
        reply = run.request(self, 'login', 1001,
                            {'username': f'm21b500000_{uid-500000:06d}',
                             'password': os.environ.get('TINYIMX_BENCH_PASSWORD', '123456')})
        assert reply.get('user_id') == uid, 'Login identity mismatch'

    def send(self, kind, body, seq=None):
        self.seq += 1
        seq = self.seq if seq is None else seq
        raw = json.dumps(body, separators=(',', ':')).encode()
        self.socket.sendall(HEADER.pack(MAGIC, 1, kind, 0, 0, seq, len(raw)) + raw)
        return seq

    def receive(self):
        raw = self.socket.recv(65536)
        assert raw, f'Unexpected disconnect for owned UID {self.uid}'
        self.buffer.extend(raw)
        while len(self.buffer) >= HEADER.size:
            magic, ver, kind, flags, reserved, seq, n = HEADER.unpack(self.buffer[:20])
            assert magic == MAGIC and ver == 1 and flags == 0 and reserved == 0 and n <= 1048576, 'Invalid wire header'
            if len(self.buffer) < 20 + n:
                break
            body = json.loads(self.buffer[20:20+n]) if n else {}
            del self.buffer[:20+n]
            self.run.emit('response', uid=self.uid, kind=kind, seq=seq, body=body)
            if kind == 9001:
                assert seq in self.hb_sent, 'Unexpected heartbeat sequence'
                self.hb_ack.add(seq)
            elif kind in (2019, 2051):
                mid = int(body['message_id'])
                sender = int(body['from'] if kind == 2019 else body['from_user_id'])
                assert 500001 <= sender <= 520000 and mid > 0, 'Unexpected delivery sender identity'
                if kind == 2019:
                    assert int(body['to']) == self.uid, 'Private delivery owner mismatch'
                else:
                    assert int(body['group_id']) > 0, 'Invalid group identity'
                self.run.deliveries.setdefault((kind, mid, self.uid), []).append(body)
                self.send(2020 if kind == 2019 else 2052, {'message_id': mid}, seq)
            else:
                assert (kind, seq) not in self.replies, 'Duplicate operation response'
                self.replies[(kind, seq)] = body

    def close(self):
        if self in self.run.clients:
            self.run.clients.remove(self)
        self.socket.close()


class Run:
    def __init__(self, args, out):
        self.args, self.out = args, out
        self.clients, self.deliveries, self.latencies = [], {}, []
        self.operations, self.assertions = [], []
        self.prefix = 'cf-' + args.run
        self.op_serial = 0

    def emit(self, event, **kw):
        with (self.out / 'timeline.jsonl').open('a') as f:
            f.write(json.dumps({'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                                'mono_ns': time.monotonic_ns(), 'event': event, **kw}) + '\n')

    def pump(self, seconds=.02, heartbeats=True):
        now = time.monotonic()
        if heartbeats:
            for c in self.clients:
                if now >= c.next_hb:
                    c.hb_sent.add(c.send(9001, {}))
                    c.next_hb = now + 5
        ready, _, _ = select.select([c.socket for c in self.clients], [], [], seconds)
        for c in list(self.clients):
            if c.socket in ready:
                c.receive()

    def until(self, predicate, what, seconds=30):
        end = time.monotonic() + seconds
        while not predicate():
            assert time.monotonic() < end, 'Timeout: ' + what
            self.pump()

    def request(self, c, name, kind, body, success=True, reason=None):
        start = time.monotonic_ns()
        seq = c.send(kind, body)
        safe = {k: v for k, v in body.items() if k != 'password'}
        self.emit('request', uid=c.uid, name=name, kind=kind, seq=seq, body=safe)
        self.until(lambda: (kind+1, seq) in c.replies or (9999, seq) in c.replies, name)
        assert (9999, seq) not in c.replies, f'{name}: unexpected server error {c.replies.get((9999, seq))}'
        reply = c.replies.pop((kind+1, seq))
        finished = time.monotonic_ns()
        row = {'name': name, 'uid': c.uid, 'type': kind, 'start_mono_ns': start, 'end_mono_ns': finished, 'ms': (finished-start)/1e6,
               'expected_success': success, 'actual_success': reply.get('success'), 'reason': reply.get('reason')}
        self.operations.append(row)
        assert reply.get('success') is success, f'{name}: {reply}'
        if reason is not None:
            assert reply.get('reason') in reason, f'{name}: wrong negative outcome {reply}'
        return reply

    def op(self):
        self.op_serial += 1
        return self.prefix + '-' + str(self.op_serial)

    def check(self, name, condition):
        self.assertions.append({'name': name, 'pass': bool(condition)})
        assert condition, name

    def group(self, c, name, kind, gid, **kw):
        return self.request(c, name, kind, {'client_operation_id': self.op(), 'group_id': gid, **kw})

    def authoritative(self, name, query, expected, seconds=30):
        end = time.monotonic() + seconds
        i = 0
        while True:
            i += 1
            observed = sql(query).strip()
            (self.out / f'{name}-{i}.tsv').write_text(observed + '\n')
            if observed == expected:
                self.check(name, True)
                return
            assert time.monotonic() < end, f'{name}: authoritative mismatch {observed!r} vs {expected!r}'
            for _ in range(10):
                self.pump(.02)

    def chain(self):
        uids = self.args.users
        a, b, c, d = [Actor(self, u, self.args.host, self.args.port) for u in uids]
        for actor in (a, b, c, d):
            profile = self.request(actor, 'profile', 2021, {})
            self.check('profile-owner-' + str(actor.uid), profile['profile']['user_id'] == actor.uid)
        friend = self.request(a, 'friend-create-accept-pair', 2011, {'to_user_id': b.uid, 'request_message': self.prefix})
        rid = int(friend['request_id'])
        incoming = self.request(b, 'friend-incoming-list', 2013, {'limit': 20})
        self.check('pending-request-listed', any(x['request_id'] == rid and x['from_user_id'] == a.uid for x in incoming['requests']))
        self.request(b, 'friend-accept', 2015, {'request_id': rid})
        self.authoritative('friend-symmetric-accepted', f'SELECT COUNT(*) FROM im_user_relations WHERE ((user_id={a.uid} AND peer_user_id={b.uid}) OR (user_id={b.uid} AND peer_user_id={a.uid})) AND relation_status=1', '2')
        friends = self.request(a, 'friend-list', 2009, {'limit': 100})
        self.check('accepted-friend-visible', any(x['friend_user_id'] == b.uid for x in friends['friends']))
        rejected = self.request(c, 'friend-create-reject-pair', 2011, {'to_user_id': d.uid, 'request_message': self.prefix})
        self.request(d, 'friend-reject', 2017, {'request_id': int(rejected['request_id'])})
        self.authoritative('friend-rejected-no-relation', f'SELECT COUNT(*) FROM im_user_relations WHERE user_id={c.uid} AND peer_user_id={d.uid}', '0')
        text = self.prefix + '-private-content'
        private_body = {'to': b.uid, 'text': text, 'client_message_id': self.prefix + '-pm'}
        sent = self.request(a, 'private-send-after-friend-accept', 2001, private_body)
        mid = int(sent['message_id'])
        self.check('private-positive-durable-identity', sent.get('stored_persistent') is True and sent['from'] == a.uid and sent['to'] == b.uid)
        self.until(lambda: (2019, mid, b.uid) in self.deliveries, 'actual private delivery')
        self.check('private-wire-content', self.deliveries[(2019, mid, b.uid)][0]['text'] == text)
        duplicate = self.request(a, 'private-idempotent-repeat', 2001, private_body)
        self.check('private-repeat-same-M', duplicate['message_id'] == mid)
        self.authoritative('private-receiver-confirmed', f'SELECT delivery_status FROM im_private_messages WHERE message_id={mid} AND from_user_id={a.uid} AND to_user_id={b.uid}', '1')
        history = self.request(b, 'private-history', 2005, {'peer_user_id': a.uid, 'limit': 20})
        historical = [x for x in history['messages'] if x['message_id'] == mid]
        self.check('history-real-message-content', len(historical) == 1 and
                   historical[0]['from'] == a.uid and historical[0]['to'] == b.uid and
                   historical[0]['message_type'] == 1 and historical[0]['delivery_status'] == 1 and
                   json.loads(historical[0]['content']) == {'from': a.uid, 'to': b.uid, 'text': text})
        conversations = self.request(b, 'conversations-before-read', 2007, {'limit': 100})
        self.check('unread-dialog-visible', any(x['peer_user_id'] == a.uid and x['unread_count'] >= 1 for x in conversations['conversations']))
        self.request(b, 'private-mark-read', 2003, {'peer_user_id': a.uid})
        self.authoritative('private-read-state', f'SELECT delivery_status FROM im_private_messages WHERE message_id={mid}', '2')
        conversations = self.request(b, 'conversations-after-read', 2007, {'limit': 100})
        self.check('dialog-unread-cleared', any(x['peer_user_id'] == a.uid and x['unread_count'] == 0 for x in conversations['conversations']))

        create_body = {'client_operation_id': self.op(), 'name': self.prefix, 'join_policy': 'open', 'max_members': 10}
        made = self.request(a, 'group-create', 2023, create_body)
        gid = int(made['group']['group_id'])
        self.emit('owned-group', group_id=gid)
        repeat = self.request(a, 'group-create-idempotence', 2023, create_body)
        self.check('group-repeat-same-G', repeat['group']['group_id'] == gid and repeat['result'] == 'reused')
        self.group(b, 'group-join', 2031, gid)
        self.group(a, 'group-invite-c', 2035, gid, target_user_id=c.uid)
        self.group(a, 'group-invite-d', 2035, gid, target_user_id=d.uid)
        current = self.request(a, 'group-get', 2025, {'group_id': gid})['group']
        self.group(a, 'group-update-versioned', 2027, gid, expected_version=current['version'], description=self.prefix + '-updated')
        first = self.request(a, 'group-members-page-one', 2045, {'group_id': gid, 'limit': 2})
        self.check('group-members-first-has-more', len(first['members']) == 2 and first['has_more'] is True)
        second = self.request(a, 'group-members-page-two', 2045, {'group_id': gid, 'limit': 2, 'after_user_id': max(x['user_id'] for x in first['members'])})
        self.check('group-members-exact-pages', {x['user_id'] for x in first['members']+second['members']} == set(uids))
        mine = self.request(b, 'my-groups', 2047, {'limit': 100})
        self.check('joined-group-visible', any(x['group_id'] == gid for x in mine['groups']))
        self.group(a, 'group-role-admin', 2039, gid, target_user_id=b.uid, role='admin')
        future = datetime.datetime.now(datetime.timezone.utc)+datetime.timedelta(minutes=10)
        self.request(a, 'group-mute-invalid-time-rejected', 2041,
                     {'client_operation_id': self.op(), 'group_id': gid, 'target_user_id': c.uid,
                      'muted_until': future.strftime('%Y-%m-%d %H:%M:%S')},
                     success=False, reason={'invalid_group_request'})
        until = future.strftime('%Y-%m-%dT%H:%M:%S.000Z')
        self.group(a, 'group-mute', 2041, gid, target_user_id=c.uid, muted_until=until)
        self.authoritative('group-mute-authoritative-active', f'SELECT COUNT(*) FROM im_group_members WHERE group_id={gid} AND user_id={c.uid} AND status=1 AND muted_until>UTC_TIMESTAMP(3)', '1')
        self.request(c, 'muted-group-send-denied', 2049, {'group_id': gid, 'client_message_id': self.prefix+'-muted', 'message_type': 1, 'content': text}, success=False, reason={'group_send_permission_denied'})
        self.authoritative('denied-group-send-not-persisted', f"SELECT COUNT(*) FROM im_group_messages WHERE group_id={gid} AND client_message_id='{self.prefix}-muted'", '0')
        self.group(a, 'group-unmute', 2041, gid, target_user_id=c.uid, muted_until='')
        group_body = {'group_id': gid, 'client_message_id': self.prefix+'-gm', 'message_type': 1, 'content': self.prefix+'-group-content'}
        gs = self.request(a, 'group-send-with-fanout', 2049, group_body)
        gm = int(gs['message_id'])
        self.until(lambda: all((2051, gm, u) in self.deliveries for u in uids[1:]), 'three actual group deliveries')
        self.check('group-wire-identity-content', all(self.deliveries[(2051, gm, u)][0]['group_id'] == gid and self.deliveries[(2051, gm, u)][0]['from_user_id'] == a.uid and self.deliveries[(2051, gm, u)][0]['content'] == group_body['content'] for u in uids[1:]))
        self.authoritative('group-all-real-receiver-ACKs', f'SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id={gm} AND group_id={gid} AND delivery_status=3 AND recipient_user_id IN({",".join(map(str,uids[1:]))})', '3')
        again = self.request(a, 'group-send-idempotence', 2049, group_body)
        self.check('group-message-repeat-same-M', again['message_id'] == gm)
        self.group(d, 'group-leave', 2033, gid)
        self.group(a, 'group-invite-returned-d', 2035, gid, target_user_id=d.uid)
        self.group(a, 'group-kick-d', 2037, gid, target_user_id=d.uid)
        self.group(a, 'group-transfer-owner', 2043, gid, target_user_id=b.uid)
        current = self.request(b, 'group-get-new-owner', 2025, {'group_id': gid})['group']
        self.check('transferred-owner', current['owner_user_id'] == b.uid)
        self.group(b, 'group-disband-owned-test-group', 2029, gid, expected_version=current['version'])

        payload = b'cross-feature-canceled-upload-fixture'
        fb = {'client_upload_id': self.prefix+'-file', 'file_name': self.prefix+'.txt', 'total_size': len(payload),
              'checksum_algorithm': 'sha256', 'expected_checksum': hashlib.sha256(payload).hexdigest()}
        begun = self.request(a, 'file-begin', 2053, fb)
        upload = int(begun['session']['upload_id'])
        self.check('file-session-owner', begun['session']['owner_user_id'] == a.uid and begun['file']['owner_user_id'] == a.uid)
        again = self.request(a, 'file-begin-idempotence', 2053, fb)
        self.check('file-repeat-same-upload', again['session']['upload_id'] == upload and again['result'] == 'reused')
        self.request(a, 'file-get-upload-session', 2055, {'upload_id': upload})
        self.request(b, 'file-cross-owner-denied', 2055, {'upload_id': upload}, success=False, reason={'file_permission_denied'})
        cancelled = self.request(a, 'file-cancel-owned-test-upload', 2057, {'upload_id': upload})
        self.check('file-canceled-state', cancelled['session']['status'] == 'canceled' and cancelled['file']['status'] == 'canceled')
        self.request(a, 'file-cancel-idempotence', 2057, {'upload_id': upload})

        file_stage = self.out / 'file-stream'
        self.emit('real-file-stream-start', actor=a.uid, test='Upload, finalize, partial download, resume without restart; no fault claim')
        service = json.loads(command(['docker', 'inspect', 'tinyimx-m21-file-service-1']))[0]
        ip = next(iter(service['NetworkSettings']['Networks'].values()))['IPAddress']
        binary = ROOT / 'build/linux-release/file_transfer_release_e2e_client'
        self.check('existing-file-stream-binary-present', binary.is_file())
        self.emit('file-stream-binary', sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
        # Parent test sockets continue processing heartbeat and delivery ACKs.
        for phase in ('prepare', 'resume'):
            with (self.out / ('file-stream-'+phase+'.log')).open('w') as f:
                p = subprocess.Popen([str(binary), phase, ip+':50055', str(file_stage), str(a.uid)], stdout=f, stderr=subprocess.STDOUT)
                end = time.monotonic()+120
                while p.poll() is None and time.monotonic()<end:
                    self.pump(.02)
                if p.poll() is None:
                    p.terminate()
                    try: p.wait(5)
                    except subprocess.TimeoutExpired: p.kill(); p.wait(5)
                    raise TimeoutError('Owned file client timeout, all evidence retained')
                self.check('file-stream-'+phase+'-exit-zero', p.returncode == 0)
        self.check('file-download-byte-exact', (file_stage/'expected.bin').read_bytes() == (file_stage/'download.bin').read_bytes())
        self.emit('group-delivery-routes', rows=sql(f'SELECT recipient_user_id,last_gateway_id,delivery_status FROM im_group_message_deliveries WHERE message_id={gm}'))
        # Quiesce all heartbeat producers, drain each outstanding response exactly.
        deadline = time.monotonic()+10
        while any(x.hb_sent != x.hb_ack for x in self.clients):
            assert time.monotonic()<deadline, 'Heartbeat drain timeout'
            self.pump(.02, heartbeats=False)
        self.check('all-actor-heartbeats-acknowledged', all(x.hb_sent == x.hb_ack for x in self.clients))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', required=True)
    parser.add_argument('--users', type=int, nargs=4, default=[519800,519802,519804,519806])
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=9000)
    parser.add_argument('--background-run')
    args = parser.parse_args()
    assert re.fullmatch('[a-zA-Z0-9-]{1,20}', args.run)
    assert len(set(args.users)) == 4 and all(519800<=u<=519950 for u in args.users), 'Owned reserved actors only'
    assert args.background_run is None or re.fullmatch('[a-zA-Z0-9-]{1,20}', args.background_run)
    os.chdir(ROOT)
    os.umask(0o077)
    out = ROOT/'.local/codex'/('cross-feature-'+args.run)
    assert not out.exists(), 'Never overwrite a prior run'
    out.mkdir()
    run = Run(args, out)
    failure = None
    try:
        ids = ','.join(map(str,args.users))
        rows = sql(f'SELECT user_id,username,status FROM im_users WHERE user_id IN({ids})')
        expected = {f'{u}\tm21b500000_{u-500000:06d}\t1' for u in args.users}
        assert set(rows.splitlines()) == expected, 'Fixture identity must match exactly'
        pairs = [(args.users[0],args.users[1]),(args.users[2],args.users[3])]
        predicate = ' OR '.join(f'(user_id={u} AND peer_user_id={v}) OR (user_id={v} AND peer_user_id={u})' for u,v in pairs)
        relations = sql('SELECT user_id,peer_user_id,relation_status FROM im_user_relations WHERE '+predicate)
        requests = sql('SELECT request_id,from_user_id,to_user_id,request_status FROM im_friend_requests WHERE '+' OR '.join(f'(from_user_id={u} AND to_user_id={v}) OR (from_user_id={v} AND to_user_id={u})' for u,v in pairs))
        assert not relations.strip() and not requests.strip(), 'Refuse modify existing social fixtures; choose unused non-adjacent pair'
        (out/'fixture-before.tsv').write_text(rows)
        audit = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'source_commit': command(['git','rev-parse','HEAD']).strip(),
                 'script_sha256': hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(), 'args': vars(args),
                 'scope': 'Four verified synthetic actors only, distinct from background load range',
                 'writes': ['New evidence files', 'Normal public friend/private/read/group/file operations', 'Owned group disband and own upload cancellation as explicit normal feature tests', 'Existing real file RPC test uploads new owned object and downloads it'],
                 'system_changes': [], 'SQL_writes': False, 'existing_credential_or_relation_resets': False,
                 'validation': 'Exact business responses, wire identities, durable states, bytes/checksum; all partial failures preserved',
                 'rollback': 'Close own actor sockets only; keep test history/files, never arbitrary row/file delete',
                 'coverage': 'Functional chain sample; no capacity P99 proof, TLS/MCP/offline/fault/soak not yet covered'}
        (out/'audit-before.json').write_text(json.dumps(audit,indent=2)+'\n')
        if args.background_run:
            bg = ROOT/'.local/codex'/('capacity-'+args.background_run)
            identities = json.loads((bg/'audit-before.json').read_text())
            start = int((bg/'control/start_ns').read_text())
            end = start + int(identities['scenario']['duration'])*1_000_000_000
            assert start <= time.monotonic_ns() < end and not (bg/'control/release').exists() and not (bg/'control/abort').exists(), 'Background steady workload must be active'
            (out/'background-window.json').write_text(json.dumps({'run':args.background_run,'users':identities['scenario']['users'],'source_commit':identities['source_commit'],'start_mono_ns':start,'end_mono_ns':end},indent=2)+'\n')
        run.chain()
        if args.background_run:
            run.check('every-operation-inside-background-steady-window', all(start <= row['start_mono_ns'] <= row['end_mono_ns'] < end for row in run.operations))
    except BaseException as e:
        failure = {'type': type(e).__name__, 'message': str(e), 'traceback': traceback.format_exc()}
        (out/'failure.json').write_text(json.dumps(failure,indent=2)+'\n')
    finally:
        hbs = [{'uid': c.uid, 'sent': len(c.hb_sent), 'ack': len(c.hb_ack)} for c in run.clients]
        for c in list(run.clients): c.close()
        (out/'operations.json').write_text(json.dumps(run.operations,indent=2)+'\n')
        by_name = {}
        for row in run.operations: by_name.setdefault(row['name'],[]).append(row['ms'])
        summary = {'status': 'FAIL' if failure else 'PASS', 'run': args.run, 'operations_completed': len(run.operations),
                   'public_request_types_observed': sorted(set(row['type'] for row in run.operations)),
                   'assertions': run.assertions, 'heartbeats': hbs,
                   'latency_samples_ms': by_name, 'failure': failure,
                   'coverage': 'Real functional chains only; each operation sample count explicit, not capacity P99 proof; TLS/MCP/offline/fault/soak NOT_RUN'}
        (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
        print(json.dumps(summary,indent=2))
    return 1 if failure else 0


if __name__ == '__main__':
    raise SystemExit(main())
