#!/usr/bin/env python3
import argparse, json, re
from pathlib import Path

GATES = [
    ("failover_connection", "failover-connection"),
    ("failover_message", "failover-message"),
    ("user_scale", "user-scale"),
    ("hotspot_group", "hotspot-group"),
    ("file", "file"),
    ("mq_fault", "mq-fault"),
    ("backpressure", "backpressure"),
    ("soak", "soak"),
]

def read_json(path: Path):
    try:
        return json.loads(path.read_text())
    except Exception:
        return None

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('run_root')
    args=ap.parse_args()
    root=Path(args.run_root)
    report={'run_root':str(root),'gates':{},'all_pass':True,'highlights':{}}
    for key,dirname in GATES:
        d=root/dirname
        marker=d/'PASS.marker'
        passed=marker.exists()
        entry={'pass':passed,'artifact_dir':str(d)}
        if marker.exists(): entry['marker']=marker.read_text(errors='replace').strip()
        if (d/'result.json').exists(): entry['result']=read_json(d/'result.json')
        if (d/'summary.txt').exists(): entry['summary']=d.joinpath('summary.txt').read_text(errors='replace').strip()
        if (d/'gate.log').exists(): entry['gate_tail']='\n'.join(d.joinpath('gate.log').read_text(errors='replace').splitlines()[-8:])
        report['gates'][key]=entry
        report['all_pass'] = report['all_pass'] and passed

    # Pull a compact set of resume/interview metrics when present.
    fc=report['gates']['failover_connection'].get('result') or {}
    fm=report['gates']['failover_message'].get('result') or {}
    soak=report['gates']['soak'].get('result') or {}
    report['highlights']={
        'failover_affected':fc.get('affected_clients'),
        'failover_recovered':fc.get('recovered_clients'),
        'failover_recovery_p99_ms':fc.get('recovery_p99_ms'),
        'failover_message_retry_attempts':fm.get('message_retry_attempts'),
        'failover_message_success_rate':fm.get('success_rate'),
        'soak_connections':soak.get('connected'),
        'soak_message_throughput':soak.get('throughput_msg_s'),
        'soak_message_p99_ms':soak.get('p99_ms'),
    }
    out_json=root/'FINAL_CAPSTONE_REPORT.json'
    out_txt=root/'FINAL_CAPSTONE_REPORT.txt'
    out_json.write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n')
    lines=['TinyIMX Final Capstone Local Acceptance', '======================================']
    for key,_ in GATES:
        e=report['gates'][key]
        lines.append(f"{key}: {'PASS' if e['pass'] else 'MISSING/FAIL'}")
    lines.append('')
    lines.append('highlights='+json.dumps(report['highlights'],ensure_ascii=False,separators=(',',':')))
    lines.append(f"FINAL={'PASS' if report['all_pass'] else 'FAIL'}")
    out_txt.write_text('\n'.join(lines)+'\n')
    print(out_txt.read_text(),end='')
    raise SystemExit(0 if report['all_pass'] else 42)

if __name__=='__main__': main()
