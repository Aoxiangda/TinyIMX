#!/usr/bin/env python3
import json, os, pathlib, sys

path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "results.jsonl")
budget = float(os.environ.get("TINYIMX_MESSAGE_P99_BUDGET_MS", "100"))
if not path.exists():
    print(f"FIRST_FAILURE=RESULTS_MISSING path={path}")
    raise SystemExit(10)
rows=[]
for line in path.read_text().splitlines():
    if not line.strip():
        continue
    r=json.loads(line)
    rate=float(r.get("target_rate_msg_s",0.0))
    if rate <= 0:
        continue
    base=(not r.get("setup_failed",True) and r.get("connected",0)==r.get("connections",-1)
          and r.get("login_ok",0)==r.get("connections",-1) and r.get("login_fail",1)==0
          and r.get("login_unresolved_fail",0)==0 and r.get("disconnects",1)==0
          and r.get("protocol_errors",1)==0 and r.get("server_errors",1)==0)
    passed=(base and r.get("chat_ack_fail",1)==0 and r.get("overload_rejections",1)==0
            and float(r.get("success_rate",0.0))>=99.9
            and float(r.get("throughput_msg_s",0.0))>=rate*0.95
            and float(r.get("p99_ms",1e18))<=budget)
    rows.append((rate, passed, r))

print("rate\tpass\tthroughput\tsuccess\tp50\tp95\tp99\toverload\tack_fail\tinflight")
for rate,passed,r in rows:
    print(f"{rate:g}\t{int(passed)}\t{float(r.get('throughput_msg_s',0)):.2f}\t"
          f"{float(r.get('success_rate',0)):.3f}\t{float(r.get('p50_ms',0)):.1f}\t"
          f"{float(r.get('p95_ms',0)):.1f}\t{float(r.get('p99_ms',0)):.1f}\t"
          f"{r.get('overload_rejections',0)}\t{r.get('chat_ack_fail',0)}\t{r.get('inflight_at_end',0)}")
last=None; first=None
for rate,passed,r in rows:
    if passed and first is None:
        last=rate
    elif not passed and first is None:
        first=rate
        break
print(f"LAST_STABLE_RATE={int(last) if last is not None else 'NONE'}")
print(f"FIRST_UNSTABLE_RATE={int(first) if first is not None else 'NONE'}")
if rows and first is None:
    print("STAGE1_MESSAGE_KNEE_NOT_REACHED=1")
else:
    print("STAGE1_MESSAGE_KNEE_NOT_REACHED=0")
