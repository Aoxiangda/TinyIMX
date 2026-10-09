import argparse
import datetime as dt
import hashlib
import json
import pathlib
import statistics

p = argparse.ArgumentParser()
p.add_argument('--control', required=True)
p.add_argument('--host-log', required=True)
p.add_argument('--output', required=True)
a = p.parse_args()
root = pathlib.Path(__file__).resolve().parent.parent
control = (root / a.control).resolve()
host = (root / a.host_log).resolve()
output = (root / a.output).resolve()
assert all(x.is_relative_to(root / 'evidence') for x in (control, host, output))
assert not output.exists()
audit = output.with_suffix('.audit-before.json')
assert not audit.exists()

def utc(value):
    assert isinstance(value, str)
    result = dt.datetime.fromisoformat(value.replace('Z', '+00:00'))
    assert result.tzinfo is not None
    return result.astimezone(dt.timezone.utc)

low = utc(json.loads((control / 'audit-before.json').read_text())['utc'])
high = utc(json.loads((control / 'summary.json').read_text())['ended_utc'])
audit.write_text(json.dumps({'utc': dt.datetime.now(dt.timezone.utc).isoformat(),
    'operation': 'Read-only exact UTC window CPU summary, preserve literal JSON ISO strings',
    'inputs': {'control_audit_sha256': hashlib.sha256((control / 'audit-before.json').read_bytes()).hexdigest(),
               'control_summary_sha256': hashlib.sha256((control / 'summary.json').read_bytes()).hexdigest(),
               'host_log_sha256': hashlib.sha256(host.read_bytes()).hexdigest()},
    'writes': str(output), 'runtime_changes': False,
    'problem': 'Prior PowerShell ConvertFromJson converted ISO strings to date objects and subsequent string parsing changed timezone; zero matches are invalid, retained.',
    'solution': 'Python JSON preserves strings; require explicit offsets and normalize UTC. Never overwrite original invalid review.'}, indent=2) + '\n')
rows = [json.loads(s) for s in host.read_text().splitlines()]
valid = [r for r in rows if r['status'] == 'OBSERVED' and low <= utc(r['utc']) <= high]
assert len(valid) > 1, 'No synchronized window coverage; do not invent CPU data'
vm = [s['percent_processor_time_onecore100'] for r in valid for s in r['top_processes'] if s['name'] == 'vmware-vmx']
assert len(vm) == len(valid)
result = {'status': 'SYNCHRONIZED_HOST_CPU_WINDOW_REVIEW',
    'start_utc': low.isoformat(), 'end_utc': high.isoformat(), 'samples': len(valid),
    'first_sample_utc': valid[0]['utc'], 'last_sample_utc': valid[-1]['utc'],
    'host_cpu_percent_mean': statistics.mean(r['host_total_percent_processor_time'] for r in valid),
    'host_cpu_percent_max': max(r['host_total_percent_processor_time'] for r in valid),
    'vmware_process_cpu_one_core100_mean': statistics.mean(vm),
    'vmware_process_cpu_one_core100_max': max(vm),
    'maximum_cim_capture_ms': max(r['collect_elapsed_ms'] for r in valid),
    'limitations': ['Window snapshots are not proof of prior login abort cause',
                    'Process percent uses one CPU as100; host total0to100',
                    'CIM observer overhead retained; diagnostic measurement, not isolated-host acceptance',
                    'All user applications preserved; no process control',
                    'Original zero-match timezone review preserved and invalidated']}
output.write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
