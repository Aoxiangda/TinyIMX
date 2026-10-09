#!/usr/bin/env python3
"""Meaningful evidence integrity/failure tests, owned immutable stage only."""
import argparse
import hashlib
import json
import pathlib
import unittest.mock
from deferred_timeline import DeferredTimeline

p = argparse.ArgumentParser()
p.add_argument("--out", required=True)
args = p.parse_args()
out = pathlib.Path(args.out)
root = pathlib.Path("/home/jackson7/projects/TinyIMX_publish/.local/codex").resolve()
assert out.resolve().is_relative_to(root) and not out.exists()
out.mkdir(mode=0o700)
(out / "audit-before.json").write_text(json.dumps({
    "operation": "Create only fresh own offline evidence test files, no sockets/SQL/containers, no deletion",
    "directory": str(out), "maximum_reservation_per_timeline": 8 * 1024 * 1024
}, indent=2) + "\n")
checks = []
def check(name, cond):
    checks.append({"name": name, "pass": bool(cond)})
    assert cond, name
def case(name, **kw):
    d = out / name
    d.mkdir()
    return DeferredTimeline(d, **kw)
a = case("capture")
body = {"message_id": 2**64-1, "content": "测试\nbody", "nested": [1, {"v": 2}]}
with unittest.mock.patch.object(pathlib.Path, "open", side_effect=AssertionError("hot path I/O")), \
     unittest.mock.patch.object(json, "dumps", side_effect=AssertionError("hot path serialization")):
    a.emit("response", mono_ns=123, uid=7, kind=2051, body=body)
check("capture-no-file-creation", not list(a.out.iterdir()))
body["nested"][1]["v"] = 9
body["content"] = "changed"
a.emit("request", mono_ns=124, body={"seq": 4})
check("deep-frozen-wire-body", a.pending[0]["body"]["nested"][1]["v"] == 2)
check("capture-exact-monotonic", [z["mono_ns"] for z in a.pending] == [123, 124])
a.flush()
check("checkpoint-discharges-bounded-memory", len(a.pending) == 0 and a.reservation == 0)
a.emit("response", mono_ns=125, body={"seq": 4})
m = a.finalize()
rows = [json.loads(z) for z in (a.out / "timeline.jsonl").read_text().splitlines()]
check("all-records-and-order", [z["mono_ns"] for z in rows] == [123, 124, 125])
check("uint64-and-unicode-preserved", rows[0]["body"]["message_id"] == 2**64-1 and rows[0]["body"]["content"] == "测试\nbody")
check("chunks-exact-count", m["captured_records"] == m["persisted_checkpoint_records"] == 3 and len(m["chunks"]) == 2)
try:
    a.emit("late")
    check("late-refused", False)
except RuntimeError:
    check("late-refused", True)
b = case("record-bound", max_records=2)
b.emit("one")
b.emit("two")
try:
    b.emit("overflow")
    check("record-bound-refuses", False)
except BufferError:
    check("record-bound-refuses", True)
check("bound-preserves-accepted-records", b.captured == 2 and len(b.pending) == 2)
check("bound-full-buffer-persisted", b.finalize()["captured_records"] == 2)
c = case("byte-bound", max_reservation=1024)
try:
    c.emit("large", body={"content": "x" * 1024})
    check("reservation-bound-refuses", False)
except BufferError:
    check("reservation-bound-refuses", True)
check("large-rejection-no-admission", c.captured == 0 and not c.pending)
check("empty-finalize-valid", c.finalize()["captured_records"] == 0)
d = case("failed-checkpoint")
d.emit("response", mono_ns=555, body={"seq": 8})
real_write = d._write_exclusive
def broken(path, payload):
    with path.open("xb") as stream:
        stream.write(payload[:7])
    raise OSError("Synthetic partial write")
d._write_exclusive = broken
try:
    d.flush()
    check("write-failure-visible", False)
except OSError:
    check("write-failure-visible", True)
check("uncertain-write-keeps-record", d.captured == 1 and len(d.pending) == 1 and len(d.chunks) == 0)
check("partial-file-retained", (d.out / "timeline-chunk-00000.jsonl").read_bytes() == b'{"utc":')
try:
    d.flush()
    check("failed-flush-no-replay", False)
except RuntimeError:
    check("failed-flush-no-replay", True)
d._write_exclusive = real_write
dm = d.finalize()
check("failed-record-recovery-explicit", dm["status"] == "FAILED_CAPTURE_PRESERVED" and dm["recovery"]["records"] == 1)
check("failed-no-normal-success-timeline", not (d.out / "timeline.jsonl").exists())
recover = json.loads((d.out / "timeline-unflushed-recovery.jsonl").read_text())
check("recovery-content-timestamp-exact", recover["mono_ns"] == 555 and recover["body"]["seq"] == 8)
e = case("checkpoint-corruption")
e.emit("request")
e.flush()
target = e.out / e.chunks[0]["path"]
with target.open("ab") as stream:
    stream.write(b"x")
try:
    e.finalize()
    check("corruption-detected", False)
except ValueError:
    check("corruption-detected", True)
f = case("reserved-fields")
try:
    f.emit("response", utc="rewrite")
    check("reserved-time-rejected", False)
except ValueError:
    check("reserved-time-rejected", True)
for limits in [{"max_records": 0}, {"max_reservation": 0}]:
    try:
        DeferredTimeline(out, **limits)
        check("zero-limit-refused", False)
    except ValueError:
        check("zero-limit-refused", True)
summary = {"status": "DEFERRED_TIMELINE_INTEGRITY_PASS", "checks": len(checks),
           "assertions": checks, "no_business_load": True, "all_failure_files_retained": True}
(out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
print(json.dumps(summary))
