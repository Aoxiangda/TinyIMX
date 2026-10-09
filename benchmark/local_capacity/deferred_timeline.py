"""Bounded measurement evidence captured in memory, persisted at safe checkpoints.

No worker threads and no I/O or JSON serialization in emit. Flush must be called
after the measured receiver ACK path and authoritative confirmation. Original
wire protocol, captured monotonic timestamps and all records are retained.
"""
import copy
import datetime
import hashlib
import json
import math
import pathlib
import time


def _reservation(value, depth=0):
    if depth > 16:
        raise ValueError("Evidence nesting bound")
    if value is None or type(value) in (bool, int):
        return 64
    if type(value) is float:
        if not math.isfinite(value):
            raise ValueError("Nonfinite evidence")
        return 64
    if isinstance(value, str):
        return 64 + 4 * len(value)
    if isinstance(value, list):
        return 64 + sum(_reservation(x, depth + 1) for x in value)
    if isinstance(value, dict):
        if not all(isinstance(k, str) for k in value):
            raise ValueError("Nonstring evidence key")
        return 64 + sum(_reservation(k, depth + 1) +
                        _reservation(v, depth + 1) for k, v in value.items())
    raise ValueError("Non-JSON evidence")


class DeferredTimeline:
    def __init__(self, out, max_records=8192, max_reservation=8 * 1024 * 1024):
        self.out = pathlib.Path(out)
        if not self.out.is_dir() or max_records < 1 or max_reservation < 1:
            raise ValueError("Require existing owned directory and positive limits")
        self.max_records = max_records
        self.max_reservation = max_reservation
        self.pending = []
        self.reservation = 0
        self.chunks = []
        self.captured = 0
        self.high_water_records = 0
        self.high_water_reservation = 0
        self.capture_wall_ns = 0
        self.failed = None
        self.finalized = False

    def emit(self, event, mono_ns=None, **kw):
        started = time.monotonic_ns()
        if self.finalized or self.failed is not None:
            raise RuntimeError("Closed or failed evidence capture")
        if {"utc", "mono_ns", "event"}.intersection(kw):
            raise ValueError("Reserved evidence field")
        record = {"utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  "mono_ns": started if mono_ns is None else mono_ns,
                  "event": event, **kw}
        budget = _reservation(record)
        if len(self.pending) >= self.max_records or self.reservation + budget > self.max_reservation:
            raise BufferError("Evidence bound reached; abort, never drop records")
        frozen = copy.deepcopy(record)
        self.pending.append(frozen)
        self.reservation += budget
        self.captured += 1
        self.high_water_records = max(self.high_water_records, len(self.pending))
        self.high_water_reservation = max(self.high_water_reservation, self.reservation)
        self.capture_wall_ns += time.monotonic_ns() - started

    @staticmethod
    def _write_exclusive(path, payload):
        with path.open("xb") as stream:
            stream.write(payload)

    def flush(self):
        if self.finalized or self.failed is not None:
            raise RuntimeError("Closed or failed flush; no automatic replay")
        if not self.pending:
            return
        target = self.out / ("timeline-chunk-%05d.jsonl" % len(self.chunks))
        try:
            payload = "".join(json.dumps(z) + "\n" for z in self.pending).encode()
            self._write_exclusive(target, payload)
        except BaseException as exc:
            self.failed = {"path": target.name, "type": type(exc).__name__,
                           "pending_records_retained": len(self.pending)}
            raise
        self.chunks.append({"path": target.name, "records": len(self.pending),
                            "bytes": len(payload),
                            "sha256": hashlib.sha256(payload).hexdigest()})
        self.pending.clear()
        self.reservation = 0

    def finalize(self):
        if self.finalized:
            raise RuntimeError("Evidence already finalized")
        if self.failed is None:
            self.flush()
        recovery = None
        if self.failed is not None:
            recovery = self.out / "timeline-unflushed-recovery.jsonl"
            payload = "".join(json.dumps(z) + "\n" for z in self.pending).encode()
            self._write_exclusive(recovery, payload)
            recovery = {"path": recovery.name, "records": len(self.pending),
                        "sha256": hashlib.sha256(payload).hexdigest()}
        chunks = []
        for z in self.chunks:
            raw = (self.out / z["path"]).read_bytes()
            if hashlib.sha256(raw).hexdigest() != z["sha256"] or raw.count(b"\n") != z["records"]:
                raise ValueError("Evidence checkpoint mismatch")
            chunks.append(raw)
        status = "FAILED_CAPTURE_PRESERVED" if self.failed else "COMPLETE"
        if self.failed is None:
            target = self.out / "timeline.jsonl"
            self._write_exclusive(target, b"".join(chunks))
            if sum(z["records"] for z in self.chunks) != self.captured:
                raise ValueError("Evidence record count mismatch")
        self.finalized = True
        result = {"status": status, "captured_records": self.captured,
                  "persisted_checkpoint_records": sum(z["records"] for z in self.chunks),
                  "chunks": self.chunks, "failure": self.failed, "recovery": recovery,
                  "high_water_records": self.high_water_records,
                  "high_water_reservation": self.high_water_reservation,
                  "capture_wall_total_ms": self.capture_wall_ns / 1e6,
                  "max_records": self.max_records,
                  "max_reservation": self.max_reservation,
                  "reservation_is_conservative_budget_not_RSS": True,
                  "no_threads_no_io_or_json_encode_during_emit": True,
                  "fsync_not_added_original_checkpoint_contract": True}
        self._write_exclusive(self.out / "timeline-manifest.json",
                              (json.dumps(result, indent=2) + "\n").encode())
        return result
