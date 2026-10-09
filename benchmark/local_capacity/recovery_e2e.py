#!/usr/bin/env python3
"""Audited live recovery experiment; two fixture messages, not capacity proof.

Run in the guest checkout. Uses its existing private production config to obtain
credentials without exporting them. Creates only owned loopback services and a
labeled Redis. Keeps test rows and a stopped Redis container for review.
"""
import argparse
import datetime
import hashlib
import json
import os
import pathlib
import re
import signal
import socket
import struct
import subprocess
import threading
import time
import uuid

HEADER = struct.Struct('!IHHHHII')
MAGIC = 0x54494D58
USERS = [(519998, 'm21b500000_019998'), (519999, 'm21b500000_019999')]


def command(args, timeout=30):
    r = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    if r.returncode:
        # Never dump a command line or private config when reporting failure.
        raise RuntimeError(f'{args[0]} exited {r.returncode}: {r.stderr[:300]}')
    return r.stdout.strip()


def mysql(sql):
    assert sql.startswith('SELECT '), 'Live experiment allows read-only SQL only'
    assert ';' not in sql, 'One SELECT per invocation'
    return command(['docker', 'exec', 'tinyimx-m21-mysql-1', 'sh', '-c',
                    'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"',
                    'codex-select', sql])


def free_port(host='127.0.0.1'):
    with socket.socket() as s:
        s.bind((host, 0))
        return s.getsockname()[1]


def wait_port(host, port, process=None, seconds=15):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process and process.poll() is not None:
            raise RuntimeError(f'Owned process exited early ({process.returncode}); inspect its log')
        try:
            with socket.create_connection((host, port), .2):
                return
        except OSError:
            time.sleep(.1)
    raise TimeoutError(f'Owned listener not ready on port {port}')


class Relay:
    """Bounded test TCP gate, preserves bytes and never synthesizes responses."""
    def __init__(self, host, port, target):
        self.target, self.gate, self.closed = target, threading.Event(), threading.Event()
        self.connections, self.lock = set(), threading.Lock()
        self.listener = socket.socket()
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind((host, port))
        self.listener.listen(8)
        self.listener.settimeout(.2)
        threading.Thread(target=self.accept, daemon=True).start()

    def accept(self):
        while not self.closed.is_set():
            try:
                s, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            with self.lock:
                if len(self.connections) >= 8:
                    s.close()
                    continue
                self.connections.add(s)
            threading.Thread(target=self.bridge, args=(s,), daemon=True).start()

    def bridge(self, client):
        server = None
        try:
            while not self.gate.wait(.1):
                if self.closed.is_set():
                    return
                with self.lock:
                    if client not in self.connections:
                        return
            server = socket.create_connection(self.target, 2)
            with self.lock:
                self.connections.add(server)

            def pump(src, dst):
                try:
                    while not self.closed.is_set():
                        b = src.recv(16384)
                        if not b:
                            break
                        dst.sendall(b)
                except OSError:
                    pass
                finally:
                    try:
                        dst.shutdown(socket.SHUT_WR)
                    except OSError:
                        pass
            t = threading.Thread(target=pump, args=(client, server), daemon=True)
            t.start()
            pump(server, client)
            t.join(2)
        except OSError:
            pass
        finally:
            for s in [client, server]:
                if s:
                    with self.lock:
                        self.connections.discard(s)
                    s.close()

    def block(self):
        self.gate.clear()
        with self.lock:
            sockets = list(self.connections)
            self.connections.clear()
        for s in sockets:
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            s.close()

    def close(self):
        self.closed.set()
        self.block()
        self.listener.close()


