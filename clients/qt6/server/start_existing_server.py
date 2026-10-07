#!/usr/bin/env python3
"""Start only the previously audited existing containers, preserving their images and mounts."""
import datetime,json,pathlib,subprocess,sys,time,uuid
ROOT=pathlib.Path('/home/jackson7/projects/TinyIMX_publish')
def docker(*args):return subprocess.check_output(['docker',*args],text=True,timeout=30)
def identity(row):return {'name':row['Name'].lstrip('/'),'id':row['Id'],'image':row['Image'],'mounts':[{'destination':m['Destination'],'source':m['Source'],'rw':m['RW']} for m in row['Mounts']]}

def start_order(names):
    # Deployment inventories follow creation order after audited replacements.
    # Start dependencies first regardless of inventory order; do not recreate init.
    roles=('mysql','redis','zookeeper','rocketmq-namesrv','rocketmq-broker','rocketmq-proxy','otel-collector','prometheus',
           'user-service','social-service','group-service','file-service','message-service','outbox-relay','unread-projector',
           'gateway-a','gateway-b','mcp-server')
    order=['tinyimx-m21-'+role+'-1' for role in roles]+['tinyimx-desktop-file-20261007','tinyimx-m21-nginx-1']
    assert len(names)==len(order) and set(names)==set(order),'Accepted service roles changed; audit startup dependencies before starting'
    return order
def main():
    start=sys.argv[1:]==['--start'];assert start or not sys.argv[1:]
    expected=json.loads((pathlib.Path(__file__).parent/'accepted-runtime-20261007.json').read_text())
    out=ROOT/'.local/codex'/('desktop-server-start-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]);out.mkdir()
    names=[r['name'] for r in expected];rows=json.loads(docker('inspect',*names))
    assert [identity(r) for r in rows]==expected,'Runtime identity/image/mount changed or container missing; refuse recreation/start; inspect new deployment first'
    assert all(r['State']['Status'] in ('running','exited','created') and not r['State']['Paused'] for r in rows),'Paused/dead/restarting container requires diagnosis'
    order=start_order(names);running={r['Name'].lstrip('/'):r['State']['Running'] for r in rows}
    plan=[name for name in order if not running[name]]
    audit={'operation':'start known existing deployment' if start else 'read-only status','utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'runtime_before':[{'identity':identity(r),'state':r['State']} for r in rows],
        'planned_starts':plan,'dependency_order':order,'recreate_pull_build_delete_volume_changes':False,'pressure':False,'one_shot_rocketmq_init':'excluded intentionally','rollback':'leave accepted existing deployment running; no recreate/delete/revert image'}
    (out/'audit-before.json').write_text(json.dumps(audit,indent=2)+'\n')
    if start:
        for name in order:
            if not running[name]:docker('start',name)
            # Dependencies must be healthy before their consumers start.
            deadline=time.monotonic()+60
            while True:
                r=json.loads(docker('inspect',name))[0]
                if r['State']['Running'] and r['State'].get('Health',{}).get('Status','healthy')=='healthy':break
                assert time.monotonic()<deadline,'Container readiness timed out; preserve evidence, diagnose before retry'
                time.sleep(1)
        rows=json.loads(docker('inspect',*names))
    assert [identity(r) for r in rows]==expected
    healthy=all(r['State']['Running'] and r['State'].get('Health',{}).get('Status','healthy')=='healthy' for r in rows)
    summary={'status':'READY' if healthy else 'NOT_READY','running':sum(r['State']['Running'] for r in rows),'expected':len(expected),'started':plan if start else [],'audit':str(out),'endpoint':'192.168.220.128:9000','file_endpoint':'http://192.168.220.128:18082','mode':'normal server; pressure paused'}
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary));return 0 if healthy else 1
if __name__=='__main__':sys.exit(main())
