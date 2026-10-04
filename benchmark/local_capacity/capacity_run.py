#!/usr/bin/env python3
"""Real nginx TCP hold/private baseline. Full-feature/TLS/fault tests remain separate."""
import argparse, collections, datetime, hashlib, ipaddress, json, math, os, pathlib, re, resource, subprocess, time

ROOT = pathlib.Path('/home/jackson7/projects/TinyIMX_publish')
MYSQL = 'tinyimx-m21-mysql-1'
IMAGE = 'sha256:05380ac715ac8fba73460a32e9b92eece6dc4f548dc9baa51e5b6fbcfecec8da'

def cmd(args):
    return subprocess.check_output(args, text=True)

def sql(statement):
    # Credentials remain inside the existing container and never enter evidence/argv.
    return cmd(['docker', 'exec', MYSQL, 'sh', '-c',
                'MYSQL_PWD="$MYSQL_ROOT_PASSWORD" exec mysql -uroot --batch --raw --skip-column-names "$MYSQL_DATABASE" -e "$1"',
                'capacity-read-or-owned-fixture', statement])

def save(path, obj):
    tmp = path.with_suffix(path.suffix + '.tmp')
    tmp.write_text(json.dumps(obj, indent=2) + '\n')
    tmp.replace(path)

def container_identity():
    names = cmd(['docker', 'ps', '--format', '{{.Names}}']).splitlines()
    return [{'name': c['Name'], 'id': c['Id'], 'image': c['Image'],
             'started': c['State']['StartedAt'], 'health': c['State'].get('Health', {}).get('Status'),
             'nonsecret_runtime_flags': {k:v for k,v in (x.split('=',1) for x in c['Config']['Env'] if '=' in x)
                 if k in ('TINYIMX_DURABLE_PRIVATE_RECOVERY_ENABLE','TINYIMX_MESSAGE_WORKER_THREADS')}}
            for c in json.loads(cmd(['docker', 'inspect', *names]))]

def percentile(hist, fraction=.99):
    counts = collections.Counter()
    for h in hist:
        counts.update({int(k): int(v) for k, v in h['bins']})
    target = math.ceil(sum(counts.values()) * fraction)
    if not target:
        return None
    cumulative = 0
    for value, count in sorted(counts.items()):
        cumulative += count
        if cumulative >= target:
            return value / 1000

