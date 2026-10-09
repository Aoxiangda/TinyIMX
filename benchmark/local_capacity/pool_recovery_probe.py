#!/usr/bin/env python3
"""Fault only owned relay connections. Real MySQL/Redis SELECT1/PING, no row changes."""
import argparse,datetime,hashlib,json,os,pathlib,select,socket,subprocess,time
from recovery_e2e import Relay
ROOT=pathlib.Path('/home/jackson7/projects/TinyIMX_publish')
class DropRelay(Relay):
    def bridge(self,client):
        if not self.gate.is_set():
            with self.lock:self.connections.discard(client)
            client.close();return
        super().bridge(client)
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--run',required=True);a=parser.parse_args()
    assert a.run.isalnum() and len(a.run)<30
    os.umask(0o077);d=ROOT/'.local/codex'/('pool-recovery-'+a.run);assert not d.exists();d.mkdir()
    binary=ROOT/'build/linux-release/pool_recovery_probe'
    (d/'audit-before.json').write_text(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'operation':'Private test relays close only own connections, repeated three outage acquisitions then recover','reads':'Actual private message configuration, real MySQL SELECT1 and RedisPING','writes':'New private settings and sanitized evidence; empty transaction only','no_changes':['Business rows','Shared containers/network/firewall','Other applications','Credentials','Global cache/swap'],'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'rollback':'Only own relay/process stopped, all evidence retained'},indent=2)+'\n')
    source=json.loads(pathlib.Path('/home/jackson7/.local/share/tinyimx/m21/config/message.json').read_text());private=d/'runtime-private';private.mkdir();out=[]
    for kind,container,port in [('mysql','tinyimx-m21-mysql-1',3306),('redis','tinyimx-m21-redis-1',6379)]:
        state=json.loads(subprocess.check_output(['docker','inspect',container],text=True))[0]
        ip=next(iter(state['NetworkSettings']['Networks'].values()))['IPAddress']
        relay=DropRelay('127.0.0.1',0,(ip,port));relay.gate.set();cfg=dict(source)
        cfg[kind]=dict(source[kind]);cfg[kind].update(enable=True,host='127.0.0.1',port=relay.listener.getsockname()[1],pool_size=1)
        cfg['logger']={'level':'error','console':False,'file':str(private/(kind+'.log'))}
        path=private/(kind+'.json');path.write_text(json.dumps(cfg));p=None;lines=[];checks=[]
        try:
            p=subprocess.Popen([str(binary),kind,str(path)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
            # Read one byte line stream through select, avoids blocking past deadline.
            deadline=time.monotonic()+40;buf=b''
            while p.poll() is None or buf:
                assert time.monotonic()<deadline,'Owned probe deadline'
                ready,_,_=select.select([p.stdout],[],[],.2)
                if not ready:
                    if p.poll() is not None:break
                    continue
                data=os.read(p.stdout.fileno(),4096)
                if not data:break
                buf+=data
                while b'\n' in buf:
                    raw,buf=buf.split(b'\n',1);line=raw.decode(errors='replace')
                    # Only our explicit check/control/result lines become public evidence.
                    if line.startswith(('CHECK ','BARRIER ','RESULT ','PROBE_FAILED')):lines.append(line)
                    if line.startswith('CHECK '):checks.append({'check':line.split()[1],'status':line.split()[2]})
                    if line=='BARRIER fault':relay.block();p.stdin.write('continue\n');p.stdin.flush()
                    if line=='BARRIER recover':relay.gate.set();p.stdin.write('continue\n');p.stdin.flush()
            code=p.wait(timeout=2)
            item={'kind':kind,'status':'PASS' if code==0 and checks and all(x['status']=='PASS' for x in checks) else 'FAIL','exit_code':code,'checks':checks,'transcript':lines}
        except Exception as e:item={'kind':kind,'status':'FAIL','error':type(e).__name__,'checks':checks,'transcript':lines}
        finally:
            relay.close()
            if p and p.poll() is None:p.terminate();p.wait(timeout=5)
        out.append(item);(d/(kind+'-result.json')).write_text(json.dumps(item,indent=2)+'\n');print(json.dumps(item),flush=True)
    summary={'status':'PASS' if all(x['status']=='PASS' for x in out) else 'FAIL','tests':out,'coverage':'Connection recovery correctness only; no performance acceptance or shared service fault'}
    (d/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');return 0 if summary['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
