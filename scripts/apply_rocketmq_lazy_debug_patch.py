"""Apply an opt-in, exact-source SDK patch; never changes an installed library.

Source authority: apache/rocketmq-clients cpp-5.1.1,
commit da8271b572b41b627bcbbdfb5b3310c0c5e4736b. Upstream licenses remain intact.
The flag defaults OFF. Formatters defer producer evaluation to spdlog/fmt,
preserving enabled debug output and backtrace capture rather than adding a
should_log-only guard that would skip backtrace messages.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re

PINNED = {
    "source/client/ReceiveMessageStreamReader.cpp": "b820ce98f52da99245f883e3506da2798782515f9a140526e8256b6f71d4fc82",
    "source/client/ClientManagerImpl.cpp": "235a1eba641fdf4b596a39d41dc0927fbb26553a1c913caef7bf068ce169b855",
}
EXPRESSION = re.compile(
    r"\b[A-Za-z_]\w*(?:(?:\.|->)[A-Za-z_]\w*)*\.(?:Short)?DebugString\(\)"
)


def debug_statements(text):
    pos = 0
    while True:
        start = text.find("SPDLOG_DEBUG(", pos)
        if start < 0:
            return
        opening = start + len("SPDLOG_DEBUG")
        depth, quote, escaped = 0, None, False
        for i in range(opening, len(text)):
            c = text[i]
            if quote:
                if escaped:
                    escaped = False
                elif c == "\\":
                    escaped = True
                elif c == quote:
                    quote = None
            elif c in ('"', "'"):
                quote = c
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if not depth:
                    yield start, i + 1
                    pos = i + 1
                    break
        else:
            raise ValueError("Unterminated pinned debug call")


def patched(text):
    replacements, count = [], 0
    for start, end in debug_statements(text):
        body = text[start:end]
        changed, n = EXPRESSION.subn(
            lambda m: "logging::MakeLazyLogValue([&] { return " + m[0] + "; })",
            body,
        )
        if n:
            replacements.append((start, end, changed))
            count += n
    for start, end, changed in reversed(replacements):
        text = text[:start] + changed + text[end:]
    needle = '#include "spdlog/spdlog.h"'
    if text.count(needle) != 1:
        raise ValueError("Pinned include boundary changed")
    return text.replace(needle, needle + '\n#include "LazyLogValue.h"'), count


def apply(source, authority, header, output):
    if source.is_symlink():
        raise ValueError("SDK source must not be a symlink")
    source = source.resolve(strict=True)
    if source.is_symlink() or not source.is_dir():
        raise ValueError("Expected a physical SDK cpp source directory")
    targets = (
        "source/client/ReceiveMessageStreamReader.cpp",
        "source/client/ClientManagerImpl.cpp",
    )
    expected_calls = {targets[0]: 1, targets[1]: 11}
    plans = []
    for name in targets:
        path = source / name
        if path.is_symlink() or path.resolve(strict=True) != path:
            raise ValueError("SDK target must remain within the physical tree")
        raw = path.read_bytes()
        before = authority["cpp/" + name]["sha256"]
        if name in PINNED and before != PINNED[name]:
            raise ValueError("Unexpected source authority")
        # Materialize expected after bytes from the immutable official preimage,
        # rather than attempting to patch already transformed expressions.
        preimage = Path(authority["__source_root__"]) / "cpp" / name
        original = preimage.read_bytes()
        if hashlib.sha256(original).hexdigest() != before:
            raise ValueError("Official source preimage changed")
        after, count = patched(original.decode("utf-8"))
        if count != expected_calls[name]:
            raise ValueError("Unexpected pinned debug-expression count")
        encoded = after.encode("utf-8")
        if raw not in (original, encoded):
            raise ValueError("Target contains unrecognized changes: " + name)
        plans.append((path, encoded, count, before, raw == encoded))
    dest = source / "source/client/include/LazyLogValue.h"
    if dest.is_symlink() or dest.parent.resolve(strict=True) != dest.parent:
        raise ValueError("Header path must remain physical")
    data = header.read_bytes()
    if dest.exists() and dest.read_bytes() != data:
        raise ValueError("Existing helper differs")
    # Validate the complete write set first. Atomic per-file replacement allows
    # an interrupted application to be resumed without accepting foreign edits.
    for path, content in [(dest, data)] + [(p, raw) for p, raw, *_ in plans]:
        if path.exists() and path.read_bytes() == content:
            continue
        temporary = path.with_name(path.name + ".tinyimx-lazy.tmp")
        if temporary.is_symlink():
            raise ValueError("Temporary patch path must not be a symlink")
        if temporary.exists():
            if not temporary.is_file() or temporary.read_bytes() != content:
                raise ValueError("Interrupted patch has unrecognized temporary bytes")
        else:
            with temporary.open("xb") as f:
                f.write(content)
                f.flush()
                os.fsync(f.fileno())
        os.replace(temporary, path)
    report = {
        "status": "PINNED_SOURCE_LAZY_DEBUG_PATCH_APPLIED_DEFAULT_OFF",
        "source": str(source),
        "flag": "TINYIMX_ROCKETMQ_LAZY_DEBUG_ENABLE",
        "default": 0,
        "files": {str(p.relative_to(source)): {
            "before": before, "after": hashlib.sha256(raw).hexdigest(),
            "wrapped_calls": count, "already_applied": already,
        } for p, raw, count, before, already in plans},
        "header_sha256": hashlib.sha256(data).hexdigest(),
        "installed_sdk_changed": False,
    }
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--authority", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    manifest = json.loads(a.authority.read_bytes())
    assert manifest["commit"] == "da8271b572b41b627bcbbdfb5b3310c0c5e4736b"
    authority = dict(manifest["file_proofs"], __source_root__=manifest["source"])
    header = Path(__file__).resolve().parents[1] / "patches/rocketmq-cpp-5.1.1/LazyLogValue.h"
    print(json.dumps(apply(a.source, authority, header, a.output)))
