#!/usr/bin/env python3
import argparse,json,re
from pathlib import Path

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('results'); ap.add_argument('--output',default=''); args=ap.parse_args()
    rows=[]
    for line in Path(args.results).read_text().splitlines():
        if not line.strip(): continue
        r=json.loads(line); rate=int(round(float(r.get('setup_ms',0)) and (1000.0/max(float(r.get('setup_ms',1)),1.0)*1000.0)))
        # Prefer requested rate encoded externally in setup relationship only as fallback; filename-independent results lack the requested rate.
        rows.append(r)
    # Infer requested rates from setup_ms only isn't reliable enough for reports. Read sibling logs instead.
    root=Path(args.results).parent
    data=[]
    for log in sorted(root.glob('auth-*.log'), key=lambda p:int(re.search(r'auth-(\d+)',p.name).group(1))):
        rate=int(re.search(r'auth-(\d+)',log.name).group(1))
        result=None
        for line in log.read_text(errors='replace').splitlines():
            if line.startswith('TINYIMX_LOADGEN_RESULT '): result=json.loads(line.split(' ',1)[1])
        if result is None: continue
        passed=(not result.get('setup_failed') and result.get('connected')==result.get('connections') and result.get('login_ok')==result.get('connections') and result.get('login_fail')==0 and result.get('login_response_fail',0)==0 and result.get('login_unresolved_fail',0)==0 and result.get('deadline_rejections',0)==0 and result.get('protocol_errors',0)==0 and result.get('server_errors',0)==0)
        data.append({'rate':rate,'pass':passed,'login_p99_ms':result.get('login_p99_ms',0),'login_ok':result.get('login_ok',0),'login_fail':result.get('login_fail',0),'overload':result.get('overload_rejections',0),'deadline':result.get('deadline_rejections',0),'setup_ms':result.get('setup_ms',0)})
    last=max((x['rate'] for x in data if x['pass']),default=0)
    first=min((x['rate'] for x in data if not x['pass']),default=0)
    out={'cases':data,'last_stable_rate':last,'first_unstable_rate':first,'knee_not_reached':first==0}
    print('rate\tpass\tlogin_p99\tlogin_ok\tlogin_fail\toverload\tdeadline')
    for x in data: print(f"{x['rate']}\t{int(x['pass'])}\t{x['login_p99_ms']}\t{x['login_ok']}\t{x['login_fail']}\t{x['overload']}\t{x['deadline']}")
    print(f"LAST_STABLE_RATE={last}"); print(f"FIRST_UNSTABLE_RATE={first if first else 'NONE'}")
    if args.output: Path(args.output).write_text(json.dumps(out,indent=2)+'\n')

if __name__=='__main__': main()
