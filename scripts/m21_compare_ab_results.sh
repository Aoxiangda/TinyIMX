#!/usr/bin/env bash
set -Eeuo pipefail
BASELINE="${1:?baseline result.json required}"; CANDIDATE="${2:?candidate result.json required}"; OUT="${3:-m21-ab-comparison.json}"
python3 - "$BASELINE" "$CANDIDATE" "$OUT" <<'PY'
import json,os,sys
b=json.load(open(sys.argv[1]));c=json.load(open(sys.argv[2]));out=sys.argv[3]
def f(x,k):return float(x.get(k,0) or 0)
def pct(new,old):return None if old==0 else (new-old)*100.0/old
r={
 'baseline':sys.argv[1],'candidate':sys.argv[2],
 'throughput_baseline':f(b,'throughput_msg_s'),'throughput_candidate':f(c,'throughput_msg_s'),
 'throughput_delta_pct':pct(f(c,'throughput_msg_s'),f(b,'throughput_msg_s')),
 'p50_baseline_ms':f(b,'p50_ms'),'p50_candidate_ms':f(c,'p50_ms'),'p50_delta_pct':pct(f(c,'p50_ms'),f(b,'p50_ms')),
 'p95_baseline_ms':f(b,'p95_ms'),'p95_candidate_ms':f(c,'p95_ms'),'p95_delta_pct':pct(f(c,'p95_ms'),f(b,'p95_ms')),
 'p99_baseline_ms':f(b,'p99_ms'),'p99_candidate_ms':f(c,'p99_ms'),'p99_delta_pct':pct(f(c,'p99_ms'),f(b,'p99_ms')),
 'success_baseline':f(b,'success_rate'),'success_candidate':f(c,'success_rate'),
 'overload_baseline':int(b.get('overload_rejections',0)),'overload_candidate':int(c.get('overload_rejections',0)),
}
json.dump(r,open(out,'w'),indent=2)
print(json.dumps(r,indent=2))
if os.getenv('M21_AB_REQUIRE_IMPROVEMENT','0')=='1':
    min_tput=float(os.getenv('M21_AB_MIN_THROUGHPUT_GAIN_PCT','0'))
    min_p99=float(os.getenv('M21_AB_MIN_P99_REDUCTION_PCT','0'))
    td=r['throughput_delta_pct'] if r['throughput_delta_pct'] is not None else -999
    p99_reduction=-(r['p99_delta_pct'] if r['p99_delta_pct'] is not None else 999)
    if td<min_tput or p99_reduction<min_p99 or r['success_candidate']<r['success_baseline']:
        raise SystemExit(f"A/B target not met throughput_gain={td:.3f}% p99_reduction={p99_reduction:.3f}%")
    print('M21_AB_OPTIMIZATION_GATE=PASS')
else:
    print('M21_AB_COMPARISON=GENERATED')
PY
echo "M21_AB_RESULT=$OUT"
