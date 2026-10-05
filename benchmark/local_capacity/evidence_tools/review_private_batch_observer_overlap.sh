#!/usr/bin/env bash
set -euo pipefail
cd /home/jackson7/projects/TinyIMX_publish
python3 - <<'PY'
import pathlib,json,math,collections
b=pathlib.Path('.local/codex')
for name in ['sqlbatch150A1','sqlbatch150B1','sqlbatch150B2','sqlbatch150A2']:
 d=b/('private-batch-endpoint-control-'+name);raw=b/('capacity-'+name)
 if not (d/'summary.json').exists():continue
 captures=[json.loads((d/(x+'-snapshot.json')).read_text()) for x in ['before','after']]
 windows=[(x['started_monotonic_ns'],x['ended_monotonic_ns']) for x in captures]
 sends={};ack=[];start=int((raw/'control/start_ns').read_text())
 for line in (raw/'worker-0/ledger.tsv').read_text().splitlines():
  f=line.split('\t');assert len(f)==7;kind=f[0];key=(f[1],f[4],f[5]);when=int(f[6])
  if kind=='send':assert key not in sends;sends[key]=when
  elif kind=='ack':
   assert key in sends;sent=sends[key];latency=(when-sent)/1e6
   overlap=any(sent<=hi and when>=lo for lo,hi in windows)
   ack.append((sent,when,latency,overlap))
 assert len(ack)==len(sends)==9000
 slow=[x for x in ack if x[2]>100];overlap=[x for x in ack if x[3]];outside=[x[2] for x in ack if not x[3]]
 bins=collections.Counter(int((x[0]-start)/1e9) for x in slow)
 print(json.dumps({'run':name,'capture_elapsed_ms':[x['capture_elapsed_ms'] for x in captures],'actual_pairs':len(ack),'ledger_event_send_to_ack_not_histogram_count_above100ms':len(slow),'observer_overlapping_pairs':len(overlap),'slow_observer_overlapping_pairs':sum(x[3] for x in slow),'diagnostic_only_p99_outside_observer_overlap_ms':sorted(outside)[math.ceil(len(outside)*.99)-1],'worst_seconds_by_slow_count':bins.most_common(10),'limits':'Overlap is correlation, not causality. Event timestamps follow original histogram timing by small encode/log work. All samples and original failing gate remain intact; excluded diagnostic does not replace official P99 or certify performance.'}))
PY