def run(a):
    os.chdir(ROOT)
    os.umask(0o077)
    assert re.fullmatch(r'[a-zA-Z0-9-]{1,20}', a.run)
    assert 2 <= a.users <= 50000 and 0 < a.rate <= 10000 and 1 <= a.duration <= 7200
    assert 100000 <= a.user_id_base <= 1000000000000
    assert re.fullmatch(r'[a-zA-Z0-9_-]{1,40}', a.username_prefix)
    assert re.fullmatch(r'sha256:[0-9a-f]{64}', a.gateway_image)
    assert re.fullmatch(r'sha256:[0-9a-f]{64}', a.message_image)
    count=math.ceil(a.users/10000)
    ipaddress.IPv4Address(a.host)
    sources=['127.0.0.'+str(i+2) for i in range(count)]
    if a.host!='127.0.0.1' or a.source_ips:
        interfaces=json.loads(cmd(['ip','-j','-4','address','show']))
        local={x['local'] for interface in interfaces for x in interface['addr_info'] if x['family']=='inet'}
        assert a.host in local, 'Override target must be an existing local published address'
        if a.source_ips:
            supplied=a.source_ips.split(',')
            assert len(supplied)>=count and len(supplied)<=8 and len(set(supplied))==len(supplied), 'Unique source per worker required'
            assert all(str(ipaddress.IPv4Address(x))==x and x in local for x in supplied), 'Use existing local IPv4 sources only; no aliases'
            sources=supplied[:count]
    stage = ROOT / '.local/codex' / ('capacity-' + a.run)
    assert not stage.exists(), 'Keep all prior runs immutable'
    stage.mkdir()
    save(stage/'preflight-audit-before.json', {'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
         'operation':'Readonly owned identity/runtime preflight and task-process-only descriptor limit',
         'scenario':vars(a), 'writes':'Fresh evidence directory only before detailed fixture audit',
         'rollback':'No system or business mutation during preflight; retain failure evidence'})
    control = stage / 'control'; control.mkdir()
    binary = ROOT / 'build/linux-release/tinyimx_capacity_worker'
    identity = container_identity()
    for s in ['gateway-a', 'gateway-b', 'message-service']:
        c = next(c for c in identity if c['name'] == '/tinyimx-m21-' + s + '-1')
        expected = a.message_image if s=='message-service' else a.gateway_image
        assert c['image'] == expected and c['health'] == 'healthy', 'Candidate runtime identity'
    soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    resource.setrlimit(resource.RLIMIT_NOFILE, (min(hard, max(soft, 20000)), hard))
    base=a.user_id_base
    total = int(sql(f"SELECT COUNT(*) FROM im_users WHERE user_id BETWEEN {base+1} AND {base+a.users} AND username=CONCAT('{a.username_prefix}',LPAD(user_id-{base},6,'0')) AND status=1;"))
    assert total == a.users, 'Existing owned fixture range incomplete; do not reset credentials'
    # Permissions require mutual friendship, including a smaller ring's closure.
    # UNION deduplicates the two-user case. Existing non-friend rows are refused.
    forward=f"SELECT user_id AS u, {base+1}+MOD(user_id-{base},{a.users}) AS v FROM im_users WHERE user_id BETWEEN {base+1} AND {base+a.users}"
    rows = sql(f"SELECT e.u,e.v,r.user_id IS NOT NULL,IFNULL(r.relation_status,0) FROM ({forward} UNION SELECT v,u FROM ({forward}) AS reverse_edges) AS e LEFT JOIN im_user_relations r ON r.user_id=e.u AND r.peer_user_id=e.v ORDER BY e.u,e.v;")
    relations = [tuple(map(int, x.split('\t'))) for x in rows.splitlines()]
    assert len(relations) == (2 if a.users==2 else 2*a.users), 'Mutual ring enumeration incomplete'
    assert all(base<u<=base+a.users and base<v<=base+a.users and u!=v for u,v,present,s in relations), 'Owned ring bounds'
    assert all(present in (0,1) and (not present or s==1) for u,v,present,s in relations), 'Refuse override existing non-friend relations'
    missing = [(u,v) for u,v,present,s in relations if not present]
    assert a.users > 1
    save(stage/'audit-before.json', {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'source_commit':cmd(['git','rev-parse','HEAD']).strip(), 'coordinator_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
        'worker_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(), 'runtime':identity,
        'scenario':vars(a), 'writes':['New evidence/ledger/control files','Normal test messages and protocol ACKs','Only missing ring relations enumerated below'],
        'missing_owned_fixture_relations':missing, 'existing_relation_overrides':False,
        'measurement':'Open-loop offered rate; skipped requests counted, positive ACK and scheduled-to-ACK histograms both retained. Ramp excluded from send latency.',
        'rollback':'Cooperative abort and stop owned workers only; no database delete, no cache/swap resets, no container restart',
        'coverage':'Normal plaintext private/hold baseline only; group, file, MCP, TLS, faults, soak and mixed workflows NOT covered'})
    if missing:
        for i in range(0, len(missing), 500):
            values=','.join(f'({u},{v},1)' for u,v in missing[i:i+500])
            sql('START TRANSACTION; INSERT INTO im_user_relations(user_id,peer_user_id,relation_status) VALUES '+values+'; COMMIT;')
    (stage/'fixture-rows-before.tsv').write_text(rows)
    prefix='l'+a.run+'-'
    assert int(sql(f"SELECT COUNT(*) FROM im_private_messages WHERE client_message_id LIKE '{prefix}%';")) == 0
    (stage/'guest-memory-before.txt').write_text(cmd(['free','-m'])+pathlib.Path('/proc/pressure/memory').read_text())
    workers=[]; logs=[]; results=[]; failure=None; start=time.monotonic(); last_sample=0
    def snapshot():
        nonlocal last_sample
        if time.monotonic()-last_sample < 5:return
        last_sample=time.monotonic()
        with (stage/'guest-resources.log').open('a') as f:
            f.write(datetime.datetime.now(datetime.timezone.utc).isoformat()+'\n')
            f.write(pathlib.Path('/proc/meminfo').read_text())
            f.write(pathlib.Path('/proc/pressure/memory').read_text())
            f.write(pathlib.Path('/proc/pressure/cpu').read_text())
            f.write(cmd(['docker','stats','--no-stream','--format','{{.Name}} CPU={{.CPUPerc}} MEM={{.MemUsage}} PIDS={{.PIDs}}']))
        available=int(re.search(r'MemAvailable:\s+(\d+)',pathlib.Path('/proc/meminfo').read_text()).group(1))
        assert available >= 768*1024, 'Guest memory safety threshold; retain failed run'
    def wait_for(name, timeout):
        deadline=time.monotonic()+timeout
        while True:
            snapshot()
            assert all(p.poll() is None for p,d in workers), 'Worker exited before '+name
            if all((d/name).exists() for p,d in workers):return
            assert time.monotonic()<deadline, 'Coordinator timeout '+name
            time.sleep(.5)
    try:
        count=math.ceil(a.users/10000)
        for i in range(count):
            n=min(10000,a.users-i*10000);d=stage/('worker-'+str(i));d.mkdir()
            log=(d/'worker.log').open('w');logs.append(log)
            argv=[str(binary),'--out',str(d),'--control',str(control),'--run-id',a.run,
                  '--connections',str(n),'--total-users',str(a.users),'--offset',str(i*10000),
                  '--user-id-base',str(base),'--username-prefix',a.username_prefix,
                  '--worker-id',str(i),'--source-ip',sources[i],'--host',a.host,'--port','9000',
                  '--rate',str(a.rate*n/a.users),'--ramp-per-sec',str(100/count),
                  '--duration',str(a.duration),'--drain-seconds','15','--verify-timeout','120',
                  '--heartbeat-seconds','15','--mode',a.mode]
            p=subprocess.Popen(argv,stdout=log,stderr=subprocess.STDOUT)
            workers.append((p,d))
        save(stage/'worker-pids.json', [{'pid':p.pid,'directory':str(d)} for p,d in workers])
        wait_for('ready.json',a.users/100*2+150)
        save(stage/'all-online.json',{'users':a.users,'monotonic_ns':time.monotonic_ns(),
                                     'workers':[json.loads((d/'ready.json').read_text()) for p,d in workers]})
        (control/'start_ns').write_text(str(time.monotonic_ns()+2_000_000_000)+'\n')
        print('ALL_ONLINE_AND_BARRIER='+str(a.users),flush=True)
        wait_for('audit-ready.json',a.duration+40)
        # All recipients remain connected until SQL and raw delivery ledger are checked.
        raw=sql(f"SELECT message_id,from_user_id,to_user_id,client_message_id,delivery_status FROM im_private_messages WHERE client_message_id LIKE '{prefix}%' ORDER BY message_id;")
        (stage/'sql-reconciliation.tsv').write_text(raw)
        db={}
        for line in raw.splitlines():
            mid,u,v,cid,state=line.split('\t');assert cid not in db,'Duplicate logical DB identity'
            db[cid]=(int(mid),int(u),int(v),int(state))
        sent={};acked={}; delivered=collections.Counter(); negative=[]
        for p,d in workers:
            for line in (d/'ledger.tsv').read_text().splitlines():
                kind,u,v,mid,seq,cid,t=line.split('\t')
                # Delivery identifies the stable M/from/to tuple. C is optional
                # on the public wire; retain deliveries even when it is absent.
                if kind=='delivery':
                    delivered[(int(mid),int(u),int(v))]+=1
                    continue
                if not cid.startswith(prefix):continue
                if kind=='send':
                    assert cid not in sent;sent[cid]=(int(u),int(v))
                elif kind=='ack':
                    assert cid not in acked;acked[cid]=(int(mid),int(u),int(v))
                elif kind=='fail':negative.append(cid)
        mismatches=[cid for cid,x in acked.items() if cid not in db or db[cid][:3]!=x or db[cid][3]!=1 or delivered[x]<1]
        save(stage/'reconciliation.json',{'sent':len(sent),'positive_ack':len(acked),'negative_ack':len(negative),'db_rows':len(db),
            'confirmed':sum(x[3]==1 for x in db.values()),'pending':sum(x[3]==0 for x in db.values()),
            'positive_ack_identity_or_confirmation_or_wire_mismatches':mismatches,
            'sent_not_durable_at_snapshot':[cid for cid in sent if cid not in db],
            'durable_without_positive_ack':[cid for cid in db if cid not in acked],
            'db_without_send':[cid for cid in db if cid not in sent],
            'note':'Snapshot absence is not a proof of permanent loss; late commits must be investigated.'})
        (control/'quiesce_heartbeats').write_text('Active window and SQL audit complete; drain every sent ping\n')
        wait_for('heartbeat-drained.json', 35)
        (control/'release').write_text('SQL_RECONCILED\n')
        for p,d in workers:assert p.wait(timeout=15)==0,'Worker final status'
        results=[json.loads((d/'final.json').read_text()) for p,d in workers]
        metrics=collections.Counter()
        for x in results:metrics.update(x['metrics'])
        p99=percentile([x['ack_histogram'] for x in results])
        scheduled_p99=percentile([x['scheduled_to_ack_histogram'] for x in results])
        gates={'all_logged_in':metrics['login_ok']==a.users,'unexpected_disconnects_zero':metrics['disconnects']==0,
               'heartbeat_9999':metrics['heartbeat_sent']>0 and metrics['heartbeat_ack']/metrics['heartbeat_sent']>=.9999,
               'all_workers_completed':all(x['status']=='COMPLETED' for x in results),
               'all_positives_durable_confirmed_delivered':not mismatches,
               'no_unknown_db_logical_messages':all(cid in sent for cid in db)}
        if a.mode=='private':gates.update({'planned_requests_exact':metrics['planned_requests']==math.ceil(a.rate*a.duration-1e-8),
            'no_skipped_offered_requests':metrics['skipped_scheduled_requests']==0,
            'all_send_positive_ack':metrics['send_attempts']==metrics['chat_ack_ok'] and metrics['chat_ack_fail']==0,
            'active_ack_throughput_95pct':metrics['ack_in_active_window']/a.duration>=.95*a.rate,
            'positive_ack_p99_le_100ms':p99 is not None and p99<=100,
            'scheduled_to_ack_p99_le_100ms':scheduled_p99 is not None and scheduled_p99<=100,
            'all_sent_db_and_confirmed':len(sent)==len(db) and all(x[3]==1 for x in db.values())})
        summary={'status':'PASS' if all(gates.values()) else 'FAIL','scenario':vars(a),'gates':gates,'metrics':dict(metrics),
                 'positive_ack_p99_ms_upper_bin':p99,'scheduled_to_ack_p99_ms_upper_bin':scheduled_p99,
                 'active_positive_ack_per_second':metrics['ack_in_active_window']/a.duration,
                 'raw_positive_ack_max_ms':max(x['ack_histogram']['max_us'] for x in results)/1000,
                 'wall_seconds':time.monotonic()-start,'coverage':'Private/hold normal plaintext only; other functions NOT_RUN'}
        save(stage/'summary.json',summary);print(json.dumps(summary,indent=2),flush=True)
    except BaseException as e:
        failure=str(e);(control/'abort').write_text(failure+'\n')
        save(stage/'failure.json',{'status':'FAIL','error':failure,'wall_seconds':time.monotonic()-start})
        print('RUN_FAILED='+failure,flush=True)
    finally:
        for p,d in workers:
            if p.poll() is None:
                try:p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.terminate();p.wait(timeout=10)
        for f in logs:f.close()
        if failure and not (stage/'summary.json').exists():
            partial=[];totals=collections.Counter();reasons=collections.Counter()
            for p,d in workers:
                last=next((d/n for n in ('final.json','live.json') if (d/n).exists()),None)
                x=json.loads(last.read_text()) if last else {}
                totals.update(x.get('metrics',{}))
                partial.append({'directory':d.name,'exit_code':p.poll(),'snapshot':last.name if last else None,
                                'status':x.get('status','NO_SNAPSHOT'),'online_now':x.get('online_now',0),
                                'started_steady_window':bool(x.get('start_steady_ns',0))})
                diagnostic=d/'failure-responses.jsonl'
                if diagnostic.exists():
                    for line in diagnostic.read_text().splitlines():
                        r=json.loads(line);reasons[r['kind']+':'+(r.get('reason') or 'NO_REASON')]+=1
            save(stage/'summary.json',{'status':'FAIL','phase':'worker_or_coordinator_abort','scenario':vars(a),
                 'error':failure,'all_online_reached':(stage/'all-online.json').exists(),'metrics':dict(totals),
                 'workers':partial,'failure_reason_counts':dict(reasons),'wall_seconds':time.monotonic()-start,
                 'coverage':'Aborted run; partial metrics are not capacity acceptance. Original window and failed responses retained.'})
        save(stage/'container-identity-after.json',container_identity())
        (stage/'guest-memory-after.txt').write_text(cmd(['free','-m'])+pathlib.Path('/proc/pressure/memory').read_text())
    return 2 if failure or not all(gates.values()) else 0

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--run',required=True);p.add_argument('--users',type=int,required=True)
    p.add_argument('--rate',type=float,default=100);p.add_argument('--duration',type=int,default=300)
    p.add_argument('--mode',choices=['private','hold'],default='private')
    p.add_argument('--user-id-base',type=int,default=500000)
    p.add_argument('--username-prefix',default='m21b500000_')
    p.add_argument('--host',default='127.0.0.1')
    p.add_argument('--source-ips',default=None,help='Comma-separated existing local IPv4 addresses, one per worker; no network changes')
    p.add_argument('--gateway-image',default=IMAGE)
    p.add_argument('--message-image',default=IMAGE,help='Exact expected MessageService image SHA; default preserves original candidate pin')
    a=p.parse_args()
    # A duplicate run is rejected before error handling can touch its evidence.
    assert re.fullmatch(r'[a-zA-Z0-9-]{1,20}',a.run)
    stage=ROOT/'.local/codex'/('capacity-'+a.run)
    assert not stage.exists(),'Keep every prior run immutable'
    try:
        raise SystemExit(run(a))
    except Exception as e:
        if stage.exists() and not (stage/'failure.json').exists():
            save(stage/'failure.json',{'status':'FAIL','phase':'preflight','type':type(e).__name__,'error':str(e)})
            if not (stage/'summary.json').exists():
                save(stage/'summary.json',{'status':'FAIL','phase':'preflight','scenario':vars(a),'error':str(e),'coverage':'No capacity acceptance; preflight failed before worker measurement'})
        raise