class Client:
    def __init__(self, port, uid, username, password, emit):
        self.uid, self.emit, self.seq = uid, emit, 100
        self.s = socket.create_connection(('127.0.0.1', port), 3)
        self.s.settimeout(.3)
        self.buffer, self.packets = bytearray(), []
        self.send(1001, {'username': username, 'password': password})
        _, _, body = self.wait(1002, seconds=10)
        assert body.get('success') is True and int(body['user_id']) == uid, \
            f'Fixture login failed: {body.get("reason", "identity mismatch")}'
        self.emit('login', user_id=uid, success=True)

    def send(self, kind, body, seq=None):
        self.seq += 1
        seq = self.seq if seq is None else seq
        b = json.dumps(body, separators=(',', ':'), ensure_ascii=False).encode()
        self.s.sendall(HEADER.pack(MAGIC, 1, kind, 0, 0, seq, len(b)) + b)
        return seq

    def poll(self):
        try:
            b = self.s.recv(65536)
            if not b:
                raise ConnectionError('Fixture client disconnected unexpectedly')
            self.buffer.extend(b)
        except socket.timeout:
            return
        while len(self.buffer) >= 20:
            magic, version, kind, flags, reserved, seq, n = HEADER.unpack(self.buffer[:20])
            assert magic == MAGIC and version == 1 and n <= 1048576, 'Invalid protocol frame'
            if len(self.buffer) < 20 + n:
                break
            body = json.loads(self.buffer[20:20+n]) if n else {}
            del self.buffer[:20+n]
            self.packets.append((kind, seq, body))

    def wait(self, kind, predicate=lambda p: True, seconds=30):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            for i, p in enumerate(self.packets):
                if p[0] == 9999:
                    raise RuntimeError(f'Fixture server error: {p[2].get("reason", "unknown")}')
                if p[0] == kind and predicate(p):
                    return self.packets.pop(i)
            self.poll()
        raise TimeoutError(f'Expected frame {kind} was not received')

    def close(self):
        self.s.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    root = pathlib.Path.cwd().resolve()
    output = pathlib.Path(args.output).resolve()
    assert root / '.local/codex' in output.parents and not output.exists(), 'Use a fresh evidence directory'
    output.mkdir(parents=True)
    private = output / 'runtime-private'
    private.mkdir(mode=0o700)
    run = 'rxe-' + uuid.uuid4().hex[:12]
    redis_name = 'codex-tinyimx-' + run
    processes, clients, relays, logs = [], [], [], []
    redis_created = False
    state = {'status': 'NOT_FINISHED', 'run_id': run, 'source_commit': command(['git', 'rev-parse', 'HEAD']),
             'capacity_proof': False, 'scenarios': [], 'historical_pending_modified': False}

    def save(name, value):
        (output / name).write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')

    def emit(event, **fields):
        row = {'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'event': event, **fields}
        with (output / 'timeline.jsonl').open('a') as f:
            f.write(json.dumps(row, ensure_ascii=False) + '\n')

    def inspect_production():
        names = [json.loads(line) for line in command(['docker', 'ps', '--format', '{{json .Names}}']).splitlines()]
        return [
            command(['docker', 'inspect', '--format', '{{.Name}} {{.Id}} {{.Image}} {{.State.StartedAt}}', n])
            for n in sorted(names) if n.startswith('tinyimx-m21-')]

    def start(name, config, env, target=None):
        config = json.loads(json.dumps(config))
        config['logger']['file'] = str(output / (name + '-application.log'))
        path = private / (name + '.json')
        path.write_text(json.dumps(config))
        path.chmod(0o600)
        log = (output / (name + '.log')).open('ab')
        logs.append(log)
        binary = root / 'build/linux-debug' / ('message_service_demo' if name.startswith('message') else 'gateway_demo')
        cmd = [str(binary), str(path)] + ([target] if target else [])
        process = subprocess.Popen(cmd, env={**os.environ, **env}, stdout=log, stderr=subprocess.STDOUT)
        processes.append(process)
        emit('owned_process_started', name=name, pid=process.pid, binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
             config_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        return process

    def stop(process, hard=False):
        if process.poll() is None:
            process.send_signal(signal.SIGKILL if hard else signal.SIGTERM)
            try:
                process.wait(12)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(5)
            emit('owned_process_stopped', pid=process.pid, exit_code=process.returncode, hard=hard)

    try:
        before = inspect_production()
        save('production-before.json', before)
        config_root = pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config')
        base = json.loads((config_root / 'gateway-a.json').read_text())
        msg = json.loads((config_root / 'message.json').read_text())
        ips = {name: command(['docker', 'inspect', '--format', '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}',
                             'tinyimx-m21-' + name + '-1']) for name in ['mysql', 'user-service', 'social-service', 'group-service', 'file-service']}
        targets = {}
        for name in ['user-service', 'social-service', 'group-service', 'file-service']:
            cmd = json.loads(command(['docker', 'inspect', '--format', '{{json .Config.Cmd}}', 'tinyimx-m21-' + name + '-1']))
            ports_found = [int(m.group(1)) for arg in cmd if (m := re.fullmatch(r'[0-9.]+:([0-9]{1,5})', arg))]
            assert len(ports_found) == 1 and 0 < ports_found[0] <= 65535, f'Cannot determine actual {name} listen port'
            targets[name] = (ips[name], ports_found[0])
            wait_port(*targets[name], seconds=3)
        redis_image = command(['docker', 'inspect', '--format', '{{.Image}}', 'tinyimx-m21-redis-1'])
        ports = {name: free_port() for name in ['redis', 'source', 'receiver', 'message', 'message-gate']}
        assert len(set(ports.values())) == len(ports), 'Port collision; retry with a fresh experiment'
        save('audit-before.json', {'timestamp_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'operation': 'Isolated live durable recovery', 'source_commit': state['source_commit'], 'ports': ports, 'existing_rpc_targets': targets,
            'owned_redis': redis_name, 'redis_image': redis_image, 'redis_memory_limit_mib': 64,
            'planned_processes': ['source gateway (and restart)', 'receiver gateway', 'MessageService (and restart)'],
            'faults': ['SIGKILL owned source', 'SIGTERM/restart owned MessageService', 'hold only test peer/RPC streams'],
            'database_writes': 'Exactly two normal Chat messages and their application ACKs, fixture IDs 519998/519999; no direct SQL writes',
            'reads': 'Existing private configs and fixture state; credentials never exported',
            'forbidden_actions': ['stop existing containers', 'alter existing configs', 'historical Pending repair or deletion', 'cache/swap clearing'],
            'rollback': 'Close test sockets, stop owned child processes, verify label and stop owned Redis only; retain rows/evidence/stopped container'})
        pending = mysql('SELECT COUNT(*) FROM im_private_messages WHERE to_user_id IN (519998,519999) AND delivery_status=0')
        assert pending == '0', 'Fixture has Pending data; do not replay unrelated rows'
        # Confirm fixtures and symmetric permission without creating/modifying accounts.
        assert mysql('SELECT COUNT(*) FROM im_users WHERE user_id IN (519998,519999)') == '2'
        assert mysql('SELECT COUNT(*) FROM im_user_relations WHERE (user_id=519998 AND peer_user_id=519999 OR user_id=519999 AND peer_user_id=519998) AND relation_status=1') == '2'
        command(['docker', 'run', '-d', '--name', redis_name, '--label', 'codex.task=tinyimx-durable-recovery',
                 '--memory', '64m', '--memory-swap', '64m', '--cpus', '.25', '--read-only', '--tmpfs', '/data:rw,size=8m',
                 '-p', f'127.0.0.1:{ports["redis"]}:6379', redis_image, 'redis-server', '--save', '', '--appendonly', 'no'])
        redis_created = True
        wait_port('127.0.0.1', ports['redis'])
        env = {'TINYIMX_USER_RPC_TARGET': '%s:%d' % targets['user-service'],
               'TINYIMX_SOCIAL_RPC_TARGET': '%s:%d' % targets['social-service'],
               'TINYIMX_GROUP_RPC_TARGET': '%s:%d' % targets['group-service'],
               'TINYIMX_FILE_RPC_TARGET': '%s:%d' % targets['file-service'],
               'TINYIMX_DURABLE_PRIVATE_RECOVERY_ENABLE': '1'}
        for c in [base, msg]:
            c['service_discovery']['provider'] = 'static'
            for section in ['zookeeper', 'observability', 'mcp', 'rocketmq', 'outbox_relay', 'unread_projection']:
                c.setdefault(section, {})['enable'] = False
            c['mysql']['host'] = ips['mysql']
            c['logger'].update(console=True, file=str(output / 'application.log'), max_file_size_mb=8, max_backup_files=2)
        msg['app']['instance_id'] = run + '-message'
        msg['gateway_registry']['enable'] = False
        message_env = dict(env)
        message = start('message-1', msg, message_env, f'127.0.0.1:{ports["message"]}')
        wait_port('127.0.0.1', ports['message'], message)
        peer_gate = Relay('127.0.0.2', ports['receiver'], ('127.0.0.1', ports['receiver']))
        message_gate = Relay('127.0.0.1', ports['message-gate'], ('127.0.0.1', ports['message']))
        relays.extend([peer_gate, message_gate])
        gateway_config = {}
        for role in ['source', 'receiver']:
            c = json.loads(json.dumps(base))
            c['app']['instance_id'] = run + '-' + role
            c['server'].update(host='127.0.0.1', port=ports[role], io_thread_count=2)
            c['mysql']['enable'] = False
            c['redis'].update(enable=True, host='127.0.0.1', port=ports['redis'], password='', db=0, pool_size=4)
            c['gateway_registry'].update(enable=True, advertise_host='127.0.0.2' if role == 'receiver' else '127.0.0.1')
            c['business_runtime'].update(worker_threads=4, max_pending_tasks=128, stripe_count=64, per_stripe_queue_capacity=16)
            gateway_config[role] = c
        receiver_env = {**env, 'TINYIMX_MESSAGE_RPC_TARGET': f'127.0.0.1:{ports["message-gate"]}'}
        source_env = {**env, 'TINYIMX_MESSAGE_RPC_TARGET': f'127.0.0.1:{ports["message"]}'}
        receiver_process = start('receiver', gateway_config['receiver'], receiver_env)
        source = start('source-1', gateway_config['source'], source_env)
        wait_port('127.0.0.1', ports['receiver'], receiver_process)
        wait_port('127.0.0.1', ports['source'], source)
        password = os.environ.get('TINYIMX_BENCH_PASSWORD', '123456')
        receiver = Client(ports['receiver'], USERS[1][0], USERS[1][1], password, emit)
        clients.append(receiver)
        sender = Client(ports['source'], USERS[0][0], USERS[0][1], password, emit)
        clients.append(sender)
        # Allow independent Redis discovery to see the receiver route.
        time.sleep(4)
        for index, scenario in enumerate(['source_process_exit', 'message_service_restart'], 1):
            message_gate.block()
            cid = run + '_' + str(index)
            text = 'owned-recovery-' + cid
            start_time = time.monotonic()
            seq = sender.send(2001, {'client_message_id': cid, 'to': USERS[1][0], 'text': text})
            _, _, ack = sender.wait(2002, lambda p: p[1] == seq, seconds=10)
            assert ack.get('success') is True and ack.get('stored_persistent') is True, 'Durable acceptance failed'
            mid = int(ack['message_id'])
            ack_ms = (time.monotonic() - start_time) * 1000
            assert mysql(f'SELECT delivery_status FROM im_private_messages WHERE message_id={mid}') == '0'
            emit('sender_durable_acceptance', scenario=scenario, client_message_id=cid, message_id=mid, ack_ms=ack_ms)
            if scenario == 'source_process_exit':
                stop(source, hard=True)
            else:
                stop(message)
                receiver.send(9001, {})
                receiver.wait(9001, seconds=5)
                emit('receiver_heartbeat_during_message_outage', user_id=receiver.uid)
                message = start('message-2', msg, message_env, f'127.0.0.1:{ports["message"]}')
                wait_port('127.0.0.1', ports['message'], message)
            fault_end = time.monotonic()
            message_gate.gate.set()
            _, delivery_seq, delivery = receiver.wait(2019, lambda p: int(p[2].get('message_id', 0)) == mid, seconds=45)
            assert int(delivery['from']) == USERS[0][0] and int(delivery['to']) == receiver.uid and delivery['text'] == text
            assert mysql(f'SELECT delivery_status FROM im_private_messages WHERE message_id={mid}') == '0', 'Confirmed without real ACK'
            recovery_ms = (time.monotonic() - fault_end) * 1000
            receiver.send(2020, {'message_id': mid}, seq=delivery_seq)
            deadline = time.monotonic() + 15
            while mysql(f'SELECT delivery_status FROM im_private_messages WHERE message_id={mid}') != '1':
                assert time.monotonic() < deadline, 'Real ACK did not converge to ReceiverConfirmed'
                time.sleep(.1)
            row = {'scenario': scenario, 'status': 'PASS', 'client_message_id': cid, 'message_id': mid,
                   'ack_ms': ack_ms, 'recovery_ms': recovery_ms, 'receiver_relogin_count': 0,
                   'pending_before_real_ack': True, 'receiver_confirmed_after_real_ack': True}
            state['scenarios'].append(row)
            emit('scenario_pass', **row)
            if index == 1:
                # SIGKILL leaves a live registry lease until TTL expiry. The
                # next independent scenario must not steal or delete it.
                gateway_config['source']['app']['instance_id'] = run + '-source-restarted'
                source = start('source-2', gateway_config['source'], source_env)
                wait_port('127.0.0.1', ports['source'], source)
                sender = Client(ports['source'], USERS[0][0], USERS[0][1], password, emit)
                clients.append(sender)
        assert inspect_production() == before, 'Existing container identity changed during experiment'
        state['status'] = 'PASS'
        state['receiver_login_count'] = 1
    except Exception as e:
        state['status'] = 'FAIL'
        state['error'] = str(e)
        emit('experiment_failed', error=str(e))
    finally:
        for c in clients:
            c.close()
        for relay in relays:
            relay.close()
        for p in reversed(processes):
            stop(p)
        if redis_created:
            label = command(['docker', 'inspect', '--format', '{{index .Config.Labels "codex.task"}}', redis_name])
            assert label == 'tinyimx-durable-recovery', 'Do not stop an unowned container'
            command(['docker', 'stop', '--time', '5', redis_name])
            state['owned_redis_retained_stopped'] = redis_name
        for log in logs:
            log.close()
        try:
            after = inspect_production()
            save('production-after.json', after)
            state['production_container_identity_unchanged'] = after == before
            if after != before:
                state['status'] = 'FAIL'
        except Exception as e:
            state['postcheck_error'] = str(e)
            state['status'] = 'FAIL'
        save('result.json', state)
        print(json.dumps(state, indent=2, ensure_ascii=False), flush=True)
    return 0 if state['status'] == 'PASS' else 2


if __name__ == '__main__':
    raise SystemExit(main())
