#!/usr/bin/env python3
import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path


def inspect_env(container: str, key: str) -> str:
    out = subprocess.check_output(
        ["docker", "inspect", container, "--format", "{{range .Config.Env}}{{println .}}{{end}}"],
        text=True,
    )
    prefix = key + "="
    values = [line[len(prefix):] for line in out.splitlines() if line.startswith(prefix)]
    if not values:
        raise RuntimeError(f"missing {key} in {container}")
    return values[-1]


def redis_mget(container: str, password: str, keys: list[str]) -> list[str]:
    if not keys:
        return []
    proc = subprocess.run(
        ["docker", "exec", container, "redis-cli", "--raw", "--no-auth-warning", "-a", password, "MGET", *keys],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    values = proc.stdout.splitlines()
    if len(values) < len(keys):
        values.extend([""] * (len(keys) - len(values)))
    return values[: len(keys)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("events_file")
    ap.add_argument("expected_gateway")
    ap.add_argument("--redis-container", default="tinyimx-m21-redis-1")
    ap.add_argument("--output", default="")
    args = ap.parse_args()

    affected: list[int] = []
    with open(args.events_file, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row.get("event") == "affected":
                affected.append(int(row["user_id"]))
    affected = sorted(set(affected))
    if not affected:
        print("FIRST_FAILURE=PRESENCE_AUDIT_NO_AFFECTED_USERS", file=sys.stderr)
        return 2

    try:
        password = inspect_env(args.redis_container, "TINYIMX_REDIS_PASSWORD")
    except RuntimeError:
        # Backward-compatible fallback for older runtime layouts.
        password = inspect_env(args.redis_container, "REDIS_PASSWORD")
    counts = {"expected": 0, "other": 0, "missing": 0, "invalid": 0}
    other_gateways: dict[str, int] = {}

    batch = 200
    for start in range(0, len(affected), batch):
        users = affected[start : start + batch]
        keys = [f"tinyimx:online:{uid}" for uid in users]
        values = redis_mget(args.redis_container, password, keys)
        for raw in values:
            if raw == "":
                counts["missing"] += 1
                continue
            try:
                record = json.loads(raw)
            except Exception:
                counts["invalid"] += 1
                continue
            gateway = str(record.get("gateway_id", ""))
            if gateway == args.expected_gateway:
                counts["expected"] += 1
            else:
                counts["other"] += 1
                other_gateways[gateway] = other_gateways.get(gateway, 0) + 1

    result = {
        "affected_users": len(affected),
        "expected_gateway": args.expected_gateway,
        **counts,
        "other_gateways": other_gateways,
        "converged_ratio": counts["expected"] / len(affected),
    }
    encoded = json.dumps(result, sort_keys=True)
    if args.output:
        Path(args.output).write_text(encoded + "\n", encoding="utf-8")
    print("TINYIMX_CAPSTONE_PRESENCE_AUDIT " + encoded)
    return 0 if counts["expected"] == len(affected) else 3


if __name__ == "__main__":
    raise SystemExit(main())
